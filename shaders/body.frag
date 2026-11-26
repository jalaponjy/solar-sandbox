// Procedural planet / moon / sun surfaces with bump-mapped lighting.
in vec3 vObj;
in vec3 vWorld;
in vec3 vN;
out vec4 fragColor;

uniform int   uType;
uniform vec3  uC1, uC2, uC3;
uniform float uSeed;
uniform vec4  uAtmo;          // rgb, strength
uniform vec3  uSunDir;        // world-space direction towards the sun
uniform float uSunI;          // sunlight intensity at this distance
uniform float uTime;
uniform mat3  uRot;
uniform vec3  uCenter;
uniform float uRadius;
uniform float uRingIn, uRingOut, uRingOp;

struct Surf {
  vec3 albedo;
  float spec, gloss;
  vec3 emit;         // always-on emission (sun, lava)
  vec3 emitNight;    // only on the dark side (city lights)
  float cloud;
};

float lowCraters(vec3 p, float a, float b, float c) {
  return craters(p, 6.0, 0.7) * a + craters(p, 17.0, 0.6) * b + craters(p, 46.0, 0.5) * c;
}

// ---- height fields (used for bump mapping) -----------------------------
float heightFn(vec3 p) {
  if (uType == 1) return lowCraters(p, 0.5, 0.35, 0.2) + fbm(p * 3.0 + uSeed, 4) * 0.25;
  if (uType == 2) {
    float land = fbm(p * 2.0 + uSeed, 6) * 0.75 + fbm(p * 6.0, 3) * 0.25;
    return max(land - 0.47, 0.0) * 0.9;
  }
  if (uType == 4) return lowCraters(p, 0.25, 0.2, 0.1) + fbm(p * 5.0 + uSeed, 4) * 0.2;
  if (uType == 8) return lowCraters(p, 0.25, 0.2, 0.1) + ridged(p * 7.0 + uSeed, 3) * 0.12;
  if (uType == 9) return fbm(p * 6.0 + uSeed, 4) * 0.12;
  if (uType == 11) return fbm(p * 5.0 + uSeed, 4) * 0.18 + craters(p, 22.0, 0.4) * 0.12;
  return 0.0;
}
float bumpStrength() {
  if (uType == 1) return 0.9;
  if (uType == 2) return 0.55;
  if (uType == 4) return 0.5;
  if (uType == 8) return 0.45;
  if (uType == 9) return 0.3;
  if (uType == 11) return 0.4;
  return 0.0;
}

// ---- gas giant bands ---------------------------------------------------
vec3 gasColor(vec3 p, bool jup, vec3 c1, vec3 c2, vec3 c3) {
  float lat = p.z;
  float turb = fbm(vec3(p.xy * 3.0, lat * 5.0) + uSeed, 5);
  float edd = fbm(vec3(p.xy * 9.0, lat * 34.0) + uSeed * 2.0, 4);
  float lt = lat * (jup ? 11.0 : 7.0) + (turb - 0.5) * (jup ? 2.6 : 1.1) + (edd - 0.5) * 0.4;
  float b = 0.5 + 0.5 * sin(lt * 3.14159);
  float b2 = 0.5 + 0.5 * sin(lt * 3.14159 * 2.3 + 1.3);
  vec3 col = mix(c1, c2, smoothstep(0.25, 0.75, b));
  col = mix(col, c3, smoothstep(0.55, 0.95, b2) * (jup ? 0.55 : 0.3));
  col *= 0.88 + 0.24 * edd;
  col = mix(col, col * vec3(0.72, 0.8, 0.98), smoothstep(0.72, 1.0, abs(lat)));
  if (jup) {  // Great Red Spot
    float lon = atan(p.y, p.x);
    float dx = (lon - 0.6) * sqrt(max(1.0 - lat * lat, 0.0));
    float dy = lat + 0.28;
    float swirl = vnoise(vec3(dx * 22.0, dy * 40.0, uSeed)) * 0.25;
    float d = length(vec2(dx / 0.21, dy / 0.075)) + swirl;
    float spot = 1.0 - smoothstep(0.75, 1.0, d);
    float halo = (1.0 - smoothstep(1.0, 1.5, d)) * 0.25;
    vec3 sc = mix(srgb2lin(vec3(0.74, 0.30, 0.20)), srgb2lin(vec3(0.88, 0.52, 0.34)), vnoise(vec3(dx * 30.0, dy * 70.0, 2.0)));
    col = mix(col, c1 * 1.15, halo);
    col = mix(col, sc, spot * 0.92);
  }
  return col;
}

