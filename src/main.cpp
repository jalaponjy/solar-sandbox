// Solar System Sandbox - window, input, camera, simulation loop and 3D scene assembly.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include "app.h"
#include "png.h"

using glm::dvec3;
using glm::vec3;

namespace {

struct Options {
  std::string screenshot;
  int frames = 0;                 // >0: run this many frames then exit (with --screenshot: save last)
  std::string focus, select;
  double dist = 0;                // km, 0 = default
  double yawDeg = NAN, pitchDeg = NAN;
  double speedDaysPerSec = NAN;
  double advanceDays = 0;
  int width = 1600, height = 900;
  bool noUI = false, trueScale = false, noOrbits = false, system = false;
  float exag = -1;
  bool paused = false;
  std::string tab;
  double editA = 1.0; float editE = -1.f;
  double clickX = -1, clickY = -1;
};

double g_scroll = 0;
bool g_shift = false, g_ctrl = false;

void scrollCallback(GLFWwindow*, double, double dy) {
  g_scroll += dy;
}

double easeInOut(double t) { return t * t * (3.0 - 2.0 * t); }

}  // namespace

// ------------------------------------------------------------------ camera helpers
double focusDistanceFor(const App& a, int body, bool systemView) {
  const Body& b = a.sim.body(body);
  const double k = (b.parent < 0) ? a.sunScale() : a.renderScale();
  double d = std::max(b.radius * k * 5.0, 800.0);          // close-up portrait
  if (systemView) {                                          // frame the regular moons too
    double ext = 0;
    for (int c : b.children) {
      const Body& m = a.sim.body(c);
      if (m.def.kind == BodyKind::Moon && m.def.aKm < 70.0 * b.radius)
        ext = std::max(ext, m.def.aKm * a.renderScale());
    }
    if (ext > 0) d = std::max(d, ext * 1.4);
  }
  if (b.parent < 0 && systemView) d = 8.0 * kAU;            // whole inner solar system
  return d;
}

void resetView(App& a) {
  a.focus = 0;
  a.yaw = 0.9; a.pitch = 0.55;
  a.goalLogDist = a.logDist = std::log(8.0 * kAU);
  a.target = a.rpos.empty() ? dvec3(0) : a.rpos[0];
  a.transition = false;
}

void focusOn(App& a, int body, bool keepDistance, bool systemView) {
  if (body < 0 || body >= a.sim.count()) return;
  a.transition = true;
  a.trProg = 0;
  a.trStart = a.target;
  a.trStartLog = a.logDist;
  a.trEndLog = keepDistance ? a.logDist : std::log(focusDistanceFor(a, body, systemView));
  a.focus = body;
  a.goalLogDist = a.trEndLog;
  // Approach from the sunlit side, a little off-axis, so the world appears as a lit crescent/gibbous.
  a.trOrient = false;
  if (body > 0 && !a.rpos.empty()) {
    const dvec3 d = glm::normalize(a.rpos[0] - a.rpos[body]);
    double yawGoal = std::atan2(d.y, d.x) + 0.7;
    const double pitchGoal = glm::clamp(std::asin(glm::clamp(d.z, -1.0, 1.0)) + 0.28, -1.2, 1.2);
    // shortest way round
    double dy = std::fmod(yawGoal - a.yaw, 2.0 * 3.14159265358979);
    if (dy > 3.14159265358979) dy -= 2.0 * 3.14159265358979;
    if (dy < -3.14159265358979) dy += 2.0 * 3.14159265358979;
    a.trYaw0 = a.yaw; a.trYaw1 = a.yaw + dy;
    a.trPitch0 = a.pitch; a.trPitch1 = pitchGoal;
    a.trOrient = true;
  }
}

dvec3 moonRenderOffset(const App& a, int parentIdx, const dvec3& rel) {
  if (a.trueScale) return rel;
  const double k = a.renderScale();
  const double r = glm::length(rel);
  if (r < 1e-9) return rel * k;
  const double r0 = 70.0 * a.sim.body(parentIdx).radius;     // far-out irregular moons are compressed
  const double m = r <= r0 ? r : r0 + (r - r0) * 0.08;
  return rel * (m / r * k);
}

