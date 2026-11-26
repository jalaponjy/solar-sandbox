#include "renderer.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "orbit.h"

// ---------------------------------------------------------------- helpers
glm::mat3 bodyRotation(const glm::dvec3& pole, double angle) {
  const Frame f = frameFromPole(pole);
  const double c = std::cos(angle), s = std::sin(angle);
  const glm::dvec3 x = f.x * c + f.y * s;
  const glm::dvec3 y = -f.x * s + f.y * c;
  return glm::mat3(glm::vec3(x), glm::vec3(y), glm::vec3(f.z));
}

static void setF(GLint l, float v) { glUniform1f(l, v); }
static void setI(GLint l, int v) { glUniform1i(l, v); }
static void setV3(GLint l, const glm::vec3& v) { glUniform3fv(l, 1, &v[0]); }
static void setM4(GLint l, const glm::mat4& m) { glUniformMatrix4fv(l, 1, GL_FALSE, glm::value_ptr(m)); }
static void setM3(GLint l, const glm::mat3& m) { glUniformMatrix3fv(l, 1, GL_FALSE, glm::value_ptr(m)); }

std::string Renderer::readShader(const std::string& file) {
  for (const std::string& d : dirs_) {
    std::ifstream in(d + "/" + file, std::ios::binary);
    if (in) { std::stringstream ss; ss << in.rdbuf(); return ss.str(); }
  }
  std::fprintf(stderr, "shader not found: %s\n", file.c_str());
  return {};
}

static GLuint compile(GLenum type, const std::string& src, const char* name) {
  GLuint s = glCreateShader(type);
  const char* p = src.c_str();
  glShaderSource(s, 1, &p, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096]; glGetShaderInfoLog(s, sizeof log, nullptr, log);
    std::fprintf(stderr, "shader compile error in %s:\n%s\n", name, log);
    glDeleteShader(s);
    return 0;
  }
  return s;
}

bool Renderer::loadProgram(Program& p, const char* name, const char* vs, const char* fs, const char* gs) {
  const std::string head = "#version 330 core\n" + common_ + "\n#line 1\n";
  GLuint v = compile(GL_VERTEX_SHADER, head + readShader(vs), vs);
  GLuint f = compile(GL_FRAGMENT_SHADER, head + readShader(fs), fs);
  GLuint g = gs ? compile(GL_GEOMETRY_SHADER, head + readShader(gs), gs) : 0;
  if (!v || !f || (gs && !g)) return false;
  p.id = glCreateProgram();
  glAttachShader(p.id, v); glAttachShader(p.id, f);
  if (g) glAttachShader(p.id, g);
  glLinkProgram(p.id);
  GLint ok = 0;
  glGetProgramiv(p.id, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[4096]; glGetProgramInfoLog(p.id, sizeof log, nullptr, log);
    std::fprintf(stderr, "link error in %s:\n%s\n", name, log);
    return false;
  }
  glDeleteShader(v); glDeleteShader(f); if (g) glDeleteShader(g);
  return true;
}

