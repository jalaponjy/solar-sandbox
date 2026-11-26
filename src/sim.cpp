#include "sim.h"
#include <algorithm>
#include <chrono>
#include <cmath>

using glm::dot;
using glm::length;

double hashAngle(int index, int salt) {
  unsigned x = (unsigned)index * 2654435761u + (unsigned)salt * 40503u + 0x9e3779b9u;
  x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
  return (double)(x & 0xFFFFFF) / (double)0x1000000 * 2.0 * kPi;
}

Simulation::Simulation() { reset(); }

int Simulation::find(const std::string& name) const {
  for (size_t i = 0; i < bodies.size(); ++i)
    if (bodies[i].def.name == name) return (int)i;
  return -1;
}

void Simulation::reset() {
  const Catalog cat = buildCatalog();
  settings = SimSettings{};
  bodies.clear();
  bodies.resize(cat.bodies.size());
  time = 0;
  for (size_t i = 0; i < cat.bodies.size(); ++i) {
    Body& b = bodies[i];
    b.def = cat.bodies[i];
    b.parent = b.def.parent.empty() ? -1 : cat.find(b.def.parent);
    b.realMass = b.mass = b.def.massKg;
    b.radius = b.def.radiusKm;
    b.trail.assign(kTrailSamples, dvec3(0));
    if (b.parent >= 0) bodies[b.parent].children.push_back((int)i);
  }

  for (size_t i = 0; i < bodies.size(); ++i) {
    Body& b = bodies[i];
    const Body* par = b.parent >= 0 ? &bodies[b.parent] : nullptr;
    // Spin axis
    if (!std::isnan(b.def.poleRA)) b.pole = poleToEcliptic(b.def.poleRA, b.def.poleDec);
    else if (par) b.pole = par->pole;
    else b.pole = dvec3(0, 0, 1);
    b.spinPhase = hashAngle((int)i, 7);

    if (!par) { b.orbitFrame = eclipticFrame(); b.pos = dvec3(0); b.vel = dvec3(0); }
    else {
      b.orbitFrame = (b.def.kind == BodyKind::Moon && !b.def.refEcliptic) ? frameFromPole(par->pole)
                                                                          : eclipticFrame();
      Elements el;
      initialElements((int)i, el);
      dvec3 r, v;
      elementsToState(el, b.orbitFrame, r, v);
      b.pos = par->pos + r;
      b.vel = par->vel + v;
    }
  }
  // Tidally locked spins follow each moon's orbital period
  for (size_t i = 0; i < bodies.size(); ++i) {
    Body& b = bodies[i];
    if (b.def.rotHours != 0) b.spinRate = 2.0 * kPi / (b.def.rotHours * 3600.0);
    else if (b.parent >= 0) {
      const double n = std::sqrt(kG * (bodies[b.parent].mass + b.mass) / std::pow(b.def.aKm, 3));
      b.spinRate = (b.def.incDeg > 90.0 && !b.def.refEcliptic) ? -n : n;
    }
  }
  // Tidally locked moons: start with the near side (local +x) pointing at the parent.
  for (size_t i = 0; i < bodies.size(); ++i) {
    Body& b = bodies[i];
    if (b.parent < 0 || b.def.rotHours != 0) continue;
    const Frame f = frameFromPole(b.pole);
    const dvec3 toParent = bodies[b.parent].pos - b.pos;
    b.spinPhase = std::atan2(dot(toParent, f.y), dot(toParent, f.x));
  }
  zeroMomentum();
  refreshClassification();
  clearTrails();
  startEnergy = totalEnergy();
}

bool Simulation::initialElements(int i, Elements& el) const {
  const Body& b = bodies[i];
  if (b.parent < 0) return false;
  const Body& par = bodies[b.parent];
  el.a = b.def.aKm; el.e = b.def.e; el.inc = b.def.incDeg * kPi / 180.0;
  el.node = (std::isnan(b.def.nodeDeg) ? hashAngle(i, 1) : b.def.nodeDeg * kPi / 180.0);
  el.peri = (std::isnan(b.def.periDeg) ? hashAngle(i, 2) : b.def.periDeg * kPi / 180.0);
  const double M0 = std::isnan(b.def.meanAnomDeg) ? hashAngle(i, 3) : b.def.meanAnomDeg * kPi / 180.0;
  el.mu = G() * (par.mass + b.mass);
  el.bound = true;
  const double n = std::sqrt(el.mu / (el.a * el.a * el.a));
  el.nu = trueFromMean(M0 + n * time, el.e);
  return true;
}

void Simulation::restoreBody(int i) {
  Body& b = bodies[i];
  b.mass = b.realMass;
  Elements el;
  if (initialElements(i, el)) {
    dvec3 r, v;
    elementsToState(el, b.orbitFrame, r, v);
    setRelativeState(i, r, v);   // also refreshes classification
  } else {
    refreshClassification();
  }
}

void Simulation::zeroMomentum() {
  dvec3 p(0); double m = 0;
  for (const Body& b : bodies) { p += b.mass * b.vel; m += b.mass; }
  const dvec3 vcm = p / m;
  for (Body& b : bodies) b.vel -= vcm;
}

void Simulation::refreshClassification() {
  massiveIdx.clear(); testIdx.clear();
  for (size_t i = 0; i < bodies.size(); ++i) {
    const Body& b = bodies[i];
    const bool big = b.def.kind != BodyKind::Moon || b.mass > 5e19;
    (big ? massiveIdx : testIdx).push_back((int)i);
    bodies[i].massive = big;
  }
  updateTimeStep();
  computeAccelerations();
}