bool projectToScreen(const App& a, const dvec3& renderPos, ImVec2& out, float& distKm) {
  const dvec3 rel = renderPos - a.camPos;
  distKm = (float)glm::length(rel);
  const glm::vec4 clip = a.cam.vp * glm::vec4(vec3(rel / 1000.0), 1.f);
  if (clip.w < 1e-9f) return false;
  const float nx = clip.x / clip.w, ny = clip.y / clip.w;
  out = ImVec2((nx * 0.5f + 0.5f) * a.winW, (1.f - (ny * 0.5f + 0.5f)) * a.winH);
  return true;
}

namespace {

// ------------------------------------------------------------------ per-frame state
void computeRenderSpace(App& a) {
  const int n = a.sim.count();
  a.rpos.resize(n); a.rrad.resize(n);
  a.sunLumi.assign(n, 1.f);
  const double k = a.renderScale(), ks = a.sunScale();
  for (int i = 0; i < n; ++i) {
    const Body& b = a.sim.body(i);
    if (b.parent < 0) {
      const double ratio = b.mass / b.realMass;
      a.rpos[i] = b.pos;
      a.rrad[i] = b.radius * ks * std::pow(ratio, 0.8);
      a.sunLumi[i] = (float)std::min(std::pow(ratio, 3.5), 200.0);
    } else if (b.def.kind == BodyKind::Moon) {
      a.rpos[i] = a.rpos[b.parent] + moonRenderOffset(a, b.parent, b.pos - a.sim.body(b.parent).pos);
      a.rrad[i] = b.radius * k;
    } else {
      a.rpos[i] = b.pos;
      a.rrad[i] = b.radius * k;
    }
  }
}

void updateCamera(App& a, double dt) {
  const double minD = std::max(1.25 * a.rrad[a.focus], 50.0);
  const double maxD = 6.0e10;
  if (a.transition) {
    a.trProg = std::min(1.0, a.trProg + dt / 1.5);
    const double e = easeInOut(a.trProg);
    a.target = glm::mix(a.trStart, a.rpos[a.focus], e);
    a.logDist = a.trStartLog + (a.trEndLog - a.trStartLog) * e;
    if (a.trOrient) {
      a.yaw = a.trYaw0 + (a.trYaw1 - a.trYaw0) * e;
      a.pitch = a.trPitch0 + (a.trPitch1 - a.trPitch0) * e;
    }
    if (a.trProg >= 1.0) a.transition = false;
  } else {
    a.target = a.rpos[a.focus];
    a.goalLogDist = glm::clamp(a.goalLogDist, std::log(minD), std::log(maxD));
    a.logDist += (a.goalLogDist - a.logDist) * (1.0 - std::exp(-dt * 12.0));
  }
  a.logDist = glm::clamp(a.logDist, std::log(minD), std::log(maxD));
  a.pitch = glm::clamp(a.pitch, -1.5, 1.5);

  const double dist = std::exp(a.logDist);
  const dvec3 dir(std::cos(a.pitch) * std::cos(a.yaw), std::cos(a.pitch) * std::sin(a.yaw), std::sin(a.pitch));
  a.camPos = a.target + dir * dist;
  a.camFwd = -dir;

  RenderCamera& c = a.cam;
  c.width = a.fbW; c.height = a.fbH;
  c.aspect = (float)a.fbW / std::max(1, a.fbH);
  const float fov = glm::radians(50.f);
  c.tanHalfFov = std::tan(fov * 0.5f);
  c.view = glm::lookAt(vec3(0), vec3(a.camFwd), vec3(0, 0, 1));
  c.proj = glm::perspective(fov, c.aspect, 1e-6f, 1e9f);
  c.vp = c.proj * c.view;
  c.viewRotInv = glm::transpose(glm::mat3(c.view));
  c.farPlane = 1e9f;
  a.focalPx = 0.5f * a.fbH / c.tanHalfFov;

  // How visible is each moon at this zoom? (fades in as its own orbit becomes big enough on screen)
  const int n = a.sim.count();
  a.moonFade.assign(n, 1.f);
  for (int i = 0; i < n; ++i) {
    const Body& b = a.sim.body(i);
    if (b.def.kind != BodyKind::Moon) continue;
    const double ext = glm::length(moonRenderOffset(a, b.parent, dvec3(b.def.aKm, 0, 0)));
    const double d = std::max(glm::length(a.rpos[b.parent] - a.camPos), 1.0);
    const double px = ext / d * a.focalPx;
    double t = glm::clamp((px - 12.0) / 30.0, 0.0, 1.0);
    t = t * t * (3 - 2 * t);
    const double far = glm::clamp((d / std::max(ext, 1.0) - 2.5) / 2.5, 0.0, 1.0);   // camera far outside the system
    t *= 1.0 - far * far * (3 - 2 * far);
    const double inside = glm::clamp((ext / d - 1.2) / 1.3, 0.0, 1.0);          // camera sits inside this orbit
    t *= 1.0 - inside * inside * (3 - 2 * inside);
    a.moonFade[i] = (i == a.focus || i == a.selected) ? 1.f : (float)t;
  }
}

// ------------------------------------------------------------------ input
void pickBody(App& a, double mx, double my, bool doFocus) {
  int best = -1;
  double bestScore = 1e30;
  const float scale = (float)a.winH / std::max(1, a.fbH);
  for (int i = 0; i < a.sim.count(); ++i) {
    const Body& b = a.sim.body(i);
    if (b.def.kind == BodyKind::Moon && a.moonFade[i] < 0.25f) continue;
    ImVec2 p; float dist;
    if (!projectToScreen(a, a.rpos[i], p, dist)) continue;
    const double rpx = a.rrad[i] / std::max<double>(dist, 1.0) * a.focalPx * scale;
    const double hit = std::max(rpx, b.def.kind == BodyKind::Moon ? 7.0 : 10.0);
    const double d = std::hypot(p.x - mx, p.y - my);
    if (d > hit) continue;
    const double score = d / hit + dist * 1e-15;   // nearest to the cursor, nearer bodies win ties
    if (score < bestScore) { bestScore = score; best = i; }
  }
  if (best >= 0) {
    a.selected = best;
    if (doFocus) focusOn(a, best);
  }
}

void handleInput(App& a, GLFWwindow* w, double dt) {
  ImGuiIO& io = ImGui::GetIO();
  static double lastX = 0, lastY = 0, pressX = 0, pressY = 0, lastClickT = -1, lastClickX = 0, lastClickY = 0;
  static bool dragging = false, pressedOnScene = false;
  double mx, my;
  glfwGetCursorPos(w, &mx, &my);
  const bool left = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  const bool middle = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
  const bool right = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
  static bool wasLeft = false, wasRotating = false;

  if (left && !wasLeft) {
    pressedOnScene = !io.WantCaptureMouse;
    pressX = mx; pressY = my; dragging = false;
  }
  const bool rotating = (left && pressedOnScene) || ((middle || right) && !io.WantCaptureMouse) ||
                        (wasRotating && (middle || right));
  if (rotating) {
    if (wasRotating || left) {
      const double dx = mx - lastX, dy = my - lastY;
      if (left && std::hypot(mx - pressX, my - pressY) > 4) dragging = true;
      if (!left || dragging) { a.yaw -= dx * 0.0055; a.pitch += dy * 0.0055; }
    }
  }
  if (!left && wasLeft && pressedOnScene && !dragging) {  // click
    const double now = glfwGetTime();
    const bool dbl = (now - lastClickT) < 0.35 && std::hypot(mx - lastClickX, my - lastClickY) < 6;
    pickBody(a, mx, my, dbl);
    if (!dbl && a.selected < 0) {}
    lastClickT = dbl ? -1 : now; lastClickX = mx; lastClickY = my;
  }
  wasLeft = left;
  wasRotating = rotating;
  lastX = mx; lastY = my;

  // Scroll wheel: zoom (default), Shift = rotate sideways, Ctrl = tilt.
  if (g_scroll != 0 && !io.WantCaptureMouse) {
    const bool shift = glfwGetKey(w, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(w, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    const bool ctrl = glfwGetKey(w, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(w, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    if (shift) a.yaw += g_scroll * 0.12;
    else if (ctrl) a.pitch += g_scroll * 0.08;
    else {
      if (a.transition) { a.transition = false; a.logDist = a.trStartLog + (a.trEndLog - a.trStartLog) * easeInOut(a.trProg); a.goalLogDist = a.logDist; }
      a.goalLogDist -= g_scroll * 0.16;
    }
  }
  g_scroll = 0;

  if (!io.WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) a.paused = !a.paused;
    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) { a.direction = -a.direction; a.sim.clearTrails(); }
    if (ImGui::IsKeyPressed(ImGuiKey_F, false) && a.selected >= 0) focusOn(a, a.selected);
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) { resetView(a); a.selected = -1; }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) a.selected = -1;
    if (ImGui::IsKeyPressed(ImGuiKey_L, false)) a.showLabels = !a.showLabels;
    if (ImGui::IsKeyPressed(ImGuiKey_O, false)) a.showOrbits = !a.showOrbits;
    if (ImGui::IsKeyPressed(ImGuiKey_T, false)) a.showTrails = !a.showTrails;
    if (ImGui::IsKeyPressed(ImGuiKey_H, false)) a.hideUI = !a.hideUI;
    if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) a.speedLog = std::min(a.speedLog + 0.25f, 8.0f);
    if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) a.speedLog = std::max(a.speedLog - 0.25f, 0.0f);
    const double rs = 1.6 * dt;
    if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) a.yaw += rs;
    if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) a.yaw -= rs;
    if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) a.pitch += rs;
    if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) a.pitch -= rs;
    // number keys jump to the planets
    static const char* planetKeys[] = {"Sun", "Mercury", "Venus", "Earth", "Mars", "Jupiter", "Saturn", "Uranus", "Neptune"};
    for (int k = 0; k < 9; ++k)
      if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_0 + k), false)) {
        const int idx = a.sim.find(planetKeys[k]);
        if (idx >= 0) { a.selected = idx; focusOn(a, idx); }
      }
  }
}

