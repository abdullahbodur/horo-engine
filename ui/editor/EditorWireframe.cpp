/**
 * @file EditorWireframe.cpp
 * @brief Submits mesh wireframe passes over scene objects when overlay mode is enabled in @ref EditorLayer.
 */
#include "ui/editor/EditorLayer.h"
#include "ui/editor/EditorLayerInternal.h"

#include "math/Mat4.h"
#include "math/MathUtils.h"
#include "math/Quaternion.h"
#include "renderer/Mesh.h"
#include "renderer/RenderViewUtils.h"
#include "renderer/Renderer.h"
#include "ui/editor/components/EditorAssetThumbnailPreview.h"

namespace Horo::Editor {
namespace {

/** @brief Component-wise multiplication of two vectors (non-uniform scale combine). */
Vec3 MultiplyComponents(const Vec3 &a, const Vec3 &b) {
  return {a.x * b.x, a.y * b.y, a.z * b.z};
}

/**
 * @brief Resolves mesh path and optional render scale from asset linkage or mesh prop.
 * @param doc Scene containing asset definitions.
 * @param obj Object whose mesh source is queried.
 * @param outMeshPath Optional mesh path output; cleared when absent or on failure.
 * @param outAssetRenderScale Optional scale parsed from asset CSV, or identity.
 * @return True when a non-empty mesh path was resolved.
 */
bool TryResolveObjectMeshPath(const SceneDocument &doc, const SceneObject &obj,
                              std::string *outMeshPath,
                              Vec3 *outAssetRenderScale) {
  if (outMeshPath)
    outMeshPath->clear();
  if (outAssetRenderScale)
    *outAssetRenderScale = Vec3::One();

  if (!obj.assetId.empty()) {
    const auto assetIt = doc.assets.find(obj.assetId);
    if (assetIt == doc.assets.end())
      return false;
    if (outMeshPath)
      *outMeshPath = assetIt->second.mesh;
    if (outAssetRenderScale) {
      Vec3 parsedScale = Vec3::One();
      if (TryParseVec3Csv(assetIt->second.renderScale, &parsedScale))
        *outAssetRenderScale = parsedScale;
    }
    return !assetIt->second.mesh.empty();
  }

  const auto propIt = obj.props.find("mesh");
  if (propIt == obj.props.end() || propIt->second.empty())
    return false;
  if (outMeshPath)
    *outMeshPath = propIt->second;
  return true;
}

/** @brief World model matrix including asset render-scale multiplier when present. */
Mat4 BuildObjectModelMatrix(const SceneDocument &doc, const SceneObject &obj) {
  Vec3 assetRenderScale = Vec3::One();
  std::string ignoredMeshPath;
  TryResolveObjectMeshPath(doc, obj, &ignoredMeshPath, &assetRenderScale);

  const Quaternion rotation = Quaternion::FromEuler(
      ToRadians(obj.pitch), ToRadians(obj.yaw), ToRadians(obj.roll));
  const Vec3 totalScale = MultiplyComponents(obj.scale, assetRenderScale);
  return Mat4::Translate(obj.position) * Mat4::Rotate(rotation) *
         Mat4::Scale(totalScale);
}

} // namespace

/** @copydoc EditorLayer::DrawWireframeOverlay */
void EditorLayer::DrawWireframeOverlay(const Camera &cam) {
  if (!Renderer::GetBackendCapabilities().supportsWireframeOverlay) {
    m_wireframeMode = false;
    return;
  }

  if (!m_wireframeMode || !m_wireframeShader.IsValid())
    return;

  const bool ownsFrame = !Renderer::IsFrameActive();
  if (ownsFrame) {
    RenderFrameConfig overlayFrame{{}, "editor-wireframe-overlay"};
    overlayFrame.clearColorBuffer = false;
    overlayFrame.clearDepthBuffer = false;
    Renderer::BeginFrame(overlayFrame);
  }
  Renderer::BeginPass(RenderPassConfig{RenderPassId::WireframeOverlay,
                                       BuildRenderView(cam),
                                       "editor-wireframe-overlay"});
  for (const auto &obj : m_document.objects) {
    std::string meshPath;
    if (!TryResolveObjectMeshPath(m_document, obj, &meshPath, nullptr))
      continue;

    const Mesh *mesh = TryGetAssetPreviewStaticMesh(meshPath);
    if (!mesh)
      continue;

    const Mat4 model = BuildObjectModelMatrix(m_document, obj);
    Renderer::SubmitWireframe(*mesh, model, m_wireframeShader, 0.3f, 0.85f,
                              0.3f);
  }
  Renderer::EndPass();
  if (ownsFrame)
    Renderer::EndFrame();
}

} // namespace Horo::Editor
