// Dear ImGui panels: time controls, explorer, body info, orbit editor, sandbox tweaks.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <imgui.h>
#include "app.h"

using glm::dvec3;

namespace {

const ImVec4 kAccent(1.0f, 0.74f, 0.28f, 1.0f);
const ImVec4 kCyan(0.45f, 0.80f, 1.0f, 1.0f);
const ImVec4 kDim(0.62f, 0.68f, 0.78f, 1.0f);
const ImVec4 kWarn(1.0f, 0.45f, 0.35f, 1.0f);
const ImVec4 kGood(0.45f, 0.90f, 0.55f, 1.0f);

constexpr double kEarthMass = 5.9722e24, kEarthRadius = 6371.0, kMoonMass = 7.342e22;
constexpr double kGSI = 6.6743e-11;
float gUiScale = 1.0f;
inline float S(float v) { return v * gUiScale; }

// ------------------------------------------------------------------ formatting
std::string sci(double v, int digits = 3) {
  if (v == 0) return "0";
  if (!std::isfinite(v)) return "inf";
  const int e = (int)std::floor(std::log10(std::fabs(v)));
  const double m = v / std::pow(10.0, e);
  char buf[64];
  if (e > -3 && e < 5) { std::snprintf(buf, sizeof buf, "%.*g", digits, v); return buf; }
  std::snprintf(buf, sizeof buf, "%.*g\xC3\x97" "10^%d", digits, m, e);   // "x" sign in UTF-8
  return buf;
}
std::string fmt(const char* f, double v) { char b[96]; std::snprintf(b, sizeof b, f, v); return b; }
std::string kmString(double km) {
  char b[96];
  if (km >= 0.05 * kAU) std::snprintf(b, sizeof b, "%.3f AU  (%s km)", km / kAU, sci(km, 4).c_str());
  else if (km >= 1e6) std::snprintf(b, sizeof b, "%.3f million km", km / 1e6);
  else std::snprintf(b, sizeof b, "%.0f km", km);
  return b;
}
std::string shortKm(double km) {
  char b[64];
  if (!std::isfinite(km)) return "infinite";
  if (km >= 0.05 * kAU) std::snprintf(b, sizeof b, "%.3f AU", km / kAU);
  else if (km >= 1e6) std::snprintf(b, sizeof b, "%.2f M km", km / 1e6);
  else std::snprintf(b, sizeof b, "%.0f km", km);
  return b;
}
std::string durationString(double seconds) {
  const double s = std::fabs(seconds);
  char b[96];
  if (s < 3600) std::snprintf(b, sizeof b, "%.1f min", s / 60);
  else if (s < 2 * 86400) std::snprintf(b, sizeof b, "%.2f hours", s / 3600);
  else if (s < 2 * kYear) std::snprintf(b, sizeof b, "%.2f days", s / 86400);
  else std::snprintf(b, sizeof b, "%.2f years", s / kYear);
  return b;
}
std::string speedString(double secPerSec) {
  char b[96];
  if (secPerSec < 60) std::snprintf(b, sizeof b, "%.0f s / s", secPerSec);
  else if (secPerSec < 3600) std::snprintf(b, sizeof b, "%.1f min / s", secPerSec / 60);
  else if (secPerSec < 86400) std::snprintf(b, sizeof b, "%.1f hours / s", secPerSec / 3600);
  else if (secPerSec < 86400 * 365.25) std::snprintf(b, sizeof b, "%.1f days / s", secPerSec / 86400);
  else std::snprintf(b, sizeof b, "%.2f years / s", secPerSec / kYear);
  return b;
}
std::string dateString(double jd) {
  double j = jd + 0.5;
  const double Z = std::floor(j), F = j - Z;
  double A = Z;
  if (Z >= 2299161) { const double al = std::floor((Z - 1867216.25) / 36524.25); A = Z + 1 + al - std::floor(al / 4); }
  const double B = A + 1524, C = std::floor((B - 122.1) / 365.25), D = std::floor(365.25 * C);
  const double E = std::floor((B - D) / 30.6001);
  const double day = B - D - std::floor(30.6001 * E);
  const int month = (int)(E < 14 ? E - 1 : E - 13);
  const int year = (int)(month > 2 ? C - 4716 : C - 4715);
  const int hh = (int)(F * 24), mm = (int)((F * 24 - hh) * 60);
  char b[64];
  std::snprintf(b, sizeof b, "%04d-%02d-%02d  %02d:%02d", year, month, (int)day, hh, mm);
  return b;
}
ImU32 colU32(const glm::vec3& c, float a = 1.f) {
  return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, a));
}
glm::vec3 accentOf(const Body& b) {
  if (b.def.kind == BodyKind::Star) return {1.0f, 0.8f, 0.3f};
  return glm::mix(b.def.look.c2, glm::vec3(1.f), 0.15f);
}
bool containsNoCase(const std::string& hay, const char* needle) {
  if (!needle[0]) return true;
  std::string h = hay, n = needle;
  for (char& c : h) c = (char)std::tolower((unsigned char)c);
  for (char& c : n) c = (char)std::tolower((unsigned char)c);
  return h.find(n) != std::string::npos;
}

void row(const char* label, const std::string& value, const ImVec4* col = nullptr) {
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::TextColored(kDim, "%s", label);
  ImGui::TableSetColumnIndex(1);
  ImGui::PushTextWrapPos(0.0f);
  if (col) ImGui::TextColored(*col, "%s", value.c_str()); else ImGui::TextUnformatted(value.c_str());
  ImGui::PopTextWrapPos();
}

