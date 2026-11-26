// Expands line segments into screen-space quads of constant pixel width (clipped against the near plane).
layout(lines) in;
layout(triangle_strip, max_vertices = 4) out;
in vec3 vPos[];
in float vAlpha[];
out float gAlpha;
out float gEdge;
uniform mat4 uVP;
uniform vec2 uViewport;
uniform float uWidth;
void main() {
  vec4 c0 = uVP * vec4(vPos[0], 1.0), c1 = uVP * vec4(vPos[1], 1.0);
  float a0 = vAlpha[0], a1 = vAlpha[1];
  const float nearW = 1e-5;
  if (c0.w < nearW && c1.w < nearW) return;
  if (c0.w < nearW) { float t = (nearW - c0.w) / (c1.w - c0.w); c0 = mix(c0, c1, t); a0 = mix(a0, a1, t); }
  if (c1.w < nearW) { float t = (nearW - c1.w) / (c0.w - c1.w); c1 = mix(c1, c0, t); a1 = mix(a1, a0, t); }
  vec2 s0 = c0.xy / c0.w * uViewport * 0.5, s1 = c1.xy / c1.w * uViewport * 0.5;
  vec2 d = s1 - s0;
  float l = length(d);
  d = l > 1e-4 ? d / l : vec2(1, 0);
  vec2 n = vec2(-d.y, d.x) * (uWidth * 0.5 + 0.75) / (uViewport * 0.5);
  vec2 e = d * 0.0;
  gAlpha = a0; gEdge = -1.0; gl_Position = applyLogDepth(vec4(c0.xy + n * c0.w, c0.z, c0.w)); EmitVertex();
  gAlpha = a0; gEdge =  1.0; gl_Position = applyLogDepth(vec4(c0.xy - n * c0.w, c0.z, c0.w)); EmitVertex();
  gAlpha = a1; gEdge = -1.0; gl_Position = applyLogDepth(vec4(c1.xy + n * c1.w, c1.z, c1.w)); EmitVertex();
  gAlpha = a1; gEdge =  1.0; gl_Position = applyLogDepth(vec4(c1.xy - n * c1.w, c1.z, c1.w)); EmitVertex();
  EndPrimitive();
}
