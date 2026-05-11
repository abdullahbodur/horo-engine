/** @file EditorAssetThumbnailPreview.cpp
 *  @brief Implements thumbnail preview rendering and texture-handle resolution for editor assets. */
#include "ui/editor/components/EditorAssetThumbnailPreview.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <numbers>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/Logger.h"
#include "core/ProjectPath.h"
#include "renderer/GltfLoader.h"
#include "renderer/IFramebuffer.h"
#include "renderer/ITexture.h"
#include "renderer/ObjLoader.h"
#include "renderer/Renderer.h"
#include "renderer/Shader.h"
#include "renderer/SkinnedMesh.h"
#include "renderer/Texture.h"
#if defined(HORO_HAS_VULKAN)
#include "renderer/VulkanRenderBackend.h"
#endif
#include "ui/editor/EditorLayerInternal.h"

namespace Horo::Editor {
namespace {

/** @brief Resolves a preview-shader file path by probing SDK and source-tree locations. */
std::filesystem::path ResolvePreviewShaderPath(const char* fileName) {
  namespace fs = std::filesystem;
  const fs::path root = ProjectPath::Root();
  const fs::path sdkRoot = ProjectPath::SdkRoot();
  const std::array<fs::path, 7> candidates = {
      sdkRoot / "renderer" / "shaders" / fileName,
      sdkRoot / "bin" / "shaders" / fileName,
      sdkRoot / "sdk" / "renderer" / "shaders" / fileName,
      root / "engine" / "renderer" / "shaders" / fileName,
      root.parent_path() / "horo-engine" / "renderer" / "shaders" / fileName,
      root / "horo-engine" / "renderer" / "shaders" / fileName,
      root / "renderer" / "shaders" / fileName,
  };

  for (const auto& candidate : candidates) {
    std::error_code ec;
    if (fs::is_regular_file(candidate, ec) && !ec)
      return candidate;
  }

  return candidates.front();
}

/** @brief Singleton renderer/cache used to generate and store asset thumbnail resources. */
struct AssetThumbnailRenderer {
  std::shared_ptr<IFramebuffer> fbo; /**< Off-screen framebuffer used for software thumbnail rendering paths. */
  int width = 512;                   /**< Thumbnail render width in pixels. */
  int height = 512;                  /**< Thumbnail render height in pixels. */
  Shader shader;                     /**< Shader used when drawing preview meshes into the thumbnail target. */

  /** @brief Cached mesh payload for one source mesh path. */
  struct CachedMesh {
    std::shared_ptr<Mesh> mesh;                /**< Static mesh asset when the source is an OBJ mesh. */
    std::shared_ptr<SkinnedMesh> skinnedMesh;  /**< Skinned mesh asset when the source is glTF/glb. */
    std::shared_ptr<Skeleton> skeleton;        /**< Skeleton associated with skinnedMesh, when present. */
    bool isSkinned = false;                    /**< True when this cache entry represents a skinned mesh path. */
  };

  std::unordered_map<std::string, CachedMesh, StringHash, std::equal_to<>>
      meshCache; /**< Loaded mesh cache keyed by absolute mesh path. */
  std::unordered_set<std::string, StringHash, std::equal_to<>> noPreviewKeys; /**< Mesh keys known to have no preview. */
  std::unordered_map<std::string, std::shared_ptr<ITexture>, StringHash,
                      std::equal_to<>>
      renderedTextureCache; /**< Rendered thumbnail textures keyed by absolute mesh path. */

  /** @brief Returns the global thumbnail renderer/cache instance. */
  static AssetThumbnailRenderer& Instance() {
    static AssetThumbnailRenderer instance;
    return instance;
  }

  /** @brief Lazily initializes framebuffer and shader resources for thumbnail rendering. */
  bool Init() {
    if (fbo != nullptr)
      return IsValid();

    FramebufferSpec spec;
    spec.width = static_cast<uint32_t>(width);
    spec.height = static_cast<uint32_t>(height);
    spec.attachmentSpec = {{{FramebufferTextureFormat::RGBA8},
                            {FramebufferTextureFormat::DEPTH24STENCIL8}}};
    fbo = Renderer::CreateFramebuffer(spec);

    if (!fbo) {
      LogError("AssetThumbnailRenderer: CreateFramebuffer returned nullptr.");
      return false;
    }

    try {
      const std::filesystem::path vertPath = ResolvePreviewShaderPath("basic.vert");
      const std::filesystem::path fragPath = ResolvePreviewShaderPath("basic.frag");
      shader = Shader::FromFiles(vertPath.generic_string(),
                                 fragPath.generic_string());
    } catch (const ShaderException& e) {
      LogError("Failed to load preview shader: {}", e.what());
      Cleanup();
      return false;
    }

    return IsValid();
  }

