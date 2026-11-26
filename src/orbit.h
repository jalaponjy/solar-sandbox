// Two-body orbit maths: state vectors <-> classical orbital elements.
#pragma once
#include <vector>
#include <glm/glm.hpp>

using dvec3 = glm::dvec3;

struct Frame {            // right-handed reference frame (z = reference-plane normal)
  dvec3 x{1, 0, 0}, y{0, 1, 0}, z{0, 0, 1};
};

struct Elements {
  double a = 0;           // semi-major axis (km); negative for hyperbolic
  double e = 0;           // eccentricity
  double inc = 0;         // radians, 0..pi
  double node = 0;        // longitude of ascending node (rad)
  double peri = 0;        // argument of periapsis (rad)
  double nu = 0;          // true anomaly (rad)
  double mu = 0;          // km^3/s^2 used for the conversion
  bool bound = true;
  double period() const;          // seconds (0 if unbound)
  double periapsis() const;       // km
  double apoapsis() const;        // km (inf if unbound)
  double speedAtPeri() const;     // km/s
  double speedAtApo() const;      // km/s
};

Frame eclipticFrame();
// Frame whose z axis is `pole` and whose x axis points at the pole's ascending node on the ecliptic.
Frame frameFromPole(const dvec3& pole);

bool stateToElements(const dvec3& r, const dvec3& v, double mu, const Frame& f, Elements& out);
void elementsToState(const Elements& el, const Frame& f, dvec3& r, dvec3& v);

double solveKepler(double meanAnomaly, double e);          // eccentric anomaly
double trueFromMean(double meanAnomaly, double e);          // true anomaly

// Points along the orbit (parent-relative, km). Hyperbolic orbits are clipped.
void sampleOrbit(const Elements& el, const Frame& f, int count, std::vector<dvec3>& out);