// ------------------------------------------------------------------ physical facts
struct Phys {
  double massKg, meanRadiusKm, density, gravity, escape;
};
Phys physicalFacts(const Body& b) {
  Phys p;
  p.massKg = b.mass;
  p.meanRadiusKm = b.def.radiusKm * std::cbrt(1.0 - b.def.flattening);
  const double R = p.meanRadiusKm * 1000.0;
  p.density = b.mass / (4.0 / 3.0 * kPi * R * R * R);
  p.gravity = kGSI * b.mass / (R * R);
  p.escape = std::sqrt(2.0 * kGSI * b.mass / R) / 1000.0;
  return p;
}

// Hill radius of body i around its parent (km), using current osculating elements.
double hillRadius(const App& a, int i) {
  const Body& b = a.sim.body(i);
  if (b.parent < 0) return INFINITY;
  const Body& p = a.sim.body(b.parent);
  Elements el;
  if (!a.sim.elements(i, el) || !el.bound) return 0;
  const double mp = p.mass;
  return el.a * (1.0 - el.e) * std::cbrt(b.mass / (3.0 * mp));
}

struct Finding { ImVec4 color; std::string text; };

// What would happen if body i ended up on orbit `res` (parent-relative)?
std::vector<Finding> analyzeOrbit(const App& a, int i, const Elements& res, bool bound) {
  std::vector<Finding> out;
  const Body& b = a.sim.body(i);
  const Body& par = a.sim.body(b.parent);
  const bool moon = b.def.kind == BodyKind::Moon;
  const double rPar = par.radius;

  if (!bound) {
    out.push_back({kWarn, moon ? "ESCAPE: " + b.def.name + " is flung out of the " + par.def.name + " system for good."
                               : "ESCAPE: " + b.def.name + " leaves the solar system on a one-way trip."});
    return out;
  }
  if (res.periapsis() < rPar + b.radius)
    out.push_back({kWarn, "IMPACT: periapsis (" + kmString(res.periapsis()) + ") is inside " + par.def.name + " - " + b.def.name + " crashes into it."});
  else if (moon) {
    const Body& P = par;
    const double rhoP = physicalFacts(P).density, rhoM = physicalFacts(b).density;
    const double roche = 2.44 * P.radius * std::cbrt(rhoP / std::max(rhoM, 100.0));
    if (res.periapsis() < roche)
      out.push_back({kWarn, "ROCHE LIMIT: it passes within " + kmString(roche) + " of " + P.def.name + " - tides would tear it apart."});
  }
  if (moon && par.parent >= 0) {
    // Hill-sphere stability (apoapsis must stay well inside)
    Elements pe;
    if (a.sim.elements(b.parent, pe) && pe.bound) {
      const double rH = pe.a * (1 - pe.e) * std::cbrt(par.mass / (3.0 * a.sim.body(par.parent).mass));
      const bool retro = res.inc > kPi / 2;
      const double limit = (retro ? 0.70 : 0.50) * rH;
      if (res.apoapsis() > limit)
        out.push_back({kWarn, "UNSTABLE: apoapsis " + kmString(res.apoapsis()) + " exceeds the stable zone (" + kmString(limit) +
                                  ") of " + par.def.name + "'s Hill sphere - the Sun will likely steal it."});
      else
        out.push_back({kGood, "Stable: stays well inside " + par.def.name + "'s Hill sphere (" + kmString(rH) + ")."});
    }
  }
  if (!moon) {
    if (res.periapsis() < a.sim.body(0).radius * 1.5)
      out.push_back({kWarn, "FALLS INTO THE SUN: periapsis is only " + kmString(res.periapsis()) + "."});
    // crossing other planets' paths
    std::string crossing;
    for (int j = 1; j < a.sim.count(); ++j) {
      if (j == i) continue;
      const Body& o = a.sim.body(j);
      if (o.parent != b.parent || o.def.kind == BodyKind::Moon) continue;
      Elements oe;
      if (!a.sim.elements(j, oe) || !oe.bound) continue;
      if (res.periapsis() < oe.apoapsis() && res.apoapsis() > oe.periapsis())
        crossing += (crossing.empty() ? "" : ", ") + o.def.name;
    }
    if (!crossing.empty())
      out.push_back({kAccent, "ORBIT CROSSING: this path now intersects " + crossing + " - close encounters, ejections or collisions become possible."});
    // moons lost by shrinking Hill sphere
    if (!b.children.empty()) {
      const double rH = res.periapsis() * std::cbrt(b.mass / (3.0 * a.sim.body(0).mass));
      int lost = 0;
      for (int c : b.children) {
        Elements me;
        if (a.sim.elements(c, me) && me.bound && me.apoapsis() > 0.5 * rH) ++lost;
      }
      if (lost > 0)
        out.push_back({kWarn, std::to_string(lost) + " of " + std::to_string(b.children.size()) + " moons lie outside the stable zone at periapsis (Hill radius " + kmString(rH) + ") and may be lost."});
    }
  }
  if (out.empty() || (out.size() == 1 && out[0].color.x < 0.6f))
    out.push_back({kGood, "No immediate dangers detected on this orbit."});
  return out;
}

// ------------------------------------------------------------------ orbit editor
void updateEditorPreview(App& a, int i) {
  OrbitEditor& ed = a.edit;
  ed.previewValid = false;
  ed.active = ed.aMul != 1.0 || ed.touchedE || ed.touchedInc || ed.speedMul != 1.0;
  Elements cur;
  if (!a.sim.elements(i, cur) || !cur.bound) return;
  Elements pe = cur;
  pe.a = cur.a * ed.aMul;
  if (ed.touchedE) pe.e = ed.e;
  if (ed.touchedInc) pe.inc = ed.incDeg * kPi / 180.0;
  dvec3 r, v;
  elementsToState(pe, a.sim.body(i).orbitFrame, r, v);
  v *= ed.speedMul;
  Elements res;
  if (!stateToElements(r, v, cur.mu, a.sim.body(i).orbitFrame, res)) return;
  ed.pr = r; ed.pv = v; ed.preview = res;
  ed.previewValid = ed.active;
}

