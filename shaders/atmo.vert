layout(location = 0) in vec3 aPos;
uniform mat4 uVP;
uniform vec3 uCenter;
uniform float uShellRadius;
out vec3 vWorld;
void main() {
  vec3 w = aPos * uShellRadius + uCenter;
  vWorld = w;
  gl_Position = applyLogDepth(uVP * vec4(w, 1.0));
}
