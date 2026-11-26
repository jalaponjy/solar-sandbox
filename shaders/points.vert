// Asteroid / Kuiper belt particles on Keplerian orbits evaluated on the GPU.
layout(location = 0) in vec3 aElem;    // a (km), e, inclination (rad)
layout(location = 1) in vec3 aAng;     // node, argument of periapsis, mean anomaly at epoch (rad)
uniform mat4 uVP;
uniform vec3 uCamRel;                  // world position of the sun in camera-relative scene units
uniform float uMu;                     // km^3/s^2 of the Sun
uniform float uTimeSec;                // seconds since J2000
uniform float uPointSize;
uniform float uMoonScale;
out float vShade;
void main() {
  float a = aElem.x, e = aElem.y, inc = aElem.z;
  float n = sqrt(uMu / (a * a * a));
  float M = aAng.z + n * uTimeSec;
  float E = M;
  for (int i = 0; i < 6; i++) E -= (E - e * sin(E) - M) / (1.0 - e * cos(E));
  float x = a * (cos(E) - e), y = a * sqrt(1.0 - e * e) * sin(E);
  float cw = cos(aAng.y), sw = sin(aAng.y), cO = cos(aAng.x), sO = sin(aAng.x), ci = cos(inc), si = sin(inc);
  vec3 pos = vec3((cO * cw - sO * sw * ci) * x + (-cO * sw - sO * cw * ci) * y,
                  (sO * cw + cO * sw * ci) * x + (-sO * sw + cO * cw * ci) * y,
                  (sw * si) * x + (cw * si) * y);
  vec3 w = pos / 1000.0 + uCamRel;     // scene units are 1000 km
  vShade = 0.5 + 0.5 * fract(sin(a * 12.9898 + e * 78.233) * 43758.5453);
  gl_Position = applyLogDepth(uVP * vec4(w, 1.0));
  gl_PointSize = uPointSize * (0.7 + 0.6 * vShade);
}