void resetEditor(App& a) {
  OrbitEditor& ed = a.edit;
  ed.aMul = 1.0; ed.speedMul = 1.0; ed.touchedE = ed.touchedInc = false;
  ed.active = false; ed.previewValid = false;
}

void orbitCompareRow(const char* label, const std::string& now, const std::string& after, bool changed) {
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0); ImGui::TextColored(kDim, "%s", label);
  ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(now.c_str());
  ImGui::TableSetColumnIndex(2);
  if (changed) ImGui::TextColored(kAccent, "%s", after.c_str()); else ImGui::TextUnformatted(after.c_str());
}

void drawOrbitTab(App& a, int i) {
  Body& b = a.sim.body(i);
  if (b.parent < 0) { ImGui::TextWrapped("The Sun sits at the centre of the system. Select a planet or moon to edit its orbit."); return; }
  OrbitEditor& ed = a.edit;
  if (ed.body != i) { resetEditor(a); ed.body = i; }

  Elements cur;
  if (!a.sim.elements(i, cur)) { ImGui::TextWrapped("Orbit undefined."); return; }
  const Body& par = a.sim.body(b.parent);
  ImGui::TextColored(kCyan, "Orbit around %s", par.def.name.c_str());
  if (!cur.bound) ImGui::TextColored(kWarn, "This body is on an unbound (escape) trajectory.");

  if (cur.bound) {
    if (!ed.touchedE) ed.e = (float)cur.e;
    if (!ed.touchedInc) ed.incDeg = (float)(cur.inc * 180.0 / kPi);

    ImGui::SeparatorText("Tweak the orbit");
    bool edited = false;
    float aMul = (float)ed.aMul;
    ImGui::SetNextItemWidth(-130);
    if (ImGui::SliderFloat("Size (a) ##a", &aMul, 0.1f, 5.0f, "x%.2f", ImGuiSliderFlags_Logarithmic)) { ed.aMul = aMul; edited = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Semi-major axis multiplier: bigger = farther away, slower, longer year");
    ImGui::SetNextItemWidth(-130);
    float e = ed.e;
    if (ImGui::SliderFloat("Eccentricity ##e", &e, 0.0f, 0.97f, "%.3f")) { ed.e = e; ed.touchedE = true; edited = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = perfect circle, closer to 1 = long stretched ellipse");
    ImGui::SetNextItemWidth(-130);
    float inc = ed.incDeg;
    if (ImGui::SliderFloat("Inclination ##i", &inc, 0.0f, 180.0f, "%.1f deg")) { ed.incDeg = inc; ed.touchedInc = true; edited = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tilt of the orbit plane. Above 90 deg the body orbits backwards (retrograde)");
    ImGui::SetNextItemWidth(-130);
    float sp = (float)ed.speedMul;
    if (ImGui::SliderFloat("Speed boost ##v", &sp, 0.3f, 1.8f, "x%.2f")) { ed.speedMul = sp; edited = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Multiplies the orbital velocity instantly (a burn). x1.41 = escape speed on a circular orbit");
    if (edited && !a.paused) a.paused = true;   // freeze time while editing

    updateEditorPreview(a, i);

    if (ed.active) {
      ImGui::TextColored(kAccent, "Time is paused while you edit.");
      const Elements& r = ed.preview;
      const bool bound = r.bound;
      ImGui::SeparatorText("Predicted result");
      if (ImGui::BeginTable("cmp", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(120));
        ImGui::TableSetupColumn("Now"); ImGui::TableSetupColumn("After");
        ImGui::TableHeadersRow();
        orbitCompareRow("Semi-major axis", shortKm(cur.a), bound ? shortKm(r.a) : "unbound", std::fabs(r.a / cur.a - 1) > 1e-3);
        orbitCompareRow("Eccentricity", fmt("%.4f", cur.e), fmt("%.4f", r.e), std::fabs(r.e - cur.e) > 1e-3);
        orbitCompareRow("Inclination", fmt("%.2f deg", cur.inc * 180 / kPi), fmt("%.2f deg", r.inc * 180 / kPi), std::fabs(r.inc - cur.inc) > 1e-3);
        orbitCompareRow("Period", durationString(cur.period()), bound ? durationString(r.period()) : "never returns", !bound || std::fabs(r.period() / cur.period() - 1) > 1e-3);
        orbitCompareRow("Closest", shortKm(cur.periapsis()), shortKm(r.periapsis()), std::fabs(r.periapsis() / cur.periapsis() - 1) > 1e-3);
        orbitCompareRow("Farthest", shortKm(cur.apoapsis()), bound ? shortKm(r.apoapsis()) : "infinite", !bound || std::fabs(r.apoapsis() / cur.apoapsis() - 1) > 1e-3);
        orbitCompareRow("Fastest speed", fmt("%.2f km/s", cur.speedAtPeri()), fmt("%.2f km/s", r.speedAtPeri()), true);
        ImGui::EndTable();
      }
      if (bound) {
        const double ratio = r.period() / cur.period();
        if (std::fabs(ratio - 1) > 1e-3)
          ImGui::TextColored(kCyan, "A year here becomes %.2fx as long (%s).", ratio, durationString(r.period()).c_str());
      }
      ImGui::SeparatorText("Consequences");
      for (const Finding& f : analyzeOrbit(a, i, r, bound)) {
        ImGui::PushStyleColor(ImGuiCol_Text, f.color);
        ImGui::TextWrapped("- %s", f.text.c_str());
        ImGui::PopStyleColor();
      }
      ImGui::Spacing();
      if (ImGui::Button("Apply orbit", ImVec2(120, 0))) {
        a.sim.setRelativeState(i, ed.pr, ed.pv);
        resetEditor(a);
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2(80, 0))) resetEditor(a);
      ImGui::SameLine();
      ImGui::TextColored(kDim, "then press Play");
    } else {
      ImGui::TextColored(kDim, "Drag a slider to preview a new orbit (shown in pink in the 3D view).");
    }
  }

  ImGui::SeparatorText("Current orbit (live)");
  if (ImGui::BeginTable("cur", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(150));
    ImGui::TableSetupColumn("");
    row("Semi-major axis", cur.bound ? kmString(cur.a) : "-");
    row("Eccentricity", fmt("%.4f", cur.e));
    row("Inclination", fmt("%.2f deg", cur.inc * 180 / kPi) + (cur.inc > kPi / 2 ? "  (retrograde)" : ""));
    row("Period", cur.bound ? durationString(cur.period()) : "-");
    row("Closest / farthest", cur.bound ? kmString(cur.periapsis()) + "  /  " + kmString(cur.apoapsis()) : "-");
    const double dist = glm::length(b.pos - par.pos);
    row("Distance now", kmString(dist));
    row("Speed now", fmt("%.2f km/s", glm::length(b.vel - par.vel)));
    ImGui::EndTable();
  }
  ImGui::Spacing();
  if (ImGui::Button("Restore original orbit")) {
    a.sim.restoreBody(i);
    resetEditor(a);
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Put this body back on its real orbit (and real mass)");
}

// ------------------------------------------------------------------ sandbox: mass etc.
void drawSandboxTab(App& a, int i) {
  Body& b = a.sim.body(i);
  const Phys p0 = physicalFacts(b);
  ImGui::TextWrapped("Change this body's mass and watch the physics respond: gravity, orbits of its moons and its neighbours all react.");
  float mul = (float)(b.mass / b.realMass);
  ImGui::SeparatorText("Mass");
  ImGui::SetNextItemWidth(-90);
  if (ImGui::SliderFloat("##mass", &mul, 0.01f, 1000.0f, "x%.3g real mass", ImGuiSliderFlags_Logarithmic)) {
    a.sim.setMassMultiplier(i, mul);
    a.edit.previewValid = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("x1")) a.sim.setMassMultiplier(i, 1.0);
  ImGui::TextColored(kDim, "Ctrl+click the slider to type an exact value.");

  const Phys p = physicalFacts(b);
  if (ImGui::BeginTable("mass", 3, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(150));
    ImGui::TableSetupColumn("Real"); ImGui::TableSetupColumn("Now");
    ImGui::TableHeadersRow();
    const double real = b.realMass;
    const double R = p.meanRadiusKm * 1000;
    orbitCompareRow("Mass", sci(real) + " kg", sci(p.massKg) + " kg", mul != 1.f);
    orbitCompareRow("Surface gravity", fmt("%.2f m/s\xC2\xB2", kGSI * real / (R * R)), fmt("%.2f m/s\xC2\xB2", p.gravity), mul != 1.f);
    orbitCompareRow("Escape velocity", fmt("%.2f km/s", std::sqrt(2 * kGSI * real / R) / 1000), fmt("%.2f km/s", p.escape), mul != 1.f);
    orbitCompareRow("Density", fmt("%.2f g/cm\xC2\xB3", real / (4.0 / 3 * kPi * R * R * R) / 1000), fmt("%.2f g/cm\xC2\xB3", p.density / 1000), mul != 1.f);
    orbitCompareRow("Your weight (70 kg)", fmt("%.0f N", 70 * kGSI * real / (R * R)), fmt("%.0f N", 70 * p.gravity), mul != 1.f);
    ImGui::EndTable();
  }
  (void)p0;

  ImGui::SeparatorText("Consequences");
  bool any = false;
  auto say = [&](const ImVec4& c, const std::string& s) { ImGui::PushStyleColor(ImGuiCol_Text, c); ImGui::TextWrapped("- %s", s.c_str()); ImGui::PopStyleColor(); any = true; };
  if (b.parent < 0) {
    const double L = std::pow(mul, 3.5), Rr = std::pow(mul, 0.8);
    say(kAccent, "Luminosity scales as mass^3.5: the Sun would shine " + fmt("%.3g", L) + "x as bright.");
    say(kCyan, "Its radius would be about " + fmt("%.3g", Rr) + "x the real one.");
    say(kCyan, "Planetary years change by " + fmt("%.3g", 1.0 / std::sqrt(mul)) + "x for orbits unchanged in size - but the planets keep their speeds, so most orbits will be reshaped.");
    say(kCyan, "The habitable zone moves to around " + fmt("%.2f", std::sqrt(L)) + " AU (Earth sits at 1 AU).");
    if (mul < 0.5f) say(kWarn, "With so little pull, planets move faster than the Sun's escape speed and will fly away.");
    if (mul > 2.0f) say(kWarn, "Planets are suddenly deep in a stronger gravity well: orbits become highly elliptical and may cross.");
  } else {
    const double rH = hillRadius(a, i);
    if (rH > 0 && std::isfinite(rH)) say(kCyan, "Hill sphere (region where this body's gravity dominates): " + kmString(rH) + ".");
    int lost = 0, total = 0;
    for (int c : b.children) {
      Elements me;
      if (!a.sim.elements(c, me)) continue;
      ++total;
      if (!me.bound || me.apoapsis() > 0.5 * rH * 1.0) ++lost;
    }
    if (total > 0) {
      if (lost > 0) say(kWarn, std::to_string(lost) + " of " + std::to_string(total) + " moons orbit beyond half the Hill radius and will drift away or be captured by the Sun.");
      else say(kGood, "All " + std::to_string(total) + " moons stay safely bound.");
    }
    if (b.def.kind != BodyKind::Moon) {
      if (mul > 5.f) say(kAccent, "A much heavier planet strongly perturbs neighbours - watch the other orbits change over time.");
      if (mul >= 80.f && b.def.kind == BodyKind::Planet) say(kAccent, "Above ~80 Jupiter masses an object would ignite as a star. Here it only gets gravity.");
    } else {
      const Body& par = a.sim.body(b.parent);
      if (mul > 20.f) say(kAccent, b.def.name + " now weighs " + fmt("%.2g", b.mass / par.mass * 100) + "% of " + par.def.name + ": both orbit a shared centre of mass.");
    }
    if (std::fabs(mul - 1.f) > 0.01f && b.def.kind != BodyKind::Moon) {
      say(kCyan, "Surface gravity is now " + fmt("%.2f", p.gravity / 9.80665) + " g.");
    }
  }
  if (!any) ImGui::TextColored(kDim, "Move the slider to see effects.");
  ImGui::Spacing();
  if (ImGui::Button("Reset mass")) a.sim.setMassMultiplier(i, 1.0);
}

// ------------------------------------------------------------------ overview tab
void drawOverview(App& a, int i) {
  Body& b = a.sim.body(i);
  const BodyDef& d = b.def;
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(d.description.c_str());
  ImGui::PopTextWrapPos();
  ImGui::Spacing();

  const Phys p = physicalFacts(b);
  ImGui::SeparatorText("Physical profile");
  if (ImGui::BeginTable("phys", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(150));
    ImGui::TableSetupColumn("");
    std::string m = sci(p.massKg) + " kg";
    if (b.def.kind == BodyKind::Moon) {
      m += p.massKg > 0.05 * kMoonMass ? "  (" + fmt("%.3g", p.massKg / kMoonMass) + " Moons)" : "";
    } else if (b.def.kind == BodyKind::Star) m += "  (" + sci(p.massKg / kEarthMass) + " Earths)";
    else m += "  (" + fmt("%.3g", p.massKg / kEarthMass) + " Earths)";
    const double massMul = b.mass / b.realMass;
    ImVec4 mc = kAccent;
    row("Mass", m + (std::fabs(massMul - 1) > 1e-6 ? "  [x" + fmt("%.3g", massMul) + " modified]" : ""), std::fabs(massMul - 1) > 1e-6 ? &mc : nullptr);
    row("Mean radius", fmt("%.1f km", p.meanRadiusKm) + "  (" + fmt("%.3g", p.meanRadiusKm / kEarthRadius) + " Earths)");
    row("Diameter", fmt("%.0f km", 2 * d.radiusKm));
    if (d.flattening > 0.01) row("Flattening", fmt("%.1f %%", d.flattening * 100) + " (squashed by its spin)");
    row("Density", fmt("%.2f g/cm\xC2\xB3", p.density / 1000));
    row("Surface gravity", fmt("%.2f m/s\xC2\xB2", p.gravity) + "  (" + fmt("%.2f", p.gravity / 9.80665) + " g)");
    row("Escape velocity", fmt("%.2f km/s", p.escape));
    if (d.rotHours != 0) {
      const double h = std::fabs(d.rotHours);
      row("Day (rotation)", (h < 48 ? fmt("%.2f hours", h) : fmt("%.2f days", h / 24)) + (d.rotHours < 0 ? "  (retrograde)" : ""));
    } else if (b.parent >= 0) row("Day (rotation)", "Tidally locked: one face always toward " + a.sim.body(b.parent).def.name);
    if (d.kind != BodyKind::Moon || d.tiltDeg > 0) row("Axial tilt", fmt("%.2f deg", d.tiltDeg));
    if (d.tempK > 0) row(d.kind == BodyKind::Star ? "Surface temperature" : "Mean temperature",
                         fmt("%.0f K", d.tempK) + "  (" + fmt("%.0f", d.tempK - 273.15) + " \xC2\xB0" "C)");
    if (d.kind != BodyKind::Moon) {
      if (d.kind == BodyKind::Star) row("Moons", "-");
      else row("Known moons", std::to_string(d.moonsKnown) + (b.children.empty() ? "" : "  (" + std::to_string(b.children.size()) + " modelled here)"));
    }
    if (d.look.ringOuter > 0) row("Rings", "Yes");
    ImGui::EndTable();
  }

  if (b.parent >= 0) {
    ImGui::SeparatorText("Orbit");
    Elements el;
    a.sim.elements(i, el);
    if (ImGui::BeginTable("orb", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(150));
      ImGui::TableSetupColumn("");
      const Body& par = a.sim.body(b.parent);
      row("Orbits", par.def.name);
      row(d.kind == BodyKind::Moon ? "Dist. from planet" : "Dist. from Sun", kmString(glm::length(b.pos - par.pos)));
      row("Semi-major axis", el.bound ? kmString(el.a) : "-");
      row(d.kind == BodyKind::Moon ? "Orbital period" : "Year length", el.bound ? durationString(el.period()) : "-");
      row("Eccentricity", fmt("%.4f", el.e));
      row("Inclination", fmt("%.2f deg", el.inc * 180 / kPi));
      row("Orbital speed", fmt("%.2f km/s", glm::length(b.vel - par.vel)));
      if (d.kind == BodyKind::Moon) {
        const Body& sunB = a.sim.body(0);
        row("Dist. from Sun", fmt("%.3f AU", glm::length(b.pos - sunB.pos) / kAU));
      }
      ImGui::EndTable();
    }
  }
}

void drawInfoPanel(App& a) {
  if (a.selected < 0) return;
  const int i = a.selected;
  Body& b = a.sim.body(i);
  ImGui::SetNextWindowPos(ImVec2((float)a.winW - 14, 14), ImGuiCond_Always, ImVec2(1, 0));
  ImGui::SetNextWindowSizeConstraints(ImVec2(S(360), 200), ImVec2(S(520), (float)a.winH - 28));
  ImGui::SetNextWindowSize(ImVec2(S(400), (float)a.winH - 28), ImGuiCond_FirstUseEver);
  bool open = true;
  ImGui::Begin("##info", &open, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);
  if (!open) a.selected = -1;

  const glm::vec3 acc = accentOf(b);
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colU32(acc)));
  ImGui::SetWindowFontScale(1.45f);
  ImGui::TextUnformatted(b.def.name.c_str());
  ImGui::SetWindowFontScale(1.0f);
  ImGui::PopStyleColor();
  ImGui::SameLine(ImGui::GetWindowWidth() - 34);
  if (ImGui::SmallButton("x")) a.selected = -1;
  ImGui::TextColored(kDim, "%s%s%s", b.def.group.c_str(), b.parent >= 0 ? "  \xC2\xB7  " : "", b.parent >= 0 ? ("orbits " + a.sim.body(b.parent).def.name).c_str() : "");

  // portrait
  const float w = ImGui::GetContentRegionAvail().x;
  const float side = std::min(w, S(330.f));
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (w - side) * 0.5f);
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  ImGui::Image((ImTextureID)(intptr_t)a.renderer.previewTexture(), ImVec2(side, side), ImVec2(0, 1), ImVec2(1, 0));
  ImGui::GetWindowDrawList()->AddRect(p0, ImVec2(p0.x + side, p0.y + side), IM_COL32(255, 255, 255, 40), 8.f, 0, 1.2f);

  if (ImGui::Button("Focus camera (F)")) focusOn(a, i);
  ImGui::SameLine();
  if (!b.children.empty() && ImGui::Button("Show moons")) {
    a.selected = i; focusOn(a, i, false, true);
  }
  ImGui::Spacing();

  if (ImGui::BeginTabBar("##tabs")) {
    const int req = a.requestTab; a.requestTab = 0;
    if (ImGui::BeginTabItem("Overview", nullptr, req == 1 ? ImGuiTabItemFlags_SetSelected : 0)) { ImGui::BeginChild("##ov", ImVec2(0, 0)); drawOverview(a, i); ImGui::EndChild(); ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("Orbit", nullptr, req == 2 ? ImGuiTabItemFlags_SetSelected : 0)) { ImGui::BeginChild("##ob", ImVec2(0, 0)); drawOrbitTab(a, i); ImGui::EndChild(); ImGui::EndTabItem(); }
    else if (a.edit.body == i) a.edit.previewValid = false;
    if (ImGui::BeginTabItem("Mass", nullptr, req == 3 ? ImGuiTabItemFlags_SetSelected : 0)) { ImGui::BeginChild("##ms", ImVec2(0, 0)); drawSandboxTab(a, i); ImGui::EndChild(); ImGui::EndTabItem(); }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

// ------------------------------------------------------------------ explorer
void bodyEntry(App& a, int i, bool indent) {
  const Body& b = a.sim.body(i);
  ImGui::PushID(i);
  const glm::vec3 c = accentOf(b);
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float h = ImGui::GetTextLineHeight();
  ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos.x + 8, pos.y + h * 0.5f + 1), 4.5f, colU32(c));
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
  if (ImGui::Selectable(b.def.name.c_str(), a.selected == i, 0, ImVec2(0, 0))) {
    a.selected = i;
    focusOn(a, i);
  }
  (void)indent;
  ImGui::PopID();
}

void drawExplorer(App& a) {
  ImGui::SetNextWindowPos(ImVec2(14, 14), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(S(300), (float)a.winH - 28), ImGuiCond_Always);
  ImGui::Begin("Solar System", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);

  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##search", "Search planets & moons...", a.search, sizeof a.search);
  ImGui::Spacing();

  ImGui::BeginChild("##list", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.52f), false);
  const bool filtering = a.search[0] != 0;
  if (filtering) {
    for (int i = 0; i < a.sim.count(); ++i)
      if (containsNoCase(a.sim.body(i).def.name, a.search)) bodyEntry(a, i, false);
  } else {
    bodyEntry(a, 0, false);
    for (int i = 1; i < a.sim.count(); ++i) {
      const Body& b = a.sim.body(i);
      if (b.parent != 0) continue;
      if (b.children.empty()) { ImGui::Indent(10); bodyEntry(a, i, true); ImGui::Unindent(10); continue; }
      ImGui::PushID(i);
      if (a.selected >= 0 && a.sim.body(a.selected).parent == i) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
      const bool open = ImGui::TreeNodeEx(("##n" + b.def.name).c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow);
      ImGui::SameLine(0, 4);
      bodyEntry(a, i, true);
      if (open) {
        ImGui::Indent(8);
        for (int c : b.children) bodyEntry(a, c, true);
        ImGui::Unindent(8);
        ImGui::TreePop();
      }
      ImGui::PopID();
    }
  }
  ImGui::EndChild();

  ImGui::Separator();
  ImGui::BeginChild("##opts", ImVec2(0, 0));
  if (ImGui::CollapsingHeader("View", ImGuiTreeNodeFlags_DefaultOpen)) {
    int mode = a.trueScale ? 1 : 0;
    if (ImGui::RadioButton("Stylized scale", &mode, 0)) a.trueScale = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("True scale", &mode, 1)) a.trueScale = true;
    a.trueScale = mode == 1;
    if (!a.trueScale) {
      ImGui::SetNextItemWidth(-1);
      ImGui::SliderFloat("##exag", &a.exag, 1.f, 60.f, "Size boost x%.0f");
    } else ImGui::TextColored(kDim, "Real sizes: planets are tiny dots.");
    ImGui::Checkbox("Orbits", &a.showOrbits); ImGui::SameLine(110);
    ImGui::Checkbox("Trails", &a.showTrails); ImGui::SameLine(200);
    ImGui::Checkbox("Labels", &a.showLabels);
    ImGui::Checkbox("Belts", &a.rs.belts); ImGui::SameLine(110);
    ImGui::Checkbox("Markers", &a.showMarkers);
    ImGui::SetNextItemWidth(-82); ImGui::SliderFloat("Bloom", &a.rs.bloom, 0.f, 2.f, "%.2f");
    ImGui::SetNextItemWidth(-82); ImGui::SliderFloat("Exposure", &a.rs.exposure, 0.4f, 2.5f, "%.2f");
    ImGui::SetNextItemWidth(-82); ImGui::SliderFloat("Orbit glow", &a.orbitOpacity, 0.f, 2.f, "%.2f");
  }
  if (ImGui::CollapsingHeader("Physics")) {
    float g = (float)a.sim.settings.gScale;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##g", &g, 0.2f, 5.0f, "Gravity (G) x%.2f", ImGuiSliderFlags_Logarithmic)) {
      a.sim.settings.gScale = g;
      a.sim.refreshClassification();
    }
    ImGui::TextWrapped("Changing G while planets keep their speed reshapes every orbit: try x2 and watch.");
    int steps = a.sim.settings.stepsPerOrbit;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt("##steps", &steps, 24, 400, "Accuracy: %d steps/orbit")) { a.sim.settings.stepsPerOrbit = steps; a.sim.refreshClassification(); }
    ImGui::TextColored(kDim, "Energy drift: %.1e", a.sim.accumulatedEnergyDrift());
    if (ImGui::Button("Reset simulation", ImVec2(-1, 0))) {
      a.sim.reset();
      a.edit = OrbitEditor{};
      a.sim.clearTrails();
      a.paused = false;
    }
  }
  if (ImGui::CollapsingHeader("Controls")) {
    ImGui::TextWrapped("Mouse wheel: zoom in / out\nDrag (left or wheel button): rotate the view\nShift + wheel: spin sideways\nCtrl + wheel: tilt\nClick a body: select it\nDouble-click: fly to it\n\nSpace: pause   R: reverse time\n[ ]: slower / faster\nF: focus   Home: reset view\nL / O / T: labels / orbits / trails\nH: hide interface   F12: screenshot\n0-8: jump to Sun, Mercury ... Neptune\nArrow keys: rotate");
  }
  ImGui::EndChild();
  ImGui::End();
}