void stepSimulation(App& a, double dt) {
  if (a.paused) { a.achieved = 1.0; return; }
  const double requested = a.direction * std::pow(10.0, (double)a.speedLog) * dt;
  const double done = a.sim.advance(requested, 9.0);
  a.achieved = requested != 0 ? std::min(1.0, std::fabs(done / requested)) : 1.0;
}

// ------------------------------------------------------------------ scene assembly
BodyDraw makeDraw(const App& a, int i) {
  const Body& b = a.sim.body(i);
  BodyDraw d;
  const dvec3 rel = a.rpos[i] - a.camPos;
  d.center = vec3(rel / 1000.0);
  d.radius = (float)(a.rrad[i] / 1000.0);
  d.flat = (float)(1.0 - b.def.flattening);
  d.rot = bodyRotation(b.pole, a.sim.spinAngle(i));
  d.look = b.def.look;
  const dvec3 toSun = a.rpos[0] - a.rpos[i];
  d.sunDir = glm::length(toSun) > 0 ? vec3(glm::normalize(toSun)) : vec3(1, 0, 0);
  const double dAU = std::max(glm::length(a.sim.body(i).pos - a.sim.body(0).pos) / kAU, 0.25);
  d.sunI = (float)(1.7 * std::pow(dAU, -0.4)) * a.sunLumi[0];
  d.atmoThickness = (b.def.kind == BodyKind::Moon) ? 0.10f : 0.12f;
  return d;
}

