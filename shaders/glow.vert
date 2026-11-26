// Camera-facing sprite for the sun's corona.
uniform mat4 uVP;
uniform vec3 uCenter;
uniform vec3 uRight;
uniform vec3 uUp;
uniform float uSize;
out vec2 vP;
void main() {
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) - vec2(0.0);
  vec2 q = vec2(float(gl_VertexID == 1 || gl_VertexID == 3), float(gl_VertexID >= 2));
  vec2 corner = (gl_VertexID == 0) ? vec2(-1, -1) : (gl_VertexID == 1) ? vec2(1, -1) : (gl_VertexID == 2) ? vec2(-1, 1) : vec2(1, 1);
  vP = corner;
  vec3 w = uCenter + (uRight * corner.x + uUp * corner.y) * uSize;
  gl_Position = applyLogDepth(uVP * vec4(w, 1.0));
}
