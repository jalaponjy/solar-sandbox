#include "catalog.h"
#include <cstdio>

using glm::vec3;

glm::dvec3 poleToEcliptic(double raDeg, double decDeg) {
  const double ra = raDeg * kPi / 180.0, dec = decDeg * kPi / 180.0;
  const double eps = 23.4392911 * kPi / 180.0;
  glm::dvec3 q(std::cos(dec) * std::cos(ra), std::cos(dec) * std::sin(ra), std::sin(dec));
  // rotate equatorial -> ecliptic about the x axis
  return glm::normalize(glm::dvec3(q.x, q.y * std::cos(eps) + q.z * std::sin(eps),
                                   -q.y * std::sin(eps) + q.z * std::cos(eps)));
}

namespace {

Appearance look(int type, vec3 c1, vec3 c2, vec3 c3, float seed = 1.f) {
  Appearance a; a.type = type; a.c1 = c1; a.c2 = c2; a.c3 = c3; a.seed = seed; return a;
}
Appearance withAtmo(Appearance a, vec3 col, float s) { a.atmo = col; a.atmoStrength = s; return a; }

void addPlanet(Catalog& c, const char* name, BodyKind kind, const char* group, double massKg,
               double radiusKm, double flat, double aAU, double e, double inc, double L, double varpi,
               double node, double ra, double dec, double rotH, double tilt, double temp, int moons,
               Appearance lk, const char* desc) {
  BodyDef b;
  b.name = name; b.parent = "Sun"; b.kind = kind; b.group = group;
  b.massKg = massKg; b.radiusKm = radiusKm; b.flattening = flat;
  b.aKm = aAU * kAU; b.e = e; b.incDeg = inc;
  b.nodeDeg = node; b.periDeg = varpi - node; b.meanAnomDeg = L - varpi;
  b.poleRA = ra; b.poleDec = dec; b.rotHours = rotH; b.tiltDeg = tilt; b.tempK = temp;
  b.moonsKnown = moons; b.look = lk; b.description = desc;
  c.bodies.push_back(b);
}

struct MoonRow {
  const char* parent; const char* name; double aKm, e, inc, radius, massKg; const char* group;
  int look;           // palette preset
  const char* desc;   // may be nullptr => generated
  double tempK;
};

enum Look { L_GREY, L_DARK, L_ICE, L_BROWN, L_RED, L_IO, L_EUROPA, L_GANY, L_CALL, L_TITAN,
            L_ENCEL, L_TRITON, L_PLUTO, L_CHARON };

Appearance moonLook(int l, float seed) {
  switch (l) {
    case L_DARK:   return look(SURF_ROCKY, {0.26f, 0.25f, 0.24f}, {0.45f, 0.44f, 0.42f}, {0.14f, 0.13f, 0.13f}, seed);
    case L_ICE:    return look(SURF_ICY, {0.80f, 0.82f, 0.85f}, {0.62f, 0.66f, 0.72f}, {0.95f, 0.97f, 1.0f}, seed);
    case L_BROWN:  return look(SURF_ROCKY, {0.42f, 0.35f, 0.28f}, {0.60f, 0.52f, 0.42f}, {0.22f, 0.18f, 0.15f}, seed);
    case L_RED:    return look(SURF_ROCKY, {0.45f, 0.22f, 0.15f}, {0.60f, 0.35f, 0.25f}, {0.2f, 0.1f, 0.08f}, seed);
    case L_IO:     return look(SURF_VOLCANIC, {0.92f, 0.82f, 0.30f}, {0.88f, 0.45f, 0.14f}, {0.18f, 0.13f, 0.10f}, seed);
    case L_EUROPA: return look(SURF_ICY, {0.88f, 0.85f, 0.78f}, {0.62f, 0.38f, 0.25f}, {0.98f, 0.96f, 0.92f}, seed);
    case L_GANY:   return look(SURF_ICY, {0.48f, 0.44f, 0.40f}, {0.30f, 0.27f, 0.25f}, {0.80f, 0.78f, 0.74f}, seed);
    case L_CALL:   return look(SURF_ROCKY, {0.28f, 0.25f, 0.22f}, {0.38f, 0.34f, 0.30f}, {0.78f, 0.78f, 0.80f}, seed);
    case L_TITAN:  return withAtmo(look(SURF_TITAN, {0.85f, 0.52f, 0.18f}, {0.95f, 0.65f, 0.28f}, {0.60f, 0.35f, 0.12f}, seed), {0.95f, 0.55f, 0.18f}, 0.9f);
    case L_ENCEL:  return look(SURF_ICY, {0.95f, 0.97f, 1.0f}, {0.60f, 0.78f, 0.95f}, {1.0f, 1.0f, 1.0f}, seed);
    case L_TRITON: return look(SURF_ICY, {0.82f, 0.74f, 0.72f}, {0.70f, 0.50f, 0.45f}, {0.95f, 0.88f, 0.86f}, seed);
    case L_PLUTO:  return look(SURF_PLUTO, {0.62f, 0.48f, 0.38f}, {0.92f, 0.87f, 0.80f}, {0.32f, 0.18f, 0.14f}, seed);
    case L_CHARON: return look(SURF_ROCKY, {0.52f, 0.50f, 0.50f}, {0.68f, 0.66f, 0.66f}, {0.30f, 0.20f, 0.18f}, seed);
    default:       return look(SURF_ROCKY, {0.45f, 0.43f, 0.41f}, {0.62f, 0.60f, 0.57f}, {0.25f, 0.24f, 0.23f}, seed);
  }
}

// Real orbital data (JPL / IAU values rounded). Inclinations are to the parent's equator
// (to the ecliptic for the Moon); irregular moons >90 deg are retrograde.
const MoonRow kMoons[] = {
  // ---- Earth ----
  {"Earth", "Moon", 384400, 0.0549, 5.145, 1737.4, 7.342e22, "Regular moon", L_GREY,
   "Earth's only natural satellite and the fifth largest moon in the solar system. It is tidally locked, always showing the same face to Earth, and its gravity drives our ocean tides. Its dark plains (maria) are ancient lava flows; the rest is heavily cratered highland.", 220},
  // ---- Mars ----
  {"Mars", "Phobos", 9376, 0.0151, 1.08, 11.1, 1.0659e16, "Captured asteroid?", L_DARK,
   "The larger of Mars' two tiny moons. It orbits so close that it circles Mars three times a day, and tidal forces are slowly pulling it inward - in roughly 50 million years it will break apart or crash.", 233},
  {"Mars", "Deimos", 23463, 0.00033, 1.79, 6.2, 1.4762e15, "Captured asteroid?", L_DARK,
   "Mars' smaller, outer moon: a smooth, dusty, irregular rock only about 12 km across that takes 30 hours to orbit.", 233},
  // ---- Jupiter: inner ----
  {"Jupiter", "Metis", 128000, 0.0002, 0.06, 21.5, 3.6e16, "Inner moon", L_DARK, nullptr, 120},
  {"Jupiter", "Adrastea", 129000, 0.0015, 0.03, 8.2, 2.0e15, "Inner moon", L_DARK, nullptr, 120},
  {"Jupiter", "Amalthea", 181366, 0.0032, 0.374, 83.5, 2.08e18, "Inner moon", L_RED,
   "A potato-shaped, reddish moon orbiting inside Io. It is one of the reddest objects in the solar system and radiates more heat than it receives, probably from Jupiter's intense radiation and magnetic field.", 120},
  {"Jupiter", "Thebe", 221889, 0.0175, 1.07, 49.3, 4.3e17, "Inner moon", L_DARK, nullptr, 120},
  // ---- Jupiter: Galilean ----
  {"Jupiter", "Io", 421800, 0.0041, 0.04, 1821.6, 8.9319e22, "Galilean moon", L_IO,
   "The most volcanically active world in the solar system, with hundreds of volcanoes spewing sulfur lava and plumes up to 500 km high. Tidal flexing from Jupiter and its neighbours keeps the interior molten.", 130},
  {"Jupiter", "Europa", 671100, 0.0094, 0.47, 1560.8, 4.7998e22, "Galilean moon", L_EUROPA,
   "A smooth, cracked ice shell hides a global saltwater ocean holding about twice the water of all Earth's oceans. Europa is one of the best places in the solar system to look for life.", 102},
  {"Jupiter", "Ganymede", 1070400, 0.0013, 0.20, 2634.1, 1.4819e23, "Galilean moon", L_GANY,
   "The largest moon in the solar system - bigger than Mercury. It is the only moon known to have its own magnetic field and likely has a buried ocean sandwiched between layers of ice.", 110},
  {"Jupiter", "Callisto", 1882700, 0.0074, 0.19, 2410.3, 1.0759e23, "Galilean moon", L_CALL,
   "The most heavily cratered object known: an ancient, dark surface that has barely changed in four billion years. It lies outside Jupiter's worst radiation, making it a candidate for a future crewed outpost.", 134},
  // ---- Jupiter: irregular ----
  {"Jupiter", "Leda", 11165000, 0.164, 27.5, 10, 1.1e16, "Irregular moon (Himalia group)", L_DARK, nullptr, 120},
  {"Jupiter", "Himalia", 11461000, 0.162, 27.5, 69.8, 4.2e18, "Irregular moon (Himalia group)", L_DARK,
   "The largest of Jupiter's irregular moons and the leader of a family of fragments believed to come from one captured asteroid.", 120},
  {"Jupiter", "Lysithea", 11717000, 0.112, 28.3, 18, 6.3e16, "Irregular moon (Himalia group)", L_DARK, nullptr, 120},
  {"Jupiter", "Elara", 11741000, 0.217, 26.6, 43, 8.7e17, "Irregular moon (Himalia group)", L_DARK, nullptr, 120},
  {"Jupiter", "Ananke", 21276000, 0.244, 148.9, 14.5, 3.0e16, "Irregular moon (retrograde)", L_DARK, nullptr, 120},
  {"Jupiter", "Carme", 23404000, 0.253, 165.0, 23, 1.3e17, "Irregular moon (retrograde)", L_DARK, nullptr, 120},
  {"Jupiter", "Pasiphae", 23624000, 0.409, 151.4, 30, 3.0e17, "Irregular moon (retrograde)", L_DARK, nullptr, 120},
  {"Jupiter", "Sinope", 23939000, 0.250, 158.1, 19, 7.5e16, "Irregular moon (retrograde)", L_DARK, nullptr, 120},
  // ---- Saturn: ring moons & co-orbitals ----
  {"Saturn", "Pan", 133584, 0.000035, 0.0, 14.1, 4.95e15, "Shepherd moon", L_ICE,
   "A walnut-shaped moon that sweeps out the Encke Gap in Saturn's A ring, its gravity keeping the gap clear.", 80},
  {"Saturn", "Daphnis", 136505, 0.0, 0.0, 3.8, 8.4e13, "Shepherd moon", L_ICE,
   "A tiny moon that carves the Keeler Gap and raises beautiful waves in the ring edges as it passes.", 80},
  {"Saturn", "Atlas", 137670, 0.0012, 0.003, 15.1, 6.6e15, "Shepherd moon", L_ICE, nullptr, 80},
  {"Saturn", "Prometheus", 139380, 0.0022, 0.008, 43.1, 1.6e17, "Shepherd moon", L_ICE,
   "Shepherds the inner edge of the F ring; its gravity sculpts streamers and channels in the ring material.", 80},
  {"Saturn", "Pandora", 141720, 0.0042, 0.050, 40.7, 1.37e17, "Shepherd moon", L_ICE, nullptr, 80},
  {"Saturn", "Epimetheus", 151422, 0.0098, 0.35, 58.1, 5.27e17, "Co-orbital moon", L_ICE,
   "Shares nearly the same orbit as Janus. Every four years the two approach and swap orbits rather than collide.", 80},
  {"Saturn", "Janus", 151472, 0.0068, 0.16, 89.5, 1.9e18, "Co-orbital moon", L_ICE,
   "Swaps orbits with Epimetheus every four years in a graceful gravitational dance.", 80},
  // ---- Saturn: major ----
  {"Saturn", "Mimas", 185540, 0.0196, 1.57, 198.2, 3.7493e19, "Regular moon", L_ICE,
   "Home of giant Herschel crater, which makes it look uncannily like the Death Star. Mimas helps maintain the Cassini Division in Saturn's rings.", 64},
  {"Saturn", "Enceladus", 238040, 0.0047, 0.01, 252.1, 1.0802e20, "Regular moon", L_ENCEL,
   "A brilliant white ice moon with geysers venting water from a hidden ocean at its south pole. Those plumes feed Saturn's E ring and contain organic molecules - another top candidate for life.", 75},
  {"Saturn", "Tethys", 294670, 0.0001, 1.09, 531.1, 6.1745e20, "Regular moon", L_ICE,
   "Almost pure water ice with a giant canyon, Ithaca Chasma, that runs three quarters of the way around the moon, and the huge crater Odysseus.", 86},
  {"Saturn", "Telesto", 294710, 0.0, 1.18, 12.4, 7.2e15, "Trojan moon", L_ICE, nullptr, 86},
  {"Saturn", "Calypso", 294710, 0.0, 1.5, 10.7, 6.3e15, "Trojan moon", L_ICE, nullptr, 86},
  {"Saturn", "Dione", 377420, 0.0022, 0.02, 561.4, 1.0955e21, "Regular moon", L_ICE,
   "A dense, icy world with bright wispy cliffs and a thin oxygen exosphere; it may also hide a subsurface ocean.", 87},
  {"Saturn", "Helene", 377400, 0.007, 0.2, 16, 2.5e16, "Trojan moon", L_ICE, nullptr, 87},
  {"Saturn", "Rhea", 527070, 0.0010, 0.35, 763.8, 2.3065e21, "Regular moon", L_ICE,
   "Saturn's second-largest moon: a heavily cratered snowball of ice and rock with a very thin atmosphere of oxygen and carbon dioxide.", 76},
  {"Saturn", "Titan", 1221870, 0.0288, 0.33, 2574.7, 1.3452e23, "Regular moon", L_TITAN,
   "The only moon with a dense atmosphere - thicker than Earth's - and the only other world with stable liquid on its surface: lakes and rivers of methane and ethane. NASA's Dragonfly rotorcraft is headed there.", 94},
  {"Saturn", "Hyperion", 1500880, 0.1230, 0.43, 135, 5.62e18, "Regular moon", L_BROWN,
   "A sponge-like, tumbling moon with a chaotic rotation that cannot be predicted, because of the gravitational tug-of-war with Titan.", 93},
  {"Saturn", "Iapetus", 3560840, 0.0276, 7.57, 734.5, 1.8056e21, "Regular moon", L_DARK,
   "Famous for two faces: one side coal-black, the other bright white like snow. A mysterious 20 km high ridge runs right along its equator.", 110},
  {"Saturn", "Phoebe", 12947780, 0.156, 175.3, 106.5, 8.292e18, "Irregular moon (retrograde)", L_DARK,
   "A captured, retrograde object that may have come from the Kuiper Belt. Its dark dust likely falls inward to coat the leading side of Iapetus.", 75},
  // ---- Uranus ----
  {"Uranus", "Cordelia", 49770, 0.0003, 0.08, 20.1, 4.4e16, "Shepherd moon", L_DARK, nullptr, 65},
  {"Uranus", "Ophelia", 53790, 0.0099, 0.10, 21.4, 5.3e16, "Shepherd moon", L_DARK, nullptr, 65},
  {"Uranus", "Bianca", 59170, 0.0009, 0.19, 25.7, 9.2e16, "Inner moon", L_DARK, nullptr, 65},
  {"Uranus", "Cressida", 61780, 0.0004, 0.01, 39.8, 3.4e17, "Inner moon", L_DARK, nullptr, 65},
  {"Uranus", "Desdemona", 62680, 0.0001, 0.11, 32, 1.8e17, "Inner moon", L_DARK, nullptr, 65},
  {"Uranus", "Juliet", 64350, 0.0007, 0.07, 46.8, 5.6e17, "Inner moon", L_DARK, nullptr, 65},
  {"Uranus", "Portia", 66090, 0.0001, 0.06, 67.6, 1.7e18, "Inner moon", L_DARK, nullptr, 65},
  {"Uranus", "Rosalind", 69940, 0.0001, 0.28, 36, 2.5e17, "Inner moon", L_DARK, nullptr, 65},
  {"Uranus", "Belinda", 75260, 0.0001, 0.03, 40.3, 3.6e17, "Inner moon", L_DARK, nullptr, 65},
  {"Uranus", "Puck", 86010, 0.0001, 0.32, 81, 2.9e18, "Inner moon", L_DARK,
   "The largest of Uranus' inner moons, discovered by Voyager 2 in 1985; its surface is dark and cratered.", 65},
  {"Uranus", "Miranda", 129900, 0.0013, 4.34, 235.8, 6.59e19, "Regular moon", L_ICE,
   "A patchwork world with a 20 km high cliff, Verona Rupes, among the tallest in the solar system. It looks as if it had been smashed apart and reassembled.", 60},
  {"Uranus", "Ariel", 190900, 0.0012, 0.04, 578.9, 1.353e21, "Regular moon", L_ICE,
   "The brightest of Uranus' moons, with young, canyon-scarred terrain suggesting past geological activity.", 58},
  {"Uranus", "Umbriel", 266000, 0.0039, 0.13, 584.7, 1.172e21, "Regular moon", L_DARK,
   "The darkest of the large Uranian moons, an ancient, heavily cratered surface with a mysterious bright ring inside Wunda crater.", 75},
  {"Uranus", "Titania", 436300, 0.0011, 0.08, 788.4, 3.527e21, "Regular moon", L_ICE,
   "The largest moon of Uranus, with huge canyons and fault scarps showing that its interior once expanded and cracked the crust.", 70},
  {"Uranus", "Oberon", 583500, 0.0014, 0.07, 761.4, 3.014e21, "Regular moon", L_BROWN,
   "The outermost large moon of Uranus, an old, cold, cratered world with dark material on crater floors.", 70},
  {"Uranus", "Caliban", 7231000, 0.159, 141.5, 21, 2.5e17, "Irregular moon (retrograde)", L_DARK, nullptr, 65},
  {"Uranus", "Sycorax", 12179000, 0.522, 159.4, 78, 2.3e18, "Irregular moon (retrograde)", L_RED, nullptr, 65},
  // ---- Neptune ----
  {"Neptune", "Naiad", 48227, 0.0003, 4.75, 33, 1.9e17, "Inner moon", L_DARK, nullptr, 55},
  {"Neptune", "Thalassa", 50075, 0.0002, 0.21, 41, 3.5e17, "Inner moon", L_DARK, nullptr, 55},
  {"Neptune", "Despina", 52526, 0.0002, 0.07, 75, 2.1e18, "Inner moon", L_DARK, nullptr, 55},
  {"Neptune", "Galatea", 61953, 0.0001, 0.05, 88, 2.1e18, "Inner moon", L_DARK, nullptr, 55},
  {"Neptune", "Larissa", 73548, 0.0014, 0.2, 97, 4.2e18, "Inner moon", L_DARK, nullptr, 55},
  {"Neptune", "Proteus", 117647, 0.0005, 0.08, 210, 4.4e19, "Regular moon", L_DARK,
   "Neptune's second-largest moon and one of the darkest objects around; its shape is limited only because it is just too small to pull itself into a sphere.", 51},
  {"Neptune", "Triton", 354759, 0.000016, 156.9, 1353.4, 2.14e22, "Captured Kuiper Belt object", L_TRITON,
   "Neptune's giant moon orbits backwards (retrograde), proving it was captured, most likely a Kuiper Belt dwarf planet. Nitrogen geysers erupt from its pink-white surface, and tidal decay will tear it apart in a few billion years.", 38},
  {"Neptune", "Nereid", 5513787, 0.7512, 32.5, 170, 3.1e19, "Irregular moon", L_ICE,
   "Orbits on one of the most eccentric orbits of any moon, swinging between 1.4 and 9.7 million km from Neptune.", 50},
  // ---- Pluto system ----
  {"Pluto", "Charon", 19591, 0.0002, 0.00, 606, 1.586e21, "Binary companion", L_CHARON,
   "Half Pluto's diameter, so large that the two orbit a point in space between them. Charon has a reddish north pole stained by gases escaping from Pluto.", 53},
  {"Pluto", "Styx", 42656, 0.0058, 0.0, 5.2, 7.5e15, "Small moon", L_ICE, nullptr, 40},
  {"Pluto", "Nix", 48694, 0.002, 0.0, 19.3, 5.0e16, "Small moon", L_ICE, nullptr, 40},
  {"Pluto", "Kerberos", 57783, 0.003, 0.4, 6, 1.65e16, "Small moon", L_DARK, nullptr, 40},
  {"Pluto", "Hydra", 64738, 0.0059, 0.2, 19.5, 4.8e16, "Small moon", L_ICE, nullptr, 40},
  // ---- Eris ----
  {"Eris", "Dysnomia", 37273, 0.0062, 78.0, 350, 8.2e19, "Regular moon", L_DARK, nullptr, 42},
};

}  // namespace

