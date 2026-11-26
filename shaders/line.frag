in float gAlpha;
in float gEdge;
out vec4 fragColor;
uniform vec3 uColor;
uniform float uFade;
void main() {
  float edge = 1.0 - smoothstep(0.35, 1.0, abs(gEdge));
  float a = gAlpha * edge * uFade;
  if (a <= 0.002) discard;
  fragColor = vec4(outColor(uColor * a), 1.0);
}