// ---------------------------------------------------------------- init
bool Renderer::init(const std::vector<std::string>& shaderDirs) {
  dirs_ = shaderDirs;
  common_ = readShader("common.glsl");
  if (common_.empty()) return false;
  bool ok = true;
  ok &= loadProgram(body_, "body", "body.vert", "body.frag");
  ok &= loadProgram(atmo_, "atmo", "atmo.vert", "atmo.frag");
  ok &= loadProgram(ring_, "ring", "ring.vert", "ring.frag");
  ok &= loadProgram(line_, "line", "line.vert", "line.frag", "line.geom");
  ok &= loadProgram(sky_, "sky", "fullscreen.vert", "sky.frag");
  ok &= loadProgram(points_, "points", "points.vert", "points.frag");
  ok &= loadProgram(glow_, "glow", "glow.vert", "glow.frag");
  ok &= loadProgram(bloomDown_, "bloom_down", "fullscreen.vert", "bloom_down.frag");
  ok &= loadProgram(bloomUp_, "bloom_up", "fullscreen.vert", "bloom_up.frag");
  ok &= loadProgram(composite_, "composite", "fullscreen.vert", "composite.frag");
  if (!ok) return false;

  // UV sphere, pole on +z
  {
    const int lon = 192, lat = 96;
    std::vector<glm::vec3> v;
    std::vector<unsigned> idx;
    for (int i = 0; i <= lat; ++i) {
      const float th = (float)kPi * i / lat;
      for (int j = 0; j <= lon; ++j) {
        const float ph = 2.f * (float)kPi * j / lon;
        v.emplace_back(std::sin(th) * std::cos(ph), std::sin(th) * std::sin(ph), std::cos(th));
      }
    }
    for (int i = 0; i < lat; ++i)
      for (int j = 0; j < lon; ++j) {
        const unsigned a = i * (lon + 1) + j, b = a + lon + 1;
        idx.insert(idx.end(), {a, b, a + 1, a + 1, b, b + 1});
      }
    sphereIndices_ = (GLuint)idx.size();
    glGenVertexArrays(1, &sphereVAO_); glGenBuffers(1, &sphereVBO_); glGenBuffers(1, &sphereEBO_);
    glBindVertexArray(sphereVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, sphereVBO_);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(glm::vec3), v.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphereEBO_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned), idx.data(), GL_STATIC_DRAW);
  }
  // Ring annulus: xy = unit direction, z = radial parameter
  {
    const int seg = 360, rad = 24;
    std::vector<glm::vec3> v;
    std::vector<unsigned> idx;
    for (int i = 0; i <= rad; ++i)
      for (int j = 0; j <= seg; ++j) {
        const float a = 2.f * (float)kPi * j / seg;
        v.emplace_back(std::cos(a), std::sin(a), (float)i / rad);
      }
    for (int i = 0; i < rad; ++i)
      for (int j = 0; j < seg; ++j) {
        const unsigned a = i * (seg + 1) + j, b = a + seg + 1;
        idx.insert(idx.end(), {a, b, a + 1, a + 1, b, b + 1});
      }
    ringIndices_ = (GLuint)idx.size();
    glGenVertexArrays(1, &ringVAO_); glGenBuffers(1, &ringVBO_); glGenBuffers(1, &ringEBO_);
    glBindVertexArray(ringVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, ringVBO_);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(glm::vec3), v.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ringEBO_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned), idx.data(), GL_STATIC_DRAW);
  }
  glGenVertexArrays(1, &emptyVAO_);
  // dynamic line buffer
  glGenVertexArrays(1, &lineVAO_); glGenBuffers(1, &lineVBO_);
  glBindVertexArray(lineVAO_);
  glBindBuffer(GL_ARRAY_BUFFER, lineVBO_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(LineVertex), (void*)sizeof(glm::vec3));

  // Asteroid belt + Kuiper belt particles (static orbital elements, animated in the shader)
  {
    struct P { float a, e, i, node, peri, M; };
    std::vector<P> pts;
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> U(0.f, 1.f);
    auto gauss = [&]() { float s = 0; for (int k = 0; k < 4; ++k) s += U(rng); return (s - 2.f) * 1.7f; };
    for (int k = 0; k < 9000; ++k) {            // main belt 2.1 - 3.3 AU with a few Kirkwood gaps
      float a;
      do { a = 2.1f + 1.25f * U(rng);
      } while ((std::fabs(a - 2.5f) < 0.03f) || (std::fabs(a - 2.82f) < 0.025f) || (std::fabs(a - 2.95f) < 0.02f));
      pts.push_back({a * (float)kAU, std::fabs(gauss()) * 0.09f, std::fabs(gauss()) * 0.12f,
                     U(rng) * 6.2832f, U(rng) * 6.2832f, U(rng) * 6.2832f});
    }
    for (int k = 0; k < 6000; ++k) {            // Kuiper belt 30 - 50 AU
      const float a = 30.f + 20.f * U(rng);
      pts.push_back({a * (float)kAU, std::fabs(gauss()) * 0.06f, std::fabs(gauss()) * 0.16f,
                     U(rng) * 6.2832f, U(rng) * 6.2832f, U(rng) * 6.2832f});
    }
    beltCount_ = (GLuint)pts.size();
    glGenVertexArrays(1, &beltVAO_); glGenBuffers(1, &beltVBO_);
    glBindVertexArray(beltVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, beltVBO_);
    glBufferData(GL_ARRAY_BUFFER, pts.size() * sizeof(P), pts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(P), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(P), (void*)(3 * sizeof(float)));
  }
  glBindVertexArray(0);

  // preview target
  glGenFramebuffers(1, &previewFBO_);
  glGenTextures(1, &previewTex_);
  glBindTexture(GL_TEXTURE_2D, previewTex_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kPreview, kPreview, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glGenRenderbuffers(1, &previewDepth_);
  glBindRenderbuffer(GL_RENDERBUFFER, previewDepth_);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, kPreview, kPreview);
  glBindFramebuffer(GL_FRAMEBUFFER, previewFBO_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, previewTex_, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, previewDepth_);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return true;
}