Catalog buildCatalog() {
  Catalog c;

  { // ---- Sun ----
    BodyDef s;
    s.name = "Sun"; s.kind = BodyKind::Star; s.group = "G-type main-sequence star";
    s.massKg = 1.98847e30; s.radiusKm = 695700; s.poleRA = 286.13; s.poleDec = 63.87;
    s.rotHours = 609.12; s.tiltDeg = 7.25; s.tempK = 5772; s.moonsKnown = 0;
    s.look = look(SURF_SUN, {1.0f, 0.45f, 0.08f}, {1.0f, 0.78f, 0.35f}, {1.0f, 0.95f, 0.8f});
    s.description =
        "The star at the centre of our solar system, containing 99.86% of all its mass. Every second it fuses about 600 million tonnes of hydrogen into helium, "
        "and the energy takes tens of thousands of years to reach the surface. It is about 4.6 billion years old and roughly halfway through its life.";
    c.bodies.push_back(s);
  }

  addPlanet(c, "Mercury", BodyKind::Planet, "Terrestrial planet", 3.3011e23, 2439.7, 0,
            0.38709927, 0.20563593, 7.00497902, 252.25032350, 77.45779628, 48.33076593,
            281.01, 61.45, 1407.6, 0.034, 440, 0,
            look(SURF_ROCKY, {0.42f, 0.40f, 0.38f}, {0.62f, 0.58f, 0.53f}, {0.25f, 0.24f, 0.23f}, 3.f),
            "The smallest planet and the closest to the Sun. Its day is longer than its year - one solar day lasts 176 Earth days - and temperatures swing from "
            "430 deg C in daylight to -180 deg C at night. Its cratered surface looks much like the Moon's, and it has a surprisingly large iron core.");

  addPlanet(c, "Venus", BodyKind::Planet, "Terrestrial planet", 4.8675e24, 6051.8, 0,
            0.72333566, 0.00677672, 3.39467605, 181.97909950, 131.60246718, 76.67984255,
            272.76, 67.16, -5832.5, 177.36, 737, 0,
            withAtmo(look(SURF_VENUS, {0.86f, 0.72f, 0.42f}, {0.96f, 0.86f, 0.62f}, {0.68f, 0.48f, 0.24f}, 2.f), {1.0f, 0.82f, 0.45f}, 0.6f),
            "Earth's twin in size, but a runaway greenhouse furnace: a crushing CO2 atmosphere 90 times denser than ours keeps the surface at 465 deg C, hot enough to melt lead. "
            "Venus spins backwards, very slowly, and clouds of sulfuric acid hide thousands of volcanoes.");

  addPlanet(c, "Earth", BodyKind::Planet, "Terrestrial planet", 5.9722e24, 6378.137, 0.00335,
            1.00000261, 0.01671123, 0.0, 100.46457166, 102.93768193, 0.0,
            0.0, 90.0, 23.9345, 23.44, 288, 1,
            withAtmo(look(SURF_EARTH, {0.03f, 0.20f, 0.52f}, {0.17f, 0.34f, 0.12f}, {0.62f, 0.52f, 0.32f}, 4.f), {0.32f, 0.58f, 1.0f}, 1.0f),
            "Our home and the only world known to harbour life. Liquid water covers 71% of its surface, a magnetic field shields it from the solar wind, and a "
            "nitrogen-oxygen atmosphere keeps it temperate. Its large Moon stabilises the 23.4 deg tilt that gives us seasons.");

  addPlanet(c, "Mars", BodyKind::Planet, "Terrestrial planet", 6.4171e23, 3396.2, 0.00589,
            1.52371034, 0.09339410, 1.84969142, -4.55343205, -23.94362959, 49.55953891,
            317.68, 52.89, 24.6229, 25.19, 210, 2,
            withAtmo(look(SURF_MARS, {0.66f, 0.30f, 0.15f}, {0.82f, 0.56f, 0.36f}, {0.30f, 0.16f, 0.11f}, 5.f), {0.9f, 0.58f, 0.4f}, 0.3f),
            "The Red Planet, coloured by iron oxide dust. It has the tallest volcano in the solar system, Olympus Mons (22 km), and a canyon system, Valles Marineris, "
            "that would span the United States. Ancient riverbeds show Mars was once warm and wet, and rovers are searching for signs it was ever alive.");

  addPlanet(c, "Jupiter", BodyKind::Planet, "Gas giant", 1.89813e27, 71492, 0.06487,
            5.20288700, 0.04838624, 1.30439695, 34.39644051, 14.72847983, 100.47390909,
            268.06, 64.50, 9.925, 3.13, 165, 97,
            withAtmo(look(SURF_JUPITER, {0.86f, 0.78f, 0.64f}, {0.66f, 0.42f, 0.25f}, {0.45f, 0.30f, 0.20f}, 6.f), {0.8f, 0.7f, 0.5f}, 0.3f),
            "The king of the planets: more than twice as massive as all the others combined and big enough to swallow 1,300 Earths. Its bands are storm-driven clouds "
            "of ammonia, and the Great Red Spot is a hurricane wider than Earth that has raged for centuries. Jupiter spins faster than any planet - a day lasts under 10 hours.");

  { // Saturn with rings
    Appearance sat = withAtmo(look(SURF_SATURN, {0.90f, 0.82f, 0.62f}, {0.78f, 0.68f, 0.50f}, {0.66f, 0.56f, 0.40f}, 7.f), {0.9f, 0.8f, 0.55f}, 0.25f);
    sat.ringInner = 1.240f; sat.ringOuter = 2.270f; sat.ringColor = {0.85f, 0.76f, 0.60f}; sat.ringOpacity = 0.95f;
    addPlanet(c, "Saturn", BodyKind::Planet, "Gas giant", 5.6834e26, 60268, 0.09796,
              9.53667594, 0.05386179, 2.48599187, 49.95424423, 92.59887831, 113.66242448,
              40.59, 83.54, 10.656, 26.73, 134, 274, sat,
              "The jewel of the solar system. Saturn's spectacular rings - 280,000 km wide but often only 10 metres thick - are made of billions of chunks of ice. "
              "The planet is so light that it would float in a big enough bathtub, and the hexagonal storm at its north pole is larger than Earth.");
  }

  { // Uranus
    Appearance ur = withAtmo(look(SURF_ICEGIANT, {0.56f, 0.82f, 0.86f}, {0.62f, 0.86f, 0.90f}, {0.46f, 0.72f, 0.80f}, 8.f), {0.5f, 0.85f, 0.95f}, 0.6f);
    ur.ringInner = 1.64f; ur.ringOuter = 2.0f; ur.ringColor = {0.55f, 0.6f, 0.65f}; ur.ringOpacity = 0.16f;
    addPlanet(c, "Uranus", BodyKind::Planet, "Ice giant", 8.6810e25, 25559, 0.0229,
              19.18916464, 0.04725744, 0.77263783, 313.23810451, 170.95427630, 74.01692503,
              257.31, -15.18, -17.24, 97.77, 76, 29, ur,
              "An ice giant that rolls around the Sun on its side - its axis is tilted 98 deg, probably from an ancient collision - so each pole gets 42 years of sunlight "
              "followed by 42 years of darkness. Methane in its atmosphere gives it a pale cyan colour, and it has the coldest planetary atmosphere, at -224 deg C.");
  }

  addPlanet(c, "Neptune", BodyKind::Planet, "Ice giant", 1.02413e26, 24764, 0.0171,
            30.06992276, 0.00859048, 1.77004347, -55.12002969, 44.96476227, 131.78422574,
            299.36, 43.46, 16.11, 28.32, 72, 16,
            withAtmo(look(SURF_ICEGIANT, {0.14f, 0.28f, 0.78f}, {0.24f, 0.44f, 0.90f}, {0.08f, 0.18f, 0.55f}, 9.f), {0.3f, 0.5f, 1.0f}, 0.6f),
            "The windiest planet, with supersonic storms reaching 2,000 km/h. Deep blue Neptune was found by mathematics before telescopes - astronomers predicted its position "
            "from tugs on Uranus' orbit. A year here lasts 165 Earth years, and it has completed only one orbit since its discovery in 1846.");

  addPlanet(c, "Ceres", BodyKind::DwarfPlanet, "Dwarf planet (asteroid belt)", 9.3835e20, 469.7, 0.0,
            2.7675, 0.0758, 10.59, 231.3, 153.9, 80.31,
            291.43, 66.76, 9.074, 4.0, 168, 0,
            look(SURF_ROCKY, {0.40f, 0.38f, 0.36f}, {0.62f, 0.60f, 0.56f}, {0.20f, 0.19f, 0.19f}, 11.f),
            "The largest object in the asteroid belt and the only dwarf planet in the inner solar system, holding about a third of the belt's mass. Bright salt deposits in "
            "Occator crater show that briny water once seeped to the surface.");

  { // Pluto (barycentre handled by N-body with Charon)
    Appearance pl = look(SURF_PLUTO, {0.62f, 0.48f, 0.38f}, {0.92f, 0.87f, 0.80f}, {0.32f, 0.18f, 0.14f}, 12.f);
    pl = withAtmo(pl, {0.45f, 0.6f, 0.95f}, 0.25f);
    addPlanet(c, "Pluto", BodyKind::DwarfPlanet, "Dwarf planet (Kuiper Belt)", 1.303e22, 1188.3, 0.0,
              39.48211675, 0.24882730, 17.14001206, 238.92903833, 224.06891629, 110.30393684,
              132.99, -6.16, -153.29, 122.53, 44, 5, pl,
              "The best-known Kuiper Belt object, demoted to dwarf planet in 2006 but far from boring. New Horizons found towering water-ice mountains, nitrogen glaciers, "
              "and a giant heart-shaped plain, Tombaugh Regio. Its orbit is so eccentric that it sometimes dips inside Neptune's. Pluto and Charon orbit each other.");
  }

  addPlanet(c, "Eris", BodyKind::DwarfPlanet, "Dwarf planet (scattered disc)", 1.6466e22, 1163, 0.0,
            67.78, 0.4407, 44.04, 33.5, 187.6, 35.95,
            0.0, 90.0, 25.9, 78.0, 42, 1,
            look(SURF_ICY, {0.88f, 0.88f, 0.90f}, {0.72f, 0.72f, 0.76f}, {0.97f, 0.97f, 1.0f}, 13.f),
            "The most massive known dwarf planet - 27% heavier than Pluto - whose discovery in 2005 triggered the debate that led to Pluto's reclassification. It follows a "
            "steep, stretched orbit that takes it up to 97 AU from the Sun, and its surface is coated in bright methane ice.");

  // ---- Moons ----
  int n = 0;
  for (const MoonRow& m : kMoons) {
    BodyDef b;
    b.name = m.name; b.parent = m.parent; b.kind = BodyKind::Moon; b.group = m.group;
    b.massKg = m.massKg; b.radiusKm = m.radius;
    b.aKm = m.aKm; b.e = m.e; b.incDeg = m.inc; b.tempK = m.tempK;
    b.refEcliptic = (std::string(m.name) == "Moon");
    b.look = moonLook(m.look, 20.0f + (float)(n++));
    if (std::string(m.name) == "Moon") { b.poleRA = 270.0; b.poleDec = 66.56; b.tiltDeg = 6.68; }
    if (m.desc) {
      b.description = m.desc;
    } else {
      char buf[512];
      std::snprintf(buf, sizeof buf,
                    "%s is a small moon of %s (%s), roughly %.0f km across, with an orbit %.0f km from the planet. "
                    "Little more than its orbit and size is known; most such moons were discovered by spacecraft flybys or large survey telescopes.",
                    m.name, m.parent, m.group, m.radius * 2.0, m.aKm);
      b.description = buf;
    }
    c.bodies.push_back(b);
  }
  return c;
}