// Lines far from the camera (relative to how far we are zoomed out) fade away, so a close-up of
// one world isn't crossed by the orbits of every other planet.
float distanceFade(const App& a, const dvec3& w) {
  const double d = glm::length(w - a.camPos);
  const double r = d / (std::exp(a.logDist) * 14.0);
  return (float)(1.0 / (1.0 + r * r));
}

void appendOrbit(std::vector<LineVertex>& out, const App& a, int i, const Elements& el,
                 const dvec3& parentRender, bool useBodyPhase) {
  const bool moon = a.sim.body(i).def.kind == BodyKind::Moon;
  const int parentIdx = a.sim.body(i).parent;
  static std::vector<dvec3> pts;
  sampleOrbit(el, a.sim.body(i).orbitFrame, 256, pts);
  out.clear();
  const double nu0 = el.bound ? el.nu : 0.0;
  for (size_t k = 0; k < pts.size(); ++k) {
    const dvec3 w = parentRender + (moon ? moonRenderOffset(a, parentIdx, pts[k]) : pts[k]);
    float alpha = 0.6f;
    if (el.bound && useBodyPhase) {
      const double nuK = 2.0 * 3.14159265358979 * (double)k / (pts.size() - 1);
      double behind = std::fmod(nu0 - nuK, 2.0 * 3.14159265358979);
      if (behind < 0) behind += 2.0 * 3.14159265358979;
      const double f = 1.0 - behind / (2.0 * 3.14159265358979);
      alpha = (float)(0.34 + 0.66 * std::pow(f, 3.0));
    }
    out.push_back({vec3((w - a.camPos) / 1000.0), alpha * distanceFade(a, w)});
  }
}

