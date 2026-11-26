in float vShade;
out vec4 fragColor;
uniform vec3 uColor;
void main() {
  vec2 c = gl_PointCoord * 2.0 - 1.0;
  float a = smoothstep(1.0, 0.2, length(c));
  fragColor = vec4(outColor(uColor * vShade * a), 1.0);
}