void Simulation::updateTimeStep() {
  double minPeriod = 1e30;
  for (size_t i = 0; i < bodies.size(); ++i) {
    Body& b = bodies[i];
    if (b.parent < 0) continue;
    Elements el;
    double period = 0;
    if (elements((int)i, el)) period = el.period();
    if (period <= 0) {      // unbound: use the instantaneous orbital timescale
      const dvec3 r = b.pos - bodies[b.parent].pos;
      period = 2 * kPi * std::sqrt(std::pow(length(r), 3) / (G() * (bodies[b.parent].mass + b.mass)));
    }
    if (period > 0 && std::isfinite(period)) minPeriod = std::min(minPeriod, period);
    // trail covers ~70% of an orbit (capped so unbound bodies still get a sensible trail)
    const double span = 0.7 * std::min(period > 0 ? period : 50 * kYear, 5000 * kYear);
    b.trailInterval = span / kTrailSamples;
  }
  dt = glm::clamp(minPeriod / std::max(20, settings.stepsPerOrbit), 5.0, 6.0 * 3600.0);
}

bool Simulation::elements(int i, Elements& out) const {
  const Body& b = bodies[i];
  if (b.parent < 0) return false;
  const Body& p = bodies[b.parent];
  return stateToElements(b.pos - p.pos, b.vel - p.vel, G() * (p.mass + b.mass), b.orbitFrame, out);
}

void Simulation::computeAccelerations() {
  const double g = G();
  const size_t M = massiveIdx.size();
  static thread_local std::vector<double> sx, sy, sz, smu;
  sx.resize(M); sy.resize(M); sz.resize(M); smu.resize(M);
  for (size_t k = 0; k < M; ++k) {
    const Body& s = bodies[massiveIdx[k]];
    sx[k] = s.pos.x; sy[k] = s.pos.y; sz[k] = s.pos.z; smu[k] = g * s.mass;
  }
  auto accelAt = [&](Body& b, int skip) {
    double ax = 0, ay = 0, az = 0;
    const double px = b.pos.x, py = b.pos.y, pz = b.pos.z;
    for (size_t k = 0; k < M; ++k) {
      if ((int)k == skip) continue;
      const double dx = sx[k] - px, dy = sy[k] - py, dz = sz[k] - pz;
      const double r2 = dx * dx + dy * dy + dz * dz + 1e-6;
      const double inv = smu[k] / (r2 * std::sqrt(r2));
      ax += dx * inv; ay += dy * inv; az += dz * inv;
    }
    b.acc = dvec3(ax, ay, az);
  };
  for (size_t k = 0; k < M; ++k) accelAt(bodies[massiveIdx[k]], (int)k);
  for (int t : testIdx) accelAt(bodies[t], -1);
}

void Simulation::step(double h) {
  const double hh = 0.5 * h;
  for (Body& b : bodies) { b.vel += b.acc * hh; b.pos += b.vel * h; }
  computeAccelerations();
  for (Body& b : bodies) b.vel += b.acc * hh;
  time += h;
}

void Simulation::recordTrails(double h) {
  const double ah = std::fabs(h);
  for (size_t i = 0; i < bodies.size(); ++i) {
    Body& b = bodies[i];
    if (b.parent < 0) continue;
    b.trailAccum += ah;
    if (b.trailAccum >= b.trailInterval) {
      b.trailAccum = 0;
      b.trail[b.trailHead] = b.pos - bodies[b.parent].pos;
      b.trailHead = (b.trailHead + 1) % kTrailSamples;
      b.trailCount = std::min(b.trailCount + 1, kTrailSamples);
    }
  }
}

void Simulation::clearTrails() {
  for (Body& b : bodies) { b.trailHead = b.trailCount = 0; b.trailAccum = 0; }
}

double Simulation::advance(double seconds, double budgetMs) {
  if (seconds == 0) return 0;
  using clock = std::chrono::steady_clock;
  const auto t0 = clock::now();
  const double sgn = seconds < 0 ? -1.0 : 1.0;
  double remaining = std::fabs(seconds), done = 0;
  int iter = 0;
  while (remaining > 1e-9) {
    const double h = std::min(dt, remaining);
    step(sgn * h);
    recordTrails(h);
    remaining -= h; done += h;
    if ((++iter & 15) == 0) {
      const double ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
      if (ms > budgetMs) break;
    }
  }
  return sgn * done;
}

void Simulation::setRelativeState(int i, const dvec3& r, const dvec3& v) {
  Body& b = bodies[i];
  if (b.parent < 0) return;
  const Body& p = bodies[b.parent];
  const dvec3 dP = (p.pos + r) - b.pos, dV = (p.vel + v) - b.vel;
  std::vector<int> stack{i};
  while (!stack.empty()) {
    const int k = stack.back(); stack.pop_back();
    bodies[k].pos += dP; bodies[k].vel += dV;
    for (int c : bodies[k].children) stack.push_back(c);
  }
  clearTrails();
  refreshClassification();
}

void Simulation::setMassMultiplier(int i, double k) {
  bodies[i].mass = bodies[i].realMass * k;
  refreshClassification();
}

double Simulation::totalEnergy() const {
  const double g = G();
  double e = 0;
  for (size_t a = 0; a < massiveIdx.size(); ++a) {
    const Body& A = bodies[massiveIdx[a]];
    e += 0.5 * A.mass * dot(A.vel, A.vel);
    for (size_t b = a + 1; b < massiveIdx.size(); ++b) {
      const Body& B = bodies[massiveIdx[b]];
      e -= g * A.mass * B.mass / length(A.pos - B.pos);
    }
  }
  return e;
}

double Simulation::accumulatedEnergyDrift() const {
  return (totalEnergy() - startEnergy) / std::fabs(startEnergy);
}
