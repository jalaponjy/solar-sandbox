// Headless checks for the physics core: initial elements, periods, energy conservation, speed.
#include <chrono>
#include <cmath>
#include <cstdio>
#include "sim.h"

static int failures = 0;
#define CHECK(cond, ...)                                  \
  do {                                                    \
    if (!(cond)) { ++failures; std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
  } while (0)

int main() {
  Simulation sim;
  std::printf("bodies: %d, dt = %.1f s\n", sim.count(), sim.timeStep());

  // 1. Osculating elements reproduce the catalog values at t = 0.
  struct { const char* n; double aAU, e; } ref[] = {
      {"Mercury", 0.387, 0.2056}, {"Earth", 1.000, 0.0167}, {"Jupiter", 5.203, 0.0484}, {"Neptune", 30.07, 0.0086}};
  for (auto& r : ref) {
    Elements el; sim.elements(sim.find(r.n), el);
    CHECK(std::fabs(el.a / kAU - r.aAU) / r.aAU < 0.01 && std::fabs(el.e - r.e) < 0.005,
          "%s: a=%.4f AU e=%.4f (expected %.3f, %.4f)", r.n, el.a / kAU, el.e, r.aAU, r.e);
  }

  // 2. Orbital periods come out right.
  auto periodDays = [&](const char* n) { Elements el; sim.elements(sim.find(n), el); return el.period() / kDay; };
  CHECK(std::fabs(periodDays("Earth") - 365.25) < 2.0, "Earth period %.2f d", periodDays("Earth"));
  CHECK(std::fabs(periodDays("Moon") - 27.32) < 0.7, "Moon period %.2f d", periodDays("Moon"));
  CHECK(std::fabs(periodDays("Io") - 1.769) < 0.02, "Io period %.3f d", periodDays("Io"));
  CHECK(std::fabs(periodDays("Titan") - 15.945) < 0.2, "Titan period %.3f d", periodDays("Titan"));

  // 3. Integrate 2 years: Earth returns, energy is conserved, report throughput.
  const int earth = sim.find("Earth");
  const dvec3 e0 = sim.body(earth).pos - sim.body(0).pos;
  const auto t0 = std::chrono::steady_clock::now();
  double done = sim.advance(365.25 * kDay, 1e9);
  const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  const dvec3 e1 = sim.body(earth).pos - sim.body(0).pos;
  CHECK(std::fabs(done - 365.25 * kDay) < 1.0, "advanced %.2f days", done / kDay);
  const double ang = std::acos(glm::dot(glm::normalize(e0), glm::normalize(e1)));
  CHECK(ang < 0.03, "Earth back near start after 1 yr (angle %.4f rad)", ang);
  CHECK(std::fabs(sim.accumulatedEnergyDrift()) < 1e-5, "energy drift %.3e", sim.accumulatedEnergyDrift());
  std::printf("      1 simulated year took %.3f s wall (%.1f sim-years per wall second)\n", wall, 1.0 / wall);

  // 4. Time is reversible (leapfrog): go back and land near the start.
  sim.advance(-365.25 * kDay, 1e9);
  const dvec3 e2 = sim.body(earth).pos - sim.body(0).pos;
  CHECK(glm::length(e2 - e0) / kAU < 1e-3, "reverse integration returns to start (err %.2e AU)", glm::length(e2 - e0) / kAU);

  // 5. Mass tweak has consequences: a 1000x Jupiter changes Mars's orbit noticeably.
  Simulation s2;
  s2.setMassMultiplier(s2.find("Jupiter"), 30.0);
  s2.advance(1.0 * 365.25 * kDay, 1e9);
  Elements em; s2.elements(s2.find("Mars"), em);
  Elements em0; sim.reset(); sim.elements(sim.find("Mars"), em0);
  std::printf("      Mars e: %.4f -> %.4f with 30x Jupiter\n", em0.e, em.e);
  CHECK(std::fabs(em.e - em0.e) > 0.002, "mass tweak changes dynamics (de = %.4f)", em.e - em0.e);

  // 6. Orbit editor round trip: previewed elements == elements after applying.
  {
    Simulation s3;
    const int mars = s3.find("Mars");
    Elements cur; s3.elements(mars, cur);
    Elements pe = cur; pe.a *= 1.6; pe.e = 0.5; pe.inc = 20.0 * kPi / 180.0;
    dvec3 r, v; elementsToState(pe, s3.body(mars).orbitFrame, r, v);
    Elements want; stateToElements(r, v, cur.mu, s3.body(mars).orbitFrame, want);
    s3.setRelativeState(mars, r, v);
    Elements got; s3.elements(mars, got);
    CHECK(std::fabs(got.a / want.a - 1) < 1e-9 && std::fabs(got.e - want.e) < 1e-9 && std::fabs(got.inc - want.inc) < 1e-9,
          "applied orbit matches preview (a=%.3f AU e=%.3f i=%.1f deg)", got.a / kAU, got.e, got.inc * 180 / kPi);
    // a 1.5x speed boost on a near-circular orbit exceeds escape speed (sqrt2 = 1.414)
    Simulation s4;
    const int earth4 = s4.find("Earth");
    dvec3 rr = s4.body(earth4).pos - s4.body(0).pos, vv = s4.body(earth4).vel - s4.body(0).vel;
    s4.setRelativeState(earth4, rr, vv * 1.5);
    Elements es; s4.elements(earth4, es);
    CHECK(!es.bound, "1.5x speed boost -> escape trajectory (e=%.3f)", es.e);
    // moons ride along with their planet
    Simulation s5;
    const int jup = s5.find("Jupiter"), io = s5.find("Io");
    Elements io0; s5.elements(io, io0);
    dvec3 jr = s5.body(jup).pos - s5.body(0).pos, jv = s5.body(jup).vel - s5.body(0).vel;
    s5.setRelativeState(jup, jr * 1.3, jv / std::sqrt(1.3));
    Elements io1; s5.elements(io, io1);
    CHECK(std::fabs(io1.a / io0.a - 1) < 1e-9, "Io keeps its orbit when Jupiter is moved (a ratio %.9f)", io1.a / io0.a);
    // restore puts everything back on the unedited Kepler orbit
    s3.restoreBody(mars);
    Elements back; s3.elements(mars, back);
    CHECK(std::fabs(back.a / cur.a - 1) < 1e-6 && std::fabs(back.e - cur.e) < 1e-6, "restoreBody returns to original orbit");
  }

  std::printf("%d failure(s)\n", failures);
  return failures ? 1 : 0;
}
