layout(location = 0) in vec3 aPos;     // xy = unit direction, z = radial parameter 0..1
uniform mat4 uVP;
uniform mat3 uRot;
uniform vec3 uCenter;
uniform float uRadius;
uniform float uRingIn, uRingOut;
out vec3 vWorld;
out vec3 vLocal;
void main() {
  float r = mix(uRingIn, uRingOut, aPos.z);
  vec3 local = vec3(aPos.xy * r, 0.0);
  vec3 w = uRot * (local * uRadius) + uCenter;
  vWorld = w;
  vLocal = local;
  gl_Position = applyLogDepth(uVP * vec4(w, 1.0));
}
