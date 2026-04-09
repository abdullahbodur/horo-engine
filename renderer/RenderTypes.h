#pragma once

#include <string>
#include <vector>

#include "math/Mat4.h"
#include "math/Vec4.h"
#include "renderer/Camera.h"
#include "renderer/Light.h"

namespace Monolith {

class Material;
class Mesh;
class Shader;
class SkinnedMesh;

enum class RenderPassId {
  CompatibilityScene,
  OpaqueScene,
  WireframeOverlay,
  DebugOverlay,
};

struct RenderView {
  Mat4 view = Mat4::Identity();
  Mat4 projection = Mat4::Identity();
  Vec3 cameraPosition = Vec3::Zero();

  static RenderView FromCamera(const Camera& camera) {
    return {camera.GetView(), camera.GetProjection(), camera.position};
  }
};

struct RenderFrameConfig {
  std::vector<Light> lights;
  std::string debugLabel;
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