void Renderer::destroyTargets() {
  if (msaaFBO_) { glDeleteFramebuffers(1, &msaaFBO_); glDeleteRenderbuffers(1, &msaaColor_); glDeleteRenderbuffers(1, &msaaDepth_); }
  if (hdrFBO_) { glDeleteFramebuffers(1, &hdrFBO_); glDeleteTextures(1, &hdrTex_); }
  for (int i = 0; i < kBloomLevels; ++i)
    if (bloomFBO_[i]) { glDeleteFramebuffers(1, &bloomFBO_[i]); glDeleteTextures(1, &bloomTex_[i]); }
  msaaFBO_ = hdrFBO_ = 0;
  for (int i = 0; i < kBloomLevels; ++i) bloomFBO_[i] = 0;
}

static GLuint makeHdrTex(int w, int h) {
  GLuint t; glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return t;
}

void Renderer::createTargets(int w, int h) {
  destroyTargets();
  w_ = w; h_ = h;
  const int samples = 4;
  glGenFramebuffers(1, &msaaFBO_);
  glGenRenderbuffers(1, &msaaColor_);
  glGenRenderbuffers(1, &msaaDepth_);
  glBindRenderbuffer(GL_RENDERBUFFER, msaaColor_);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA16F, w, h);
  glBindRenderbuffer(GL_RENDERBUFFER, msaaDepth_);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT32F, w, h);
  glBindFramebuffer(GL_FRAMEBUFFER, msaaFBO_);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaaColor_);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msaaDepth_);

  glGenFramebuffers(1, &hdrFBO_);
  hdrTex_ = makeHdrTex(w, h);
  glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hdrTex_, 0);

  int bw = w, bh = h;
  for (int i = 0; i < kBloomLevels; ++i) {
    bw = std::max(1, bw / 2); bh = std::max(1, bh / 2);
    bloomW_[i] = bw; bloomH_[i] = bh;
    bloomTex_[i] = makeHdrTex(bw, bh);
    glGenFramebuffers(1, &bloomFBO_[i]);
    glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO_[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex_[i], 0);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::resize(int w, int h) {
  if (w < 1 || h < 1) return;
  if (w != w_ || h != h_) createTargets(w, h);
}

void Renderer::setCommon(Program& p) {
  setF(p.at("uFar"), cam_.farPlane);
  setI(p.at("uLDR"), ldr_);
}

// ---------------------------------------------------------------- frame
void Renderer::beginScene(const RenderCamera& cam, float timeSec, const RenderSettings& rs) {
  cam_ = cam; rs_ = rs; time_ = timeSec; ldr_ = 0;
  glBindFramebuffer(GL_FRAMEBUFFER, msaaFBO_);
  glViewport(0, 0, w_, h_);
  glClearColor(0, 0, 0, 1);
  glDepthMask(GL_TRUE);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_MULTISAMPLE);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);

  glUseProgram(sky_.id);
  setCommon(sky_);
  setM3(sky_.at("uViewRotInv"), cam_.viewRotInv);
  setF(sky_.at("uTanHalf"), cam_.tanHalfFov);
  setF(sky_.at("uAspect"), cam_.aspect);
  setF(sky_.at("uHeight"), (float)cam_.height);
  setF(sky_.at("uSkyBright"), rs_.skyBrightness);
  glBindVertexArray(emptyVAO_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glEnable(GL_PROGRAM_POINT_SIZE);
}

