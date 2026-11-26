in vec2 vP;
out vec4 fragColor;
uniform vec3 uColor;
uniform float uCoreRadius;   // fraction of the sprite occupied by the solid disc
uniform float uIntensity;
void main() {
  float r = length(vP);
  if (r > 1.0) discard;
  float x = max(r / uCoreRadius, 1.0);
  float g = 1.0 / (x * x * x) * (1.0 - smoothstep(0.7, 1.0, r));
  g += 0.06 / (1.0 + 20.0 * (r - uCoreRadius) * (r - uCoreRadius) * 4.0) * step(uCoreRadius, r) * (1.0 - smoothstep(0.6, 1.0, r));
  fragColor = vec4(outColor(uColor * g * uIntensity), 1.0);
}
