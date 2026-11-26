// Procedural deep-space backdrop: layered stars, a Milky Way band, and faint nebulae.
in vec2 vUV;
out vec4 fragColor;
uniform mat3 uViewRotInv;
uniform float uTanHalf;
uniform float uAspect;
uniform float uHeight;
uniform float uSkyBright;

vec3 starLayer(vec3 d, float scale, float density, float pxAngle, float gain) {
  vec3 q = d * scale;
  vec3 ip = floor(q), fp = fract(q);
  float h = hash13(ip);
  if (h > density) return vec3(0.0);
  vec3 o = hash33(ip + 1.7) * 0.8 + 0.1;
  float dist = length(fp - o);
  float px = scale * pxAngle;
  float mag = pow(hash13(ip + 9.1), 3.0);          // few bright, many faint
  float size = max(px * 0.9, 0.02);
  float I = smoothstep(size * 1.3, 0.0, dist) * (0.04 + mag * gain);
  vec3 tint = mix(vec3(1.0, 0.72, 0.5), vec3(0.65, 0.78, 1.0), hash13(ip + 4.4));
  tint = mix(vec3(1.0), tint, 0.75);
  return tint * I;
}

void main() {
  vec2 ndc = vUV * 2.0 - 1.0;
  vec3 d = normalize(uViewRotInv * vec3(ndc.x * uAspect * uTanHalf, ndc.y * uTanHalf, -1.0));
  float px = 2.0 * uTanHalf / uHeight;

  // Galactic plane (tilted) and centre direction
  vec3 gN = normalize(vec3(0.46, -0.20, 0.86));
  vec3 gC = normalize(vec3(-0.05, -0.87, -0.49));
  float gl = abs(dot(d, gN));
  float band = exp(-gl * gl * 55.0);
  float core = exp(-pow(max(dot(d, gC), 0.0) * 1.0 - 1.0, 2.0) * 6.0);
  float dust = fbm(d * 9.0 + 3.0, 5);
  float cloudy = fbm(d * 3.0, 5);
  float milky = band * (0.5 + 0.7 * cloudy) * (1.0 - 0.45 * smoothstep(0.42, 0.7, dust));
  vec3 col = vec3(0.60, 0.65, 0.90) * milky * 0.055;
  col += vec3(1.0, 0.82, 0.55) * core * band * 0.06;

  // Faint coloured nebulae
  float n1 = fbm(d * 2.2 + 11.0, 5), n2 = fbm(d * 3.1 + 40.0, 5);
  col += vec3(0.10, 0.04, 0.16) * smoothstep(0.45, 0.9, n1) * 0.05;
  col += vec3(0.02, 0.07, 0.14) * smoothstep(0.45, 0.9, n2) * 0.05;

  // Stars: three layers + dense stars concentrated in the galactic band
  vec3 st = starLayer(d, 30.0, 0.30, px, 3.2) + starLayer(d, 80.0, 0.10, px, 1.6)
          + starLayer(d, 200.0, 0.05, px, 1.0);
  st += starLayer(d, 380.0, 0.015 + 0.30 * band, px, 0.6) * 0.8;
  col += st * 0.8;

  col *= uSkyBright;
  fragColor = vec4(outColor(col), 1.0);
}
