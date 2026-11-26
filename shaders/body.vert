layout(location = 0) in vec3 aPos;     // unit sphere, +z = rotation pole
uniform mat4 uVP;
uniform mat3 uRot;        // body frame -> world (camera-relative axes)
uniform vec3 uCenter;     // camera-relative centre
uniform float uRadius;
uniform float uFlat;      // 1 - flattening (polar squash)
out vec3 vObj;
out vec3 vWorld;
out vec3 vN;
void main() {
  vec3 local = vec3(aPos.xy, aPos.z * uFlat);
  vec3 w = uRot * (local * uRadius) + uCenter;
  vObj = aPos;
  vWorld = w;
  vN = normalize(uRot * vec3(aPos.xy, aPos.z / uFlat));
  gl_Position = applyLogDepth(uVP * vec4(w, 1.0));
}
