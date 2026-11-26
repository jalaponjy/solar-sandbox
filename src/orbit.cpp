#include "orbit.h"
#include <cmath>
#include "catalog.h"

using glm::cross;
using glm::dot;
using glm::length;
using glm::normalize;

double Elements::period() const {
  if (!bound || a <= 0) return 0;
  return 2.0 * kPi * std::sqrt(a * a * a / mu);
}
double Elements::periapsis() const { return a * (1.0 - e); }
double Elements::apoapsis() const { return bound ? a * (1.0 + e) : INFINITY; }
double Elements::speedAtPeri() const { return std::sqrt(mu * (1.0 + e) / (a * (1.0 - e))); }
double Elements::speedAtApo() const {
  return bound ? std::sqrt(mu * (1.0 - e) / (a * (1.0 + e))) : 0.0;
}

Frame eclipticFrame() { return Frame{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; }

Frame frameFromPole(const dvec3& pole) {
  Frame f;
  f.z = normalize(pole);
  dvec3 n = cross(dvec3(0, 0, 1), f.z);
  if (length(n) < 1e-9) n = dvec3(1, 0, 0);
  f.x = normalize(n);
  f.y = cross(f.z, f.x);
  return f;
}

bool stateToElements(const dvec3& rw, const dvec3& vw, double mu, const Frame& f, Elements& el) {
  const dvec3 r(dot(rw, f.x), dot(rw, f.y), dot(rw, f.z));
  const dvec3 v(dot(vw, f.x), dot(vw, f.y), dot(vw, f.z));
  const double rm = length(r), v2 = dot(v, v);
  if (rm <= 0 || mu <= 0) return false;
  const dvec3 h = cross(r, v);
  const double hm = length(h);
  if (hm < 1e-12) return false;
  const dvec3 evec = ((v2 - mu / rm) * r - dot(r, v) * v) / mu;
  const double e = length(evec);
  const double energy = 0.5 * v2 - mu / rm;
  el.mu = mu;
  el.e = e;
  el.bound = energy < 0 && e < 1.0;
  el.a = std::fabs(energy) < 1e-14 ? INFINITY : -mu / (2.0 * energy);
  el.inc = std::acos(glm::clamp(h.z / hm, -1.0, 1.0));
  const dvec3 nvec(-h.y, h.x, 0.0);
  const double nm = length(nvec);
  const double eps = 1e-10;
  if (nm > eps * hm) el.node = std::atan2(nvec.y, nvec.x); else el.node = 0.0;
  if (el.node < 0) el.node += 2 * kPi;

  auto angle = [](const dvec3& a, const dvec3& b, const dvec3& ref) {
    double c = dot(a, b) / (length(a) * length(b));
    double ang = std::acos(glm::clamp(c, -1.0, 1.0));
    return dot(cross(a, b), ref) < 0 ? 2 * kPi - ang : ang;
  };
  const dvec3 hn = h / hm;
  if (e > eps) {
    if (nm > eps * hm) el.peri = angle(nvec, evec, hn);
    else el.peri = std::atan2(evec.y, evec.x) * (h.z >= 0 ? 1 : -1);
    if (el.peri < 0) el.peri += 2 * kPi;
    el.nu = angle(evec, r, hn);
  } else {  // circular: measure from the node
    el.peri = 0;
    if (nm > eps * hm) el.nu = angle(nvec, r, hn);
    else el.nu = std::atan2(r.y, r.x);
    if (el.nu < 0) el.nu += 2 * kPi;
  }
  return true;
}

void elementsToState(const Elements& el, const Frame& f, dvec3& r, dvec3& v) {
  const double mu = el.mu;
  const double p = el.bound ? el.a * (1 - el.e * el.e) : el.a * (1 - el.e * el.e);  // a<0 for hyperbola
  const double cnu = std::cos(el.nu), snu = std::sin(el.nu);
  const double rm = p / (1.0 + el.e * cnu);
  const double sp = std::sqrt(mu / std::fabs(p));
  const dvec3 rp(rm * cnu, rm * snu, 0), vp(-sp * snu, sp * (el.e + cnu), 0);
  const double cO = std::cos(el.node), sO = std::sin(el.node);
  const double cw = std::cos(el.peri), sw = std::sin(el.peri);
  const double ci = std::cos(el.inc), si = std::sin(el.inc);
  auto rot = [&](const dvec3& q) {
    const dvec3 l(
        (cO * cw - sO * sw * ci) * q.x + (-cO * sw - sO * cw * ci) * q.y,
        (sO * cw + cO * sw * ci) * q.x + (-sO * sw + cO * cw * ci) * q.y,
        (sw * si) * q.x + (cw * si) * q.y);
    return l.x * f.x + l.y * f.y + l.z * f.z;
  };
  r = rot(rp);
  v = rot(vp);
}

double solveKepler(double M, double e) {
  M = std::fmod(M, 2 * kPi);
  if (M < 0) M += 2 * kPi;
  double E = e < 0.8 ? M : kPi;
  for (int i = 0; i < 40; ++i) {
    const double d = (E - e * std::sin(E) - M) / (1.0 - e * std::cos(E));
    E -= d;
    if (std::fabs(d) < 1e-13) break;
  }
  return E;
}

double trueFromMean(double M, double e) {
  const double E = solveKepler(M, e);
  return 2.0 * std::atan2(std::sqrt(1 + e) * std::sin(E / 2), std::sqrt(1 - e) * std::cos(E / 2));
}

void sampleOrbit(const Elements& el, const Frame& f, int count, std::vector<dvec3>& out) {
  out.clear();
  Elements t = el;
  double lo = 0, hi = 2 * kPi;
  if (!el.bound) {  // clip hyperbola where r would exceed ~200 AU
    const double p = std::fabs(el.a) * (el.e * el.e - 1.0);
    const double rmax = 200.0 * kAU;
    const double c = (p / rmax - 1.0) / el.e;
    const double lim = std::min(std::acos(glm::clamp(-1.0 / el.e, -1.0, 1.0)) * 0.999,
                                std::acos(glm::clamp(c, -1.0, 1.0)));
    lo = -lim; hi = lim;
  }
  out.reserve(count + 1);
  for (int i = 0; i <= count; ++i) {
    t.nu = lo + (hi - lo) * (double)i / count;
    dvec3 r, v;
    elementsToState(t, f, r, v);
    out.push_back(r);
  }
}