void drawTimeBar(App& a) {
  ImGui::SetNextWindowPos(ImVec2((float)a.winW * 0.5f, 14), ImGuiCond_Always, ImVec2(0.5f, 0));
  ImGui::Begin("##time", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
  if (ImGui::Button(a.paused ? "  Play  " : " Pause ")) a.paused = !a.paused;
  ImGui::SameLine();
  if (ImGui::Button(a.direction > 0 ? "Forward" : "Reverse")) { a.direction = -a.direction; a.sim.clearTrails(); }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(230);
  ImGui::SliderFloat("##speed", &a.speedLog, 0.f, 8.5f, "");
  ImGui::SameLine();
  ImGui::TextColored(kAccent, "%s", speedString(std::pow(10.0, a.speedLog)).c_str());
  static const struct { const char* n; float v; } presets[] = {{"1 h/s", 3.556f}, {"1 d/s", 4.937f}, {"10 d/s", 5.937f}, {"1 mo/s", 6.42f}, {"1 yr/s", 7.499f}};
  for (const auto& p : presets) {
    ImGui::SameLine();
    if (ImGui::SmallButton(p.n)) a.speedLog = p.v;
  }
  ImGui::TextColored(kCyan, "%s", dateString(a.sim.julianDay()).c_str());
  ImGui::SameLine();
  ImGui::TextColored(kDim, " (J2000 %+.2f yr)", a.sim.timeSeconds() / kYear);
  if (!a.paused && a.achieved < 0.97)
    { ImGui::SameLine(); ImGui::TextColored(kWarn, "  CPU-limited: %.0f%% of requested speed", a.achieved * 100); }
  if (a.paused) { ImGui::SameLine(); ImGui::TextColored(kAccent, "  PAUSED"); }
  ImGui::End();
}

}  // namespace