  /** @brief Returns true if thumbnail-render resources are ready for drawing. */
  bool IsValid() const { return fbo != nullptr && shader.IsValid(); }

  /** @brief Clears cached meshes/textures and releases GPU-side render resources. */
  void Cleanup() {
    meshCache.clear();
    noPreviewKeys.clear();
    renderedTextureCache.clear();
    fbo.reset();
  }

  /** @brief Ensures renderer resources are released when the singleton is destroyed. */
  ~AssetThumbnailRenderer() { Cleanup(); }
};

/** @brief Loads and caches a mesh for preview rendering, returning nullptr on unsupported/failed assets. */
AssetThumbnailRenderer::CachedMesh* TryLoadAssetMesh(std::string_view meshPath) {
  if (meshPath.empty())
    return nullptr;

  const std::filesystem::path path =
      ResolveProjectRelativeOrAbsolutePath(meshPath);
  const std::string ext = ToLowerAscii(path.extension().string());

  auto& renderer = AssetThumbnailRenderer::Instance();
  const std::string cacheKey = path.generic_string();

  if (renderer.noPreviewKeys.contains(cacheKey))
    return nullptr;

  auto it = renderer.meshCache.find(cacheKey);
  if (it != renderer.meshCache.end())
    return &it->second;

  AssetThumbnailRenderer::CachedMesh entry;

  try {
    if (ext == ".obj") {
      entry.mesh = std::make_shared<Mesh>(ObjLoader::Load(path.generic_string()));
      entry.isSkinned = false;
    } else if (ext == ".gltf" || ext == ".glb") {
      GltfLoadResult result = GltfLoader::Load(path.generic_string());
      if (result.mesh) {
        entry.skinnedMesh = result.mesh;
        entry.skeleton = result.skeleton;
        entry.isSkinned = true;
      } else {
        LogWarn("[Thumbnail] GLTF load failed (no mesh): {}", cacheKey);
        renderer.noPreviewKeys.insert(cacheKey);
        return nullptr;
      }
    } else {
      LogWarn("[Thumbnail] Unsupported mesh format: {}", ext);
      renderer.noPreviewKeys.insert(cacheKey);
      return nullptr;
    }
  } catch (const ObjLoader::ObjLoaderException& e) {
    LogWarn("[Thumbnail] Failed to load mesh for preview: {} (error: {})",
            cacheKey, e.what());
    renderer.noPreviewKeys.insert(cacheKey);
    return nullptr;
  }

  it = renderer.meshCache.try_emplace(cacheKey, std::move(entry)).first;
  return &it->second;
}

/** @brief Builds view/projection matrices that frame the supplied mesh for thumbnail rendering. */
void FitCameraToMesh(const AssetThumbnailRenderer::CachedMesh& mesh, Mat4& outView,
                     Mat4& outProj) {
  Vec3 aabbCenter;
  Vec3 aabbHalf;

  if (mesh.isSkinned && mesh.skinnedMesh) {
    aabbCenter = mesh.skinnedMesh->GetLocalAabbCenter();
    aabbHalf = mesh.skinnedMesh->GetHalfExtents();
  } else if (!mesh.isSkinned && mesh.mesh) {
    aabbCenter = mesh.mesh->GetLocalAabbCenter();
    aabbHalf = mesh.mesh->GetHalfExtents();
  } else {
    outView = Mat4::Identity();
    outProj = Mat4::Perspective(45.0f, 1.0f, 0.01f, 100.0f);
    return;
  }

  const float maxHalf = std::max({aabbHalf.x, aabbHalf.y, aabbHalf.z});
  const float fov = 45.0f * std::numbers::pi_v<float> / 180.0f;
  const float distance = maxHalf * 1.3f / std::tan(fov * 0.5f);

  const Vec3 camPos =
      aabbCenter + Vec3(distance * 0.9f, distance * 0.4f, distance * 0.9f);
  outView = Mat4::LookAt(camPos, aabbCenter, Vec3(0, 1, 0));
  outProj = Mat4::Perspective(45.0f, 1.0f, 0.01f, 1000.0f);
}

/** @brief Renders the cached mesh into an off-screen target and returns the resulting texture handle. */
RenderTargetHandle RenderMeshToThumbnail(
    const AssetThumbnailRenderer::CachedMesh& mesh, const std::string& meshKey) {
  if (const RenderBackendCapabilities caps = Renderer::GetBackendCapabilities();
      !caps.supportsOffscreenTargets || !caps.supportsNativeTextureHandles)
    return {};

#if defined(HORO_HAS_VULKAN)
  if (Renderer::GetBackendId() == RenderBackendId::Vulkan) {
    auto* backend =
        dynamic_cast<VulkanRenderBackend*>(Renderer::GetBackendForInterop());
    if (!backend)
      return {};

    auto& renderer = AssetThumbnailRenderer::Instance();
    if (!backend->EnsureOffscreenRenderTarget(
            meshKey, static_cast<uint32_t>(renderer.width),
            static_cast<uint32_t>(renderer.height))) {
      LogWarn("[Thumbnail] Vulkan offscreen target creation failed for {}: {}",
              meshKey, backend->GetLastError());
      return {};
    }

    RenderTargetHandle handle;
    if (!backend->TryGetOffscreenRenderTargetHandle(meshKey, &handle, false)) {
      LogWarn(
          "[Thumbnail] Vulkan offscreen handle registration failed for {}: {}",
          meshKey, backend->GetLastError());
      return {};
    }
    return handle;
  }
#endif

  auto& renderer = AssetThumbnailRenderer::Instance();
  if (const auto cacheIt = renderer.renderedTextureCache.find(meshKey);
      cacheIt != renderer.renderedTextureCache.end()) {
    const auto& tex = cacheIt->second;
    return tex ? tex->GetRenderTargetHandle(true) : RenderTargetHandle{};
  }

  if (!renderer.IsValid())
    return {};

  const auto prevViewport = Renderer::GetViewport();

  renderer.fbo->Bind();
  Renderer::SetViewport(0, 0, renderer.width, renderer.height);
  Renderer::ClearColorAndDepth(0.15f, 0.15f, 0.15f, 1.0f);
  Renderer::SetupOpaqueRenderState();

  Mat4 view;
  Mat4 proj;
  FitCameraToMesh(mesh, view, proj);

  renderer.shader.Bind();
  renderer.shader.SetMat4("u_model", Mat4::Identity());
  renderer.shader.SetMat4("u_view", view);
  renderer.shader.SetMat4("u_projection", proj);
  renderer.shader.SetVec3("u_cameraPos", view.Inverse().GetTranslation());
  renderer.shader.SetVec4("u_color", Vec4(0.8f, 0.8f, 0.8f, 1.0f));
  renderer.shader.SetInt("u_hasTexture", 0);
  renderer.shader.SetInt("u_lightCount", 2);

  renderer.shader.SetInt("u_lights[0].type", 0);
  renderer.shader.SetVec3("u_lights[0].direction",
                          Vec3(-1.0f, -1.5f, -0.5f).Normalized());
  renderer.shader.SetVec3("u_lights[0].color", Vec3(1.2f, 1.2f, 1.1f));

  renderer.shader.SetInt("u_lights[1].type", 0);
  renderer.shader.SetVec3("u_lights[1].direction",
                          Vec3(1.0f, 0.5f, 1.0f).Normalized());
  renderer.shader.SetVec3("u_lights[1].color", Vec3(0.3f, 0.35f, 0.5f));

  if (mesh.isSkinned && mesh.skinnedMesh) {
    mesh.skinnedMesh->Draw();
  } else if (!mesh.isSkinned && mesh.mesh) {
    mesh.mesh->Draw();
  }

  std::vector<uint32_t> pixels(
      static_cast<size_t>(renderer.width * renderer.height));
  if (std::string readError; !Renderer::ReadbackRegionRgba8(
          0, 0, renderer.width, renderer.height, pixels.data(), &readError)) {
    LogWarn("AssetThumbnailRenderer: readback failed: {}", readError);
    renderer.fbo->Unbind();
    Renderer::SetViewport(prevViewport[0], prevViewport[1], prevViewport[2],
                          prevViewport[3]);
    return {};
  }

  TextureSpec texSpec;
  texSpec.width = static_cast<uint32_t>(renderer.width);
  texSpec.height = static_cast<uint32_t>(renderer.height);
  texSpec.format = TextureFormat::RGBA8;
  texSpec.filter = TextureFilter::Linear;
  texSpec.wrap = TextureWrap::ClampToEdge;
  texSpec.generateMips = false;
  auto destTex = Renderer::CreateTexture(texSpec);
  if (!destTex) {
    LogWarn("AssetThumbnailRenderer: texture creation failed");
    renderer.fbo->Unbind();
    Renderer::SetViewport(prevViewport[0], prevViewport[1], prevViewport[2],
                          prevViewport[3]);
    return {};
  }
  destTex->SetData(pixels.data(),
                   static_cast<uint32_t>(pixels.size() * sizeof(uint32_t)));
  renderer.renderedTextureCache[meshKey] = destTex;

  renderer.fbo->Unbind();
  Renderer::SetViewport(prevViewport[0], prevViewport[1], prevViewport[2],
                        prevViewport[3]);

  return destTex->GetRenderTargetHandle(true);
}

/** @brief Tries to render a mesh-derived thumbnail for the given asset definition. */
RenderTargetHandle TryRenderAssetMeshPreview(const AssetDef& asset) {
  if (asset.mesh.empty())
    return {};

  auto& thumbnailRenderer = AssetThumbnailRenderer::Instance();
#if defined(HORO_HAS_VULKAN)
  if (const bool useVulkanOffscreen =
          Renderer::GetBackendId() == RenderBackendId::Vulkan;
      !useVulkanOffscreen && !thumbnailRenderer.Init())
    return {};
#else
  if (auto& renderer = thumbnailRenderer; !renderer.Init())
    return {};
#endif

  const auto* meshEntry = TryLoadAssetMesh(asset.mesh);
  if (!meshEntry)
    return {};

  const std::string meshKey =
      ResolveProjectRelativeOrAbsolutePath(asset.mesh).generic_string();
  return RenderMeshToThumbnail(*meshEntry, meshKey);
}

/** @brief Retrieves (or loads and caches) a glTF albedo texture handle for preview fallback rendering. */
RenderTargetHandle TryGetCachedGltfAlbedoPreview(
    const std::filesystem::path& meshPath,
    std::unordered_map<std::string, std::shared_ptr<Texture>, StringHash,
                       std::equal_to<>>* cache) {
  if (!cache)
    return {};

  const std::string absMesh = meshPath.generic_string();
  auto it = cache->find(absMesh);
  if (it == cache->end()) {
    GltfLoadResult result = GltfLoader::Load(absMesh);
    if (!result.albedoTexture || !result.albedoTexture->IsValid())
      return {};
    it = cache->try_emplace(absMesh, std::move(result.albedoTexture)).first;
  }

  if (!it->second || !it->second->IsValid())
    return {};
  return it->second->GetRenderTargetHandle();
}

} // namespace

namespace {
/** @brief Loads a texture from disk (with caching) and returns its native render-target handle. */
RenderTargetHandle LoadTextureHandleByPath(
    const std::filesystem::path& path,
    std::unordered_map<std::string, Texture, StringHash, std::equal_to<>>* cache) {
    const RenderBackendCapabilities caps = Renderer::GetBackendCapabilities();
    if (!caps.supportsNativeTextureHandles)
        return {};
    if (path.empty())
        return {};
    if (std::error_code ec; !std::filesystem::is_regular_file(path, ec) || ec)
        return {};
    const std::string abs = path.generic_string();
    auto it = cache->find(abs);
    if (it == cache->end()) {
        Texture tex = Texture::FromFile(abs, false);
        it = cache->try_emplace(abs, std::move(tex)).first;
    }
    if (!it->second.IsValid())
        return {};
    return it->second.GetRenderTargetHandle();
}

/** @brief Attempts to resolve a preview handle for an .obj mesh via its associated diffuse texture. */
RenderTargetHandle TryResolveObjMeshPreview(
    const std::filesystem::path& meshPath,
    std::unordered_map<std::string, Texture, StringHash, std::equal_to<>>* cache) {
    const std::string diffusePath =
        ObjLoader::FindDiffuseTexture(meshPath.generic_string());
    return LoadTextureHandleByPath(
        ResolveProjectRelativeOrAbsolutePath(diffusePath), cache);
}

/** @brief Attempts to resolve a preview handle for a glTF/glb mesh via its cached albedo texture. */
RenderTargetHandle TryResolveGltfMeshPreview(
    const std::filesystem::path& meshPath,
    std::unordered_map<std::string, std::shared_ptr<Texture>, StringHash, std::equal_to<>>* cache) {
    return TryGetCachedGltfAlbedoPreview(meshPath, cache);
}
}  // namespace

/** @copydoc TryGetAssetPreviewHandle */
bool TryGetAssetPreviewHandle(std::string_view assetId, const AssetDef& asset,
                              RenderTargetHandle* outHandle) {
  if (!outHandle)
    return false;
  *outHandle = {};

  const RenderBackendCapabilities caps = Renderer::GetBackendCapabilities();
  if (!caps.supportsNativeTextureHandles)
    return false;

  static std::unordered_map<std::string, Texture, StringHash, std::equal_to<>>
      s_textureByPath;
  static std::unordered_map<std::string, std::shared_ptr<Texture>, StringHash,
                            std::equal_to<>>
      s_gltfTextureByMesh;
  static std::unordered_set<std::string, StringHash, std::equal_to<>>
      s_noPreviewCache;

  const std::string key =
      std::format("{}|{}|{}", assetId, asset.mesh, asset.albedoMap);
  if (s_noPreviewCache.contains(key))
    return false;

  // 1. Prefer an offscreen-rendered mesh preview when supported.
  if (caps.supportsOffscreenTargets && !asset.mesh.empty()) {
    if (const RenderTargetHandle handle = TryRenderAssetMeshPreview(asset);
        handle.IsValid()) {
      *outHandle = handle;
      return true;
    }
  }

  // 2. Fall back to the explicit albedo map when provided.
  if (!asset.albedoMap.empty()) {
    const std::filesystem::path albedo =
        ResolveProjectRelativeOrAbsolutePath(asset.albedoMap);
    if (const RenderTargetHandle handle =
            LoadTextureHandleByPath(albedo, &s_textureByPath);
        handle.IsValid()) {
      *outHandle = handle;
      return true;
    }
  }

  // 3. Fall back to a diffuse/albedo texture extracted from the mesh file.
  const std::filesystem::path meshPath =
      ResolveProjectRelativeOrAbsolutePath(asset.mesh);
  if (!meshPath.empty()) {
    const std::string ext = ToLowerAscii(meshPath.extension().string());
    RenderTargetHandle handle;
    if (ext == ".obj")
      handle = TryResolveObjMeshPreview(meshPath, &s_textureByPath);
    else if (ext == ".gltf" || ext == ".glb")
      handle = TryResolveGltfMeshPreview(meshPath, &s_gltfTextureByMesh);
    if (handle.IsValid()) {
      *outHandle = handle;
      return true;
    }
  }

  s_noPreviewCache.insert(key);
  return false;
}

/** @copydoc ToImTextureId */
ImTextureID ToImTextureId(const RenderTargetHandle& handle) {
  using enum RenderNativeHandleType;
  if (!handle.IsValid())
    return (ImTextureID)0;

  switch (handle.nativeType) {
  case OpenGLTexture2D:
  case VulkanImGuiDescriptorSet:
    return (ImTextureID)(intptr_t)handle.nativeHandle;
  case None:
  default:
    return (ImTextureID)0;
  }
}

/** @copydoc TryGetAssetPreviewStaticMesh */
const Mesh* TryGetAssetPreviewStaticMesh(std::string_view meshPath) {
  const auto* meshEntry = TryLoadAssetMesh(meshPath);
  if (!meshEntry || !meshEntry->mesh)
    return nullptr;
  return meshEntry->mesh.get();
}

/** @copydoc ClearAssetThumbnailMeshCaches */
void ClearAssetThumbnailMeshCaches() {
  auto& renderer = AssetThumbnailRenderer::Instance();
  renderer.noPreviewKeys.clear();
  renderer.meshCache.clear();
}

} // namespace Horo::Editor