void renderScene(App& a, float timeSec) {
  Renderer& R = a.renderer;
  const int n = a.sim.count();

  // portrait of the selected body (rendered before the scene so ImGui can use it)
  if (a.selected >= 0 && !a.hideUI) {
    BodyDraw d = makeDraw(a, a.selected);
    const Body& b = a.sim.body(a.selected);
    d.look = b.def.look;
    d.rot = bodyRotation(glm::normalize(dvec3(0.20, 0.95, 0.24)), timeSec * 0.22 + b.spinPhase);
    R.renderPreview(d, timeSec);
  }

  R.beginScene(a.cam, timeSec, a.rs);
  const Body& sun = a.sim.body(0);
  R.drawBelt(vec3((a.rpos[0] - a.camPos) / 1000.0), a.sim.mu(0), a.sim.timeSeconds(), 1.7f);

  // bodies
  std::vector<BodyDraw> draws; std::vector<int> ids;
  for (int i = 0; i < n; ++i) {
    const Body& b = a.sim.body(i);
    if (b.def.kind == BodyKind::Moon && a.moonFade[i] <= 0.001f) continue;
    const dvec3 rel = a.rpos[i] - a.camPos;
    const double dist = glm::length(rel);
    if (glm::dot(rel, a.camFwd) < -a.rrad[i] * 1.5) continue;
    const double rpx = a.rrad[i] / std::max(dist, 1.0) * a.focalPx;
    if (rpx < 0.3 && i != 0) continue;
    draws.push_back(makeDraw(a, i));
    ids.push_back(i);
  }
  for (const BodyDraw& d : draws) R.drawBody(d);
  for (const BodyDraw& d : draws) R.drawRing(d);
  for (const BodyDraw& d : draws) R.drawAtmosphere(d);

  // the Sun's glow
  {
    const dvec3 rel = a.rpos[0] - a.camPos;
    const double dist = glm::length(rel);
    const float rad = (float)(a.rrad[0] / 1000.0);
    const float size = std::max(rad * 5.0f, (float)(dist / 1000.0) * 0.03f);
    const float lum = std::min(a.sunLumi[0], 30.f);
    if (glm::dot(rel, a.camFwd) > -a.rrad[0]) R.drawGlow(vec3(rel / 1000.0), size, std::min(rad / size, 0.9f), vec3(1.0f, 0.72f, 0.40f), 0.5f * std::sqrt(lum));
  }

  // orbits, trails, orbit-editor preview
  std::vector<LineVertex> verts;
  const bool wantLines = a.showOrbits || a.showTrails;
  for (int i = 1; wantLines && i < n; ++i) {
    const Body& b = a.sim.body(i);
    const bool moon = b.def.kind == BodyKind::Moon;
    const float fade = moon ? a.moonFade[i] : 1.f;
    if (fade <= 0.003f) continue;
    const dvec3 parentRender = a.rpos[b.parent];
    const bool sel = (i == a.selected);
    vec3 col = moon ? vec3(0.45f, 0.62f, 0.95f) : (b.def.kind == BodyKind::DwarfPlanet ? vec3(0.65f, 0.45f, 0.95f) : vec3(0.30f, 0.60f, 1.0f));
    if (sel) col = vec3(1.0f, 0.72f, 0.25f);
    if (a.showOrbits) {
      Elements el;
      if (a.sim.elements(i, el)) {
        appendOrbit(verts, a, i, el, parentRender, true);
        R.drawLines(verts, col * (sel ? 1.0f : 0.55f), sel ? 2.2f : 1.5f, fade * a.orbitOpacity * (sel ? 1.4f : 1.0f));
      }
    }
    if (a.showTrails && b.trailCount > 2) {
      verts.clear();
      const int cnt = b.trailCount;
      for (int k = 0; k < cnt; ++k) {
        const int idx = (b.trailHead - cnt + k + 2 * kTrailSamples) % kTrailSamples;
        const dvec3 w = parentRender + (moon ? moonRenderOffset(a, b.parent, b.trail[idx]) : b.trail[idx]);
        const float t = (float)(k + 1) / (float)(cnt + 1);
        verts.push_back({vec3((w - a.camPos) / 1000.0), t * t * distanceFade(a, w)});
      }
      const dvec3 head = a.rpos[i];
      verts.push_back({vec3((head - a.camPos) / 1000.0), 1.0f});
      R.drawLines(verts, vec3(0.9f, 0.95f, 1.0f) * (sel ? 1.2f : 0.75f), 1.8f, fade * 0.85f);
    }
  }
  if (a.edit.previewValid && a.selected >= 0 && a.edit.body == a.selected) {
    const Body& b = a.sim.body(a.selected);
    appendOrbit(verts, a, a.selected, a.edit.preview, a.rpos[b.parent], false);
    const float pulse = 0.75f + 0.25f * std::sin(timeSec * 4.f);
    R.drawLines(verts, a.edit.preview.bound ? vec3(1.0f, 0.35f, 0.75f) : vec3(1.0f, 0.2f, 0.2f), 2.6f, 1.6f * pulse);
  }
  (void)sun;
  R.endScene();
}

