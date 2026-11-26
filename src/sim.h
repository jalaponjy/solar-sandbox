// N-body gravity simulation of the solar system (leapfrog / kick-drift-kick).
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "catalog.h"
#include "orbit.h"

struct Body {
  BodyDef def;
  int parent = -1;
  std::vector<int> children;

  double realMass = 0;     // kg (catalog value)
  double mass = 0;         // kg (current, user tweakable)
  double radius = 1;       // km
  dvec3 pos{0}, vel{0};    // km, km/s (Sun-centred inertial frame, ecliptic axes)
  dvec3 acc{0};

  // Spin
  dvec3 pole{0, 0, 1};     // unit vector (ecliptic coordinates)
  double spinRate = 0;     // rad/s (negative = retrograde)
  double spinPhase = 0;

  Frame orbitFrame;        // reference frame for this body's orbital elements
  bool massive = true;     // exerts gravity on others (else a test particle)

  // Trail (parent-relative positions, ring buffer)
  std::vector<dvec3> trail;
  int trailHead = 0, trailCount = 0;
  double trailAccum = 0;   // seconds since last sample
  double trailInterval = 1e30;
};

constexpr int kTrailSamples = 360;

struct SimSettings {
  double gScale = 1.0;           // multiplies G
  int stepsPerOrbit = 90;        // time-step resolution for the fastest orbit
};

class Simulation {
 public:
  Simulation();
  void reset();                              // restore real J2000 initial conditions
  // Advance by `seconds` of simulated time (may be negative). Stops early if the wall
  // clock budget (ms) runs out; returns the simulated seconds actually advanced.
  double advance(double seconds, double budgetMs);

  int find(const std::string& name) const;
  int count() const { return (int)bodies.size(); }
  Body& body(int i) { return bodies[i]; }
  const Body& body(int i) const { return bodies[i]; }

  double G() const { return kG * settings.gScale; }
  double mu(int i) const { return G() * bodies[i].mass; }
  double timeSeconds() const { return time; }   // since J2000
  double julianDay() const { return 2451545.0 + time / kDay; }

  // Osculating elements of body i relative to its parent.
  bool elements(int i, Elements& out) const;
  // Mean-motion based dt currently used for a single step.
  double timeStep() const { return dt; }

  // Set a new parent-relative state for body i. Moons (descendants) are carried along.
  void setRelativeState(int i, const dvec3& r, const dvec3& v);
  void setMassMultiplier(int i, double k);
  // Put body i back on its original (unedited) Keplerian orbit at the current time and reset its mass.
  void restoreBody(int i);
  bool initialElements(int i, Elements& el) const;
  void refreshClassification();     // massive/test-particle split, dt, barycentre
  void clearTrails();
  double spinAngle(int i) const { return bodies[i].spinPhase + bodies[i].spinRate * time; }

  SimSettings settings;
  double accumulatedEnergyDrift() const;     // relative energy error vs. start (diagnostic)
  double totalEnergy() const;

 private:
  void computeAccelerations();
  void step(double h);
  void recordTrails(double h);
  void updateTimeStep();
  void zeroMomentum();

  std::vector<Body> bodies;
  std::vector<int> massiveIdx, testIdx;
  double time = 0;
  double dt = 300;
  double startEnergy = 0;
};

// Pseudo-random but deterministic phase for undefined orbital angles.
double hashAngle(int index, int salt);
