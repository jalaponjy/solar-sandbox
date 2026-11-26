// Static data for every body in the sandbox (real-world values, J2000-ish).
#pragma once
#include <cmath>
#include <string>
#include <vector>
#include <glm/glm.hpp>

constexpr double kAU = 149597870.7;      // km
constexpr double kG = 6.6743e-20;        // km^3 kg^-1 s^-2
constexpr double kPi = 3.14159265358979323846;
constexpr double kDay = 86400.0;         // s
constexpr double kYear = 365.25 * 86400; // s

enum class BodyKind { Star, Planet, DwarfPlanet, Moon };

// Procedural surface styles understood by shaders/body.frag
enum SurfaceType {
  SURF_SUN = 0, SURF_ROCKY = 1, SURF_EARTH = 2, SURF_VENUS = 3, SURF_MARS = 4,
  SURF_JUPITER = 5, SURF_SATURN = 6, SURF_ICEGIANT = 7, SURF_ICY = 8,
  SURF_VOLCANIC = 9, SURF_TITAN = 10, SURF_PLUTO = 11
};

struct Appearance {
  int type = SURF_ROCKY;
  glm::vec3 c1{0.5f}, c2{0.7f}, c3{0.3f};  // palette (sRGB-ish 0..1)
  glm::vec3 atmo{0.0f};                     // atmosphere glow colour
  float atmoStrength = 0.0f;                // 0 = none
  float seed = 1.0f;
  float ringInner = 0, ringOuter = 0;       // in planet radii (0 = no rings)
  glm::vec3 ringColor{0.8f, 0.72f, 0.55f};
  float ringOpacity = 0.9f;
};

struct BodyDef {
  std::string name, parent;     // parent "" for the Sun
  BodyKind kind = BodyKind::Planet;
  std::string group;            // "Terrestrial planet", "Galilean moon", ...
  double massKg = 0;
  double radiusKm = 1;          // equatorial radius
  double flattening = 0;        // (Req - Rpol) / Req
  // Orbit (relative to the parent). Angles in degrees.
  double aKm = 0, e = 0, incDeg = 0;
  double nodeDeg = NAN, periDeg = NAN, meanAnomDeg = NAN;  // NaN => pseudo-random
  bool refEcliptic = false;     // inclination measured from the ecliptic instead of parent's equator
  // Spin
  double poleRA = NAN, poleDec = NAN; // deg, ICRF; NaN => inherit from parent
  double rotHours = 0;          // sidereal period (negative = retrograde); 0 => tidally locked
  double tiltDeg = 0;           // axial tilt for display
  double tempK = 0;             // mean/effective temperature
  int moonsKnown = 0;           // for the "known moons" fact
  Appearance look;
  std::string description;
};

struct Catalog {
  std::vector<BodyDef> bodies;  // parents always appear before their children
  int find(const std::string& name) const {
    for (size_t i = 0; i < bodies.size(); ++i)
      if (bodies[i].name == name) return (int)i;
    return -1;
  }
};

Catalog buildCatalog();

// Equatorial (ICRF) pole -> unit vector in ecliptic coordinates.
glm::dvec3 poleToEcliptic(double raDeg, double decDeg);
