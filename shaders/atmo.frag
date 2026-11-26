// Analytic atmosphere: integrates a exponential density along the view ray inside a shell.
in vec3 vWorld;
out vec4 fragColor;
uniform vec3 uCenter;
uniform float uPlanetRadius;
uniform float uShellRadius;
uniform vec4 uAtmo;       // rgb colour, strength
uniform vec3 uSunDir;
uniform float uSunI;
void main() {
  vec3 ro = vec3(0.0);                 // camera at the origin
  vec3 rd = normalize(vWorld);
  vec3 oc = ro - uCenter;
  float b = dot(oc, rd);
  float cShell = dot(oc, oc) - uShellRadius * uShellRadius;
  float disc = b * b - cShell;
  if (disc <= 0.0) discard;
  float sq = sqrt(disc);
  float tIn = max(-b - sq, 0.0), tOut = -b + sq;
  // planet occlusion
  float cP = dot(oc, oc) - uPlanetRadius * uPlanetRadius;
  float dP = b * b - cP;
  if (dP > 0.0) {
    float tHit = -b - sqrt(dP);
    if (tHit > 0.0) tOut = min(tOut, tHit);
  }
  float len = max(tOut - tIn, 0.0);
  if (len <= 0.0) discard;
  vec3 mid = ro + rd * (tIn + 0.5 * len);
  vec3 nrm = normalize(mid - uCenter);
  float alt = (length(mid - uCenter) - uPlanetRadius) / uPlanetRadius;
  float shellT = (uShellRadius - uPlanetRadius) / uPlanetRadius;
  float dens = exp(-max(alt, 0.0) / (0.26 * shellT));
  float I = len / uPlanetRadius * dens;
  float ndl = dot(nrm, normalize(uSunDir));
  float lit = smoothstep(-0.35, 0.55, ndl);
  // sunset tint near the terminator
  vec3 tint = mix(vec3(1.0, 0.45, 0.2), uAtmo.rgb, smoothstep(-0.05, 0.45, ndl));
  vec3 col = tint * I * lit * uAtmo.a * 0.75 * uSunI;
  fragColor = vec4(outColor(col), 1.0);
}