void Renderer::drawBelt(const glm::vec3& sunRel, double muSun, double timeSec, float pointSize) {
  if (!rs_.belts) return;
  glUseProgram(points_.id);
  setCommon(points_);
  setM4(points_.at("uVP"), cam_.vp);
  setV3(points_.at("uCamRel"), sunRel);
  setF(points_.at("uMu"), (float)muSun);
  setF(points_.at("uTimeSec"), (float)timeSec);
  setF(points_.at("uPointSize"), pointSize);
  setV3(points_.at("uColor"), glm::vec3(0.62f, 0.55f, 0.48f) * 0.40f);
  glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE);
  glDepthMask(GL_FALSE);
  glBindVertexArray(beltVAO_);
  glDrawArrays(GL_POINTS, 0, (GLsizei)beltCount_);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
}

void Renderer::setBodyUniforms(Program& p, const BodyDraw& b) {
  setCommon(p);
  setM4(p.at("uVP"), cam_.vp);
  setM3(p.at("uRot"), b.rot);
  setV3(p.at("uCenter"), b.center);
  setF(p.at("uRadius"), b.radius);
  setF(p.at("uFlat"), b.flat);
  setV3(p.at("uSunDir"), b.sunDir);
  setF(p.at("uSunI"), b.sunI);
  setF(p.at("uTime"), time_);
  setI(p.at("uType"), b.look.type);
  setV3(p.at("uC1"), b.look.c1); setV3(p.at("uC2"), b.look.c2); setV3(p.at("uC3"), b.look.c3);
  setF(p.at("uSeed"), b.look.seed);
  glUniform4f(p.at("uAtmo"), b.look.atmo.r, b.look.atmo.g, b.look.atmo.b, b.look.atmoStrength);
  setF(p.at("uRingIn"), b.look.ringInner);
  setF(p.at("uRingOut"), b.look.ringOuter);
  setF(p.at("uRingOp"), b.look.ringOpacity);
}

void Renderer::drawBody(const BodyDraw& b) {
  glUseProgram(body_.id);
  setBodyUniforms(body_, b);
  glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW);
  glBindVertexArray(sphereVAO_);
  glDrawElements(GL_TRIANGLES, (GLsizei)sphereIndices_, GL_UNSIGNED_INT, nullptr);
  glDisable(GL_CULL_FACE);
}

void Renderer::drawRing(const BodyDraw& b) {
  if (b.look.ringOuter <= 0) return;
  glUseProgram(ring_.id);
  setBodyUniforms(ring_, b);
  setV3(ring_.at("uRingColor"), b.look.ringColor);
  glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
  glBindVertexArray(ringVAO_);
  glDrawElements(GL_TRIANGLES, (GLsizei)ringIndices_, GL_UNSIGNED_INT, nullptr);
  glDepthMask(GL_TRUE); glDisable(GL_BLEND);
}

