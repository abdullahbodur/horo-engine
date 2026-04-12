#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "math/Mat4.h"
#include "math/Vec3.h"
#include "math/Vec4.h"
#include "renderer/Light.h"

namespace Monolith {

class Material;
class Mesh;
class Shader;
class SkinnedMesh;

enum class RenderFeatureQualityTier : uint8_t {
  Off = 0,
  Low = 1,
  Medium = 2,
  High = 3,
  Ultra = 4,
};

enum class RenderPassId {
  CompatibilityScene,
  OpaqueScene,
  ScreenSpaceReflections,
  ScreenSpaceGlobalIllumination,
  TemporalGiResolve,
  GiComposite,
  WireframeOverlay,
  DebugOverlay,
};

struct RenderView {
  Mat4 view = Mat4::Identity();
  Mat4 projection = Mat4::Identity();
  Vec3 cameraPosition = Vec3::Zero();
};

struct GiPipelineFrameConfig {
  bool enableScreenSpaceReflections = false;
  RenderFeatureQualityTier reflectionQuality = RenderFeatureQualityTier::Off;
  bool enableScreenSpaceGlobalIllumination = false;
  RenderFeatureQualityTier globalIlluminationQuality = RenderFeatureQualityTier::Off;
  bool enableTemporalResolve = false;
  bool enableComposite = false;
  bool resetTemporalHistory = false;
};

struct RenderFrameConfig {
  std::vector<Light> lights;
  std::string debugLabel;
  Vec4 clearColor = {0.1f, 0.1f, 0.15f, 1.0f};
  bool clearColorBuffer = true;
  bool clearDepthBuffer = true;
  GiPipelineFrameConfig giPipeline;
};

struct RenderPassConfig {
  RenderPassId id = RenderPassId::OpaqueScene;
  RenderView view;
  std::string debugLabel;
};

struct MeshDrawCommand {
  const Mesh* mesh = nullptr;
  const Material* material = nullptr;
  Mat4 modelMatrix = Mat4::Identity();
};

struct SkinnedMeshDrawCommand {
  const SkinnedMesh* mesh = nullptr;
  const Material* material = nullptr;
  Mat4 modelMatrix = Mat4::Identity();
  const std::vector<Mat4>* boneMatrices = nullptr;
};

struct WireframeDrawCommand {
  const Mesh* mesh = nullptr;
  const Shader* shader = nullptr;
  Mat4 modelMatrix = Mat4::Identity();
  Vec4 color = {0.2f, 0.8f, 0.2f, 1.0f};
};

}  // namespace Monolith
