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
  s2.setMassMultiplier(s2.find("Jupiter"), 1000.0);
  s2.advance(5 * 365.25 * kDay, 1e9);
  Elements em; s2.elements(s2.find("Mars"), em);
  Elements em0; sim.reset(); sim.elements(sim.find("Mars"), em0);
  std::printf("      Mars e: %.4f -> %.4f with 1000x Jupiter\n", em0.e, em.e);
  CHECK(std::fabs(em.e - em0.e) > 0.01, "mass tweak changes dynamics");

  std::printf("%d failure(s)\n", failures);
  return failures ? 1 : 0;
}