Surf surface(vec3 p) {
  Surf s;
  s.spec = 0.0; s.gloss = 30.0; s.emit = vec3(0); s.emitNight = vec3(0); s.cloud = 0.0;
  vec3 c1 = srgb2lin(uC1), c2 = srgb2lin(uC2), c3 = srgb2lin(uC3);
  s.albedo = c1;

  if (uType == 0) {  // ---- Sun
    float t = uTime * 0.03;
    float g = fbm(p * 7.0 + vec3(0, 0, t), 5);
    float g2 = fbm(p * 19.0 - vec3(t * 2.0), 4);
    float spots = smoothstep(0.64, 0.72, fbm(p * 3.0 + uSeed + t * 0.2, 4));
    vec3 col = mix(c1, c2, smoothstep(0.3, 0.7, g * 0.7 + g2 * 0.5));
    col = mix(col, c3, smoothstep(0.55, 0.85, g2) * 0.4);
    col *= 1.0 - spots * 0.45;
    s.emit = col * 1.9;
    s.albedo = vec3(0);
  } else if (uType == 1) {  // ---- cratered rock
    float h = lowCraters(p, 0.5, 0.35, 0.2);
    float maria = smoothstep(0.50, 0.58, fbm(p * 1.7 + uSeed, 5));
    vec3 base = mix(c1, c2, fbm(p * 5.0 + uSeed * 3.0, 5));
    base = mix(base, c3, maria * 0.75);
    s.albedo = base * (1.0 + clamp(h, -0.4, 0.5) * 0.7);
  } else if (uType == 2) {  // ---- Earth
    float land = fbm(p * 2.0 + uSeed, 6) * 0.75 + fbm(p * 6.0, 3) * 0.25;
    const float sea = 0.47;
    float isLand = smoothstep(sea, sea + 0.012, land);
    float elev = clamp((land - sea) / 0.2, 0.0, 1.0);
    float lat = abs(p.z);
    vec3 shallow = c1 * 1.9 + srgb2lin(vec3(0.0, 0.18, 0.2));
    vec3 ocean = mix(c1 * 0.5, shallow, smoothstep(sea - 0.10, sea, land));
    float desert = smoothstep(0.45, 0.62, fbm(p * 2.7 + 3.0, 4)) * (1.0 - smoothstep(0.55, 0.9, lat));
    vec3 landc = mix(c2, c3, desert);
    landc *= 0.8 + 0.4 * fbm(p * 14.0, 3);
    landc = mix(landc, srgb2lin(vec3(0.35, 0.32, 0.29)), smoothstep(0.35, 0.8, elev));
    vec3 col = mix(ocean, landc, isLand);
    float ice = smoothstep(0.86, 0.93, lat + 0.05 * fbm(p * 8.0, 3)) + smoothstep(0.75, 0.95, elev) * isLand * 0.8;
    col = mix(col, vec3(0.9, 0.93, 0.97), clamp(ice, 0.0, 1.0));
    float cl = smoothstep(0.50, 0.74, fbm(p * vec3(3.0, 3.0, 5.5) + vec3(fbm(p * 2.0 + 1.0, 3) * 1.4) + vec3(uTime * 0.003, 0.0, 0.0), 6));
    cl *= 0.92;
    s.cloud = cl;
    s.albedo = mix(col, vec3(0.93), cl);
    s.spec = (1.0 - isLand) * (1.0 - clamp(ice, 0.0, 1.0)) * (1.0 - cl) * 0.9;
    s.gloss = 110.0;
    float cities = smoothstep(0.62, 0.9, vnoise(p * 26.0) * (0.5 + vnoise(p * 6.0)));
    s.emitNight = vec3(1.0, 0.72, 0.38) * cities * isLand * (1.0 - clamp(ice, 0.0, 1.0)) * (1.0 - cl * 0.9) * 0.9;
  } else if (uType == 3) {  // ---- Venus clouds
    vec3 q = p * vec3(1.6, 1.6, 3.0);
    float w = fbm(q * 2.0 + uSeed, 4);
    float s1 = fbm(q * 3.0 + vec3(w * 2.6) + vec3(uTime * 0.003, 0.0, 0.0), 5);
    s.albedo = mix(mix(c1, c2, s1), c3, smoothstep(0.55, 0.8, fbm(q * 5.0 + w * 3.0, 4)) * 0.5);
  } else if (uType == 4) {  // ---- Mars
    float m = fbm(p * 2.5 + uSeed, 5);
    vec3 col = mix(c2, c1, smoothstep(0.38, 0.62, m));
    float dark = smoothstep(0.55, 0.72, fbm(p * 3.2 + 11.0 + uSeed, 5));
    col = mix(col, c3, dark * 0.8);
    col *= 0.85 + 0.3 * fbm(p * 16.0, 3);
    float cap = smoothstep(0.88, 0.94, abs(p.z) + 0.04 * fbm(p * 9.0, 3));
    s.albedo = mix(col, vec3(0.92, 0.9, 0.88), cap);
  } else if (uType == 5) {  // ---- Jupiter
    s.albedo = gasColor(p, true, c1, c2, c3);
  } else if (uType == 6) {  // ---- Saturn
    s.albedo = gasColor(p, false, c1, c2, c3);
  } else if (uType == 7) {  // ---- Uranus / Neptune
    float lat = p.z;
    float band = 0.5 + 0.5 * sin(lat * 12.0 + (fbm(p * vec3(2, 2, 3) + uSeed, 4) - 0.5) * 1.8);
    vec3 col = mix(c1, c2, band * 0.6 + 0.25 * fbm(p * 3.0 + uSeed, 3));
    col = mix(col, c2 * 1.15, smoothstep(0.5, 1.0, abs(lat)) * 0.35);
    if (uSeed > 8.5) {  // Neptune: Great Dark Spot and bright cirrus
      float lon = atan(p.y, p.x);
      float dx = (lon - 1.0) * sqrt(max(1.0 - lat * lat, 0.0)), dy = lat + 0.35;
      float d = length(vec2(dx / 0.30, dy / 0.12));
      col = mix(col, c3 * 0.55, (1.0 - smoothstep(0.7, 1.0, d)) * 0.85);
      float cir = smoothstep(0.62, 0.82, fbm(p * vec3(7, 7, 20) + 4.0, 4)) * smoothstep(1.7, 0.6, d + 0.4);
      col = mix(col, vec3(0.85, 0.92, 1.0), cir * 0.6);
    }
    s.albedo = col;
  } else if (uType == 8) {  // ---- icy moon
    vec3 base = mix(c1, c3, fbm(p * 4.0 + uSeed, 4));
    vec3 pw = p * 6.0 + vec3(fbm(p * 3.0 + uSeed, 3) * 2.0);
    float lines = smoothstep(0.62, 0.9, ridged(pw, 4));
    vec3 col = mix(base, c2, lines * 0.7);
    float h = lowCraters(p, 0.25, 0.2, 0.1);
    s.albedo = col * (1.0 + clamp(h, -0.3, 0.5) * 0.6);
    s.spec = 0.25; s.gloss = 40.0;
  } else if (uType == 9) {  // ---- Io
    float n1 = fbm(p * 5.0 + uSeed, 5);
    vec3 col = mix(c1, c2, smoothstep(0.4, 0.65, n1));
    float pits = smoothstep(0.70, 0.78, fbm(p * 11.0 + uSeed * 3.0, 4));
    col = mix(col, c3, pits);
    col *= 0.9 + 0.2 * fbm(p * 22.0, 2);
    s.albedo = col;
    s.emit = vec3(1.0, 0.32, 0.05) * pits * 0.12;
  } else if (uType == 10) {  // ---- Titan: featureless orange haze
    float b = 0.5 + 0.5 * sin(p.z * 3.2 + (fbm(p * 2.0 + uSeed, 4) - 0.5) * 1.8);
    s.albedo = mix(c1, c2, b) * (0.92 + 0.16 * fbm(p * 6.0, 3));
  } else if (uType == 11) {  // ---- Pluto: nitrogen heart + dark whale
    float lon = atan(p.y, p.x), lat = p.z;
    vec2 q = vec2((lon - 3.0) * sqrt(max(1.0 - lat * lat, 0.0)), lat - 0.1);
    float lobe1 = length(q - vec2(0.2, 0.18)), lobe2 = length(q - vec2(-0.2, 0.18));
    float tip = length((q - vec2(0.0, -0.12)) * vec2(1.0, 1.6));
    float heart = max(1.0 - smoothstep(0.17, 0.26, lobe1), max(1.0 - smoothstep(0.17, 0.26, lobe2), 1.0 - smoothstep(0.12, 0.22, tip)));
    vec3 col = mix(c1, c1 * 0.8, fbm(p * 4.0 + uSeed, 5));
    float whale = smoothstep(0.5, 0.75, fbm(p * 3.0 + 4.0, 4)) * (1.0 - smoothstep(0.1, 0.4, abs(lat + 0.1)));
    col = mix(col, c3, whale * 0.8);
    col = mix(col, c2, heart * 0.9);
    s.albedo = col * (0.9 + 0.2 * fbm(p * 12.0, 3));
    s.spec = 0.1; s.gloss = 30.0;
  }
  return s;
}