Options parseArgs(int argc, char** argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    const std::string s = argv[i];
    auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
    if (s == "--screenshot") o.screenshot = next();
    else if (s == "--frames") o.frames = std::atoi(next());
    else if (s == "--focus") o.focus = next();
    else if (s == "--select") o.select = next();
    else if (s == "--dist") o.dist = std::atof(next());
    else if (s == "--yaw") o.yawDeg = std::atof(next());
    else if (s == "--pitch") o.pitchDeg = std::atof(next());
    else if (s == "--speed") o.speedDaysPerSec = std::atof(next());
    else if (s == "--advance-days") o.advanceDays = std::atof(next());
    else if (s == "--width") o.width = std::atoi(next());
    else if (s == "--height") o.height = std::atoi(next());
    else if (s == "--no-ui") o.noUI = true;
    else if (s == "--true-scale") o.trueScale = true;
    else if (s == "--no-orbits") o.noOrbits = true;
    else if (s == "--system") o.system = true;
    else if (s == "--tab") o.tab = next();
    else if (s == "--click") { std::sscanf(next(), "%lf,%lf", &o.clickX, &o.clickY); }
    else if (s == "--edit-a") o.editA = std::atof(next());
    else if (s == "--edit-e") o.editE = (float)std::atof(next());
    else if (s == "--exag") o.exag = (float)std::atof(next());
    else if (s == "--paused") o.paused = true;
    else if (s == "--help" || s == "-h") {
      std::printf("Solar System Sandbox\n"
                  "  --width/--height N   window size\n"
                  "  --focus NAME         start focused on a body (e.g. Saturn)\n"
                  "  --select NAME        open the info panel for a body\n"
                  "  --dist KM            camera distance in km\n"
                  "  --true-scale         start in true-scale mode\n"
                  "  --screenshot FILE.png --frames N   render N frames, save a PNG and quit\n");
      std::exit(0);
    }
  }
  return o;
}

std::vector<std::string> shaderDirs(const char* argv0) {
  std::vector<std::string> d;
  std::string exe = argv0 ? argv0 : "";
  const size_t slash = exe.find_last_of("/\\");
  if (slash != std::string::npos) d.push_back(exe.substr(0, slash) + "/shaders");
  d.push_back("shaders");
  d.push_back(std::string(SOLAR_SOURCE_DIR) + "/shaders");
  return d;
}

}  // namespace

static int fatal(const char* msg) {
  std::fprintf(stderr, "FATAL: %s\n", msg);
  std::fflush(stderr);
#ifdef _WIN32
  std::string text = std::string(msg) + "\n\nDetails were written to solar_sandbox_log.txt (next to where you ran the program).";
  MessageBoxA(nullptr, text.c_str(), "Solar System Sandbox", MB_OK | MB_ICONERROR);
#endif
  return 1;
}

