in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex;
uniform vec2 uTexel;       // texel size of the SOURCE (smaller mip)
uniform float uRadius;
void main() {
  vec2 d = uTexel * uRadius;
  vec3 c = texture(uTex, vUV).rgb * 4.0;
  c += texture(uTex, vUV + vec2(-d.x, 0)).rgb * 2.0 + texture(uTex, vUV + vec2(d.x, 0)).rgb * 2.0;
  c += texture(uTex, vUV + vec2(0, -d.y)).rgb * 2.0 + texture(uTex, vUV + vec2(0, d.y)).rgb * 2.0;
  c += texture(uTex, vUV + vec2(-d.x, -d.y)).rgb + texture(uTex, vUV + vec2(d.x, -d.y)).rgb;
  c += texture(uTex, vUV + vec2(-d.x, d.y)).rgb + texture(uTex, vUV + vec2(d.x, d.y)).rgb;
  fragColor = vec4(c / 16.0, 1.0);
}