void main() {
  vec3 p = normalize(vObj);
  vec3 N0 = normalize(vN);
  vec3 V = normalize(-vWorld);
  vec3 L = normalize(uSunDir);
  Surf s = surface(p);

  // Bump mapping from the type's height field
  vec3 N = N0;
  float bs = bumpStrength();
  if (bs > 0.0) {
    vec3 t = cross(vec3(0, 0, 1), p);
    if (dot(t, t) < 1e-6) t = vec3(1, 0, 0);
    t = normalize(t);
    vec3 bt = cross(p, t);
    const float eps = 0.004;
    float h0 = heightFn(p);
    float hx = heightFn(normalize(p + t * eps));
    float hy = heightFn(normalize(p + bt * eps));
    vec3 pn = normalize(p - bs * ((hx - h0) * t + (hy - h0) * bt) / eps * 0.045);
    N = normalize(uRot * pn);
    N = normalize(mix(N0, N, 1.0));
  }

  float nl = dot(N, L);
  float wrap = uAtmo.a * 0.12;
  float diff = max((nl + wrap) / (1.0 + wrap), 0.0);
  vec3 sunCol = vec3(1.0, 0.97, 0.92) * uSunI;

  // Ring shadow cast on the planet
  float shadow = 1.0;
  if (uRingOut > 0.0) {
    vec3 P = vWorld - uCenter;
    vec3 Nr = normalize(uRot * vec3(0, 0, 1));
    float den = dot(L, Nr);
    if (abs(den) > 1e-4) {
      float tt = -dot(P, Nr) / den;
      if (tt > 0.0) {
        float rr = length(P + L * tt) / uRadius;
        float x = (rr - uRingIn) / (uRingOut - uRingIn);
        if (x > 0.0 && x < 1.0) shadow = 1.0 - clamp(ringDensity(x) * uRingOp, 0.0, 0.95);
      }
    }
  }

  vec3 col = s.albedo * (sunCol * diff * shadow + vec3(0.028, 0.030, 0.040));
  vec3 H = normalize(L + V);
  col += sunCol * pow(max(dot(N, H), 0.0), s.gloss) * s.spec * step(0.0, nl) * shadow;
  col += s.emit;
  col += s.emitNight * smoothstep(0.08, -0.18, nl);
  if (uAtmo.a > 0.0 && uType != 0) {
    float rim = pow(1.0 - max(dot(N0, V), 0.0), 3.0);
    col += uAtmo.rgb * rim * uAtmo.a * 0.25 * smoothstep(-0.1, 0.5, nl) * uSunI;
  }
  fragColor = vec4(outColor(col), 1.0);
}