int main(int argc, char** argv) {
  if (!std::freopen("solar_sandbox_log.txt", "w", stderr)) { /* keep console stderr */ }
  glfwSetErrorCallback([](int code, const char* desc) { std::fprintf(stderr, "GLFW error %d: %s\n", code, desc); });
  const Options opt = parseArgs(argc, argv);
  if (!glfwInit()) return fatal("Failed to initialise GLFW");
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
  GLFWwindow* window = glfwCreateWindow(opt.width, opt.height, "Solar System Sandbox", nullptr, nullptr);
  if (!window) { glfwTerminate(); return fatal("Could not create a window with an OpenGL 3.3 context. Update your graphics driver."); }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(opt.frames > 0 ? 0 : 1);
  if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) return fatal("Failed to load OpenGL functions.");
  std::fprintf(stderr, "OpenGL: %s | %s\n", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));
  glfwSetScrollCallback(window, scrollCallback);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  float xs = 1, ys = 1;
  glfwGetWindowContentScale(window, &xs, &ys);
  setupImGui(std::max(1.0f, xs));
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330 core");

  App* app = new App();
  App& a = *app;
  if (!a.renderer.init(shaderDirs(argv[0]))) return fatal("Shader loading/compiling failed (shaders folder missing next to the exe, or your GPU rejected a shader).");

  // initial state
  computeRenderSpace(a);
  resetView(a);
  if (opt.trueScale) a.trueScale = true;
  if (opt.exag > 0) a.exag = opt.exag;
  if (opt.noOrbits) { a.showOrbits = false; a.showTrails = false; }
  if (!std::isnan(opt.speedDaysPerSec)) a.speedLog = (float)std::log10(std::max(opt.speedDaysPerSec, 1e-6) * 86400.0);
  a.hideUI = opt.noUI;
  a.paused = opt.paused;
  if (opt.advanceDays > 0) a.sim.advance(opt.advanceDays * kDay, 1e9);
  computeRenderSpace(a);
  if (!opt.select.empty()) a.selected = a.sim.find(opt.select);
  if (!opt.focus.empty()) {
    const int f = a.sim.find(opt.focus);
    if (f >= 0) {
      a.focus = f;
      a.target = a.rpos[f];
      a.logDist = a.goalLogDist = std::log(focusDistanceFor(a, f, opt.system));
      if (f > 0) {
        const dvec3 d = glm::normalize(a.rpos[0] - a.rpos[f]);
        a.yaw = std::atan2(d.y, d.x) + 0.7;
        a.pitch = std::asin(glm::clamp(d.z, -1.0, 1.0)) + 0.28;
      }
    }
  }
  if (opt.dist > 0) a.logDist = a.goalLogDist = std::log(opt.dist);
  if (opt.tab == "orbit") a.requestTab = 2; else if (opt.tab == "mass") a.requestTab = 3;
  if (a.selected >= 0 && (opt.editA != 1.0 || opt.editE >= 0)) {
    a.edit.body = a.selected;
    a.edit.aMul = opt.editA;
    if (opt.editE >= 0) { a.edit.e = opt.editE; a.edit.touchedE = true; }
    a.paused = true;
  }
  if (!std::isnan(opt.yawDeg)) a.yaw = opt.yawDeg * 3.14159265 / 180.0;
  if (!std::isnan(opt.pitchDeg)) a.pitch = opt.pitchDeg * 3.14159265 / 180.0;

  double last = glfwGetTime();
  int frame = 0;
  bool wasDir = true;
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    const double now = glfwGetTime();
    const double dt = std::min(now - last, 0.1);
    last = now;
    a.wallTime = now;

    int ww, wh, fw, fh;
    glfwGetWindowSize(window, &ww, &wh);
    glfwGetFramebufferSize(window, &fw, &fh);
    if (fw == 0 || fh == 0) { glfwWaitEvents(); continue; }
    a.winW = ww; a.winH = wh; a.fbW = fw; a.fbH = fh;
    a.renderer.resize(fw, fh);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    const bool dirNow = a.direction > 0;
    if (dirNow != wasDir) { a.sim.clearTrails(); wasDir = dirNow; }

    handleInput(a, window, dt);
    if (opt.clickX >= 0 && frame == 2) pickBody(a, opt.clickX, opt.clickY, false);
    stepSimulation(a, opt.frames > 0 ? 0.0 : dt);
    computeRenderSpace(a);
    updateCamera(a, opt.frames > 0 ? 1.0 : dt);
    if (!a.hideUI) drawUI(a);
    drawOverlays(a);

    renderScene(a, (float)now);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    ++frame;

    if (opt.frames > 0 && frame >= opt.frames) {
      if (!opt.screenshot.empty()) {
        std::vector<uint8_t> px; int w, h;
        a.renderer.readScreenshot(px, w, h);
        if (savePNG(opt.screenshot.c_str(), px.data(), w, h)) std::printf("saved %s (%dx%d)\n", opt.screenshot.c_str(), w, h);
        else std::fprintf(stderr, "could not write %s\n", opt.screenshot.c_str());
      }
      break;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F12, false)) {
      std::vector<uint8_t> px; int w, h;
      a.renderer.readScreenshot(px, w, h);
      savePNG("solar_sandbox_screenshot.png", px.data(), w, h);
    }
    glfwSwapBuffers(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  delete app;
  return 0;
}