void Renderer::drawAtmosphere(const BodyDraw& b) {
  if (b.look.atmoStrength <= 0 || b.look.type == SURF_SUN) return;
  const float shell = b.radius * (1.0f + b.atmoThickness);
  glUseProgram(atmo_.id);
  setCommon(atmo_);
  setM4(atmo_.at("uVP"), cam_.vp);
  setV3(atmo_.at("uCenter"), b.center);
  setF(atmo_.at("uShellRadius"), shell);
  setF(atmo_.at("uPlanetRadius"), b.radius);
  glUniform4f(atmo_.at("uAtmo"), b.look.atmo.r, b.look.atmo.g, b.look.atmo.b, b.look.atmoStrength);
  setV3(atmo_.at("uSunDir"), b.sunDir);
  setF(atmo_.at("uSunI"), b.sunI);
  glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE);
  glDepthMask(GL_FALSE);
  glEnable(GL_CULL_FACE);
  glCullFace(glm::length(b.center) < shell ? GL_FRONT : GL_BACK);   // camera inside the shell?
  glBindVertexArray(sphereVAO_);
  glDrawElements(GL_TRIANGLES, (GLsizei)sphereIndices_, GL_UNSIGNED_INT, nullptr);
  glCullFace(GL_BACK); glDisable(GL_CULL_FACE);
  glDepthMask(GL_TRUE); glDisable(GL_BLEND);
}

void Renderer::drawGlow(const glm::vec3& center, float size, float core, const glm::vec3& color, float intensity) {
  glUseProgram(glow_.id);
  setCommon(glow_);
  setM4(glow_.at("uVP"), cam_.vp);
  setV3(glow_.at("uCenter"), center);
  const glm::mat3 vr = glm::mat3(cam_.view);
  setV3(glow_.at("uRight"), glm::vec3(vr[0][0], vr[1][0], vr[2][0]));
  setV3(glow_.at("uUp"), glm::vec3(vr[0][1], vr[1][1], vr[2][1]));
  setF(glow_.at("uSize"), size);
  setF(glow_.at("uCoreRadius"), core);
  setV3(glow_.at("uColor"), color);
  setF(glow_.at("uIntensity"), intensity);
  glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE);
  glDepthMask(GL_FALSE);
  glBindVertexArray(emptyVAO_);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDepthMask(GL_TRUE); glDisable(GL_BLEND);
}

void Renderer::drawLines(const std::vector<LineVertex>& v, const glm::vec3& color, float width, float fade) {
  if (v.size() < 2 || fade <= 0.002f) return;
  glUseProgram(line_.id);
  setCommon(line_);
  setM4(line_.at("uVP"), cam_.vp);
  glUniform2f(line_.at("uViewport"), (float)cam_.width, (float)cam_.height);
  setF(line_.at("uWidth"), width);
  setV3(line_.at("uColor"), color);
  setF(line_.at("uFade"), fade);
  glBindVertexArray(lineVAO_);
  glBindBuffer(GL_ARRAY_BUFFER, lineVBO_);
  glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(LineVertex), v.data(), GL_STREAM_DRAW);
  glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE);
  glDepthMask(GL_FALSE);
  glDrawArrays(GL_LINE_STRIP, 0, (GLsizei)v.size());
  glDepthMask(GL_TRUE); glDisable(GL_BLEND);
}

