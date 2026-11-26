// Prepended (after #version) to every shader. Shared helpers: noise, log depth, tone mapping.
uniform float uFar;          // far plane for logarithmic depth
uniform int   uLDR;          // 1 = write display-ready colour (used by the preview renderer)

vec4 applyLogDepth(vec4 c) {
  c.z = (2.0 * log2(max(1e-6, 1.0 + c.w)) / log2(uFar + 1.0) - 1.0) * c.w;
  return c;
}

vec3 acesTonemap(vec3 x) {
  const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
vec3 outColor(vec3 c) {
  if (uLDR != 0) return pow(acesTonemap(c), vec3(1.0 / 2.2));
  return c;
}
vec3 srgb2lin(vec3 c) { return pow(c, vec3(2.2)); }

// ---------------------------------------------------------------- noise
float hash13(vec3 p3) {
  p3 = fract(p3 * 0.1031);
  p3 += dot(p3, p3.zyx + 31.32);
  return fract((p3.x + p3.y) * p3.z);
}
vec3 hash33(vec3 p3) {
  p3 = fract(p3 * vec3(0.1031, 0.1030, 0.0973));
  p3 += dot(p3, p3.yxz + 33.33);
  return fract((p3.xxy + p3.yxx) * p3.zyx);
}
float vnoise(vec3 x) {
  vec3 i = floor(x), f = fract(x);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(mix(hash13(i), hash13(i + vec3(1, 0, 0)), f.x),
                 mix(hash13(i + vec3(0, 1, 0)), hash13(i + vec3(1, 1, 0)), f.x), f.y),
             mix(mix(hash13(i + vec3(0, 0, 1)), hash13(i + vec3(1, 0, 1)), f.x),
                 mix(hash13(i + vec3(0, 1, 1)), hash13(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}
float fbm(vec3 p, int oct) {
  float s = 0.0, a = 0.5;
  for (int i = 0; i < oct; i++) {
    s += a * vnoise(p);
    p = p * 2.03 + vec3(5.2, 1.3, 8.7);
    a *= 0.5;
  }
  return s;
}
float ridged(vec3 p, int oct) {
  float s = 0.0, a = 0.5;
  for (int i = 0; i < oct; i++) {
    float n = 1.0 - abs(2.0 * vnoise(p) - 1.0);
    s += a * n * n;
    p = p * 2.07 + vec3(1.7, 9.2, 3.1);
    a *= 0.5;
  }
  return s;
}

// Impact craters: bowls with raised rims scattered on a 3D Voronoi grid.
float craters(vec3 p, float scale, float density) {
  p *= scale;
  vec3 ip = floor(p), fp = fract(p);
  float h = 0.0;
  for (int z = -1; z <= 1; z++)
    for (int y = -1; y <= 1; y++)
      for (int x = -1; x <= 1; x++) {
        vec3 g = vec3(x, y, z);
        vec3 cell = ip + g;
        if (hash13(cell + 3.3) > density) continue;
        vec3 o = hash33(cell) * 0.7 + 0.15;
        float rad = 0.12 + 0.32 * hash13(cell + 7.7);
        float t = length(g + o - fp) / rad;
        float bowl = t < 1.0 ? -(1.0 - t * t) : 0.0;
        float rim = exp(-pow((t - 1.05) * 3.5, 2.0)) * 0.45;
        h += (bowl * 0.9 + rim) * rad * 2.2;
      }
  return h;
}

// ---------------------------------------------------------------- rings
// Procedural ring profile over x in [0,1] (inner->outer edge): C ring, B ring, Cassini
// division, A ring and Encke gap, with fine ringlet structure.
float ringProfile(float x) {
  float c = 0.20 * smoothstep(0.0, 0.05, x) * (1.0 - smoothstep(0.26, 0.29, x));
  float b = 0.98 * smoothstep(0.27, 0.31, x) * (1.0 - smoothstep(0.68, 0.71, x));
  float a = 0.72 * smoothstep(0.78, 0.80, x) * (1.0 - smoothstep(0.985, 1.0, x));
  a *= 1.0 - 0.95 * (1.0 - smoothstep(0.0, 0.012, abs(x - 0.93)));
  float cass = 0.07 * smoothstep(0.70, 0.72, x) * (1.0 - smoothstep(0.77, 0.79, x));
  return clamp(c + b + a + cass, 0.0, 1.0);
}
float ringDensity(float x) {
  float fine = 0.55 + 0.45 * vnoise(vec3(x * 260.0, 1.0, 2.0));
  fine *= 0.75 + 0.25 * vnoise(vec3(x * 900.0, 5.0, 9.0));
  return ringProfile(x) * fine;
}
