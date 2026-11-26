// Shared application state for main.cpp (window, camera, simulation loop) and ui.cpp (panels).
#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "renderer.h"
#include "sim.h"

struct ImVec2;

// Live orbit editor for the selected body (Orbit tab).
struct OrbitEditor {
  int body = -1;
  double aMul = 1.0;          // semi-major axis multiplier
  float e = 0.f;              // target eccentricity (used once touched)
  float incDeg = 0.f;         // target inclination (used once touched)
  double speedMul = 1.0;      // multiplier on the orbital velocity after rebuilding
  bool touchedE = false, touchedInc = false;
  bool active = false;        // user changed something => preview is shown
  // preview of the resulting orbit
  bool previewValid = false;
  Elements preview;           // elements of the resulting state
  dvec3 pr{0}, pv{0};         // resulting parent-relative state
  bool crossesRef = false;
};

struct App {
  Simulation sim;
  Renderer renderer;
  RenderSettings rs;

  // window
  int winW = 1600, winH = 900, fbW = 1600, fbH = 900;
  bool hideUI = false;

  // time
  bool paused = false;
  float speedLog = 5.94f;      // log10(simulated seconds per real second)
  int direction = 1;
  double achieved = 1.0;       // fraction of the requested speed actually simulated
  double wallTime = 0;

  // camera (orbit camera around a focused body)
  int focus = 0;
  double yaw = 0.9, pitch = 0.5, logDist = 0, goalLogDist = 0;
  glm::dvec3 target{0};
  bool transition = false;
  double trProg = 0, trStartLog = 0, trEndLog = 0;
  glm::dvec3 trStart{0};
  double trYaw0 = 0, trYaw1 = 0, trPitch0 = 0, trPitch1 = 0;
  bool trOrient = false;
  bool followBody = true;

  // view options
  bool trueScale = false;
  float exag = 20.f;
  bool showOrbits = true, showTrails = true, showLabels = true, showMarkers = true;
  float orbitOpacity = 1.0f;

  // selection / editing
  int selected = -1;
  OrbitEditor edit;
  char search[64] = "";
  int requestTab = 0;          // 1 overview, 2 orbit, 3 mass (set once, consumed by the UI)
  float lastMassMul = 1.f;
  bool massTouch = false;

  // per-frame derived data
  std::vector<glm::dvec3> rpos;       // render-space positions (km)
  std::vector<double> rrad;           // render-space radii (km)
  std::vector<float> moonFade;        // per moon: visibility (0..1); 1 for non-moons
  std::vector<float> sunLumi;         // luminosity multiplier (Sun mass tweak)
  glm::dvec3 camPos{0};
  glm::dvec3 camFwd{0, 0, -1};
  RenderCamera cam;
  float focalPx = 1000.f;
  float renderScale() const { return trueScale ? 1.f : exag; }
  float sunScale() const { return trueScale ? 1.f : std::max(1.f, exag * 0.12f); }
};

// main.cpp
void focusOn(App& a, int body, bool keepDistance = false, bool systemView = false);
void resetView(App& a);
bool projectToScreen(const App& a, const glm::dvec3& renderPos, ImVec2& out, float& distKm);
double focusDistanceFor(const App& a, int body, bool systemView = false);
glm::dvec3 moonRenderOffset(const App& a, int parentIdx, const glm::dvec3& realRelativeKm);

// ui.cpp
void setupImGui(float scale);
void drawUI(App& a);        // panels
void drawOverlays(App& a);  // labels / markers / selection ring on the 3D view