void Renderer::endScene() {
  glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
  // resolve MSAA
  glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFBO_);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, hdrFBO_);
  glBlitFramebuffer(0, 0, w_, h_, 0, 0, w_, h_, GL_COLOR_BUFFER_BIT, GL_NEAREST);

  glBindVertexArray(emptyVAO_);
  // downsample chain
  glUseProgram(bloomDown_.id);
  setCommon(bloomDown_);
  for (int i = 0; i < kBloomLevels; ++i) {
    glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO_[i]);
    glViewport(0, 0, bloomW_[i], bloomH_[i]);
    const GLuint src = i == 0 ? hdrTex_ : bloomTex_[i - 1];
    const int sw = i == 0 ? w_ : bloomW_[i - 1], sh = i == 0 ? h_ : bloomH_[i - 1];
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src);
    setI(bloomDown_.at("uTex"), 0);
    glUniform2f(bloomDown_.at("uTexel"), 1.f / sw, 1.f / sh);
    setI(bloomDown_.at("uFirst"), i == 0 ? 1 : 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  // upsample chain (additive)
  glUseProgram(bloomUp_.id);
  setCommon(bloomUp_);
  glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE);
  for (int i = kBloomLevels - 1; i > 0; --i) {
    glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO_[i - 1]);
    glViewport(0, 0, bloomW_[i - 1], bloomH_[i - 1]);
    glBindTexture(GL_TEXTURE_2D, bloomTex_[i]);
    setI(bloomUp_.at("uTex"), 0);
    glUniform2f(bloomUp_.at("uTexel"), 1.f / bloomW_[i], 1.f / bloomH_[i]);
    setF(bloomUp_.at("uRadius"), 1.0f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  glDisable(GL_BLEND);

  // composite to the window
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, w_, h_);
  glUseProgram(composite_.id);
  setCommon(composite_);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, hdrTex_);
  glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, bloomTex_[0]);
  setI(composite_.at("uScene"), 0); setI(composite_.at("uBloom"), 1);
  setF(composite_.at("uBloomStrength"), rs_.bloom);
  setF(composite_.at("uExposure"), rs_.exposure);
  glUniform2f(composite_.at("uResolution"), (float)w_, (float)h_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glActiveTexture(GL_TEXTURE0);
}

bool Renderer::readScreenshot(std::vector<uint8_t>& rgba, int& w, int& h) {
  w = w_; h = h_;
  std::vector<uint8_t> tmp((size_t)w * h * 4);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glReadBuffer(GL_BACK);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, tmp.data());
  rgba.resize(tmp.size());
  for (int y = 0; y < h; ++y)   // flip vertically
    std::copy(tmp.begin() + (size_t)(h - 1 - y) * w * 4, tmp.begin() + (size_t)(h - y) * w * 4, rgba.begin() + (size_t)y * w * 4);
  for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
  return true;
}

// ---------------------------------------------------------------- preview
GLuint Renderer::renderPreview(const BodyDraw& body, float timeSec, int* sizeOut) {
  const RenderCamera savedCam = cam_;
  const float savedTime = time_;
  const int savedLdr = ldr_;

  RenderCamera c;
  const float fov = glm::radians(28.f);
  c.tanHalfFov = std::tan(fov * 0.5f);
  c.aspect = 1.f; c.width = c.height = kPreview;
  c.farPlane = 1000.f;
  c.view = glm::mat4(1.f);
  c.proj = glm::perspective(fov, 1.f, 0.05f, 1000.f);
  c.vp = c.proj * c.view;
  c.viewRotInv = glm::mat3(1.f);
  cam_ = c; time_ = timeSec; ldr_ = 1;

  BodyDraw b = body;
  const float extent = std::max(1.0f, b.look.ringOuter * 0.95f);
  b.radius = 1.0f / extent;
  b.center = glm::vec3(0.f, 0.f, -b.radius * 3.85f / 1.0f * 1.0f);
  b.center.z = -4.6f / extent * 1.0f;
  b.sunDir = glm::normalize(glm::vec3(-0.78f, 0.30f, 0.55f));
  b.sunI = 1.55f;
  if (b.look.type == SURF_SUN) b.radius *= 1.0f;

  glBindFramebuffer(GL_FRAMEBUFFER, previewFBO_);
  glViewport(0, 0, kPreview, kPreview);
  glDepthMask(GL_TRUE);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);

  glUseProgram(sky_.id);
  setCommon(sky_);
  setM3(sky_.at("uViewRotInv"), glm::mat3(1.f));
  setF(sky_.at("uTanHalf"), c.tanHalfFov);
  setF(sky_.at("uAspect"), 1.f);
  setF(sky_.at("uHeight"), (float)kPreview);
  setF(sky_.at("uSkyBright"), 2.2f);
  glBindVertexArray(emptyVAO_);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL);
  drawBody(b);
  drawRing(b);
  drawAtmosphere(b);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  cam_ = savedCam; time_ = savedTime; ldr_ = savedLdr;
  if (sizeOut) *sizeOut = kPreview;
  return previewTex_;
}
