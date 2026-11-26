in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uBloomStrength;
uniform float uExposure;
uniform vec2 uResolution;
void main() {
  vec3 hdr = texture(uScene, vUV).rgb;
  vec3 bloom = texture(uBloom, vUV).rgb;
  vec3 col = hdr + bloom * uBloomStrength;
  col *= uExposure;
  vec2 q = vUV - 0.5;
  col *= 1.0 - dot(q, q) * 0.55;                       // vignette
  col = acesTonemap(col);
  col = pow(col, vec3(1.0 / 2.2));
  float n1 = hash13(vec3(gl_FragCoord.xy, 1.0)), n2 = hash13(vec3(gl_FragCoord.yx + 17.0, 3.0));
  col += (n1 + n2 - 1.0) * 1.6 / 255.0;                // triangular dither against banding
  fragColor = vec4(col, 1.0);
}
