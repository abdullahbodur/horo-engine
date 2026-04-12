#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "math/Mat4.h"
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "math/Vec4.h"
#include "renderer/Light.h"

namespace Monolith {

class Material;
class Mesh;
class Shader;
class SkinnedMesh;

enum class RenderPassId {
  CompatibilityScene,
  OpaqueScene,
  DeferredOpaque,
  WireframeOverlay,
  DebugOverlay,
};

struct RenderView {
  Mat4 view = Mat4::Identity();
  Mat4 projection = Mat4::Identity();
  Vec3 cameraPosition = Vec3::Zero();
};

enum class TemporalQualityTier : uint8_t {
  Disabled = 0,
  Low,
  Medium,
  High,
  Ultra,
};

struct TemporalJitterConfig {
  bool enabled = false;
  TemporalQualityTier qualityTier = TemporalQualityTier::Disabled;
  uint32_t sequenceSeed = 0;
  uint32_t sequenceLength = 0;
  uint64_t frameIndex = 0;
};

inline constexpr uint32_t TemporalJitterSampleCountForTier(TemporalQualityTier tier) {
  switch (tier) {
    case TemporalQualityTier::Disabled:
      return 1u;
    case TemporalQualityTier::Low:
      return 2u;
    case TemporalQualityTier::Medium:
      return 8u;
    case TemporalQualityTier::High:
      return 16u;
    case TemporalQualityTier::Ultra:
      return 32u;
  }
  return 1u;
}

inline float HaltonSequence(uint32_t index, uint32_t base) {
  float result = 0.0f;
  float inverseBase = 1.0f / static_cast<float>(base);
  float fraction = inverseBase;
  uint32_t i = index;
  while (i > 0u) {
    result += static_cast<float>(i % base) * fraction;
    i /= base;
    fraction *= inverseBase;
  }
  return result;
}

inline Vec2 BuildTemporalJitterOffset(const TemporalJitterConfig& jitter) {
  if (!jitter.enabled || jitter.qualityTier == TemporalQualityTier::Disabled)
    return Vec2::Zero();

  const uint32_t tierSampleCount = TemporalJitterSampleCountForTier(jitter.qualityTier);
  const uint32_t requestedSequenceLength = std::max(jitter.sequenceLength, 1u);
  const uint32_t sampleCount = std::max(tierSampleCount, requestedSequenceLength);
  const uint64_t deterministicIndex64 =
      (jitter.frameIndex + static_cast<uint64_t>(jitter.sequenceSeed)) % sampleCount;
  const uint32_t deterministicIndex = static_cast<uint32_t>(deterministicIndex64);
  const float jitterX = HaltonSequence(deterministicIndex + 1u, 2u) - 0.5f;
  const float jitterY = HaltonSequence(deterministicIndex + 1u, 3u) - 0.5f;
  return {jitterX, jitterY};
}

struct RenderFrameTemporalState {
  bool enableTemporalReprojection = false;
  bool forceHistoryReset = false;
  bool cameraCut = false;
  bool sceneChanged = false;
  uint64_t sceneToken = 0;
  uint64_t cameraToken = 0;
  uint32_t renderWidth = 0;
  uint32_t renderHeight = 0;
  TemporalJitterConfig jitter{};
};

struct RenderFrameConfig {
  std::vector<Light> lights;
  std::string debugLabel;
  Vec4 clearColor = {0.1f, 0.1f, 0.15f, 1.0f};
  bool clearColorBuffer = true;
  bool clearDepthBuffer = true;
  RenderFrameTemporalState temporal{};
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