// ------------------------------------------------------------------ public
void setupImGui(float scale) {
  ImGuiIO& io = ImGui::GetIO();
  gUiScale = scale;
  io.IniFilename = nullptr;
  const char* fonts[] = {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
                         "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                         "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                         "/System/Library/Fonts/Helvetica.ttc", "/Library/Fonts/Arial.ttf"};
  bool loaded = false;
  for (const char* f : fonts) {
    FILE* fp = std::fopen(f, "rb");
    if (!fp) continue;
    std::fclose(fp);
    if (io.Fonts->AddFontFromFileTTF(f, 16.0f * scale)) { loaded = true; break; }
  }
  if (!loaded) { io.Fonts->AddFontDefault(); io.FontGlobalScale = 1.15f * scale; }

  ImGuiStyle& s = ImGui::GetStyle();
  s.WindowRounding = 10; s.FrameRounding = 6; s.GrabRounding = 6; s.ChildRounding = 6;
  s.PopupRounding = 6; s.TabRounding = 6; s.ScrollbarRounding = 8;
  s.WindowBorderSize = 1; s.FrameBorderSize = 0;
  s.WindowPadding = ImVec2(12, 10); s.FramePadding = ImVec2(8, 4); s.ItemSpacing = ImVec2(8, 6);
  s.ScaleAllSizes(scale);
  ImVec4* c = s.Colors;
  c[ImGuiCol_WindowBg] = ImVec4(0.035f, 0.045f, 0.075f, 0.82f);
  c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_PopupBg] = ImVec4(0.05f, 0.06f, 0.10f, 0.95f);
  c[ImGuiCol_Border] = ImVec4(0.45f, 0.60f, 0.95f, 0.22f);
  c[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.15f, 0.24f, 0.85f);
  c[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.23f, 0.36f, 0.9f);
  c[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.28f, 0.44f, 0.95f);
  c[ImGuiCol_TitleBg] = ImVec4(0.05f, 0.07f, 0.12f, 0.9f);
  c[ImGuiCol_TitleBgActive] = ImVec4(0.07f, 0.10f, 0.18f, 0.95f);
  c[ImGuiCol_Button] = ImVec4(0.16f, 0.24f, 0.42f, 0.85f);
  c[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.36f, 0.62f, 0.95f);
  c[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.45f, 0.75f, 1.0f);
  c[ImGuiCol_Header] = ImVec4(0.20f, 0.30f, 0.52f, 0.55f);
  c[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.38f, 0.64f, 0.75f);
  c[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.45f, 0.75f, 0.9f);
  c[ImGuiCol_SliderGrab] = ImVec4(1.0f, 0.74f, 0.28f, 0.95f);
  c[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.85f, 0.45f, 1.0f);
  c[ImGuiCol_CheckMark] = ImVec4(1.0f, 0.74f, 0.28f, 1.0f);
  c[ImGuiCol_Tab] = ImVec4(0.12f, 0.17f, 0.30f, 0.85f);
  c[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.38f, 0.64f, 0.95f);
  c[ImGuiCol_TabSelected] = ImVec4(0.20f, 0.32f, 0.58f, 1.0f);
  c[ImGuiCol_Separator] = ImVec4(0.45f, 0.60f, 0.95f, 0.25f);
  c[ImGuiCol_Text] = ImVec4(0.90f, 0.93f, 0.98f, 1.0f);
  c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f);
  c[ImGuiCol_TableHeaderBg] = ImVec4(0.12f, 0.17f, 0.30f, 0.9f);
}

