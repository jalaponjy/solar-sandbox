// OpenGL 3.3 renderer: procedural planets, atmospheres, rings, orbit lines, HDR bloom.
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <glad/gl.h>
#include <glm/glm.hpp>
#include "catalog.h"

struct RenderCamera {
  glm::mat4 view{1.f}, proj{1.f}, vp{1.f};
  glm::mat3 viewRotInv{1.f};
  float tanHalfFov = 0.4f, aspect = 1.f, farPlane = 1e9f;
  int width = 1, height = 1;
};

struct RenderSettings {
  float bloom = 0.65f;
  float exposure = 1.15f;
  float skyBrightness = 1.0f;
  bool belts = true;
};

struct BodyDraw {                 // everything the shaders need to draw one body
  glm::vec3 center{0};            // camera-relative, scene units (1 unit = 1000 km)
  float radius = 1;
  float flat = 1;                 // 1 - flattening
  glm::mat3 rot{1.f};             // body frame -> world
  Appearance look;
  glm::vec3 sunDir{1, 0, 0};
  float sunI = 1;
  float atmoThickness = 0.12f;    // shell height as a fraction of the radius
};

struct LineVertex { glm::vec3 p; float a; };

glm::mat3 bodyRotation(const glm::dvec3& pole, double angle);   // spin about the pole

class Renderer {
 public:
  bool init(const std::vector<std::string>& shaderDirs);
  void resize(int w, int h);

  void beginScene(const RenderCamera& cam, float timeSec, const RenderSettings& rs);
  void drawBelt(const glm::vec3& sunRel, double muSun, double timeSec, float pointSize);
  void drawBody(const BodyDraw& b);
  void drawRing(const BodyDraw& b);
  void drawAtmosphere(const BodyDraw& b);
  void drawGlow(const glm::vec3& center, float size, float coreRadius, const glm::vec3& color, float intensity);
  void drawLines(const std::vector<LineVertex>& v, const glm::vec3& color, float width, float fade);
  void endScene();                    // resolve MSAA, bloom, tone map to the window

  // Renders a rotating portrait of a body into an internal texture and returns it.
  GLuint renderPreview(const BodyDraw& b, float timeSec, int* sizeOut = nullptr);
  bool readScreenshot(std::vector<uint8_t>& rgba, int& w, int& h);
  GLuint previewTexture() const { return previewTex_; }

 private:
  struct Program {
    GLuint id = 0;
    std::unordered_map<std::string, GLint> loc;
    GLint at(const char* n) {
      auto it = loc.find(n);
      if (it != loc.end()) return it->second;
      return loc[n] = glGetUniformLocation(id, n);
    }
  };
  bool loadProgram(Program& p, const char* name, const char* vs, const char* fs, const char* gs = nullptr);
  std::string readShader(const std::string& file);
  void setCommon(Program& p);
  void createTargets(int w, int h);
  void destroyTargets();
  void drawBodyInner(const BodyDraw& b);
  void setBodyUniforms(Program& p, const BodyDraw& b);

  std::vector<std::string> dirs_;
  std::string common_;
  Program body_, atmo_, ring_, line_, sky_, points_, glow_, bloomDown_, bloomUp_, composite_;
  GLuint sphereVAO_ = 0, sphereVBO_ = 0, sphereEBO_ = 0, sphereIndices_ = 0;
  GLuint ringVAO_ = 0, ringVBO_ = 0, ringEBO_ = 0, ringIndices_ = 0;
  GLuint emptyVAO_ = 0;
  GLuint lineVAO_ = 0, lineVBO_ = 0;
  GLuint beltVAO_ = 0, beltVBO_ = 0, beltCount_ = 0;

  // targets
  int w_ = 0, h_ = 0;
  GLuint msaaFBO_ = 0, msaaColor_ = 0, msaaDepth_ = 0;
  GLuint hdrFBO_ = 0, hdrTex_ = 0;
  static constexpr int kBloomLevels = 6;
  GLuint bloomFBO_[kBloomLevels]{}, bloomTex_[kBloomLevels]{};
  int bloomW_[kBloomLevels]{}, bloomH_[kBloomLevels]{};
  GLuint previewFBO_ = 0, previewTex_ = 0, previewDepth_ = 0;
  static constexpr int kPreview = 420;

  RenderCamera cam_;
  RenderSettings rs_;
  float time_ = 0;
  int ldr_ = 0;
};
