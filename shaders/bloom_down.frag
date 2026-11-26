in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex;
uniform vec2 uTexel;       // texel size of the SOURCE
uniform int uFirst;        // 1 = apply a soft threshold & Karis average
float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
vec3 prefilter(vec3 c) {
  float br = max(max(c.r, c.g), c.b);
  float soft = clamp(br - 0.9 + 0.5, 0.0, 1.0);
  soft = soft * soft * 0.5;
  float w = max(soft, br - 0.9) / max(br, 1e-4);
  return c * w;
}
void main() {
  vec2 uv = vUV;
  vec3 a = texture(uTex, uv + uTexel * vec2(-2, 2)).rgb, b = texture(uTex, uv + uTexel * vec2(0, 2)).rgb, c = texture(uTex, uv + uTexel * vec2(2, 2)).rgb;
  vec3 d = texture(uTex, uv + uTexel * vec2(-2, 0)).rgb, e = texture(uTex, uv).rgb, f = texture(uTex, uv + uTexel * vec2(2, 0)).rgb;
  vec3 g = texture(uTex, uv + uTexel * vec2(-2, -2)).rgb, h = texture(uTex, uv + uTexel * vec2(0, -2)).rgb, i = texture(uTex, uv + uTexel * vec2(2, -2)).rgb;
  vec3 j = texture(uTex, uv + uTexel * vec2(-1, 1)).rgb, k = texture(uTex, uv + uTexel * vec2(1, 1)).rgb;
  vec3 l = texture(uTex, uv + uTexel * vec2(-1, -1)).rgb, m = texture(uTex, uv + uTexel * vec2(1, -1)).rgb;
  vec3 col;
  if (uFirst == 1) {
    vec3 g0 = (prefilter(a) + prefilter(b) + prefilter(d) + prefilter(e)) * 0.25;
    vec3 g1 = (prefilter(b) + prefilter(c) + prefilter(e) + prefilter(f)) * 0.25;
    vec3 g2 = (prefilter(d) + prefilter(e) + prefilter(g) + prefilter(h)) * 0.25;
    vec3 g3 = (prefilter(e) + prefilter(f) + prefilter(h) + prefilter(i)) * 0.25;
    vec3 g4 = (prefilter(j) + prefilter(k) + prefilter(l) + prefilter(m)) * 0.25;
    float w0 = 1.0 / (1.0 + luma(g0)), w1 = 1.0 / (1.0 + luma(g1)), w2 = 1.0 / (1.0 + luma(g2)),
          w3 = 1.0 / (1.0 + luma(g3)), w4 = 1.0 / (1.0 + luma(g4));
    col = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3) * 0.125 / 1.0 * 1.0;
    col = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4 * 4.0) / (w0 + w1 + w2 + w3 + 4.0 * w4);
  } else {
    col = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
  }
  fragColor = vec4(col, 1.0);
}