void drawUI(App& a) {
  drawTimeBar(a);
  drawExplorer(a);
  drawInfoPanel(a);
}

void drawOverlays(App& a) {
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  ImGuiIO& io = ImGui::GetIO();
  const float scale = (float)a.winH / std::max(1, a.fbH);
  int hover = -1;
  float hoverScore = 1e9f;
  const bool uiFree = !io.WantCaptureMouse;
  for (int i = 0; i < a.sim.count(); ++i) {
    const Body& b = a.sim.body(i);
    const bool moon = b.def.kind == BodyKind::Moon;
    const float fade = moon ? a.moonFade[i] : 1.f;
    if (fade < 0.04f) continue;
    ImVec2 p; float dist;
    if (!projectToScreen(a, a.rpos[i], p, dist)) continue;
    if (p.x < -50 || p.y < -50 || p.x > a.winW + 50 || p.y > a.winH + 50) continue;
    const float rpx = (float)(a.rrad[i] / std::max<double>(dist, 1.0)) * a.focalPx * scale;
    const bool sel = (i == a.selected);
    const glm::vec3 c = accentOf(b);
    const float alpha = fade;

    // marker for things too small to see
    if (a.showMarkers && rpx < (moon ? 2.5f : 5.0f)) {
      const float r = moon ? 2.4f : 4.5f;
      dl->AddCircle(p, r, colU32(glm::mix(c, glm::vec3(1.f), 0.4f), 0.85f * alpha), 20, moon ? 1.2f : 1.6f);
    }
    if (sel) {
      const float r = std::max(rpx + 7.f, 12.f);
      dl->AddCircle(p, r, IM_COL32(255, 190, 70, 230), 40, 1.8f);
      const float t = (float)ImGui::GetTime();
      dl->AddCircle(p, r + 3.f + 2.f * std::sin(t * 3.f), IM_COL32(255, 190, 70, 60), 40, 1.2f);
    }
    // labels
    bool showLabel = a.showLabels || sel;
    if (moon && !sel) {
      const double orbitPx = b.def.aKm * a.renderScale() / std::max<double>(glm::length(a.rpos[b.parent] - a.camPos), 1.0) * a.focalPx * scale;
      showLabel = a.showLabels && fade > 0.5f && orbitPx > 34.0 && (b.radius > 400 || orbitPx > 90.0);
      if (a.focus == i) showLabel = a.showLabels;
    }
    if (showLabel) {
      const float off = std::max(rpx, moon ? 3.f : 5.f) + 7.f;
      const ImVec2 tp(p.x + off, p.y - 8.f);
      const std::string& name = b.def.name;
      dl->AddText(ImVec2(tp.x + 1, tp.y + 1), IM_COL32(0, 0, 0, (int)(190 * alpha)), name.c_str());
      dl->AddText(tp, sel ? IM_COL32(255, 205, 110, (int)(255 * alpha)) : IM_COL32(215, 228, 255, (int)(235 * alpha)), name.c_str());
    }
    if (uiFree && alpha > 0.4f) {
      const float d = std::hypot(p.x - io.MousePos.x, p.y - io.MousePos.y);
      const float hit = std::max(rpx, 10.f);
      if (d < hit && d < hoverScore) { hover = i; hoverScore = d; }
    }
  }
  if (hover >= 0 && !a.hideUI) {
    ImGui::BeginTooltip();
    const Body& b = a.sim.body(hover);
    ImGui::TextColored(kAccent, "%s", b.def.name.c_str());
    ImGui::TextColored(kDim, "%s", b.def.group.c_str());
    ImGui::TextColored(kDim, "click to select - double-click to fly there");
    ImGui::EndTooltip();
  }
}
