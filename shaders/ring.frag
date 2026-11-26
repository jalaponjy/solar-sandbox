in vec3 vWorld;
in vec3 vLocal;
out vec4 fragColor;
uniform mat3 uRot;
uniform vec3 uCenter;
uniform float uRadius;
uniform float uRingIn, uRingOut, uRingOp;
uniform vec3 uRingColor;
uniform vec3 uSunDir;
uniform float uSunI;
void main() {
  float rr = length(vLocal.xy);
  float x = (rr - uRingIn) / (uRingOut - uRingIn);
  if (x < 0.0 || x > 1.0) discard;
  float dens = ringDensity(x) * uRingOp;
  if (dens < 0.004) discard;
  vec3 L = normalize(uSunDir);
  vec3 Nr = normalize(uRot * vec3(0, 0, 1));
  vec3 V = normalize(-vWorld);
  float lit = 0.30 + 0.70 * abs(dot(Nr, L));
  // planet shadow on the rings
  vec3 oc = vWorld - uCenter;
  float b = dot(oc, L);
  float c = dot(oc, oc) - uRadius * uRadius;
  float disc = b * b - c;
  float shadow = 1.0;
  if (disc > 0.0 && -b - sqrt(disc) > -1e30 && (-b + sqrt(disc)) > 0.0 && b < 0.0)
    shadow = 0.04;
  float tint = 0.85 + 0.3 * noise3(vec3(x * 55.0, 3.0, 1.0));
  // forward scattering: rings glow when seen against the sun side
  float fwd = pow(max(dot(-V, L), 0.0), 3.0) * 0.6;
  vec3 col = srgb2lin(uRingColor) * tint * (lit * shadow * uSunI + fwd * uSunI * 0.6 * shadow);
  fragColor = vec4(outColor(col), clamp(dens, 0.0, 1.0));
}
