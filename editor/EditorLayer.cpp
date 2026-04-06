#include "editor/EditorLayer.h"
#include "editor/TransformGizmo.h"

// Windows headers must come before GLFW to avoid type redefinition conflicts
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#endif

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <ctime>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "core/LogBuffer.h"
#include "core/Logger.h"
#include "core/ProjectPath.h"
#include "editor/ProjectEntryFilter.h"
#include "editor/EditorDebugTrace.h"
#include "editor/EditorAssetImport.h"
#include "editor/EditorSearch.h"
#include "editor/EditorUiLogic.h"
#include "editor/Raycaster.h"
#include "math/Mat4.h"
#include "math/MathUtils.h"
#include "math/Quaternion.h"
#include "math/Transform.h"
#include "editor/SceneSerializer.h"
#include "renderer/DebugDraw.h"
#include "renderer/GltfLoader.h"
#include "renderer/Mesh.h"
#include "renderer/ObjLoader.h"
#include "renderer/RenderContext.h"
#include "renderer/Renderer.h"
#include "renderer/Shader.h"
#include "renderer/SkinnedMesh.h"
#include "renderer/Texture.h"
#include "scene/Entity.h"
#include "scene/Registry.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/PlayerTagComponent.h"
#include "scene/components/TransformComponent.h"

namespace Monolith {
namespace Editor {

namespace {

// Must match DrawToolbar / DrawStatusBar so left-column windows do not overlap each other
// or the status strip.
constexpr float kEditorToolbarH = 36.0f;
constexpr float kEditorStatusH = 24.0f;
constexpr float kBottomDockH = 200.0f;
constexpr float kLeftDockW = 308.0f;
// Avoid re-reading directories every ImGui frame (Windows "not responding" on large trees).
constexpr uint32_t kProjectListingCacheFrames = 48;

// Split Objects (top) vs Assets (bottom) within [toolbar .. status]. Small gap so they
// never z-fight at the seam.
static void ComputeLeftColumnLayout(const ImGuiIO& io,
                                    float* outObjectsTop,
                                    float* outObjectsH,
                                    float* outAssetsTop,
                                    float* outAssetsH) {
  const float workTop = kEditorToolbarH;
  const float workBottom = io.DisplaySize.y - kEditorStatusH - kBottomDockH;
  const float workH = std::max(0.0f, workBottom - workTop);
  constexpr float kMidGap = 4.0f;
  const float mid = workTop + workH * 0.52f;
  *outObjectsTop = workTop;
  *outObjectsH = std::max(48.0f, mid - workTop - kMidGap * 0.5f);
  *outAssetsTop = mid + kMidGap * 0.5f;
  *outAssetsH = std::max(64.0f, workBottom - *outAssetsTop);
}

// World-space selection / picking bounds for a prop when ECS has a valid _eid.
bool TryPropWorldAabb(Registry& reg, const SceneObject& obj, Vec3& outCenter, Vec3& outHalf) {
  if (obj.type != SceneObjectType::Prop)
    return false;
  auto it = obj.props.find("_eid");
  if (it == obj.props.end())
    return false;
  Entity e = static_cast<Entity>(std::stoul(it->second));
  if (!reg.IsAlive(e) || !reg.Has<MeshComponent>(e) || !reg.Has<TransformComponent>(e))
    return false;
  const auto& mc = reg.Get<MeshComponent>(e);
  const auto& tc = reg.Get<TransformComponent>(e);
  if (!mc.mesh)
    return false;
  Transform wt(tc.current.position, tc.current.rotation, tc.current.scale);
  WorldAabbFromLocalBox(mc.mesh->GetLocalAabbCenter(), mc.mesh->GetHalfExtents(), wt, outCenter,
                        outHalf);
  return true;
}

static const std::string kEmptyParentId;

static const std::string& GetParentId(const SceneObject& obj) {
  const auto it = obj.props.find("parentId");
  return (it != obj.props.end()) ? it->second : kEmptyParentId;
}

static int FindObjectIndexById(const SceneDocument& doc, const std::string& id) {
  if (id.empty())
    return -1;
  for (int i = 0; i < static_cast<int>(doc.objects.size()); ++i) {
    if (doc.objects[static_cast<size_t>(i)].id == id)
      return i;
  }
  return -1;
}

static bool IsDescendantOf(const SceneDocument& doc, int nodeIdx, int ancestorIdx) {
  if (nodeIdx < 0 || ancestorIdx < 0 || nodeIdx >= static_cast<int>(doc.objects.size()) ||
      ancestorIdx >= static_cast<int>(doc.objects.size()))
    return false;

  int cur = nodeIdx;
  for (int guard = 0; guard < static_cast<int>(doc.objects.size()); ++guard) {
    const int p = FindObjectIndexById(doc, GetParentId(doc.objects[static_cast<size_t>(cur)]));
    if (p < 0)
      return false;
    if (p == ancestorIdx)
      return true;
    cur = p;
  }
  return false;
}

static void PropagateHierarchyTransformDelta(SceneDocument& doc,
                                             int parentIdx,
                                             const Vec3& oldParentPos,
                                             const Quaternion& oldParentRot,
                                             const Vec3& newParentPos,
                                             const Quaternion& newParentRot,
                                             const std::function<void(const SceneObject&)>& transformCb,
                                             const std::vector<int>& skipIndices = {}) {
  if (parentIdx < 0 || parentIdx >= static_cast<int>(doc.objects.size()))
    return;

  const Quaternion deltaRot = newParentRot * oldParentRot.Inverse();
  for (int i = 0; i < static_cast<int>(doc.objects.size()); ++i) {
    if (i == parentIdx || !IsDescendantOf(doc, i, parentIdx))
      continue;
    // Skip children that are themselves being directly manipulated (e.g. multi-select gizmo).
    bool shouldSkip = false;
    for (int sk : skipIndices) {
      if (sk == i) {
        shouldSkip = true;
        break;
      }
    }
    if (shouldSkip)
      continue;

    SceneObject& child = doc.objects[static_cast<size_t>(i)];
    const Vec3 oldRel = child.position - oldParentPos;
    child.position = newParentPos + deltaRot * oldRel;

    const Quaternion childRot =
        Quaternion::FromEuler(ToRadians(child.pitch), ToRadians(child.yaw), ToRadians(child.roll));
    const Quaternion rotatedChild = deltaRot * childRot;
    const Vec3 e = rotatedChild.ToEuler();
    child.pitch = ToDegrees(e.x);
    child.yaw = ToDegrees(e.y);
    child.roll = ToDegrees(e.z);

    if (transformCb)
      transformCb(child);
  }
}

// Screen direction of a world-space axis for the orientation corner gizmo.
// Uses the view matrix directly — pivot-based perspective projection would introduce
// distortion for off-centre or off-screen pivots and is incorrect for a corner widget.
static void WorldAxisToScreenDir(const Camera& cam,
                                 const Vec3& worldUnit,
                                 float* outDx,
                                 float* outDy,
                                 float* outViewZ = nullptr) {
  const Mat4 view = cam.GetView();
  const Vec3 e = view.TransformVector(worldUnit);
  if (outViewZ)
    *outViewZ = e.z;
  const float dx = e.x;
  const float dy = -e.y;  // ImGui Y is down
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len < 1e-4f) {
    *outDx = 1.f;
    *outDy = 0.f;
    return;
  }
  *outDx = dx / len;
  *outDy = dy / len;
}

static float DistSqPointSegment2D(float px, float py, float ax, float ay, float bx, float by) {
  const float abx = bx - ax, aby = by - ay;
  const float apx = px - ax, apy = py - ay;
  const float abLen2 = abx * abx + aby * aby;
  if (abLen2 < 1e-8f)
    return apx * apx + apy * apy;
  float t = (apx * abx + apy * aby) / abLen2;
  t = std::clamp(t, 0.f, 1.f);
  const float cx = ax + t * abx, cy = ay + t * aby;
  const float dx = px - cx, dy = py - cy;
  return dx * dx + dy * dy;
}

#if defined(__APPLE__)
static std::string ReadPathFromOsascript(const char* cmd) {
  FILE* pipe = popen(cmd, "r");
  if (!pipe)
    return {};
  char buf[1024] = {};
  std::string out;
  while (std::fgets(buf, sizeof(buf), pipe) != nullptr)
    out += buf;
  pclose(pipe);
  out.erase(std::remove_if(out.begin(), out.end(), [](char c) {
              return c == '\n' || c == '\r';
            }),
            out.end());
  return out;
}
#endif

std::string PickObjFilePath() {
#ifdef _WIN32
  char filePath[MAX_PATH] = {};
  OPENFILENAMEA ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFilter = "OBJ Files\0*.obj\0All Files\0*.*\0";
  ofn.lpstrFile = filePath;
  ofn.nMaxFile = sizeof(filePath);
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (GetOpenFileNameA(&ofn))
    return filePath;
  return {};
#elif defined(__APPLE__)
  // Avoid `of type {"obj"}` — it often fails on modern macOS; we validate extension in code.
  return ReadPathFromOsascript(
      "/usr/bin/osascript -e 'try' "
      "-e 'POSIX path of (choose file with prompt \"Select OBJ file\")' "
      "-e 'on error' -e 'return \"\"' -e 'end try' 2>/dev/null");
#else
  return {};
#endif
}

static bool IsTextureFilePath(const std::string& path) {
  if (path.empty())
    return false;
  namespace fs = std::filesystem;
  std::string ext = fs::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" ||
         ext == ".webp" || ext == ".hdr";
}

static std::string ToLowerAscii(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

static std::filesystem::path ResolveProjectRelativeOrAbsolutePath(const std::string& rawPath) {
  if (rawPath.empty())
    return {};
  namespace fs = std::filesystem;
  fs::path p(rawPath);
  if (p.is_absolute())
    return p;
  return ProjectPath::Root() / p;
}

static std::filesystem::path ResolvePreviewShaderPath(const char* fileName) {
  namespace fs = std::filesystem;
  const fs::path root = ProjectPath::Root();
  const std::array<fs::path, 3> candidates = {
      root / "engine" / "renderer" / "shaders" / fileName,
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

const char* SceneObjectTypeToString(SceneObjectType type) {
  switch (type) {
    case SceneObjectType::Panel:
      return "Panel";
    case SceneObjectType::Prop:
      return "Prop";
    case SceneObjectType::Light:
      return "Light";
    case SceneObjectType::Camera:
      return "Camera";
  }
  return "Panel";
}

bool ParseSceneObjectType(const std::string& raw, SceneObjectType* outType) {
  if (!outType)
    return false;
  std::string value = raw;
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  if (value == "panel") {
    *outType = SceneObjectType::Panel;
    return true;
  }
  if (value == "prop") {
    *outType = SceneObjectType::Prop;
    return true;
  }
  if (value == "light") {
    *outType = SceneObjectType::Light;
    return true;
  }
  if (value == "camera") {
    *outType = SceneObjectType::Camera;
    return true;
  }
  return false;
}

// ===== 3D Asset Thumbnail Rendering Infrastructure =====
// Framebuffer object for offscreen mesh rendering (shared across all asset previews).
struct AssetThumbnailRenderer {
  unsigned int fbo = 0;
  unsigned int colorTexture = 0;
  unsigned int depthRenderBuffer = 0;
  int width = 512;
  int height = 512;
  Shader shader;  // Basic lighting shader for thumbnail

  // Cache: assetKey -> currently rendered mesh/texture ID
  struct CachedMesh {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<SkinnedMesh> skinnedMesh;
    std::shared_ptr<Skeleton> skeleton;
    bool isSkinned = false;
  };
  std::unordered_map<std::string, CachedMesh> meshCache;
  std::unordered_set<std::string> noPreviewKeys;
  // Per-mesh rendered texture cache: each mesh gets its own texture so all
  // thumbnails don't overwrite each other by sharing a single FBO attachment.
  std::unordered_map<std::string, unsigned int> renderedTextureCache;

  static AssetThumbnailRenderer& Instance() {
    static AssetThumbnailRenderer instance;
    return instance;
  }

  bool Init() {
    if (fbo != 0)
      return IsValid();

    // Create FBO
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    // Create color texture
    glGenTextures(1, &colorTexture);
    glBindTexture(GL_TEXTURE_2D, colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);

    // Create depth renderbuffer
    glGenRenderbuffers(1, &depthRenderBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRenderBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRenderBuffer);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
      LOG_ERROR("AssetThumbnailRenderer: FBO incomplete, status=%d", (int)status);
      Cleanup();
      return false;
    }

    // Load simple lighting shader
    try {
      const std::filesystem::path vertPath = ResolvePreviewShaderPath("basic.vert");
      const std::filesystem::path fragPath = ResolvePreviewShaderPath("basic.frag");
      shader = Shader::FromFiles(vertPath.generic_string(), fragPath.generic_string());
    } catch (const std::exception& e) {
      LOG_ERROR("Failed to load preview shader: %s", e.what());
      Cleanup();
      return false;
    }

    return IsValid();
  }

  bool IsValid() const {
    return fbo != 0 && colorTexture != 0 && depthRenderBuffer != 0 && shader.IsValid();
  }

  void Cleanup() {
    meshCache.clear();
    noPreviewKeys.clear();
    for (auto& [key, texId] : renderedTextureCache) {
      if (texId != 0)
        glDeleteTextures(1, &texId);
    }
    renderedTextureCache.clear();
    if (colorTexture != 0) {
      glDeleteTextures(1, &colorTexture);
      colorTexture = 0;
    }
    if (depthRenderBuffer != 0) {
      glDeleteRenderbuffers(1, &depthRenderBuffer);
      depthRenderBuffer = 0;
    }
    if (fbo != 0) {
      glDeleteFramebuffers(1, &fbo);
      fbo = 0;
    }
  }

  ~AssetThumbnailRenderer() { Cleanup(); }
};

// Try to load a static mesh (OBJ) or skinned mesh (GLTF/GLB) from the given path.
// Returns cache entry or nullptr on failure.
static AssetThumbnailRenderer::CachedMesh* TryLoadAssetMesh(const std::string& meshPath) {
  if (meshPath.empty())
    return nullptr;

  const std::filesystem::path path = ResolveProjectRelativeOrAbsolutePath(meshPath);
  const std::string ext = ToLowerAscii(path.extension().string());

  auto& renderer = AssetThumbnailRenderer::Instance();
  const std::string cacheKey = path.generic_string();

  // Check no-preview cache
  if (renderer.noPreviewKeys.find(cacheKey) != renderer.noPreviewKeys.end()) {
    return nullptr;
  }

  // Check mesh cache
  auto it = renderer.meshCache.find(cacheKey);
  if (it != renderer.meshCache.end()) {
    return &it->second;
  }

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
        LOG_WARN("[Thumbnail] GLTF load failed (no mesh): %s", cacheKey.c_str());
        renderer.noPreviewKeys.insert(cacheKey);
        return nullptr;
      }
    } else {
      LOG_WARN("[Thumbnail] Unsupported mesh format: %s", ext.c_str());
      renderer.noPreviewKeys.insert(cacheKey);
      return nullptr;
    }
  } catch (const std::exception& e) {
    LOG_WARN("[Thumbnail] Failed to load mesh for preview: %s (error: %s)", cacheKey.c_str(), e.what());
    renderer.noPreviewKeys.insert(cacheKey);
    return nullptr;
  }

  it = renderer.meshCache.emplace(cacheKey, std::move(entry)).first;
  return &it->second;
}

// Fit camera to mesh AABB with a comfortable margin. Returns view and projection matrices.
static void FitCameraToMesh(const AssetThumbnailRenderer::CachedMesh& mesh,
                            Mat4& outView, Mat4& outProj) {
  Vec3 aabbCenter;
  Vec3 aabbHalf;

  if (mesh.isSkinned && mesh.skinnedMesh) {
    aabbCenter = mesh.skinnedMesh->GetLocalAabbCenter();
    aabbHalf = mesh.skinnedMesh->GetHalfExtents();
  } else if (!mesh.isSkinned && mesh.mesh) {
    aabbCenter = mesh.mesh->GetLocalAabbCenter();
    aabbHalf = mesh.mesh->GetHalfExtents();
  } else {
    // Fallback: identity view/proj
    outView = Mat4::Identity();
    outProj = Mat4::Perspective(45.0f, 1.0f, 0.01f, 100.0f);
    return;
  }

  // Compute distance to fit AABB on screen (with 1.3x margin for breathing room)
  const float maxHalf = std::max({aabbHalf.x, aabbHalf.y, aabbHalf.z});
  const float fov = 45.0f * 3.14159f / 180.f;
  const float distance = maxHalf * 1.3f / std::tan(fov * 0.5f);

  // Position camera at ~22° elevation for a clear side-on view of the object
  const Vec3 camPos = aabbCenter + Vec3(distance * 0.9f, distance * 0.4f, distance * 0.9f);
  outView = Mat4::LookAt(camPos, aabbCenter, Vec3(0, 1, 0));
  outProj = Mat4::Perspective(45.0f, 1.0f, 0.01f, 1000.0f);
}

// Render a mesh to the thumbnail framebuffer and return a per-mesh cached texture ID.
// meshKey must be the resolved mesh path (same key used in meshCache).
static unsigned int RenderMeshToThumbnail(const AssetThumbnailRenderer::CachedMesh& mesh,
                                          const std::string& meshKey) {
  auto& renderer = AssetThumbnailRenderer::Instance();

  // Return existing per-mesh texture if already rendered this session.
  auto cacheIt = renderer.renderedTextureCache.find(meshKey);
  if (cacheIt != renderer.renderedTextureCache.end())
    return cacheIt->second;

  if (!renderer.IsValid())
    return 0;

  // Save GL state (viewport)
  GLint prevViewport[4] = {0, 0, 1, 1};
  glGetIntegerv(GL_VIEWPORT, prevViewport);

  // Bind FBO and set viewport
  glBindFramebuffer(GL_FRAMEBUFFER, renderer.fbo);
  glViewport(0, 0, renderer.width, renderer.height);

  // Clear and setup rendering
  glClearColor(0.15f, 0.15f, 0.15f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);

  // Setup camera matrices
  Mat4 view, proj;
  FitCameraToMesh(mesh, view, proj);

  // Render mesh with basic shader
  renderer.shader.Bind();
  renderer.shader.SetMat4("u_model", Mat4::Identity());
  renderer.shader.SetMat4("u_view", view);
  renderer.shader.SetMat4("u_projection", proj);
  renderer.shader.SetVec3("u_cameraPos", view.Inverse().GetTranslation());
  renderer.shader.SetVec4("u_color", Vec4(0.8f, 0.8f, 0.8f, 1.0f));
  renderer.shader.SetInt("u_hasTexture", 0);
  renderer.shader.SetInt("u_lightCount", 2);

  // Key light: upper-left, warm white
  renderer.shader.SetInt("u_lights[0].type", 0);
  renderer.shader.SetVec3("u_lights[0].direction", Vec3(-1.0f, -1.5f, -0.5f).Normalized());
  renderer.shader.SetVec3("u_lights[0].color", Vec3(1.2f, 1.2f, 1.1f));

  // Fill light: lower-right, cool blue at lower intensity for depth contrast
  renderer.shader.SetInt("u_lights[1].type", 0);
  renderer.shader.SetVec3("u_lights[1].direction", Vec3(1.0f, 0.5f, 1.0f).Normalized());
  renderer.shader.SetVec3("u_lights[1].color", Vec3(0.3f, 0.35f, 0.5f));

  // Draw mesh
  if (mesh.isSkinned && mesh.skinnedMesh) {
    mesh.skinnedMesh->Draw();
  } else if (!mesh.isSkinned && mesh.mesh) {
    mesh.mesh->Draw();
  }

  // Copy FBO contents into a new per-mesh texture so each asset keeps its own image.
  unsigned int destTex = 0;
  glGenTextures(1, &destTex);
  glBindTexture(GL_TEXTURE_2D, destTex);
  // glReadPixels only runs once per mesh (then cached); CPU cost is acceptable.
  std::vector<unsigned char> pixels(static_cast<size_t>(renderer.width * renderer.height * 3));
  glReadPixels(0, 0, renderer.width, renderer.height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, renderer.width, renderer.height, 0, GL_RGB, GL_UNSIGNED_BYTE,
               pixels.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  renderer.renderedTextureCache[meshKey] = destTex;

  // Restore GL state
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

  return destTex;
}

static bool TryGetAssetPreviewTextureId(const std::string& assetId,
                                        const AssetDef& asset,
                                        unsigned int* outTextureId,
                                        bool* outNeedsYFlip = nullptr) {
  if (!outTextureId)
    return false;
  *outTextureId = 0;
  if (outNeedsYFlip)
    *outNeedsYFlip = false;

  static std::unordered_map<std::string, Texture> s_textureByPath;
  static std::unordered_map<std::string, std::shared_ptr<Texture>> s_gltfTextureByMesh;
  static std::unordered_set<std::string> s_noPreviewCache;

  const std::string key = assetId + "|" + asset.mesh + "|" + asset.albedoMap;
  if (s_noPreviewCache.find(key) != s_noPreviewCache.end())
    return false;

  auto loadTextureByPath = [&](const std::filesystem::path& path) -> unsigned int {
    if (path.empty())
      return 0;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec)
      return 0;
    const std::string abs = path.generic_string();
    auto it = s_textureByPath.find(abs);
    if (it == s_textureByPath.end()) {
      Texture tex = Texture::FromFile(abs, false);
      it = s_textureByPath.emplace(abs, std::move(tex)).first;
    }
    if (!it->second.IsValid())
      return 0;
    return it->second.GetNativeId();
  };

  // Priority 1: Try to render the 3D mesh as a thumbnail (offscreen FBO render)
  if (!asset.mesh.empty()) {
    auto& thumbnailRenderer = AssetThumbnailRenderer::Instance();
    if (thumbnailRenderer.Init()) {
      if (auto* meshEntry = TryLoadAssetMesh(asset.mesh)) {
        const std::string meshKey =
            ResolveProjectRelativeOrAbsolutePath(asset.mesh).generic_string();
        const unsigned int texId = RenderMeshToThumbnail(*meshEntry, meshKey);
        if (texId != 0) {
          *outTextureId = texId;
          // OpenGL FBO textures have origin at bottom-left; ImGui expects top-left.
          // Caller must flip Y UVs when displaying this texture.
          if (outNeedsYFlip)
            *outNeedsYFlip = true;
          return true;
        }
      }
    }
  }

  // Priority 2: Fallback to texture-based thumbnails
  if (!asset.albedoMap.empty()) {
    const std::filesystem::path albedo = ResolveProjectRelativeOrAbsolutePath(asset.albedoMap);
    const unsigned int texId = loadTextureByPath(albedo);
    if (texId != 0) {
      *outTextureId = texId;
      return true;
    }
  }

  const std::filesystem::path meshPath = ResolveProjectRelativeOrAbsolutePath(asset.mesh);
  if (!meshPath.empty()) {
    const std::string ext = ToLowerAscii(meshPath.extension().string());

    if (ext == ".obj") {
      const std::string diffusePath = ObjLoader::FindDiffuseTexture(meshPath.generic_string());
      const unsigned int texId = loadTextureByPath(ResolveProjectRelativeOrAbsolutePath(diffusePath));
      if (texId != 0) {
        *outTextureId = texId;
        return true;
      }
    }

    if (ext == ".gltf" || ext == ".glb") {
      const std::string absMesh = meshPath.generic_string();
      auto it = s_gltfTextureByMesh.find(absMesh);
      if (it == s_gltfTextureByMesh.end()) {
        GltfLoadResult result = GltfLoader::Load(absMesh);
        if (result.albedoTexture && result.albedoTexture->IsValid())
          it = s_gltfTextureByMesh.emplace(absMesh, std::move(result.albedoTexture)).first;
      }
      if (it != s_gltfTextureByMesh.end() && it->second && it->second->IsValid()) {
        *outTextureId = it->second->GetNativeId();
        return true;
      }
    }
  }

  s_noPreviewCache.insert(key);
  return false;
}

static ImTextureID ToImTextureId(unsigned int textureId) {
  return (ImTextureID)(intptr_t)textureId;
}

std::string PickTextureFilePath() {
#ifdef _WIN32
  char filePath[MAX_PATH] = {};
  OPENFILENAMEA ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFilter = "Images\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.webp\0"
                    "PNG\0*.png\0"
                    "JPEG\0*.jpg;*.jpeg\0"
                    "All Files\0*.*\0";
  ofn.lpstrFile = filePath;
  ofn.nMaxFile = sizeof(filePath);
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (GetOpenFileNameA(&ofn))
    return filePath;
  return {};
#elif defined(__APPLE__)
  return ReadPathFromOsascript(
      "/usr/bin/osascript -e 'try' "
      "-e 'POSIX path of (choose file with prompt \"Select texture image\")' "
      "-e 'on error' -e 'return \"\"' -e 'end try' 2>/dev/null");
#else
  return {};
#endif
}

// Copy a picked texture into assets/models (same convention as OBJ import) and return
// a project-relative path for the scene JSON / LevelLoader.
// subfolderHint: asset name (e.g. "enemy") → copies into assets/models/enemy/.
// Empty hint falls back to flat assets/models/.
static std::string ImportTextureToAssetsModels(const std::string& pickedPath, std::string* outError,
                                               const std::string& subfolderHint = {}) {
  namespace fs = std::filesystem;
  if (pickedPath.empty())
    return {};
  const fs::path src(pickedPath);
  std::error_code ec;
  if (!fs::is_regular_file(src, ec) || ec) {
    if (outError)
      *outError = "Texture path is not a file.";
    return {};
  }
  if (!IsTextureFilePath(pickedPath)) {
    if (outError)
      *outError = "Unsupported image type (use png, jpg, bmp, tga, webp, …).";
    return {};
  }

  const fs::path destDir = subfolderHint.empty()
                               ? ProjectPath::Root() / "assets/models"
                               : ProjectPath::Root() / "assets/models" / subfolderHint;
  fs::create_directories(destDir, ec);
  if (ec) {
    if (outError)
      *outError = "Cannot create " + destDir.string() + ": " + ec.message();
    return {};
  }

  const fs::path dest = destDir / src.filename();
  fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    if (outError)
      *outError = "Copy failed: " + ec.message();
    return {};
  }
  return fs::relative(dest, ProjectPath::Root()).generic_string();
}

// Copy the .mtl referenced by objSrcPath and all textures it references
// into destDirStr. Silently skips any file that cannot be copied.
static void CopyCompanionAssets(const std::string& objSrcPath, const std::string& destDirStr) {
  namespace fs = std::filesystem;
  const fs::path srcDir(fs::path(objSrcPath).parent_path());
  const fs::path destDir(destDirStr);

  std::ifstream objFile(objSrcPath);
  if (!objFile.is_open())
    return;

  std::string mtlName;
  std::string line;
  while (std::getline(objFile, line)) {
    if (line.rfind("mtllib ", 0) == 0) {
      mtlName = line.substr(7);
      while (!mtlName.empty() && (mtlName.back() == '\r' || mtlName.back() == ' '))
        mtlName.pop_back();
      break;
    }
  }
  if (mtlName.empty())
    return;

  const fs::path mtlSrc = srcDir / mtlName;
  if (!fs::exists(mtlSrc))
    return;
  std::error_code ec;
  fs::copy_file(mtlSrc, destDir / mtlName, fs::copy_options::overwrite_existing, ec);

  std::ifstream mtlFile(mtlSrc.string());
  if (!mtlFile.is_open())
    return;

  while (std::getline(mtlFile, line)) {
    if (line.size() < 8)
      continue;
    std::string prefix = line.substr(0, 4);
    std::transform(prefix.begin(), prefix.end(), prefix.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (prefix != "map_")
      continue;

    const size_t spacePos = line.find(' ');
    if (spacePos == std::string::npos)
      continue;
    std::string texName = line.substr(spacePos + 1);
    while (!texName.empty() && (texName.back() == '\r' || texName.back() == ' '))
      texName.pop_back();
    if (texName.empty())
      continue;

    const fs::path texSrc = srcDir / texName;
    if (fs::exists(texSrc))
      fs::copy_file(texSrc, destDir / texName, fs::copy_options::overwrite_existing, ec);
  }
}

// Copy picked .obj into assets/models/{folderHint or stem}/ (and companion MTL/textures).
// folderHint: asset ID if already known (e.g. "signenemy-5") so OBJ lands in the same
// folder as its texture. Falls back to OBJ filename stem when empty.
// Returns project-relative mesh path (e.g. assets/models/signenemy-5/signenemy.obj).
static std::string ImportObjFileIntoAssetsModels(const std::string& pickedPath, std::string* outError,
                                                 const std::string& folderHint = {}) {
  namespace fs = std::filesystem;
  if (pickedPath.empty())
    return {};
  if (!IsObjFilePath(pickedPath)) {
    if (outError)
      *outError = "Selected file is not .obj";
    return {};
  }
  const fs::path src(pickedPath);
  const std::string folderName = folderHint.empty() ? src.stem().string() : folderHint;
  const fs::path destDir = ProjectPath::Root() / "assets/models" / folderName;
  std::error_code ec;
  fs::create_directories(destDir, ec);
  if (ec) {
    if (outError)
      *outError = "Cannot create " + destDir.string() + ": " + ec.message();
    return {};
  }
  const fs::path dest = destDir / src.filename();
  fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    if (outError)
      *outError = "Copy failed: " + ec.message();
    return {};
  }
  CopyCompanionAssets(src.string(), destDir.string());
  return (fs::path("assets/models") / folderName / src.filename()).generic_string();
}

}  // namespace

// ---- Lifecycle ---------------------------------------------------------------

void EditorLayer::Init(GLFWwindow* window) {
  m_window = window;
  m_mcpController.Initialize();
  m_mcpSettingsDraft = m_mcpController.GetSettings();
  if (m_mcpController.SettingsDocument().parseError)
    LOG_WARN("[MCP] Settings load fallback: %s", m_mcpController.SettingsDocument().error.c_str());

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGui::GetIO().IniFilename = nullptr;  // don't write imgui.ini

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 410");

  m_schema.LoadFromFile("assets/editor_schema.json");

  // Load wireframe shader
  try {
    const std::filesystem::path wv = ResolvePreviewShaderPath("wire.vert");
    const std::filesystem::path wf = ResolvePreviewShaderPath("wire.frag");
    m_wireframeShader = Shader::FromFiles(wv.generic_string(), wf.generic_string());
  } catch (const std::exception& e) {
    LOG_WARN("[Editor] Failed to load wireframe shader: %s", e.what());
  }

  // Clear thumbnail caches to allow fresh asset preview generation
  auto& renderer = AssetThumbnailRenderer::Instance();
  renderer.noPreviewKeys.clear();
  renderer.meshCache.clear();
  LOG_INFO("[Editor] Asset thumbnail caches cleared on Init");
}

void EditorLayer::Shutdown() {
  m_mcpController.Shutdown();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
}

void EditorLayer::Toggle() {
  m_active = !m_active;
  if (!m_active)
    m_playMode = false;
  if (m_flyMode) {
    m_flyMode = false;
    m_flyCamInitialized = false;
  }
  m_closeRequested = false;
  m_confirmExitOpen = false;
  m_exitConfirmError.clear();
  glfwSetInputMode(m_window, GLFW_CURSOR, m_active ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
  m_prevMouseL = false;
  m_selectedIndices.clear();
  m_mcpController.SetEditorActive(m_active);
}

void EditorLayer::SetProjectBrowserRoot(std::filesystem::path root) {
  InvalidateProjectBrowserCache();
  m_projectBrowserRoot.clear();
  m_projectBrowserRootValid = false;
  m_projectBrowserCwd.clear();
  m_projectBrowserCwdValid = false;
  if (root.empty())
    return;
  std::error_code ec;
  std::filesystem::path canon = std::filesystem::weakly_canonical(root, ec);
  if (ec)
    canon = std::move(root);
  if (!std::filesystem::is_directory(canon, ec) || ec) {
    m_projectBrowserRoot = canon;
    return;
  }
  m_projectBrowserRoot = std::move(canon);
  m_projectBrowserRootValid = true;
  m_projectBrowserCwd = m_projectBrowserRoot;
  m_projectBrowserCwdValid = true;
}

void EditorLayer::SetProjectBrowserExtraBlocklist(std::unordered_set<std::string> names) {
  m_projectExtraBlocklist = std::move(names);
  InvalidateProjectBrowserCache();
}

void EditorLayer::InvalidateProjectBrowserCache() {
  m_projectDirCache.clear();
}

void EditorLayer::LoadDocument(SceneDocument doc) {
  if (doc.filePath.empty())
    doc.filePath = "assets/scenes/scene.json";

  for (auto& obj : doc.objects) {
    if (obj.type != SceneObjectType::Prop)
      continue;
    const auto behIt = obj.props.find("behavior");
    if (behIt == obj.props.end() || behIt->second.empty() || behIt->second == "none")
      continue;

    bool hasScript = false;
    for (const auto& comp : obj.components) {
      if (comp.type == "script") {
        hasScript = true;
        break;
      }
    }
    if (!hasScript) {
      ComponentDesc script;
      script.type = "script";
      script.props["behaviorTag"] = behIt->second;
      obj.components.push_back(std::move(script));
    }
    obj.props.erase("behavior");
  }

  m_document = std::move(doc);
  m_lastSavedDocument = m_document;
  m_selectedIndices.clear();
  m_selectedAssetId.clear();
}

void EditorLayer::OnPathsDropped(int pathCount, const char** utf8Paths, float dropX, float dropY) {
  if (!utf8Paths || pathCount <= 0)
    return;
  m_pendingPathDropPaths.clear();
  m_pendingPathDropPaths.reserve(static_cast<size_t>(pathCount));
  for (int i = 0; i < pathCount; ++i) {
    if (utf8Paths[i])
      m_pendingPathDropPaths.emplace_back(utf8Paths[i]);
  }
  if (m_pendingPathDropPaths.empty())
    return;
  m_pendingPathDropX = dropX;
  m_pendingPathDropY = dropY;
  m_hasPendingPathDrop = true;
}

void EditorLayer::ProcessPendingPathDrops() {
  if (!m_hasPendingPathDrop)
    return;
  m_hasPendingPathDrop = false;

  constexpr float kTextureDropHitSlopPx = 6.0f;
  const float px = m_pendingPathDropX;
  const float py = m_pendingPathDropY;
  auto showAlbedoTextureToast = [this] {
    m_clipboardToastLabel = "Albedo texture set";
    m_clipboardToastTime = 2.0f;
  };

  for (const std::string& path : m_pendingPathDropPaths) {
    if (!IsTextureFilePath(path))
      continue;

    if (m_albedoDraftDrop.Contains(px, py, kTextureDropHitSlopPx)) {
      std::string err;
      const std::string rel = ImportTextureToAssetsModels(path, &err, m_assetDraftId);
      if (rel.empty()) {
        if (!err.empty())
          LOG_WARN("Texture drop: %s", err.c_str());
        continue;
      }
      m_assetDraftAlbedoMap = rel;
      showAlbedoTextureToast();
      m_pendingPathDropPaths.clear();
      return;
    }
    if (m_albedoSelDrop.Contains(px, py, kTextureDropHitSlopPx) && !m_selectedAssetId.empty()) {
      const auto it = m_document.assets.find(m_selectedAssetId);
      if (it != m_document.assets.end()) {
        std::string err;
        const std::string rel = ImportTextureToAssetsModels(path, &err, m_selectedAssetId);
        if (rel.empty()) {
          if (!err.empty())
            LOG_WARN("Texture drop: %s", err.c_str());
          continue;
        }
        it->second.albedoMap = rel;
        m_document.dirty = true;
        showAlbedoTextureToast();
      }
      m_pendingPathDropPaths.clear();
      return;
    }
  }

  for (const std::string& path : m_pendingPathDropPaths) {
    if (!IsObjFilePath(path))
      continue;
    std::string err;
    const std::string meshTag = ImportObjFileIntoAssetsModels(path, &err, m_assetDraftId);
    if (meshTag.empty()) {
      if (!err.empty())
        LOG_WARN("Drop import: %s", err.c_str());
      m_assetImportError = err.empty() ? "Drop import failed." : err;
      m_openNewAssetHeader = true;
      m_pendingPathDropPaths.clear();
      return;
    }
    m_assetDraftMesh = meshTag;
    if (m_assetDraftId.empty())
      m_assetDraftId = AssetIdFromImportedPath(path);
    m_assetDraftRenderScale = SuggestRenderScale(meshTag);
    m_assetImportError.clear();
    m_openNewAssetHeader = true;
    m_clipboardToastLabel = "OBJ dropped — draft ready";
    m_clipboardToastTime = 2.2f;
    m_pendingPathDropPaths.clear();
    return;
  }

  m_pendingPathDropPaths.clear();
}

void EditorLayer::SyncRuntimeEntityIds(Registry& registry) {
  std::vector<int> propIndices;
  propIndices.reserve(m_document.objects.size());
  for (int i = 0; i < static_cast<int>(m_document.objects.size()); ++i) {
    if (m_document.objects[static_cast<size_t>(i)].type == SceneObjectType::Prop)
      propIndices.push_back(i);
  }

  std::vector<Entity> meshEntities;
  for (Entity e : registry.GetEntities<MeshComponent>()) {
    if (registry.Has<PlayerTagComponent>(e))
      continue;
    meshEntities.push_back(e);
  }
  std::sort(meshEntities.begin(), meshEntities.end());

  const size_t propN = propIndices.size();
  const size_t meshN = meshEntities.size();
  const size_t n = std::min(propN, meshN);
  if (propN != meshN) {
    LOG_WARN(
        "EditorLayer::SyncRuntimeEntityIds: %zu prop(s) vs %zu mesh entity(ies); mapping first %zu",
        propN, meshN, n);
  }

  for (size_t j = 0; j < n; ++j) {
    m_document.objects[static_cast<size_t>(propIndices[j])].props["_eid"] =
        std::to_string(meshEntities[j]);
  }
  for (size_t j = n; j < propN; ++j)
    m_document.objects[static_cast<size_t>(propIndices[j])].props.erase("_eid");
}

// ---- Per-frame update --------------------------------------------------------

bool EditorLayer::OnUpdate(float dt, Camera& cam, int screenW, int screenH) {
  if (m_clipboardToastTime > 0.0f)
    m_clipboardToastTime = std::max(0.0f, m_clipboardToastTime - dt);

  if (m_active) {
    ProcessMcpCommands();

    if (ShouldFinalizeEditorClose(m_closeRequested, m_wantsReload)) {
      Toggle();
      return false;
    }

    if (m_playMode) {
      PublishMcpSnapshot();
      return false;
    }

    EditorTrace("OnUpdate frame=%u fly=%d", static_cast<unsigned>(ImGui::GetFrameCount()),
                m_flyMode ? 1 : 0);

    ImGuiIO& io = ImGui::GetIO();

    const bool accelHeld = glfwGetKey(m_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                           glfwGetKey(m_window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS ||
                           glfwGetKey(m_window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
                           glfwGetKey(m_window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;
    const bool shiftHeld = glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                           glfwGetKey(m_window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    const bool slashHeld = glfwGetKey(m_window, GLFW_KEY_SLASH) == GLFW_PRESS;
    const bool f1Held = glfwGetKey(m_window, GLFW_KEY_F1) == GLFW_PRESS;
    const bool currHelpToggle = f1Held || (slashHeld && shiftHeld);
    if (ShouldToggleHelpPopup(currHelpToggle, m_prevHelpToggle, io.WantTextInput,
                              ImGui::IsAnyItemActive())) {
      m_helpOpen = !m_helpOpen;
      if (!m_helpOpen)
        m_helpSearchQuery.clear();
    }
    m_prevHelpToggle = currHelpToggle;

    const bool currQuickOpenToggle = accelHeld && glfwGetKey(m_window, GLFW_KEY_P) == GLFW_PRESS;
    if (ShouldOpenQuickOpen(currQuickOpenToggle, m_prevQuickOpenToggle, m_flyMode,
                            io.WantTextInput, ImGui::IsAnyItemActive())) {
      m_quickOpenOpen = true;
      m_quickOpenQuery.clear();
    }
    m_prevQuickOpenToggle = currQuickOpenToggle;

    const bool currEsc = glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
    const bool hasBlockingPopup = m_helpOpen || m_quickOpenOpen || m_assetSearchOpen ||
                                  m_confirmDeleteObjectsOpen || m_confirmDeleteAssetOpen ||
                                  m_confirmExitOpen;
    // Escape: dismiss gizmo only (no editor quit on Escape; use File / window close).
    if (currEsc && !m_prevEsc && !io.WantTextInput && !ImGui::IsAnyItemActive() &&
        !hasBlockingPopup && m_gizmo.IsActive()) {
      m_gizmo.Deactivate();
    }
    m_prevEsc = currEsc;

    // Tab toggles fly mode
    bool currTab = glfwGetKey(m_window, GLFW_KEY_TAB) == GLFW_PRESS;
    if (currTab && !m_prevTab)
      ToggleFlyMode(cam);
    m_prevTab = currTab;

    if (m_flyMode) {
      UpdateFlyCamera(dt, cam);
      // Keep gizmo anchored to object even while flying
      if (m_gizmo.IsActive()) {
        const int syncIdx = PrimaryIdx();
        if (syncIdx >= 0 && syncIdx < static_cast<int>(m_document.objects.size())) {
          const auto& syncObj = m_document.objects[syncIdx];
          Quaternion  syncRot = Quaternion::FromEuler(ToRadians(syncObj.pitch),
                                                      ToRadians(syncObj.yaw),
                                                      ToRadians(syncObj.roll));
          m_gizmo.SyncTarget(syncObj.position, syncRot, syncObj.scale);
        }
      }
    } else {
      // Ctrl/Cmd + Shift + C copies selected object reference code to clipboard.
      bool currCopyRef = accelHeld && shiftHeld && glfwGetKey(m_window, GLFW_KEY_C) == GLFW_PRESS;
      const int idx = PrimaryIdx();
      const bool hasPrimarySelection = idx >= 0 && idx < static_cast<int>(m_document.objects.size());
      if (ShouldCopySelectionRef(currCopyRef, m_prevCopyRef, io.WantTextInput,
                                 ImGui::IsAnyItemActive(), hasPrimarySelection)) {
        if (idx >= 0 && idx < static_cast<int>(m_document.objects.size())) {
          const std::string code = BuildSelectionRefCode(m_document.objects[idx], idx);
          glfwSetClipboardString(m_window, code.c_str());
          m_clipboardToastLabel = "Reference copied";
          m_clipboardToastTime = 1.6f;
        }
      }
      m_prevCopyRef = currCopyRef;

      // ---- Gizmo mode hotkeys (W/E/R) ----------------------------------------
      if (!io.WantTextInput && !ImGui::IsAnyItemActive()) {
        const int gizmoIdx = PrimaryIdx();
        bool currW = glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS;
        bool currE = glfwGetKey(m_window, GLFW_KEY_E) == GLFW_PRESS;
        bool currR = glfwGetKey(m_window, GLFW_KEY_R) == GLFW_PRESS;

        if (gizmoIdx >= 0 &&
            gizmoIdx < static_cast<int>(m_document.objects.size())) {
          const auto& gizmoObj = m_document.objects[gizmoIdx];
          Quaternion  gizmoRot = Quaternion::FromEuler(ToRadians(gizmoObj.pitch),
                                                       ToRadians(gizmoObj.yaw),
                                                       ToRadians(gizmoObj.roll));
          if (currW && !m_prevGizmoW)
            m_gizmo.Activate(GizmoMode::Translate, gizmoObj.position, gizmoRot,
                             gizmoObj.scale);
          if (currE && !m_prevGizmoE)
            m_gizmo.Activate(GizmoMode::Rotate, gizmoObj.position, gizmoRot,
                             gizmoObj.scale);
          if (currR && !m_prevGizmoR)
            m_gizmo.Activate(GizmoMode::Scale, gizmoObj.position, gizmoRot,
                             gizmoObj.scale);
        }
        m_prevGizmoW = currW;
        m_prevGizmoE = currE;
        m_prevGizmoR = currR;
      }

      // Sync gizmo to primary selected object each frame
      if (m_gizmo.IsActive()) {
        const int syncIdx = PrimaryIdx();
        if (syncIdx < 0 ||
            syncIdx >= static_cast<int>(m_document.objects.size())) {
          m_gizmo.Deactivate();
        } else {
          const auto& syncObj = m_document.objects[syncIdx];
          Quaternion  syncRot = Quaternion::FromEuler(ToRadians(syncObj.pitch),
                                                      ToRadians(syncObj.yaw),
                                                      ToRadians(syncObj.roll));
          m_gizmo.SyncTarget(syncObj.position, syncRot, syncObj.scale);
        }
      }

      // ---- Gizmo update (consumes mouse before picking) ----------------------
      Vec3       dPos   = Vec3::Zero();
      Quaternion dRot   = Quaternion::Identity();
      Vec3       dScale = Vec3::One();
      bool gizmoConsumed = false;
      if (m_gizmo.IsActive()) {
        gizmoConsumed = m_gizmo.Update(m_window, cam, screenW, screenH,
                                       dPos, dRot, dScale);

        // --- Surface snap (Ctrl) and Grid snap (Shift) ---
        if (m_gizmo.GetMode() == GizmoMode::Translate) {
          const bool ctrlHeld =
              glfwGetKey(m_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
              glfwGetKey(m_window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS ||
              glfwGetKey(m_window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
              glfwGetKey(m_window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;
          const bool shiftHeldSnap =
              glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
              glfwGetKey(m_window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

          GizmoAxis dragAxis = m_gizmo.GetDragAxis();
          const int primIdx  = PrimaryIdx();

          if (ctrlHeld && dragAxis != GizmoAxis::None &&
              primIdx >= 0 && primIdx < static_cast<int>(m_document.objects.size())) {

            const auto& selfObj    = m_document.objects[primIdx];
            Vec3        selfCenter = selfObj.position;
            Vec3        selfHalf   = selfObj.scale;
            if (m_liveRegistry)
              TryPropWorldAabb(*m_liveRegistry, selfObj, selfCenter, selfHalf);
            Vec3 rawPos = selfCenter + dPos;

            int axisIdx = (dragAxis == GizmoAxis::X) ? 0 :
                          (dragAxis == GizmoAxis::Y) ? 1 : 2;

            float bestDist   = std::numeric_limits<float>::max();
            float bestOffset = 0.0f;
            bool  didSnap    = false;

            for (int oi = 0; oi < static_cast<int>(m_document.objects.size()); ++oi) {
              if (IsSelected(oi)) continue;
              const auto& other       = m_document.objects[oi];
              Vec3        otherCenter = other.position;
              Vec3        otherHalfV  = other.scale;
              if (m_liveRegistry)
                TryPropWorldAabb(*m_liveRegistry, other, otherCenter, otherHalfV);
              float otherHalf = otherHalfV[axisIdx];
              float selfHalf1 = selfHalf[axisIdx];

              const float selfFaces[2]  = { rawPos[axisIdx] - selfHalf1,
                                            rawPos[axisIdx] + selfHalf1 };
              const float otherFaces[2] = { otherCenter[axisIdx] - otherHalf,
                                            otherCenter[axisIdx] + otherHalf };

              for (int sf = 0; sf < 2; ++sf) {
                for (int of = 0; of < 2; ++of) {
                  float gap = std::abs(selfFaces[sf] - otherFaces[of]);
                  if (gap < bestDist) {
                    bestDist   = gap;
                    bestOffset = otherFaces[of] - selfFaces[sf];
                    didSnap    = true;
                  }
                }
              }
            }

            if (didSnap) {
              Vec3 snappedCenter      = rawPos;
              snappedCenter[axisIdx] += bestOffset;
              Vec3 centerToOrigin     = selfObj.position - selfCenter;
              dPos = snappedCenter + centerToOrigin - selfObj.position;

              Vec3 axisDir   = m_gizmo.AxisDir(dragAxis);
              Vec3 facePoint = selfObj.position + dPos;
              DebugDraw::Line(facePoint - axisDir * 0.3f,
                              facePoint + axisDir * 0.3f,
                              {1.0f, 1.0f, 0.0f, 1.0f});
            }
          }

          if (shiftHeldSnap && dPos.LengthSq() > 1e-12f &&
              primIdx >= 0 && primIdx < static_cast<int>(m_document.objects.size())) {

            constexpr float kGridSize = 0.5f;
            const auto& selfObj = m_document.objects[primIdx];
            Vec3         rawPos = selfObj.position + dPos;

            rawPos.x = std::round(rawPos.x / kGridSize) * kGridSize;
            rawPos.y = std::round(rawPos.y / kGridSize) * kGridSize;
            rawPos.z = std::round(rawPos.z / kGridSize) * kGridSize;
            dPos = rawPos - selfObj.position;
          }
        }

        // Detect any non-trivial delta
        float dRotXYZSq = dRot.x * dRot.x + dRot.y * dRot.y + dRot.z * dRot.z;
        bool anyDelta = dPos.LengthSq() > 1e-10f
                     || dRotXYZSq > 1e-8f
                     || std::abs(dScale.x - 1.0f) > 1e-6f
                     || std::abs(dScale.y - 1.0f) > 1e-6f
                     || std::abs(dScale.z - 1.0f) > 1e-6f;
        if (anyDelta) {
          for (int si : m_selectedIndices) {
            if (si < 0 || si >= static_cast<int>(m_document.objects.size()))
              continue;
            auto& applyObj   = m_document.objects[si];

            // Capture pre-delta state so we can propagate to children below.
            const Vec3 oldObjPos = applyObj.position;
            const Quaternion oldObjRot = Quaternion::FromEuler(ToRadians(applyObj.pitch),
                                                               ToRadians(applyObj.yaw),
                                                               ToRadians(applyObj.roll));

            applyObj.position = applyObj.position + dPos;
            applyObj.scale.x *= dScale.x;
            applyObj.scale.y *= dScale.y;
            applyObj.scale.z *= dScale.z;
            Quaternion nextRot = oldObjRot;
            if (dRotXYZSq > 1e-8f) {
              nextRot        = (dRot * oldObjRot).Normalized();
              Vec3 euler     = nextRot.ToEuler();
              applyObj.pitch = ToDegrees(euler.x);
              applyObj.yaw   = ToDegrees(euler.y);
              applyObj.roll  = ToDegrees(euler.z);
            }
            m_document.dirty = true;
            if (m_transformCb)
              m_transformCb(applyObj);

            // Propagate the same delta to all hierarchy children of this object,
            // skipping any children that are themselves directly selected (they
            // already receive the delta above and must not be moved twice).
            PropagateHierarchyTransformDelta(
                m_document, si, oldObjPos, oldObjRot,
                applyObj.position, nextRot, m_transformCb, m_selectedIndices);
          }
        }
      }

      if (gizmoConsumed) {
        // Update m_prevMouseL so HandlePicking doesn't see a phantom click
        // on the frame when the gizmo releases.
        m_prevMouseL = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
      } else {
        HandlePicking(cam, screenW, screenH);
      }

      // Del key — delete all selected objects immediately
      bool currDel = glfwGetKey(m_window, GLFW_KEY_DELETE) == GLFW_PRESS;
      if (ShouldRequestDeleteSelection(currDel, m_prevDel, !m_selectedIndices.empty()))
        RequestDeleteSelectedObjects();
      m_prevDel = currDel;
    }

    ApplyPendingViewSnap(cam);
    PublishMcpSnapshot();
  }

  ImGuiIO& io = ImGui::GetIO();
  return m_active && !m_flyMode && (io.WantCaptureMouse || io.WantCaptureKeyboard);
}

void EditorLayer::SetHotReloadOverlay(
    bool active, float progress01, float spinnerAngleRad, const std::string& label) {
  m_hotReloadOverlayActive = active;
  m_hotReloadOverlayProgress = std::max(0.0f, std::min(1.0f, progress01));
  m_hotReloadOverlaySpinner = spinnerAngleRad;
  m_hotReloadOverlayLabel = label;
}

void EditorLayer::ProcessDeferredFilePicks() {
  const DeferredFilePick pick = m_deferredFilePick;
  if (pick == DeferredFilePick::None)
    return;
  m_deferredFilePick = DeferredFilePick::None;

#if !defined(_WIN32) && !defined(__APPLE__)
  (void)pick;
  return;
#endif

  switch (pick) {
    case DeferredFilePick::None:
      break;
    case DeferredFilePick::ImportObjBulk: {
      m_assetImportError.clear();
      const std::string chosen = PickObjFilePath();
      if (chosen.empty())
        break;
      std::string err;
      const std::string meshTag = ImportObjFileIntoAssetsModels(chosen, &err, m_assetDraftId);
      if (!meshTag.empty()) {
        m_assetDraftMesh = meshTag;
        if (m_assetDraftId.empty())
          m_assetDraftId = AssetIdFromImportedPath(chosen);
        m_assetDraftRenderScale = SuggestRenderScale(meshTag);
        m_assetImportError.clear();
      } else if (!err.empty())
        m_assetImportError = err;
      break;
    }
    case DeferredFilePick::NewAssetAlbedo: {
      std::string err;
      const std::string rel = ImportTextureToAssetsModels(PickTextureFilePath(), &err, m_assetDraftId);
      if (!rel.empty())
        m_assetDraftAlbedoMap = rel;
      else if (!err.empty())
        LOG_WARN("Texture browse: %s", err.c_str());
      break;
    }
    case DeferredFilePick::SelectedAssetAlbedo: {
      const std::string id = m_selectedAssetId;
      if (id.empty() || m_document.assets.find(id) == m_document.assets.end())
        break;
      std::string err;
      const std::string rel = ImportTextureToAssetsModels(PickTextureFilePath(), &err, id);
      if (!rel.empty()) {
        m_document.assets[id].albedoMap = rel;
        m_document.dirty = true;
      } else if (!err.empty())
        LOG_WARN("Texture browse: %s", err.c_str());
      break;
    }
  }
}

void EditorLayer::Render(const Camera& cam, int screenW, int screenH) {
  ProcessDeferredFilePicks();
  if (!m_active) {
    m_albedoDraftDrop.Clear();
    m_albedoSelDrop.Clear();
    m_viewGizmoPickRect.Clear();
  }
  ProcessPendingPathDrops();

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  if (m_active) {
    // Viewport drop target drawn FIRST so all panels sit on top of it (higher z-order)
    if (!m_playMode)
      DrawViewportDropTarget(cam, screenW, screenH);
    DrawToolbar();
    if (!m_playMode)
      DrawViewGimbal(cam);
    DrawObjectList();
    DrawAssetsPanel();
    DrawPropertiesPanel();
    DrawBottomDock();
    DrawStatusBar();
    DrawHelpPopup();
    DrawQuickOpenPopup();
    DrawSettingsModal();
    DrawDeleteConfirmModals();
    DrawExitConfirmModal();
    if (!m_playMode) {
      DrawSelectionHighlight();  // queues to DebugDraw
      if (m_gizmo.IsActive())
        m_gizmo.Draw(cam, screenW, screenH);  // queues to DebugDraw
    }
  }
  DrawHotReloadOverlay();
  DrawClipboardToast();

  // Wireframe pass: clears solid scene and draws edges; must happen before
  // DebugDraw::Flush so selection highlight/gizmo render on top.
  if (m_active && !m_playMode)
    DrawWireframeOverlay(cam);

  // Flush any queued debug primitives (selection box, gizmo, etc.) before ImGui
  DebugDraw::Flush(cam);

  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void EditorLayer::DrawClipboardToast() {
  if (m_clipboardToastTime <= 0.0f)
    return;

  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* draw = ImGui::GetForegroundDrawList();

  const ImVec2 size(230.0f, 32.0f);
  const ImVec2 pos(io.DisplaySize.x - size.x - 14.0f, io.DisplaySize.y - size.y - 14.0f);
  const ImVec2 max(pos.x + size.x, pos.y + size.y);

  draw->AddRectFilled(pos, max, IM_COL32(12, 18, 28, 215), 8.0f);
  draw->AddRect(pos, max, IM_COL32(90, 190, 255, 185), 8.0f, 0, 1.0f);
  const char* label = m_clipboardToastLabel.empty() ? "Reference copied" : m_clipboardToastLabel.c_str();
  draw->AddText(ImVec2(pos.x + 10.0f, pos.y + 9.0f), IM_COL32(220, 235, 255, 255), label);
}

void EditorLayer::DrawHotReloadOverlay() {
  if (!m_hotReloadOverlayActive)
    return;

  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* draw = ImGui::GetForegroundDrawList();

  const ImVec2 panelSize(280.0f, 74.0f);
  const ImVec2 panelPos((io.DisplaySize.x - panelSize.x) * 0.5f, 18.0f);
  const ImVec2 panelMax(panelPos.x + panelSize.x, panelPos.y + panelSize.y);

  draw->AddRectFilled(panelPos, panelMax, IM_COL32(10, 14, 22, 215), 10.0f);
  draw->AddRect(panelPos, panelMax, IM_COL32(70, 120, 190, 180), 10.0f, 0, 1.0f);

  const ImVec2 spinnerCenter(panelPos.x + 24.0f, panelPos.y + panelSize.y * 0.5f);
  const float spinnerR = 10.0f;
  draw->AddCircle(spinnerCenter, spinnerR, IM_COL32(80, 90, 120, 200), 24, 2.0f);

  const float arcStart = m_hotReloadOverlaySpinner;
  const float arcEnd = arcStart + 2.5f;
  draw->PathArcTo(spinnerCenter, spinnerR, arcStart, arcEnd, 24);
  draw->PathStroke(IM_COL32(110, 210, 255, 255), false, 3.0f);

  const char* label = m_hotReloadOverlayLabel.empty() ? "Hot Reload" : m_hotReloadOverlayLabel.c_str();
  draw->AddText(ImVec2(panelPos.x + 44.0f, panelPos.y + 14.0f), IM_COL32(230, 240, 255, 255), label);

  const ImVec2 barMin(panelPos.x + 44.0f, panelPos.y + 42.0f);
  const ImVec2 barMax(panelMax.x - 16.0f, panelPos.y + 56.0f);
  draw->AddRectFilled(barMin, barMax, IM_COL32(26, 32, 46, 255), 4.0f);

  const float w = (barMax.x - barMin.x) * m_hotReloadOverlayProgress;
  if (w > 1.0f)
    draw->AddRectFilled(barMin, ImVec2(barMin.x + w, barMax.y), IM_COL32(90, 190, 255, 255), 4.0f);
}

// ---- Toolbar -----------------------------------------------------------------

void EditorLayer::DrawToolbar() {
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, kEditorToolbarH));
  ImGui::SetNextWindowBgAlpha(0.85f);
  ImGui::Begin("##toolbar",
               nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus);

  ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "EDITOR");
  ImGui::SameLine();
  ImGui::Separator();
  ImGui::SameLine();

  const bool hasSelectedAsset = !m_selectedAssetId.empty() &&
                                m_document.assets.find(m_selectedAssetId) != m_document.assets.end();
  const int primaryIdx = PrimaryIdx();
  const bool hasSingleSelection = CanEditSingleSelection(
      static_cast<int>(m_selectedIndices.size()),
      primaryIdx,
      static_cast<int>(m_document.objects.size()));

  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("New Scene"))
      AddNewScene();
    if (ImGui::MenuItem("Open Scene..."))
      OpenAdditionalSceneFile();
    ImGui::Separator();
    if (ImGui::MenuItem("Settings...")) {
      m_settingsOpen = true;
      m_mcpSettingsDraft = m_mcpController.GetSettings();
      m_mcpSettingsError.clear();
    }
    ImGui::EndMenu();
  }
  ImGui::SameLine();

  if (ImGui::Button("Add"))
    ImGui::OpenPopup("##toolbar_add_popup");
  if (ImGui::BeginPopup("##toolbar_add_popup")) {
    if (ImGui::MenuItem("Panel"))
      AddObject(SceneObjectType::Panel);
    if (ImGui::MenuItem("Prop"))
      AddObject(SceneObjectType::Prop);
    if (ImGui::MenuItem("Light"))
      AddObject(SceneObjectType::Light);
    if (ImGui::MenuItem("Camera"))
      AddObject(SceneObjectType::Camera);

    ImGui::Separator();
    if (!hasSelectedAsset)
      ImGui::BeginDisabled();
    if (ImGui::MenuItem("Prop from Selected Asset"))
      AddObjectFromSelectedAsset();
    if (!hasSelectedAsset)
      ImGui::EndDisabled();
    ImGui::EndPopup();
  }
  ImGui::SameLine();

  if (!hasSingleSelection)
    ImGui::BeginDisabled();
  if (ImGui::Button("Edit"))
    ImGui::OpenPopup("##toolbar_edit_popup");
  if (ImGui::BeginPopup("##toolbar_edit_popup")) {
    if (ImGui::MenuItem("Rename..."))
      OpenRenameObjectModal(primaryIdx);
    if (ImGui::MenuItem("Duplicate"))
      DuplicatePrimarySelection();
    if (ImGui::MenuItem("Delete"))
      RequestDeleteSelectedObjects();
    ImGui::Separator();
    if (ImGui::MenuItem("Copy Ref", "Ctrl/Cmd+Shift+C")) {
      const int idx = PrimaryIdx();
      if (idx >= 0 && idx < static_cast<int>(m_document.objects.size())) {
        const std::string ref = BuildSelectionRefCode(m_document.objects[static_cast<size_t>(idx)], idx);
        ImGui::SetClipboardText(ref.c_str());
        m_clipboardToastLabel = "Reference copied";
        m_clipboardToastTime = 1.5f;
      }
    }
    ImGui::EndPopup();
  }
  if (!hasSingleSelection)
    ImGui::EndDisabled();
  ImGui::SameLine();

  if (ImGui::Button("View"))
    ImGui::OpenPopup("##toolbar_view_popup");
  if (ImGui::BeginPopup("##toolbar_view_popup")) {
    const bool flyBefore = m_flyMode;
    if (ImGui::MenuItem("Fly Mode", "Tab", m_flyMode)) {
      m_flyMode = !m_flyMode;
      m_flyCamInitialized = false;
      m_prevCursorInit = false;
      glfwSetInputMode(m_window, GLFW_CURSOR,
                       m_flyMode ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }
    if (flyBefore || m_flyMode)
      ImGui::TextDisabled("WASD + mouse");
    if (ImGui::MenuItem("Help", "? / F1"))
      m_helpOpen = true;
    if (ImGui::MenuItem("Quick Open", "Ctrl/Cmd+P"))
      m_quickOpenOpen = true;
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  ImGui::TextDisabled("Copy Ref: Ctrl/Cmd+Shift+C");
  ImGui::SameLine();
  ImGui::Separator();
  ImGui::SameLine();
  if (!m_playMode) {
    if (ImGui::Button("Play")) {
      m_playMode = true;
      if (m_flyMode) {
        m_flyMode = false;
        m_flyCamInitialized = false;
        m_prevCursorInit = false;
      }
      glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }
  } else {
    if (ImGui::Button("Stop")) {
      m_playMode = false;
      glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
  }
  ImGui::SameLine();

  // Right-aligned controls
  const float rightW = 300.0f;
  ImGui::SetCursorPosX(io.DisplaySize.x - rightW);

  if (ImGui::Button("Load")) {
    std::string path =
        m_document.filePath.empty() ? "assets/scenes/scene.json" : m_document.filePath;
    try {
      m_document = SceneSerializer::LoadFromFile(path);
      m_selectedIndices.clear();
    } catch (const std::exception& e) {
      LOG_ERROR("EditorLayer: failed to load scene: %s", e.what());
    }
  }
  ImGui::SameLine();

  {
    const bool canSave = m_document.dirty;
    if (!canSave)
      ImGui::BeginDisabled();
    if (ImGui::Button("Save")) {
      std::string saveError;
      if (!SaveDocument(&saveError))
        LOG_ERROR("EditorLayer: save failed: %s", saveError.c_str());
    }
    if (!canSave)
      ImGui::EndDisabled();
  }
  ImGui::SameLine();

  if (ImGui::Button("Close editor")) {
    if (ResolveEditorExitDecision(m_document.dirty) ==
        EditorExitDecision::PromptUnsavedConfirm) {
      m_confirmExitOpen = true;
      m_exitConfirmError.clear();
    } else {
      m_closeRequested = true;
    }
  }

  ImGui::End();
}

void EditorLayer::DrawStatusBar() {
  ImGuiIO& io = ImGui::GetIO();
  const EditorStatusText status = BuildEditorStatusText(
      EditorStatusSnapshot{static_cast<int>(m_selectedIndices.size()), m_document.dirty, m_flyMode, m_wantsReload});

  ImGui::SetNextWindowPos(ImVec2(0.0f, io.DisplaySize.y - kEditorStatusH));
  ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, kEditorStatusH));
  ImGui::SetNextWindowBgAlpha(0.82f);
  ImGui::Begin("##editor_statusbar",
               nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus);

  ImGui::TextDisabled("Sel: %d", status.selectionCount);
  ImGui::SameLine();
  ImGui::TextDisabled("|");
  ImGui::SameLine();
  ImGui::TextDisabled("Dirty: %s", status.dirtyText);
  ImGui::SameLine();
  ImGui::TextDisabled("|");
  ImGui::SameLine();
  ImGui::TextDisabled("Fly: %s", status.flyText);
  ImGui::SameLine();
  ImGui::TextDisabled("|");
  ImGui::SameLine();
  ImGui::TextDisabled("Reload: %s", status.reloadText);

  ImGui::End();
}

const std::vector<std::pair<std::filesystem::path, bool>>* EditorLayer::GetProjectDirListing(
    const std::filesystem::path& absPath) {
  namespace fs = std::filesystem;
  std::error_code existsEc;
  if (!fs::is_directory(absPath, existsEc) || existsEc)
    return nullptr;

  const std::string key = absPath.generic_string();
  const uint32_t frame = static_cast<uint32_t>(ImGui::GetFrameCount());
  auto it = m_projectDirCache.find(key);
  if (it != m_projectDirCache.end()) {
    const uint32_t age = frame - it->second.cachedAtFrame;
    if (age < kProjectListingCacheFrames)
      return &it->second.entries;
  }

  std::vector<std::pair<fs::path, bool>> sorted;
  std::error_code ec;
  for (fs::directory_iterator dit(absPath, fs::directory_options::skip_permission_denied, ec), end;
       !ec && dit != end;
       dit.increment(ec)) {
    const fs::directory_entry ent = *dit;
    const std::string name = ent.path().filename().string();
    if (Editor::IsHiddenDotEntry(name))
      continue;
    std::error_code typEc;
    const bool isDir = ent.is_directory(typEc);
    if (isDir && !typEc && Editor::IsBlockedProjectDirName(name, &m_projectExtraBlocklist))
      continue;
    sorted.emplace_back(ent.path(), isDir && !typEc);
  }
  std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
    if (a.second != b.second)
      return a.second > b.second;
    return a.first.filename() < b.first.filename();
  });

  ProjectDirCache slot;
  slot.cachedAtFrame = frame;
  slot.entries = std::move(sorted);
  m_projectDirCache[key] = std::move(slot);
  return &m_projectDirCache.at(key).entries;
}

void EditorLayer::DrawProjectTreeRecursive(const std::filesystem::path& absPath,
                                           const std::filesystem::path& /*displayRoot*/) {
  namespace fs = std::filesystem;
  const auto* listing = GetProjectDirListing(absPath);
  if (!listing)
    return;

  for (const auto& ent : *listing) {
    const fs::path& p = ent.first;
    const bool isDir = ent.second;
    const std::string name = p.filename().string();
    if (isDir) {
      if (ImGui::TreeNodeEx(name.c_str(), 0)) {
        DrawProjectTreeRecursive(p, absPath);
        ImGui::TreePop();
      }
    } else {
      ImGui::BulletText("%s", name.c_str());
    }
  }
}

static void FormatLogTime(const LogLine& entry, char* buf, size_t bufSize) {
  using clock = std::chrono::system_clock;
  const std::time_t t = clock::to_time_t(entry.time);
  std::tm tmBuf{};
#ifdef _WIN32
  localtime_s(&tmBuf, &t);
#else
  localtime_r(&t, &tmBuf);
#endif
  std::strftime(buf, bufSize, "%H:%M:%S", &tmBuf);
}

void EditorLayer::DrawBottomDock() {
  ImGuiIO& io = ImGui::GetIO();
  const float dockTop = io.DisplaySize.y - kEditorStatusH - kBottomDockH;
  ImGui::SetNextWindowPos(ImVec2(0.0f, dockTop));
  ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, kBottomDockH));
  ImGui::SetNextWindowBgAlpha(0.88f);
  ImGui::Begin("##editor_bottom_dock",
               nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus);

  if (ImGui::BeginTabBar("##bottom_tabs", ImGuiTabBarFlags_None)) {
    if (ImGui::BeginTabItem("Project")) {
      if (m_projectBrowserRootValid &&
          std::filesystem::is_directory(m_projectBrowserRoot)) {
        if (!m_projectBrowserCwdValid ||
            !std::filesystem::is_directory(m_projectBrowserCwd) ||
            m_projectBrowserCwd.empty()) {
          m_projectBrowserCwd = m_projectBrowserRoot;
          m_projectBrowserCwdValid = true;
        }

        ImGui::BeginChild("##project_tiles", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

        bool cwdChanged = false;
        std::filesystem::path nextCwd = m_projectBrowserCwd;
        const std::string rootLabel = m_projectBrowserRoot.filename().string().empty()
                                          ? m_projectBrowserRoot.generic_string()
                                          : m_projectBrowserRoot.filename().string();
        if (ImGui::SmallButton(rootLabel.c_str())) {
          nextCwd = m_projectBrowserRoot;
          cwdChanged = true;
        }

        std::error_code relEc;
        const std::filesystem::path relPath =
            std::filesystem::relative(m_projectBrowserCwd, m_projectBrowserRoot, relEc);
        if (!relEc && !relPath.empty() && relPath != ".") {
          std::filesystem::path crumb = m_projectBrowserRoot;
          for (const auto& part : relPath) {
            ImGui::SameLine();
            ImGui::TextUnformatted(">");
            ImGui::SameLine();
            crumb /= part;
            const std::string partLabel = part.string();
            if (ImGui::SmallButton(partLabel.c_str())) {
              nextCwd = crumb;
              cwdChanged = true;
            }
          }
        }

        ImGui::Separator();

        const auto* listing = GetProjectDirListing(m_projectBrowserCwd);
        if (!listing) {
          ImGui::TextDisabled("Cannot read '%s'", m_projectBrowserCwd.generic_string().c_str());
        } else {
          const float spacing = 10.0f;
          const float minTileW = 120.0f;
          const float tileH = 104.0f;
          const float iconW = 64.0f;
          const float iconH = 42.0f;
          const float availW = std::max(0.0f, ImGui::GetContentRegionAvail().x);
          const int columns = std::max(1, static_cast<int>((availW + spacing) / (minTileW + spacing)));
          const float tileW = std::max(minTileW,
                                       (availW - spacing * static_cast<float>(columns - 1)) /
                                           static_cast<float>(columns));

          int itemIndex = 0;
          for (const auto& ent : *listing) {
            const std::filesystem::path& p = ent.first;
            const bool isDir = ent.second;
            const std::string name = p.filename().string();

            ImGui::PushID(p.generic_string().c_str());
            ImGui::InvisibleButton("##project_item_btn", ImVec2(tileW, tileH));
            if (ImGui::IsItemClicked()) {
              if (isDir) {
                nextCwd = p;
                cwdChanged = true;
              }
            }

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
              ImGui::SetTooltip("%s", p.generic_string().c_str());

            const ImVec2 rMin = ImGui::GetItemRectMin();
            const ImVec2 rMax = ImGui::GetItemRectMax();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const bool hovered = ImGui::IsItemHovered();
            const ImU32 hoverBg = ImGui::ColorConvertFloat4ToU32(ImVec4(0.22f, 0.30f, 0.42f, 0.28f));
            if (hovered) {
              dl->AddRectFilled(ImVec2(rMin.x + 2.0f, rMin.y + 2.0f), ImVec2(rMax.x - 2.0f, rMax.y - 2.0f),
                                hoverBg, 8.0f);
            }

            const float iconX = rMin.x + (tileW - iconW) * 0.5f;
            const float iconY = rMin.y + 10.0f;
            if (isDir) {
              const ImU32 tabCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.30f, 0.68f, 0.92f, 1.0f));
              const ImU32 bodyCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.25f, 0.60f, 0.86f, 1.0f));
              const ImU32 edgeCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.18f, 0.45f, 0.66f, 1.0f));
              dl->AddRectFilled(ImVec2(iconX + 5.0f, iconY), ImVec2(iconX + iconW * 0.56f, iconY + 10.0f), tabCol,
                                4.0f, ImDrawFlags_RoundCornersTop);
              dl->AddRectFilled(ImVec2(iconX, iconY + 7.0f), ImVec2(iconX + iconW, iconY + iconH), bodyCol, 6.0f);
              dl->AddRect(ImVec2(iconX, iconY + 7.0f), ImVec2(iconX + iconW, iconY + iconH), edgeCol, 6.0f);
            } else {
              const ImU32 pageCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.86f, 0.90f, 0.96f, 1.0f));
              const ImU32 foldCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.74f, 0.80f, 0.90f, 1.0f));
              const ImU32 edgeCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.58f, 0.65f, 0.76f, 1.0f));
              const ImVec2 p0(iconX + 8.0f, iconY);
              const ImVec2 p1(iconX + iconW - 8.0f, iconY + iconH);
              dl->AddRectFilled(p0, p1, pageCol, 4.0f);
              dl->AddRect(p0, p1, edgeCol, 4.0f);
              dl->AddTriangleFilled(ImVec2(p1.x - 14.0f, p0.y), ImVec2(p1.x, p0.y), ImVec2(p1.x, p0.y + 14.0f),
                                    foldCol);
            }

            std::string label = name;
            const float maxLabelW = tileW - 10.0f;
            if (ImGui::CalcTextSize(label.c_str()).x > maxLabelW) {
              while (!label.empty() && ImGui::CalcTextSize((label + "...").c_str()).x > maxLabelW)
                label.pop_back();
              label += "...";
            }

            const ImVec2 labelSz = ImGui::CalcTextSize(label.c_str());
            const float labelX = rMin.x + std::max(4.0f, (tileW - labelSz.x) * 0.5f);
            const float labelY = iconY + iconH + 10.0f;
            dl->AddText(ImVec2(labelX, labelY), ImGui::GetColorU32(ImGuiCol_Text), label.c_str());

            ImGui::PopID();

            ++itemIndex;
            if ((itemIndex % columns) != 0)
              ImGui::SameLine(0.0f, spacing);
          }
        }

        if (cwdChanged) {
          m_projectBrowserCwd = std::move(nextCwd);
          m_projectBrowserCwdValid = true;
        }

        ImGui::EndChild();
      } else {
        ImGui::TextDisabled("Set project root (host app) to browse files.");
      }
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Console")) {
      int nInfo = 0;
      int nWarn = 0;
      int nErr = 0;
      LogBuffer::Instance().GetCounts(&nInfo, &nWarn, &nErr);
      if (ImGui::SmallButton("Clear"))
        LogBuffer::Instance().Clear();
      ImGui::SameLine();
      ImGui::Checkbox("Info", &m_consoleShowInfo);
      ImGui::SameLine();
      ImGui::Checkbox("Warn", &m_consoleShowWarn);
      ImGui::SameLine();
      ImGui::Checkbox("Error", &m_consoleShowError);
      ImGui::SameLine();
      ImGui::TextDisabled("I:%d W:%d E:%d", nInfo, nWarn, nErr);

      ImGui::Separator();
      ImGui::BeginChild("##console_scroll", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
      LogBuffer& logBuf = LogBuffer::Instance();
      const uint64_t rev = logBuf.Revision();
      if (rev != m_consoleLogRevision) {
        logBuf.CopyLinesTo(&m_consoleLinesCache);
        m_consoleLogRevision = rev;
      }

      m_consoleVisibleScratch.clear();
      for (int i = 0; i < static_cast<int>(m_consoleLinesCache.size()); ++i) {
        const LogLine& entry = m_consoleLinesCache[static_cast<size_t>(i)];
        if (entry.level == LogLevel::Info && !m_consoleShowInfo)
          continue;
        if (entry.level == LogLevel::Warn && !m_consoleShowWarn)
          continue;
        if (entry.level == LogLevel::Error && !m_consoleShowError)
          continue;
        m_consoleVisibleScratch.push_back(i);
      }

      // No ImGuiListClipper here: wrapped ImGui::Text rows have variable height and can confuse
      // the clipper (hang or broken scroll). LogBuffer caps lines (~1k); a linear pass is fine.
      EditorTrace("Console draw visible=%d cached=%zu rev=%llu",
                  static_cast<int>(m_consoleVisibleScratch.size()), m_consoleLinesCache.size(),
                  static_cast<unsigned long long>(rev));

      for (int row = 0; row < static_cast<int>(m_consoleVisibleScratch.size()); ++row) {
        const LogLine& entry =
            m_consoleLinesCache[static_cast<size_t>(m_consoleVisibleScratch[static_cast<size_t>(row)])];
        char timeBuf[32];
        FormatLogTime(entry, timeBuf, sizeof(timeBuf));
        const char* levelStr =
            entry.level == LogLevel::Info ? "INFO" : entry.level == LogLevel::Warn ? "WARN" : "ERR";
        ImVec4 color(0.85f, 0.9f, 1.0f, 1.0f);
        if (entry.level == LogLevel::Warn)
          color = ImVec4(1.0f, 0.85f, 0.4f, 1.0f);
        else if (entry.level == LogLevel::Error)
          color = ImVec4(1.0f, 0.45f, 0.4f, 1.0f);

        char lineBuf[1024];
        std::snprintf(lineBuf, sizeof(lineBuf), "[%s] %s (%s:%d) %s", timeBuf, levelStr,
                      entry.file.c_str(), entry.line, entry.message.c_str());

        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(lineBuf);
        ImGui::PopStyleColor();
      }
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("MCP")) {
      DrawMcpTab();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  ImGui::End();
}

void EditorLayer::DrawMcpTab() {
  const Mcp::McpStatusSnapshot status = m_mcpController.GetStatusSnapshot();

  ImGui::Text("Status: %s", status.running ? "Running" : "Stopped");
  ImGui::SameLine();
  ImGui::TextDisabled("|");
  ImGui::SameLine();
  ImGui::Text("Enabled: %s", status.enabled ? "Yes" : "No");
  ImGui::SameLine();
  ImGui::TextDisabled("|");
  ImGui::SameLine();
  ImGui::Text("Endpoint: %s", status.endpointUrl.c_str());

  ImGui::Text("Requests: %llu", static_cast<unsigned long long>(status.totalRequests));
  ImGui::SameLine();
  ImGui::TextDisabled("|");
  ImGui::SameLine();
  ImGui::Text("Active: %d", status.activeConnections);
  ImGui::SameLine();
  ImGui::TextDisabled("|");
  ImGui::SameLine();
  ImGui::Text("Tools: %d", static_cast<int>(status.toolCount));
  ImGui::SameLine();
  ImGui::TextDisabled("|");
  ImGui::SameLine();
  ImGui::Text("Resources: %d", static_cast<int>(status.resourceCount));

  if (!status.lastError.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
    ImGui::TextWrapped("%s", status.lastError.c_str());
    ImGui::PopStyleColor();
  }

  if (ImGui::Button("Open Settings")) {
    m_settingsOpen = true;
    m_mcpSettingsDraft = m_mcpController.GetSettings();
    m_mcpSettingsError.clear();
  }
  ImGui::SameLine();
  if (ImGui::Button("Copy Claude Config")) {
    const std::string snippet = m_mcpController.BuildClaudeConfigSnippet();
    glfwSetClipboardString(m_window, snippet.c_str());
    m_clipboardToastLabel = "Claude MCP config copied";
    m_clipboardToastTime = 1.5f;
  }
  ImGui::SameLine();
  if (ImGui::Button("Copy Codex Config")) {
    const std::string snippet = m_mcpController.BuildCodexConfigSnippet();
    glfwSetClipboardString(m_window, snippet.c_str());
    m_clipboardToastLabel = "Codex MCP config copied";
    m_clipboardToastTime = 1.5f;
  }

  ImGui::Separator();
  ImGui::TextDisabled("Recent MCP activity");
  ImGui::BeginChild("##mcp_activity", ImVec2(0, 0), true);
  for (const Mcp::McpActivityEntry& entry : status.recentActivity) {
    ImVec4 color = entry.ok ? ImVec4(0.75f, 0.95f, 0.75f, 1.0f) : ImVec4(1.0f, 0.55f, 0.5f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::Text("[%s] %s", entry.timeText.c_str(), entry.target.c_str());
    ImGui::PopStyleColor();
    if (!entry.detail.empty())
      ImGui::TextWrapped("  %s", entry.detail.c_str());
  }
  ImGui::EndChild();
}

void EditorLayer::DrawSettingsModal() {
  if (m_settingsOpen)
    ImGui::OpenPopup("Editor Settings");

  if (!ImGui::BeginPopupModal("Editor Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return;

  ImGui::TextDisabled("Built-in MCP");
  ImGui::Checkbox("Enable built-in MCP", &m_mcpSettingsDraft.enabled);
  ImGui::Checkbox("Auto-start when editor opens", &m_mcpSettingsDraft.autoStart);

  int port = m_mcpSettingsDraft.port;
  if (ImGui::InputInt("Port", &port))
    m_mcpSettingsDraft.port = std::max(1, std::min(65535, port));

  ImGui::Text("Host: %s", Mcp::kDefaultMcpHost);
  m_mcpSettingsDraft.host = Mcp::kDefaultMcpHost;

  const std::string endpoint =
      "http://" + m_mcpSettingsDraft.host + ":" + std::to_string(m_mcpSettingsDraft.port) + "/mcp";
  ImGui::TextWrapped("Endpoint: %s", endpoint.c_str());

  if (!m_mcpSettingsError.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
    ImGui::TextWrapped("%s", m_mcpSettingsError.c_str());
    ImGui::PopStyleColor();
  }

  ImGui::Separator();
  if (ImGui::Button("Apply", ImVec2(120.0f, 0.0f))) {
    std::string err;
    if (m_mcpController.ApplySettings(m_mcpSettingsDraft, &err)) {
      m_mcpSettingsDraft = m_mcpController.GetSettings();
      m_mcpSettingsError.clear();
      m_settingsOpen = false;
      ImGui::CloseCurrentPopup();
    } else {
      m_mcpSettingsError = err;
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
    m_settingsOpen = false;
    m_mcpSettingsDraft = m_mcpController.GetSettings();
    m_mcpSettingsError.clear();
    ImGui::CloseCurrentPopup();
  }

  ImGui::EndPopup();
}

void EditorLayer::DrawViewGimbal(const Camera& cam) {
  ImGuiIO& io = ImGui::GetIO();
  constexpr float kPanelW = 280.0f;
  constexpr float kWinW = 128.0f;
  constexpr float kWinH = 138.0f;
  const float wx = io.DisplaySize.x - kPanelW - kWinW - 10.0f;
  const float wy = 42.0f;

  // Wireframe toggle button — sits just to the left of the gimbal window
  constexpr float kBtnSize = 28.0f;
  const float btnX = wx - kBtnSize - 6.0f;
  const float btnY = wy + (kWinH - kBtnSize) * 0.5f;
  ImGui::SetNextWindowPos(ImVec2(btnX, btnY));
  ImGui::SetNextWindowSize(ImVec2(kBtnSize, kBtnSize));
  ImGui::SetNextWindowBgAlpha(0.0f);
  ImGui::Begin("##wire_btn", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDecoration);
  if (m_wireframeMode)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.15f, 0.90f));
  else
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.22f, 0.30f, 0.80f));
  if (ImGui::Button("[W]", ImVec2(kBtnSize, kBtnSize)))
    m_wireframeMode = !m_wireframeMode;
  ImGui::PopStyleColor();
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Toggle Wireframe");
  ImGui::End();

  ImGui::SetNextWindowPos(ImVec2(wx, wy));
  ImGui::SetNextWindowSize(ImVec2(kWinW, kWinH));
  ImGui::SetNextWindowBgAlpha(0.78f);
  ImGui::Begin("##view_gimbal",
               nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus);

  const ImVec2 wpos = ImGui::GetWindowPos();
  const ImVec2 wsize = ImGui::GetWindowSize();
  m_viewGizmoPickRect.valid = true;
  m_viewGizmoPickRect.minX = wpos.x;
  m_viewGizmoPickRect.minY = wpos.y;
  m_viewGizmoPickRect.maxX = wpos.x + wsize.x;
  m_viewGizmoPickRect.maxY = wpos.y + wsize.y;

  ImGui::TextUnformatted("View");

  const int idx = PrimaryIdx();

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 inner0 = ImGui::GetWindowContentRegionMin();
  const ImVec2 inner1 = ImGui::GetWindowContentRegionMax();
  const ImVec2 center = ImVec2(wpos.x + inner0.x + (inner1.x - inner0.x) * 0.5f,
                               wpos.y + inner0.y + (inner1.y - inner0.y) * 0.5f + 6.0f);

  constexpr float kShaftPx = 36.0f;
  constexpr float kHitPx = 12.0f;
  constexpr float kHitPxSq = kHitPx * kHitPx;

  struct AxisDraw {
    ViewSnap posSnap;
    ViewSnap negSnap;
    Vec3 worldPlus;
    ImU32 col;
    const char* label;
  };
  static const AxisDraw kAxes[] = {
      {ViewSnap::Right, ViewSnap::Left, {1.0f, 0.0f, 0.0f}, IM_COL32(220, 72, 72, 255), "X"},
      {ViewSnap::Top, ViewSnap::Bottom, {0.0f, 1.0f, 0.0f}, IM_COL32(88, 200, 96, 255), "Y"},
      {ViewSnap::Front, ViewSnap::Back, {0.0f, 0.0f, 1.0f}, IM_COL32(82, 148, 255, 255), "Z"},
  };

  // Pre-compute screen directions and view-space depth once per frame.
  // Shared by hover and draw loops — no redundant WorldAxisToScreenDir calls.
  struct AxisCache {
    float dx, dy;  // normalised screen direction of the +axis
    float viewZ;   // view-space Z (positive = toward camera) for depth sort
    int origIdx;
  };
  AxisCache cache[3];
  for (int i = 0; i < 3; i++) {
    float vz = 0.f;
    WorldAxisToScreenDir(cam, kAxes[i].worldPlus, &cache[i].dx, &cache[i].dy, &vz);
    cache[i].viewZ  = vz;
    cache[i].origIdx = i;
  }

  ViewSnap hoverSnap = ViewSnap::None;

  if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
    const ImVec2 mouse = io.MousePos;
    float bestD = kHitPxSq;  // was kHitPxSq * 4.f — that doubled the effective hit radius
    const float cx = center.x, cy = center.y;
    for (int i = 0; i < 3; i++) {
      const AxisDraw& ad = kAxes[cache[i].origIdx];
      const float px1 = cx + cache[i].dx * kShaftPx;
      const float py1 = cy + cache[i].dy * kShaftPx;
      const float px2 = cx - cache[i].dx * kShaftPx;
      const float py2 = cy - cache[i].dy * kShaftPx;
      const float d1 = DistSqPointSegment2D(mouse.x, mouse.y, cx, cy, px1, py1);
      const float d2 = DistSqPointSegment2D(mouse.x, mouse.y, cx, cy, px2, py2);
      if (d1 < bestD) { bestD = d1; hoverSnap = ad.posSnap; }
      if (d2 < bestD) { bestD = d2; hoverSnap = ad.negSnap; }
    }
  }

  // Sort ascending by view-space Z so background axes draw first (painter's algorithm).
  std::sort(std::begin(cache), std::end(cache),
            [](const AxisCache& a, const AxisCache& b) { return a.viewZ < b.viewZ; });

  const float fs = ImGui::GetFontSize();
  const float cx = center.x, cy = center.y;
  for (int si = 0; si < 3; si++) {
    const AxisCache& ac = cache[si];
    const AxisDraw& ad = kAxes[ac.origIdx];
    const float px1 = cx + ac.dx * kShaftPx;
    const float py1 = cy + ac.dy * kShaftPx;
    const float px2 = cx - ac.dx * kShaftPx;
    const float py2 = cy - ac.dy * kShaftPx;

    const bool hlPos = (hoverSnap == ad.posSnap);
    const bool hlNeg = (hoverSnap == ad.negSnap);
    ImU32 cPos = ad.col;
    ImU32 cNeg = ad.col;
    switch (ad.posSnap) {
      case ViewSnap::Right: cNeg = IM_COL32(150, 48, 48, 255); break;
      case ViewSnap::Top:   cNeg = IM_COL32(58, 145, 64, 255); break;
      case ViewSnap::Front: cNeg = IM_COL32(52, 100, 190, 255); break;
      default: break;
    }
    if (hlPos) cPos = IM_COL32(255, 255, 200, 255);
    if (hlNeg) cNeg = IM_COL32(255, 255, 200, 255);

    dl->AddLine(center, ImVec2(px1, py1), cPos, hlPos ? 4.0f : 3.0f);
    dl->AddLine(center, ImVec2(px2, py2), cNeg, hlNeg ? 4.0f : 2.5f);
    dl->AddCircleFilled(ImVec2(px1, py1), hlPos ? 5.0f : 4.0f, cPos, 10);
    dl->AddCircleFilled(ImVec2(px2, py2), hlNeg ? 4.5f : 3.5f, cNeg, 10);

    // Offset label along the axis direction so it doesn't overlap the circle
    // regardless of which way the axis is pointing on screen.
    const float tDist = (hlPos ? 5.0f : 4.0f) + 3.0f;
    dl->AddText(ImVec2(px1 + ac.dx * tDist - fs * 0.3f,
                       py1 + ac.dy * tDist - fs * 0.5f),
                cPos, ad.label);
  }

  if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && ImGui::IsMouseClicked(0) &&
      hoverSnap != ViewSnap::None)
    m_pendingViewSnap = hoverSnap;

  if (idx < 0 || idx >= static_cast<int>(m_document.objects.size()))
    ImGui::TextDisabled("Pivot: origin");

  ImGui::End();
}

// ---- Hierarchy panel ---------------------------------------------------------

// Short icon prefix shown before each object in the hierarchy tree.
static const char* ObjectTypeIcon(SceneObjectType type) {
  switch (type) {
    case SceneObjectType::Prop:   return "[>]";
    case SceneObjectType::Light:  return "[*]";
    case SceneObjectType::Camera: return "[C]";
    default:                      return "[=]";  // Panel / board
  }
}

void EditorLayer::DrawObjectList() {
  ImGuiIO& io = ImGui::GetIO();
  float objTop = 0.0f;
  float objH = 0.0f;
  float asTop = 0.0f;
  float asH = 0.0f;
  ComputeLeftColumnLayout(io, &objTop, &objH, &asTop, &asH);

  ImGui::SetNextWindowPos(ImVec2(0.0f, objTop));
  ImGui::SetNextWindowSize(ImVec2(kLeftDockW, objH));
  ImGui::SetNextWindowBgAlpha(0.85f);
  ImGui::Begin(
      "Hierarchy", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);

  // Search bar (applies to primary scene)
  char searchBuf[256] = {};
  std::snprintf(searchBuf, sizeof(searchBuf), "%s", m_objectSearchQuery.c_str());
  ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, true);
  if (ImGui::InputTextWithHint("##object_search", "Search objects...", searchBuf, sizeof(searchBuf)))
    m_objectSearchQuery = searchBuf;
  ImGui::PopItemFlag();
  ImGui::Separator();

  // Primary scene
  DrawSceneHeader(m_document, true, -1);

  // Right-click empty space → add object to primary scene
  if (ImGui::BeginPopupContextWindow("obj_ctx_empty",
                                     ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
    if (ImGui::BeginMenu("Add")) {
      if (ImGui::MenuItem("Panel"))
        AddObject(SceneObjectType::Panel);
      if (ImGui::MenuItem("Prop"))
        AddObject(SceneObjectType::Prop);
      if (ImGui::MenuItem("Light"))
        AddObject(SceneObjectType::Light);
      if (ImGui::MenuItem("Camera"))
        AddObject(SceneObjectType::Camera);
      ImGui::EndMenu();
    }
    ImGui::EndPopup();
  }

  if (m_renameObjectOpen) {
    ImGui::OpenPopup("Rename Object");
    m_renameObjectOpen = false;
  }
  if (ImGui::BeginPopupModal("Rename Object", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    char nameBuf[256] = {};
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", m_renameObjectDraft.c_str());
    if (ImGui::InputText("New ID", nameBuf, sizeof(nameBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
      m_renameObjectDraft = nameBuf;
    } else if (std::strncmp(nameBuf, m_renameObjectDraft.c_str(), sizeof(nameBuf)) != 0) {
      m_renameObjectDraft = nameBuf;
    }

    if (!m_renameObjectError.empty())
      ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", m_renameObjectError.c_str());

    bool applyRequested = false;
    if (ImGui::Button("Apply"))
      applyRequested = true;
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      m_renameObjectError.clear();
      m_renameObjectIndex = -1;
      ImGui::CloseCurrentPopup();
    }

    if (applyRequested) {
      if (m_renameObjectIndex < 0 || m_renameObjectIndex >= static_cast<int>(m_document.objects.size())) {
        m_renameObjectError = "Selected object is no longer valid.";
      } else if (m_renameObjectDraft.empty()) {
        m_renameObjectError = "ID cannot be empty.";
      } else {
        const int existingIdx = FindObjectIndexById(m_document, m_renameObjectDraft);
        if (existingIdx >= 0 && existingIdx != m_renameObjectIndex) {
          m_renameObjectError = "ID already exists.";
        } else {
          SceneObject& target = m_document.objects[static_cast<size_t>(m_renameObjectIndex)];
          const std::string oldId = target.id;
          const std::string newId = m_renameObjectDraft;
          if (oldId != newId) {
            target.id = newId;
            for (auto& other : m_document.objects) {
              auto p = other.props.find("parentId");
              if (p != other.props.end() && p->second == oldId)
                p->second = newId;
              auto f = other.props.find("followTargetId");
              if (f != other.props.end() && f->second == oldId)
                f->second = newId;
            }
            m_document.dirty = true;
          }
          m_renameObjectError.clear();
          m_renameObjectIndex = -1;
          ImGui::CloseCurrentPopup();
        }
      }
    }

    ImGui::EndPopup();
  }

  ImGui::End();
}

// Renders a Unity-style collapsible scene header, then the objects tree under it.
void EditorLayer::DrawSceneHeader(SceneDocument& doc, bool isPrimary, int additionalIndex) {
  // Build label: sceneName + dirty indicator for primary scene
  std::string label = doc.sceneName.empty() ? "Scene" : doc.sceneName;
  if (isPrimary && doc.dirty)
    label += " *";

  // Tint the primary scene header slightly to distinguish it
  if (isPrimary)
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.33f, 0.52f, 1.0f));

  ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  char nodeId[32];
  std::snprintf(nodeId, sizeof(nodeId), isPrimary ? "##scene_primary" : "##scene_%d", additionalIndex);
  const bool open = ImGui::TreeNodeEx(
      nodeId,
      ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
          ImGuiTreeNodeFlags_DefaultOpen,
      "%s", label.c_str());

  if (isPrimary)
    ImGui::PopStyleColor();

  // Scene header context menu
  char ctxId[40];
  std::snprintf(ctxId, sizeof(ctxId), isPrimary ? "scene_hdr_ctx_p" : "scene_hdr_ctx_%d",
                additionalIndex);
  if (ImGui::BeginPopupContextItem(ctxId)) {
    if (isPrimary) {
      if (ImGui::BeginMenu("Add")) {
        if (ImGui::MenuItem("Panel"))
          AddObject(SceneObjectType::Panel);
        if (ImGui::MenuItem("Prop"))
          AddObject(SceneObjectType::Prop);
        if (ImGui::MenuItem("Light"))
          AddObject(SceneObjectType::Light);
        if (ImGui::MenuItem("Camera"))
          AddObject(SceneObjectType::Camera);
        ImGui::EndMenu();
      }
      ImGui::Separator();
      std::string saveErr;
      if (ImGui::MenuItem("Save Scene"))
        SaveDocument(&saveErr);
    } else {
      std::string saveErr;
      if (ImGui::MenuItem("Save Scene"))
        SaveAdditionalScene(additionalIndex, &saveErr);
      ImGui::Separator();
      if (ImGui::MenuItem("Close Scene"))
        CloseAdditionalScene(additionalIndex);
    }
    ImGui::EndPopup();
  }

  // Drop onto scene header → unparent the dragged object (make it a root)
  if (isPrimary && ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT_INDEX")) {
      if (payload->DataSize == sizeof(int)) {
        const int src = *static_cast<const int*>(payload->Data);
        if (src >= 0 && src < static_cast<int>(doc.objects.size())) {
          doc.objects[static_cast<size_t>(src)].props.erase("parentId");
          doc.dirty = true;
        }
      }
    }
    ImGui::EndDragDropTarget();
  }

  if (open) {
    DrawObjectsTree(doc, isPrimary);
    ImGui::TreePop();
  }
}

// Renders the tree of objects inside a scene, plus runtime entities for the primary scene.
void EditorLayer::DrawObjectsTree(SceneDocument& doc, bool isPrimary) {
  // Search filter applies only to the primary scene
  const std::string& query = isPrimary ? m_objectSearchQuery : "";

  int shownObjectCount = 0;

  if (!query.empty()) {
    // Flat filtered list (search mode, primary scene only)
    for (int i = 0; i < static_cast<int>(doc.objects.size()); ++i) {
      ImGui::PushID(i);
      auto& obj = doc.objects[static_cast<size_t>(i)];
      if (!ObjectMatchesQuickOpenQuery(obj, query)) {
        ImGui::PopID();
        continue;
      }
      const char* icon = ObjectTypeIcon(obj.type);
      const char* typeName = ObjectTypeLabel(obj.type);

      char selectableId[32];
      std::snprintf(selectableId, sizeof(selectableId), "##obj_%d", i);

      const float lineH = ImGui::GetTextLineHeight();
      const float rowH = obj.assetId.empty() ? lineH : lineH * 2.0f + 2.0f;

      if (ImGui::Selectable(selectableId, IsSelected(i), 0, ImVec2(0, rowH))) {
        auto& selIo = ImGui::GetIO();
        if (selIo.KeyShift && m_lastClickedHierarchyIdx >= 0) {
          const int lo = std::min(m_lastClickedHierarchyIdx, i);
          const int hi = std::max(m_lastClickedHierarchyIdx, i);
          m_selectedIndices.clear();
          for (int ri = lo; ri <= hi; ++ri)
            m_selectedIndices.push_back(ri);
        } else if (selIo.KeyCtrl || selIo.KeySuper) {
          ToggleSelect(i);
          m_lastClickedHierarchyIdx = i;
        } else {
          m_selectedIndices = {i};
          m_lastClickedHierarchyIdx = i;
        }
      }
      ImGui::SameLine();
      {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::BeginGroup();
        ImGui::TextDisabled("%s %s", icon, typeName);
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::Text("%s", obj.id.c_str());
        if (!obj.assetId.empty()) {
          ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + lineH + 2.0f));
          ImGui::TextDisabled("  > %s", obj.assetId.c_str());
        }
        ImGui::EndGroup();
      }

      if (ImGui::BeginPopupContextItem("obj_ctx")) {
        if (ImGui::BeginMenu("Add")) {
          if (ImGui::MenuItem("Panel"))
            AddObject(SceneObjectType::Panel, obj.id);
          if (ImGui::MenuItem("Prop"))
            AddObject(SceneObjectType::Prop, obj.id);
          if (ImGui::MenuItem("Light"))
            AddObject(SceneObjectType::Light, obj.id);
          if (ImGui::MenuItem("Camera"))
            AddObject(SceneObjectType::Camera, obj.id);
          ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Rename..."))
          OpenRenameObjectModal(i);
        if (ImGui::MenuItem("Duplicate")) {
          m_selectedIndices = {i};
          DuplicatePrimarySelection();
        }
        if (ImGui::MenuItem("Delete")) {
          m_selectedIndices = {i};
          RequestDeleteSelectedObjects();
        }
        ImGui::EndPopup();
      }

      ++shownObjectCount;
      ImGui::PopID();
    }

    if (shownObjectCount == 0 && !doc.objects.empty()) {
      ImGui::TextDisabled("No objects match '%s'", query.c_str());
      if (ImGui::Button("Clear Search"))
        m_objectSearchQuery.clear();
    }
  } else {
    // Tree view
    std::unordered_map<std::string, int> idToIndex;
    idToIndex.reserve(doc.objects.size());
    for (int i = 0; i < static_cast<int>(doc.objects.size()); ++i)
      idToIndex[doc.objects[static_cast<size_t>(i)].id] = i;

    std::vector<std::vector<int>> children(doc.objects.size());
    std::vector<int> roots;
    roots.reserve(doc.objects.size());
    for (int i = 0; i < static_cast<int>(doc.objects.size()); ++i) {
      const SceneObject& obj = doc.objects[static_cast<size_t>(i)];
      int p = -1;
      const auto pIt = idToIndex.find(GetParentId(obj));
      if (pIt != idToIndex.end())
        p = pIt->second;
      if (p >= 0 && p != i)
        children[static_cast<size_t>(p)].push_back(i);
      else
        roots.push_back(i);
    }

    std::function<void(int)> drawNode;
    drawNode = [&](int idx) {
      if (idx < 0 || idx >= static_cast<int>(doc.objects.size()))
        return;
      SceneObject& obj = doc.objects[static_cast<size_t>(idx)];
      ++shownObjectCount;

      ImGui::PushID(idx);
      const bool hasChildren = !children[static_cast<size_t>(idx)].empty();
      ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
      if (!hasChildren)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
      if (isPrimary && IsSelected(idx))
        flags |= ImGuiTreeNodeFlags_Selected;

      const char* icon = ObjectTypeIcon(obj.type);
      std::string nodeLabel = std::string(icon) + " " + obj.id;
      if (!obj.assetId.empty())
        nodeLabel += "  [" + obj.assetId + "]";

      const bool open = ImGui::TreeNodeEx("##obj_tree", flags, "%s", nodeLabel.c_str());

      if (isPrimary && ImGui::IsItemClicked()) {
        auto& treeIo = ImGui::GetIO();
        if (treeIo.KeyShift && m_lastClickedHierarchyIdx >= 0) {
          const int lo = std::min(m_lastClickedHierarchyIdx, idx);
          const int hi = std::max(m_lastClickedHierarchyIdx, idx);
          m_selectedIndices.clear();
          for (int ri = lo; ri <= hi; ++ri)
            m_selectedIndices.push_back(ri);
        } else if (treeIo.KeyCtrl || treeIo.KeySuper) {
          ToggleSelect(idx);
          m_lastClickedHierarchyIdx = idx;
        } else {
          m_selectedIndices = {idx};
          m_lastClickedHierarchyIdx = idx;
        }
      }

      if (isPrimary) {
        if (ImGui::BeginDragDropSource()) {
          ImGui::SetDragDropPayload("SCENE_OBJECT_INDEX", &idx, sizeof(int));
          ImGui::TextUnformatted(obj.id.c_str());
          ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
          if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT_INDEX")) {
            if (payload->DataSize == sizeof(int)) {
              const int src = *static_cast<const int*>(payload->Data);
              const bool valid = src >= 0 && src < static_cast<int>(doc.objects.size()) &&
                                 src != idx && !IsDescendantOf(doc, idx, src);
              if (valid) {
                doc.objects[static_cast<size_t>(src)].props["parentId"] = obj.id;
                doc.dirty = true;
              }
            }
          }
          ImGui::EndDragDropTarget();
        }

        if (ImGui::BeginPopupContextItem("obj_ctx")) {
          if (ImGui::BeginMenu("Add")) {
            if (ImGui::MenuItem("Panel"))
              AddObject(SceneObjectType::Panel, obj.id);
            if (ImGui::MenuItem("Prop"))
              AddObject(SceneObjectType::Prop, obj.id);
            if (ImGui::MenuItem("Light"))
              AddObject(SceneObjectType::Light, obj.id);
            if (ImGui::MenuItem("Camera"))
              AddObject(SceneObjectType::Camera, obj.id);
            ImGui::EndMenu();
          }
          ImGui::Separator();
          if (ImGui::MenuItem("Rename..."))
            OpenRenameObjectModal(idx);
          if (ImGui::MenuItem("Duplicate")) {
            m_selectedIndices = {idx};
            DuplicatePrimarySelection();
          }
          if (ImGui::MenuItem("Delete")) {
            m_selectedIndices = {idx};
            RequestDeleteSelectedObjects();
          }
          ImGui::Separator();
          const bool hasParent = !GetParentId(obj).empty();
          if (ImGui::MenuItem("Unparent", nullptr, false, hasParent)) {
            obj.props.erase("parentId");
            doc.dirty = true;
          }
          ImGui::EndPopup();
        }
      }

      if (open && hasChildren) {
        for (int childIdx : children[static_cast<size_t>(idx)])
          drawNode(childIdx);
        ImGui::TreePop();
      }

      ImGui::PopID();
    };

    for (int rootIdx : roots)
      drawNode(rootIdx);

    if (doc.objects.empty()) {
      ImGui::TextDisabled("No objects in scene");
      if (isPrimary)
        ImGui::TextDisabled("Tip: right-click or use Assets panel.");
    }

    // Runtime entities: ECS entities that have no corresponding editor object (primary only)
    if (isPrimary && m_liveRegistry) {
      std::unordered_set<Entity> docEntities;
      for (const auto& obj : doc.objects) {
        const auto it = obj.props.find("_eid");
        if (it != obj.props.end()) {
          try {
            docEntities.insert(static_cast<Entity>(std::stoul(it->second)));
          } catch (...) {}
        }
      }

      std::vector<Entity> runtimeEntities;
      for (Entity e : m_liveRegistry->GetEntities<TransformComponent>()) {
        if (docEntities.find(e) == docEntities.end())
          runtimeEntities.push_back(e);
      }

      if (!runtimeEntities.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.65f, 0.65f, 1.0f));
        if (ImGui::TreeNodeEx("##runtime_entities",
                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth,
                              "[rt] Runtime (%zu)", runtimeEntities.size())) {
          for (Entity e : runtimeEntities) {
            ImGui::PushID(static_cast<int>(e));
            ImGui::TreeNodeEx(
                "##rt_ent",
                ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                    ImGuiTreeNodeFlags_SpanAvailWidth,
                "[>] Entity %u", static_cast<unsigned>(e));
            ImGui::PopID();
          }
          ImGui::TreePop();
        }
        ImGui::PopStyleColor();
      }
    }
  }
}

// ---- Multi-scene helpers -----------------------------------------------------

void EditorLayer::AddNewScene() {
  SceneDocument newDoc;
  newDoc.sceneId = "scene";
  newDoc.sceneName = "Scene";
  newDoc.dirty = true;
  m_document = std::move(newDoc);
  m_selectedIndices.clear();
  m_selectedAssetId.clear();
}

void EditorLayer::OpenAdditionalSceneFile() {
#ifdef _WIN32
  char filePath[MAX_PATH] = {};
  OPENFILENAMEA ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = nullptr;
  ofn.lpstrFilter = "Scene Files\0*.horo;*.json\0All Files\0*.*\0";
  ofn.lpstrFile = filePath;
  ofn.nMaxFile = MAX_PATH;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  if (GetOpenFileNameA(&ofn)) {
    try {
      SceneDocument doc = SceneSerializer::LoadFromFile(filePath);
      m_document = std::move(doc);
      m_lastSavedDocument = m_document;
      m_selectedIndices.clear();
      m_selectedAssetId.clear();
    } catch (const std::exception& e) {
      LOG_ERROR("EditorLayer: failed to open scene '{}': {}", filePath, e.what());
    }
  }
#endif
}

void EditorLayer::CloseAdditionalScene(int index) {
  if (index < 0 || index >= static_cast<int>(m_additionalScenes.size()))
    return;
  m_additionalScenes.erase(m_additionalScenes.begin() + index);
}

bool EditorLayer::SaveAdditionalScene(int index, std::string* outError) {
  if (index < 0 || index >= static_cast<int>(m_additionalScenes.size()))
    return false;
  SceneDocument& doc = m_additionalScenes[static_cast<size_t>(index)];
  if (doc.filePath.empty()) {
    if (outError)
      *outError = "No file path — use Save As.";
    return false;
  }
  try {
    SceneSerializer::SaveToFile(doc, doc.filePath);
    doc.dirty = false;
    return true;
  } catch (const std::exception& e) {
    if (outError)
      *outError = e.what();
    return false;
  }
}

void EditorLayer::DrawAssetsPanel() {
  ImGuiIO& io = ImGui::GetIO();
  float objTop = 0.0f;
  float objH = 0.0f;
  float assetsTop = 0.0f;
  float assetsH = 0.0f;
  ComputeLeftColumnLayout(io, &objTop, &objH, &assetsTop, &assetsH);

  ImGui::SetNextWindowPos(ImVec2(0.0f, assetsTop));
  ImGui::SetNextWindowSize(ImVec2(kLeftDockW, assetsH));
  ImGui::SetNextWindowBgAlpha(0.85f);
  ImGui::Begin(
      "Assets", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);

  m_albedoDraftDrop.Clear();
  m_albedoSelDrop.Clear();

  ImGui::TextDisabled("Assets");
  ImGui::SameLine();
  if (ImGui::Button("+", ImVec2(28.0f, 0.0f)))
    m_openNewAssetHeader = true;
  ImGui::SameLine();
  ImGui::SetCursorPosX(kLeftDockW - 74.0f);
  if (ImGui::Button("Search", ImVec2(64.0f, 0.0f))) {
    m_assetSearchOpen = true;
    m_assetSearchQuery.clear();
  }
  ImGui::Separator();

  std::vector<std::string> assetIds;
  assetIds.reserve(m_document.assets.size());
  for (const auto& [assetId, _] : m_document.assets)
    assetIds.push_back(assetId);
  std::sort(assetIds.begin(), assetIds.end());

  if (m_assetSearchOpen) {
    ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Asset Spotlight", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextDisabled("Search assets");
      ImGui::SetNextItemWidth(420.0f);
      char searchBuf[256] = {};
      std::snprintf(searchBuf, sizeof(searchBuf), "%s", m_assetSearchQuery.c_str());
      if (ImGui::InputTextWithHint("##asset_search_input", "Type an asset id or mesh...", searchBuf, sizeof(searchBuf)))
        m_assetSearchQuery = searchBuf;

      ImGui::Separator();
      bool picked = false;
      for (const auto& assetId : assetIds) {
        const auto assetIt = m_document.assets.find(assetId);
        if (assetIt == m_document.assets.end())
          continue;
        const auto& asset = assetIt->second;

        if (!AssetMatchesQuickOpenQuery(assetId, asset, m_assetSearchQuery))
          continue;

        if (ImGui::Selectable(assetId.c_str(), m_selectedAssetId == assetId)) {
          m_selectedAssetId = assetId;
          picked = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("mesh: %s", asset.mesh.c_str());
      }

      if (picked || ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        m_assetSearchOpen = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    } else {
      ImGui::OpenPopup("Asset Spotlight");
    }
  }

  bool openNewAssetModal = m_openNewAssetHeader;
  if (m_openNewAssetHeader)
    m_openNewAssetHeader = false;

  ImGui::BeginChild("##asset_grid", ImVec2(0, 0), false);

  const float spacing = 8.0f;
  const float minTileW = 130.0f;
  const float availW = std::max(40.0f, ImGui::GetContentRegionAvail().x);
  const int columns = std::max(1, static_cast<int>((availW + spacing) / (minTileW + spacing)));
  const float tileW = (availW - spacing * static_cast<float>(columns - 1)) / static_cast<float>(columns);
  const float thumbPad = 8.0f;
  const float thumbSize = tileW - thumbPad * 2.0f;  // square preview area
  const float tileH = thumbSize + thumbPad * 2.0f + 22.0f;  // thumb + pads + label row

  int shownAssetCount = 0;
  for (const auto& assetId : assetIds) {
    const auto assetIt = m_document.assets.find(assetId);
    if (assetIt == m_document.assets.end())
      continue;
    const auto& asset = assetIt->second;

    if (!AssetMatchesQuickOpenQuery(assetId, asset, m_assetSearchQuery))
      continue;

    ++shownAssetCount;

    const int visibleIndex = shownAssetCount - 1;
    const int col = visibleIndex % columns;
    if (col > 0)
      ImGui::SameLine(0.0f, spacing);

    ImGui::PushID(assetId.c_str());
    const bool isSelectedAsset = (m_selectedAssetId == assetId);
    if (isSelectedAsset)
      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.14f, 0.24f, 0.34f, 0.70f));
    ImGui::BeginChild("##asset_tile", ImVec2(tileW, tileH), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (isSelectedAsset)
      ImGui::PopStyleColor();

    ImGui::InvisibleButton("##asset_tile_select", ImVec2(tileW - 2.0f, tileH - 2.0f));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
      m_selectedAssetId = isSelectedAsset ? std::string() : assetId;
    }

    // Drag source: allows dropping onto the 3D viewport to create a prop
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
      ImGui::SetDragDropPayload("ASSET_ID", assetId.c_str(),
                                assetId.size() + 1);
      ImGui::Text("+ %s", assetId.c_str());
      ImGui::EndDragDropSource();
    }

    if (ImGui::BeginPopupContextItem("##asset_tile_ctx")) {
      if (ImGui::MenuItem("Add Prop")) {
        SceneObject obj = MakeObjectFromAsset(m_document, assetId, m_schema);
        m_document.objects.push_back(std::move(obj));
        m_selectedIndices = {static_cast<int>(m_document.objects.size()) - 1};
        m_document.dirty = true;
        TriggerReload();
      }
      if (ImGui::MenuItem("Delete Asset"))
        RequestDeleteAsset(assetId);
      ImGui::EndPopup();
    }

    const ImVec2 tileMin = ImGui::GetWindowPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 thumbMin(tileMin.x + thumbPad, tileMin.y + thumbPad);
    const ImVec2 thumbMax(thumbMin.x + thumbSize, thumbMin.y + thumbSize);

    dl->AddRectFilled(thumbMin, thumbMax, ImGui::ColorConvertFloat4ToU32(ImVec4(0.10f, 0.13f, 0.18f, 0.95f)),
                      6.0f);
    dl->AddRect(thumbMin, thumbMax, ImGui::ColorConvertFloat4ToU32(ImVec4(0.26f, 0.34f, 0.46f, 1.0f)), 6.0f);

    unsigned int previewTexId = 0;
    bool previewNeedsYFlip = false;
    if (TryGetAssetPreviewTextureId(assetId, asset, &previewTexId, &previewNeedsYFlip) && previewTexId != 0) {
      const ImVec2 uv0 = previewNeedsYFlip ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
      const ImVec2 uv1 = previewNeedsYFlip ? ImVec2(1.0f, 0.0f) : ImVec2(1.0f, 1.0f);
      dl->AddImage(ToImTextureId(previewTexId), thumbMin, thumbMax, uv0, uv1);
    } else {
      const ImU32 labelCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.67f, 0.74f, 0.84f, 0.95f));
      const ImU32 meshCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.55f, 0.64f, 0.75f, 0.95f));
      const std::string ext = asset.mesh.empty() ? std::string("mesh") :
                              ToLowerAscii(std::filesystem::path(asset.mesh).extension().string());
      dl->AddText(ImVec2(thumbMin.x + 8.0f, thumbMin.y + 10.0f), labelCol, "No preview texture");
      dl->AddText(ImVec2(thumbMin.x + 8.0f, thumbMin.y + 30.0f), meshCol, ext.c_str());
    }

    std::string nameLabel = assetId;
    const float maxLabelW = tileW - 14.0f;
    if (ImGui::CalcTextSize(nameLabel.c_str()).x > maxLabelW) {
      while (!nameLabel.empty() && ImGui::CalcTextSize((nameLabel + "...").c_str()).x > maxLabelW)
        nameLabel.pop_back();
      nameLabel += "...";
    }
    const ImVec2 nameSz = ImGui::CalcTextSize(nameLabel.c_str());
    const float nameX = tileMin.x + std::max(7.0f, (tileW - nameSz.x) * 0.5f);
    const float nameY = thumbMax.y + 6.0f;
    dl->AddText(ImVec2(nameX, nameY), ImGui::GetColorU32(ImGuiCol_Text), nameLabel.c_str());

    ImGui::EndChild();
    ImGui::PopID();
  }

  const FilteredListState assetState =
      EvaluateFilteredListState(assetIds.size(), shownAssetCount, m_assetSearchQuery);
  if (assetState != FilteredListState::None) {
    ImGui::Spacing();
    if (assetState == FilteredListState::EmptyData) {
      ImGui::TextDisabled("Asset registry is empty");
      ImGui::TextDisabled("Create your first asset to enable fast prop placement.");
      if (ImGui::Button("Create First Asset"))
        openNewAssetModal = true;
    } else if (assetState == FilteredListState::NoMatches) {
      ImGui::TextDisabled("No assets match '%s'", m_assetSearchQuery.c_str());
      if (ImGui::Button("Clear Asset Search"))
        m_assetSearchQuery.clear();
    }
  }

  ImGui::EndChild();

  if (openNewAssetModal)
    ImGui::OpenPopup("Create Asset");
  ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal("Create Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    const float blockW = 470.0f;
    ImGui::PushItemWidth(blockW);

    if (ImGui::Button("Import .obj...", ImVec2(blockW, 0.0f))) {
      m_assetImportError.clear();
#if !defined(_WIN32) && !defined(__APPLE__)
      m_assetImportError = "Import dialog is not supported on this platform yet.";
#else
      m_deferredFilePick = DeferredFilePick::ImportObjBulk;
#endif
    }
    if (!m_assetImportError.empty()) {
      ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + blockW);
      ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "%s", m_assetImportError.c_str());
      ImGui::PopTextWrapPos();
    }

    ImGui::Spacing();

    char idBuf[128] = {};
    std::snprintf(idBuf, sizeof(idBuf), "%s", m_assetDraftId.c_str());
    ImGui::TextDisabled("Asset ID");
    if (ImGui::InputText("##draft_id", idBuf, sizeof(idBuf)))
      m_assetDraftId = idBuf;

    char meshBuf[256] = {};
    std::snprintf(meshBuf, sizeof(meshBuf), "%s", m_assetDraftMesh.c_str());
    ImGui::TextDisabled("Mesh");
    if (ImGui::InputText("##draft_mesh", meshBuf, sizeof(meshBuf)))
      m_assetDraftMesh = meshBuf;

    char scaleBuf[128] = {};
    std::snprintf(scaleBuf, sizeof(scaleBuf), "%s", m_assetDraftRenderScale.c_str());
    ImGui::TextDisabled("Render scale");
    if (ImGui::InputText("##draft_scale", scaleBuf, sizeof(scaleBuf)))
      m_assetDraftRenderScale = scaleBuf;

    char albDraftBuf[512] = {};
    std::snprintf(albDraftBuf, sizeof(albDraftBuf), "%s", m_assetDraftAlbedoMap.c_str());
    ImGui::TextDisabled("Albedo map (optional)");
    const ImVec2 draftAlbLabelMin = ImGui::GetItemRectMin();
    const ImVec2 draftAlbLabelMax = ImGui::GetItemRectMax();
    if (ImGui::InputText("##draft_albedo", albDraftBuf, sizeof(albDraftBuf)))
      m_assetDraftAlbedoMap = albDraftBuf;
    {
      const ImVec2 fMin = ImGui::GetItemRectMin();
      const ImVec2 fMax = ImGui::GetItemRectMax();
      m_albedoDraftDrop.valid = true;
      m_albedoDraftDrop.minX = std::min(draftAlbLabelMin.x, fMin.x);
      m_albedoDraftDrop.minY = draftAlbLabelMin.y;
      m_albedoDraftDrop.maxX = std::max(draftAlbLabelMax.x, fMax.x);
      m_albedoDraftDrop.maxY = fMax.y;
    }

#if defined(_WIN32) || defined(__APPLE__)
    if (ImGui::Button("Browse texture...##alb_pick_draft", ImVec2(blockW, 0.0f)))
      m_deferredFilePick = DeferredFilePick::NewAssetAlbedo;
#else
    ImGui::BeginDisabled();
    ImGui::Button("Browse texture...##alb_pick_draft", ImVec2(blockW, 0.0f));
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("Texture file dialog is not available on this platform.");
#endif

    ImGui::Spacing();

    const bool canCreate = !m_assetDraftId.empty() && !m_assetDraftMesh.empty();
    if (!canCreate)
      ImGui::BeginDisabled();
    if (ImGui::Button("Create Asset", ImVec2(150.0f, 0.0f))) {
      AssetDef def;
      def.mesh = m_assetDraftMesh;
      def.renderScale = m_assetDraftRenderScale.empty() ? "1.0000,1.0000,1.0000" : m_assetDraftRenderScale;
      def.albedoMap = m_assetDraftAlbedoMap;
      m_document.assets[m_assetDraftId] = std::move(def);
      m_selectedAssetId = m_assetDraftId;
      m_assetDraftId.clear();
      m_assetDraftMesh.clear();
      m_assetDraftRenderScale = "1.0000,1.0000,1.0000";
      m_assetDraftAlbedoMap.clear();
      m_assetImportError.clear();
      m_document.dirty = true;
      TriggerReload();
      ImGui::CloseCurrentPopup();
    }
    if (!canCreate)
      ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(150.0f, 0.0f))) {
      m_assetImportError.clear();
      ImGui::CloseCurrentPopup();
    }

    ImGui::PopItemWidth();
    ImGui::EndPopup();
  }

  ImGui::End();
}

void EditorLayer::DrawHelpPopup() {
  if (!m_helpOpen)
    return;
  const std::span<const ShortcutRow> shortcuts = GetEditorShortcuts();

  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowSize(ImVec2(620.0f, 420.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - 620.0f) * 0.5f, (io.DisplaySize.y - 420.0f) * 0.5f),
                          ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("Help - Keyboard Shortcuts", &m_helpOpen, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  ImGui::TextDisabled("Search by category, command, or key");
  char searchBuf[256] = {};
  std::snprintf(searchBuf, sizeof(searchBuf), "%s", m_helpSearchQuery.c_str());
  if (ImGui::InputTextWithHint("##shortcut_search", "Find shortcut...", searchBuf, sizeof(searchBuf)))
    m_helpSearchQuery = searchBuf;

  ImGui::Separator();
  ImGui::Columns(3, "shortcut_columns", false);
  ImGui::SetColumnWidth(0, 130.0f);
  ImGui::SetColumnWidth(1, 300.0f);
  ImGui::TextUnformatted("Category");
  ImGui::NextColumn();
  ImGui::TextUnformatted("Command");
  ImGui::NextColumn();
  ImGui::TextUnformatted("Shortcut");
  ImGui::NextColumn();
  ImGui::Separator();

  int shownCount = 0;
  for (const auto& row : shortcuts) {
    if (!MatchesShortcutQuery(row, m_helpSearchQuery))
      continue;

    ImGui::TextDisabled("%s", row.category);
    ImGui::NextColumn();
    ImGui::TextUnformatted(row.command);
    ImGui::NextColumn();
    ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f), "%s", row.keys);
    ImGui::NextColumn();
    ++shownCount;
  }

  ImGui::Columns(1);
  if (shownCount == 0)
    ImGui::TextDisabled("No shortcut matches '%s'", m_helpSearchQuery.c_str());

  ImGui::Separator();
  ImGui::TextDisabled("Tip: press ? or F1 to close this window quickly.");
  ImGui::End();
}

void EditorLayer::DrawQuickOpenPopup() {
  if (m_quickOpenOpen) {
    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::IsPopupOpen("Quick Open"))
      ImGui::OpenPopup("Quick Open");
  }

  if (!ImGui::BeginPopupModal("Quick Open", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return;

  ImGui::TextDisabled("Open object or asset");
  ImGui::SetNextItemWidth(520.0f);
  char queryBuf[256] = {};
  std::snprintf(queryBuf, sizeof(queryBuf), "%s", m_quickOpenQuery.c_str());
  if (ImGui::InputTextWithHint("##quick_open_input", "Type id, type, asset, or mesh...", queryBuf, sizeof(queryBuf)))
    m_quickOpenQuery = queryBuf;

  ImGui::Separator();

  bool picked = false;
  int shownCount = 0;

  ImGui::TextDisabled("Objects");
  for (int i = 0; i < static_cast<int>(m_document.objects.size()); ++i) {
    const auto& obj = m_document.objects[i];
    const char* typeName = ObjectTypeLabel(obj.type);

    if (!ObjectMatchesQuickOpenQuery(obj, m_quickOpenQuery))
      continue;

    const std::string label = "Object: " + obj.id + "##quick_open_obj_" + std::to_string(i);
    if (ImGui::Selectable(label.c_str(), IsSelected(i))) {
      m_selectedIndices = {i};
      picked = true;
    }
    ImGui::SameLine();
    if (obj.assetId.empty())
      ImGui::TextDisabled("type: %s", typeName);
    else
      ImGui::TextDisabled("type: %s  |  asset: %s", typeName, obj.assetId.c_str());
    ++shownCount;
  }

  ImGui::Spacing();
  ImGui::TextDisabled("Assets");
  for (const auto& [assetId, asset] : m_document.assets) {
    if (!AssetMatchesQuickOpenQuery(assetId, asset, m_quickOpenQuery))
      continue;

    const std::string label = "Asset: " + assetId + "##quick_open_asset_" + assetId;
    if (ImGui::Selectable(label.c_str(), m_selectedAssetId == assetId)) {
      m_selectedAssetId = assetId;
      picked = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("mesh: %s", asset.mesh.c_str());
    ++shownCount;
  }

  if (shownCount == 0)
    ImGui::TextDisabled("No match for '%s'", m_quickOpenQuery.c_str());

  if (picked || ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    m_quickOpenOpen = false;
    ImGui::CloseCurrentPopup();
  }

  ImGui::EndPopup();
}

void EditorLayer::DrawDeleteConfirmModals() {
  if (m_confirmDeleteObjectsOpen)
    ImGui::OpenPopup("Confirm Delete Objects");

  if (ImGui::BeginPopupModal("Confirm Delete Objects", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    int validCount = 0;
    for (int idx : m_pendingDeleteObjectIndices)
      if (idx >= 0 && idx < static_cast<int>(m_document.objects.size()))
        ++validCount;

    if (validCount <= 0) {
      m_confirmDeleteObjectsOpen = false;
      m_pendingDeleteObjectIndices.clear();
      ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
      return;
    }

    ImGui::Text("Delete %d selected object(s)?", validCount);
    ImGui::TextDisabled("This action cannot be undone.");
    ImGui::Separator();

    if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f))) {
      m_confirmDeleteObjectsOpen = false;
      m_pendingDeleteObjectIndices.clear();
      ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Delete", ImVec2(110.0f, 0.0f))) {
      std::vector<int> sorted = m_pendingDeleteObjectIndices;
      std::sort(sorted.rbegin(), sorted.rend());
      for (int idx : sorted) {
        if (idx >= 0 && idx < static_cast<int>(m_document.objects.size()))
          m_document.objects.erase(m_document.objects.begin() + idx);
      }
      m_selectedIndices.clear();
      m_document.dirty = true;
      TriggerReload();

      m_confirmDeleteObjectsOpen = false;
      m_pendingDeleteObjectIndices.clear();
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }

  if (m_confirmDeleteAssetOpen)
    ImGui::OpenPopup("Confirm Delete Asset");

  if (ImGui::BeginPopupModal("Confirm Delete Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (m_pendingDeleteAssetId.empty() ||
        m_document.assets.find(m_pendingDeleteAssetId) == m_document.assets.end()) {
      m_confirmDeleteAssetOpen = false;
      m_pendingDeleteAssetId.clear();
      ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
      return;
    }

    ImGui::Text("Delete asset '%s'?", m_pendingDeleteAssetId.c_str());
    ImGui::TextDisabled("All object bindings to this asset will be cleared.");
    ImGui::Separator();

    if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f))) {
      m_confirmDeleteAssetOpen = false;
      m_pendingDeleteAssetId.clear();
      ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Delete", ImVec2(110.0f, 0.0f))) {
      for (auto& obj : m_document.objects) {
        if (obj.assetId == m_pendingDeleteAssetId)
          obj.assetId.clear();
      }
      if (m_selectedAssetId == m_pendingDeleteAssetId)
        m_selectedAssetId.clear();
      m_document.assets.erase(m_pendingDeleteAssetId);
      m_document.dirty = true;
      TriggerReload();

      m_confirmDeleteAssetOpen = false;
      m_pendingDeleteAssetId.clear();
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }
}

void EditorLayer::DrawExitConfirmModal() {
  if (m_confirmExitOpen)
    ImGui::OpenPopup("Unsaved Changes");

  if (!ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return;

  ImGui::TextUnformatted("You have unsaved changes.");
  ImGui::TextDisabled("Are you sure you want to exit editor mode?");
  ImGui::Separator();

  if (!m_exitConfirmError.empty())
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_exitConfirmError.c_str());

  if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
    m_confirmExitOpen = false;
    m_exitConfirmError.clear();
    ImGui::CloseCurrentPopup();
  }

  ImGui::SameLine();
  if (ImGui::Button("Discard & Exit", ImVec2(120.0f, 0.0f))) {
    DiscardUnsavedChanges();
    m_confirmExitOpen = false;
    m_exitConfirmError.clear();
    ImGui::CloseCurrentPopup();
    m_closeRequested = true;
  }

  ImGui::SameLine();
  if (ImGui::Button("Save & Exit", ImVec2(120.0f, 0.0f))) {
    std::string saveError;
    if (SaveDocument(&saveError)) {
      m_confirmExitOpen = false;
      m_exitConfirmError.clear();
      ImGui::CloseCurrentPopup();
      m_closeRequested = true;
    } else {
      m_exitConfirmError = saveError;
    }
  }

  ImGui::EndPopup();
}

// ---- Properties panel --------------------------------------------------------

void EditorLayer::DrawPropertiesPanel() {
  ImGuiIO& io = ImGui::GetIO();
  const float W = 280.0f;
  const float workTop = kEditorToolbarH;
  const float workBottom = io.DisplaySize.y - kEditorStatusH - kBottomDockH;

  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - W, workTop));
  ImGui::SetNextWindowSize(ImVec2(W, workBottom - workTop));
  ImGui::SetNextWindowBgAlpha(0.85f);
  ImGui::Begin(
      "Properties", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);

  // ---- Multi-selection summary ----
  if (m_selectedIndices.size() > 1) {
    ImGui::Text("%d objects selected", static_cast<int>(m_selectedIndices.size()));
    ImGui::Separator();
    if (ImGui::Button("Delete Selected")) {
      RequestDeleteSelectedObjects();
    }
    ImGui::End();
    return;
  }

  int primaryIdx = PrimaryIdx();
  if (primaryIdx < 0 || primaryIdx >= static_cast<int>(m_document.objects.size())) {
    if (!m_selectedAssetId.empty()) {
      auto assetIt = m_document.assets.find(m_selectedAssetId);
      if (assetIt == m_document.assets.end()) {
        m_selectedAssetId.clear();
      } else {
        ImGui::TextDisabled("Asset");
        ImGui::TextUnformatted(m_selectedAssetId.c_str());

        if (ImGui::Button("Deselect Asset", ImVec2(-1.0f, 0.0f))) {
          m_selectedAssetId.clear();
          ImGui::End();
          return;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        char meshEditBuf[512] = {};
        std::snprintf(meshEditBuf, sizeof(meshEditBuf), "%s", assetIt->second.mesh.c_str());
        if (ImGui::InputText("Mesh", meshEditBuf, sizeof(meshEditBuf))) {
          assetIt->second.mesh = meshEditBuf;
          m_document.dirty = true;
        }

        char scaleEditBuf[128] = {};
        std::snprintf(scaleEditBuf, sizeof(scaleEditBuf), "%s", assetIt->second.renderScale.c_str());
        if (ImGui::InputText("Render Scale", scaleEditBuf, sizeof(scaleEditBuf))) {
          assetIt->second.renderScale = scaleEditBuf;
          m_document.dirty = true;
          TriggerReload();
        }

        ImGui::TextDisabled("Albedo map (optional)");
        const ImVec2 selAlbLabelMin = ImGui::GetItemRectMin();
        const ImVec2 selAlbLabelMax = ImGui::GetItemRectMax();
        char albBuf[512] = {};
        std::snprintf(albBuf, sizeof(albBuf), "%s", assetIt->second.albedoMap.c_str());
        if (ImGui::InputText("##sel_alb", albBuf, sizeof(albBuf))) {
          assetIt->second.albedoMap = albBuf;
          m_document.dirty = true;
        }
        {
          const ImVec2 fMin = ImGui::GetItemRectMin();
          const ImVec2 fMax = ImGui::GetItemRectMax();
          m_albedoSelDrop.valid = true;
          m_albedoSelDrop.minX = std::min(selAlbLabelMin.x, fMin.x);
          m_albedoSelDrop.minY = selAlbLabelMin.y;
          m_albedoSelDrop.maxX = std::max(selAlbLabelMax.x, fMax.x);
          m_albedoSelDrop.maxY = fMax.y;
        }

#if defined(_WIN32) || defined(__APPLE__)
        if (ImGui::Button("Browse texture...##alb_pick_asset", ImVec2(-1.0f, 0.0f)))
          m_deferredFilePick = DeferredFilePick::SelectedAssetAlbedo;
#else
        ImGui::BeginDisabled();
        ImGui::Button("Browse texture...##alb_pick_asset", ImVec2(-1.0f, 0.0f));
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
          ImGui::SetTooltip("Texture file dialog is not available on this platform.");
#endif

        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float fullW = ImGui::GetContentRegionAvail().x;
        const float btnW = std::max(90.0f, (fullW - gap) * 0.5f);
        if (ImGui::Button("Add Prop##sel_add", ImVec2(btnW, 0.0f))) {
          SceneObject obj = MakeObjectFromAsset(m_document, m_selectedAssetId, m_schema);
          m_document.objects.push_back(std::move(obj));
          m_selectedIndices = {static_cast<int>(m_document.objects.size()) - 1};
          m_document.dirty = true;
          TriggerReload();
        }
        ImGui::SameLine(0.0f, gap);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.18f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.55f, 0.22f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.65f, 0.15f, 0.15f, 1.0f));
        if (ImGui::Button("Delete Asset##sel_del", ImVec2(btnW, 0.0f)))
          RequestDeleteAsset(m_selectedAssetId);
        ImGui::PopStyleColor(3);

        ImGui::End();
        return;
      }
    }

    ImGui::TextDisabled("No selection");
    ImGui::TextDisabled("Pick an object or asset to edit properties.");
    ImGui::End();
    return;
  }

  SceneObject& obj = m_document.objects[primaryIdx];

  // ---- Identity ----
  ImGui::LabelText("ID", "%s", obj.id.c_str());
  const char* typeName = (obj.type == SceneObjectType::Prop)    ? "Prop"
                         : (obj.type == SceneObjectType::Light) ? "Light"
                         : (obj.type == SceneObjectType::Camera) ? "Camera"
                                                                 : "Panel";
  ImGui::LabelText("Type", "%s", typeName);

  {
    std::string currentParent = GetParentId(obj);
    std::vector<std::string> parentIds;
    std::vector<const char*> parentItems;
    parentItems.push_back("<root>");
    int currentIdx = 0;
    for (int i = 0; i < static_cast<int>(m_document.objects.size()); ++i) {
      if (i == primaryIdx || IsDescendantOf(m_document, i, primaryIdx))
        continue;
      const std::string& id = m_document.objects[static_cast<size_t>(i)].id;
      parentIds.push_back(id);
      parentItems.push_back(parentIds.back().c_str());
      if (id == currentParent)
        currentIdx = static_cast<int>(parentItems.size()) - 1;
    }
    if (ImGui::Combo("Parent", &currentIdx, parentItems.data(), static_cast<int>(parentItems.size()))) {
      if (currentIdx == 0)
        obj.props.erase("parentId");
      else
        obj.props["parentId"] = parentIds[static_cast<size_t>(currentIdx - 1)];
      m_document.dirty = true;
    }
  }
  ImGui::Separator();

  // ---- Camera-specific properties ----
  if (obj.type == SceneObjectType::Camera) {
    const Vec3 oldPos = obj.position;
    const Quaternion oldRot =
        Quaternion::FromEuler(ToRadians(obj.pitch), ToRadians(obj.yaw), ToRadians(obj.roll));
    bool changedTransform = false;

    float pos[3] = {obj.position.x, obj.position.y, obj.position.z};
    if (ImGui::DragFloat3("Position", pos, 0.05f)) {
      obj.position = {pos[0], pos[1], pos[2]};
      changedTransform = true;
      m_document.dirty = true;
      if (m_transformCb)
        m_transformCb(obj);
    }

    if (ImGui::DragFloat("Yaw", &obj.yaw, 1.0f, -360.0f, 360.0f)) {
      changedTransform = true;
      m_document.dirty = true;
      if (m_transformCb)
        m_transformCb(obj);
    }

    float pitch = obj.pitch;
    if (ImGui::DragFloat("Pitch", &pitch, 1.0f, -89.0f, 89.0f)) {
      obj.pitch = std::max(-89.0f, std::min(89.0f, pitch));
      changedTransform = true;
      m_document.dirty = true;
      if (m_transformCb)
        m_transformCb(obj);
    }

    if (changedTransform) {
      const Quaternion newRot =
          Quaternion::FromEuler(ToRadians(obj.pitch), ToRadians(obj.yaw), ToRadians(obj.roll));
      PropagateHierarchyTransformDelta(
          m_document, primaryIdx, oldPos, oldRot, obj.position, newRot, m_transformCb);
    }

    ImGui::Separator();

    // FOV
    {
      auto& fovStr = obj.props["fov"];
      if (fovStr.empty()) fovStr = "60";
      float fov = std::stof(fovStr);
      if (ImGui::SliderFloat("FOV", &fov, 1.0f, 179.0f)) {
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "%.4f", fov);
        fovStr = tmp;
        m_document.dirty = true;
      }
    }

    // Near Clip
    {
      auto& nearStr = obj.props["nearClip"];
      if (nearStr.empty()) nearStr = "0.1";
      float v = std::stof(nearStr);
      if (ImGui::DragFloat("Near Clip", &v, 0.01f, 0.001f, 100.0f)) {
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "%.4f", v);
        nearStr = tmp;
        m_document.dirty = true;
      }
    }

    // Far Clip
    {
      auto& farStr = obj.props["farClip"];
      if (farStr.empty()) farStr = "500";
      float v = std::stof(farStr);
      if (ImGui::DragFloat("Far Clip", &v, 1.0f, 1.0f, 100000.0f)) {
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "%.4f", v);
        farStr = tmp;
        m_document.dirty = true;
      }
    }

    ImGui::Separator();

    // Follow Target dropdown
    {
      auto& followId = obj.props["followTargetId"];
      std::vector<const char*> targetItems;
      targetItems.push_back("<none>");
      int curTarget = 0;
      int ti = 1;
      for (const auto& other : m_document.objects) {
        if (other.id == obj.id)
          continue;
        targetItems.push_back(other.id.c_str());
        if (other.id == followId)
          curTarget = ti;
        ++ti;
      }
      if (ImGui::Combo("Follow Target", &curTarget, targetItems.data(),
                       static_cast<int>(targetItems.size()))) {
        followId = (curTarget == 0) ? "" : targetItems[static_cast<size_t>(curTarget)];
        m_document.dirty = true;
      }
    }

    ImGui::Separator();
    if (ImGui::Button("Delete"))
      RequestDeleteSelectedObjects();
    ImGui::End();
    return;
  }

  // ---- Transform (non-camera objects) ----
  const Vec3 oldPos = obj.position;
  const Quaternion oldRot =
      Quaternion::FromEuler(ToRadians(obj.pitch), ToRadians(obj.yaw), ToRadians(obj.roll));
  bool changedTransform = false;

  float pos[3] = {obj.position.x, obj.position.y, obj.position.z};
  if (ImGui::DragFloat3("Position", pos, 0.05f)) {
    obj.position = {pos[0], pos[1], pos[2]};
    changedTransform = true;
    m_document.dirty = true;
    if (m_transformCb)
      m_transformCb(obj);
  }

  float scl[3] = {obj.scale.x, obj.scale.y, obj.scale.z};
  if (ImGui::DragFloat3("Scale", scl, 0.02f, 0.01f, 200.0f)) {
    obj.scale = {scl[0], scl[1], scl[2]};
    m_document.dirty = true;
    if (m_transformCb)
      m_transformCb(obj);
  }

  // ---- Rotation (Euler angles: Pitch / Yaw / Roll) ----
  float rot[3] = {obj.pitch, obj.yaw, obj.roll};
  if (ImGui::DragFloat3("Rotation (P/Y/R)", rot, 1.0f, -360.0f, 360.0f)) {
    obj.pitch = rot[0];
    obj.yaw = rot[1];
    obj.roll = rot[2];
    changedTransform = true;
    m_document.dirty = true;
    if (m_transformCb)
      m_transformCb(obj);
  }

  if (changedTransform) {
    const Quaternion newRot =
        Quaternion::FromEuler(ToRadians(obj.pitch), ToRadians(obj.yaw), ToRadians(obj.roll));
    PropagateHierarchyTransformDelta(
        m_document, primaryIdx, oldPos, oldRot, obj.position, newRot, m_transformCb);
  }

  ImGui::Separator();
  ImGui::Text("Asset");

  std::vector<const char*> assetItems;
  assetItems.reserve(m_document.assets.size() + 1);
  assetItems.push_back("<none>");
  int currentAssetIndex = 0;
  int assetIndex = 1;
  for (const auto& [assetId, _] : m_document.assets) {
    assetItems.push_back(assetId.c_str());
    if (obj.assetId == assetId)
      currentAssetIndex = assetIndex;
    ++assetIndex;
  }

  if (ImGui::Combo("Asset ID", &currentAssetIndex, assetItems.data(), static_cast<int>(assetItems.size()))) {
    if (currentAssetIndex == 0)
      obj.assetId.clear();
    else
      obj.assetId = assetItems[static_cast<size_t>(currentAssetIndex)];
    m_document.dirty = true;
    TriggerReload();
  }

  if (!obj.assetId.empty()) {
    auto assetIt = m_document.assets.find(obj.assetId);
    if (assetIt != m_document.assets.end()) {
      ImGui::TextDisabled("mesh: %s", assetIt->second.mesh.c_str());
      ImGui::TextDisabled("renderScale: %s", assetIt->second.renderScale.c_str());
    } else {
      ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "Missing asset: %s", obj.assetId.c_str());
    }
  }

  ImGui::Separator();
  ImGui::Text("Props");

  // ---- Schema-driven fields ----
  const TypeSchema* schema = m_schema.GetSchema(obj.type);
  if (schema) {
    for (const auto& fd : schema->fields) {
      std::string& val = obj.props[fd.key];
      if (val.empty())
        val = fd.defaultValue;

      switch (fd.widget) {
        case FieldDef::Widget::String: {
          char buf[256] = {};
          std::snprintf(buf, sizeof(buf), "%s", val.c_str());
          if (ImGui::InputText(fd.label.c_str(), buf, sizeof(buf))) {
            val = buf;
            m_document.dirty = true;
          }
          break;
        }
        case FieldDef::Widget::Float: {
          float f = val.empty() ? fd.minVal : std::stof(val);
          if (ImGui::SliderFloat(fd.label.c_str(), &f, fd.minVal, fd.maxVal)) {
            char tmp[32];
            std::snprintf(tmp, sizeof(tmp), "%.4f", f);
            val = tmp;
            m_document.dirty = true;
          }
          break;
        }
        case FieldDef::Widget::Bool: {
          bool b = (val == "true" || val == "1");
          if (ImGui::Checkbox(fd.label.c_str(), &b)) {
            val = b ? "true" : "false";
            m_document.dirty = true;
          }
          break;
        }
        case FieldDef::Widget::Enum: {
          int cur = 0;
          for (int i = 0; i < static_cast<int>(fd.options.size()); ++i)
            if (fd.options[i] == val) {
              cur = i;
              break;
            }

          // Build null-separated list for ImGui::Combo
          std::string items;
          for (auto& opt : fd.options) {
            items += opt;
            items += '\0';
          }
          items += '\0';

          if (ImGui::Combo(fd.label.c_str(), &cur, items.c_str())) {
            val = fd.options[static_cast<size_t>(cur)];
            m_document.dirty = true;
          }
          break;
        }
        case FieldDef::Widget::Color3: {
          float col[3] = {1.0f, 1.0f, 1.0f};
          if (!val.empty()) {
            // Parse "r,g,b" using strtof to avoid MSVC sscanf deprecation
            char tmp[64] = {};
            std::snprintf(tmp, sizeof(tmp), "%s", val.c_str());
            char* p = tmp;
            char* end = nullptr;
            col[0] = std::strtof(p, &end);
            if (end && *end)
              p = end + 1;
            col[1] = std::strtof(p, &end);
            if (end && *end)
              p = end + 1;
            col[2] = std::strtof(p, nullptr);
          }
          if (ImGui::ColorEdit3(fd.label.c_str(), col)) {
            char tmp[64];
            std::snprintf(tmp, sizeof(tmp), "%.4f,%.4f,%.4f", col[0], col[1], col[2]);
            val = tmp;
            m_document.dirty = true;
          }
          break;
        }
      }
    }
  }

  // ---- Components ----
  ImGui::Separator();
  ImGui::Text("Components");

  int removeIdx = -1;
  for (int ci = 0; ci < static_cast<int>(obj.components.size()); ++ci) {
    ComponentDesc& comp = obj.components[ci];

    // Collapsing header label + inline [x] button via ID stack trick
    std::string headerLabel = (comp.type == "light")      ? "Light"
                              : (comp.type == "rigidbody") ? "RigidBody"
                              : (comp.type == "script")    ? "Script"
                                                           : comp.type;
    ImGui::PushID(ci);
    bool open = ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    bool removeThisComponent = false;

    if (ImGui::BeginPopupContextItem("comp_ctx")) {
      if (ImGui::MenuItem("Remove Component"))
        removeThisComponent = true;
      ImGui::EndPopup();
    }

    // Remove button on the same line, right-aligned
    float btnW = ImGui::CalcTextSize("x").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - btnW);
    if (ImGui::SmallButton("x"))
      removeThisComponent = true;

    if (removeThisComponent)
      removeIdx = ci;

    if (open) {
      if (comp.type == "light") {
        // Intensity
        {
          float v = comp.props.count("intensity") ? std::strtof(comp.props["intensity"].c_str(), nullptr) : 1.0f;
          if (ImGui::SliderFloat("Intensity", &v, 0.0f, 10.0f)) {
            char tmp[32];
            std::snprintf(tmp, sizeof(tmp), "%.4f", v);
            comp.props["intensity"] = tmp;
            m_document.dirty = true;
          }
        }
        // Color
        {
          float col[3] = {1.0f, 1.0f, 1.0f};
          if (comp.props.count("color")) {
            char tmp[64] = {};
            std::snprintf(tmp, sizeof(tmp), "%s", comp.props["color"].c_str());
            char* p = tmp;
            char* end = nullptr;
            col[0] = std::strtof(p, &end);
            if (end && *end) p = end + 1;
            col[1] = std::strtof(p, &end);
            if (end && *end) p = end + 1;
            col[2] = std::strtof(p, nullptr);
          }
          if (ImGui::ColorEdit3("Color", col)) {
            char tmp[64];
            std::snprintf(tmp, sizeof(tmp), "%.4f,%.4f,%.4f", col[0], col[1], col[2]);
            comp.props["color"] = tmp;
            m_document.dirty = true;
          }
        }
        // Radius
        {
          float v = comp.props.count("radius") ? std::strtof(comp.props["radius"].c_str(), nullptr) : 5.0f;
          if (ImGui::DragFloat("Radius", &v, 0.1f, 0.0f, 100.0f)) {
            char tmp[32];
            std::snprintf(tmp, sizeof(tmp), "%.4f", v);
            comp.props["radius"] = tmp;
            m_document.dirty = true;
          }
        }
      } else if (comp.type == "rigidbody") {
        // Mass
        {
          float v = comp.props.count("mass") ? std::strtof(comp.props["mass"].c_str(), nullptr) : 1.0f;
          if (ImGui::DragFloat("Mass", &v, 0.1f, 0.0f, 10000.0f)) {
            char tmp[32];
            std::snprintf(tmp, sizeof(tmp), "%.4f", v);
            comp.props["mass"] = tmp;
            m_document.dirty = true;
          }
        }
        // Is Kinematic
        {
          bool b = comp.props.count("isKinematic") && comp.props["isKinematic"] == "true";
          if (ImGui::Checkbox("Is Kinematic", &b)) {
            comp.props["isKinematic"] = b ? "true" : "false";
            m_document.dirty = true;
          }
        }
        // Use Gravity
        {
          bool b = !comp.props.count("useGravity") || comp.props["useGravity"] == "true";
          if (ImGui::Checkbox("Use Gravity", &b)) {
            comp.props["useGravity"] = b ? "true" : "false";
            m_document.dirty = true;
          }
        }
      } else if (comp.type == "script") {
        std::vector<std::string> options;
        if (m_scriptBehaviorOptionsCb)
          options = m_scriptBehaviorOptionsCb();
        options.erase(std::remove_if(options.begin(), options.end(), [](const std::string& s) {
                        return s.empty();
                      }),
                      options.end());
        std::sort(options.begin(), options.end());
        options.erase(std::unique(options.begin(), options.end()), options.end());

        std::string current = comp.props["behaviorTag"];
        if (!current.empty() && std::find(options.begin(), options.end(), current) == options.end())
          options.push_back(current);

        std::vector<const char*> labels;
        labels.reserve(options.size() + 1);
        labels.push_back("<none>");
        int currentIdx = 0;
        for (int i = 0; i < static_cast<int>(options.size()); ++i) {
          labels.push_back(options[static_cast<size_t>(i)].c_str());
          if (options[static_cast<size_t>(i)] == current)
            currentIdx = i + 1;
        }

        if (ImGui::Combo("Behavior", &currentIdx, labels.data(), static_cast<int>(labels.size()))) {
          comp.props["behaviorTag"] = (currentIdx == 0) ? "" : options[static_cast<size_t>(currentIdx - 1)];
          m_document.dirty = true;
        }
      }
    }
    ImGui::PopID();
  }

  if (removeIdx >= 0) {
    obj.components.erase(obj.components.begin() + removeIdx);
    m_document.dirty = true;
  }

  // ---- Add Component button ----
  ImGui::Spacing();
  if (ImGui::Button("+ Add Component")) {
    ImGui::OpenPopup("##add_component_popup");
  }
  if (ImGui::BeginPopup("##add_component_popup")) {
    if (ImGui::MenuItem("Light")) {
      ComponentDesc cd;
      cd.type = "light";
      cd.props["intensity"] = "1.0000";
      cd.props["color"] = "1.0000,1.0000,1.0000";
      cd.props["radius"] = "5.0000";
      obj.components.push_back(std::move(cd));
      m_document.dirty = true;
    }
    if (ImGui::MenuItem("RigidBody")) {
      ComponentDesc cd;
      cd.type = "rigidbody";
      cd.props["mass"] = "1.0000";
      cd.props["isKinematic"] = "false";
      cd.props["useGravity"] = "true";
      obj.components.push_back(std::move(cd));
      m_document.dirty = true;
    }
    if (ImGui::MenuItem("Script")) {
      ComponentDesc cd;
      cd.type = "script";
      cd.props["behaviorTag"] = "";
      obj.components.push_back(std::move(cd));
      m_document.dirty = true;
    }
    ImGui::EndPopup();
  }

  ImGui::Separator();
  if (ImGui::Button("Delete")) {
    RequestDeleteSelectedObjects();
  }

  ImGui::End();
}

// ---- Picking -----------------------------------------------------------------

void EditorLayer::HandlePicking(const Camera& cam, int screenW, int screenH) {
  bool currL = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  bool clicked = currL && !m_prevMouseL;
  m_prevMouseL = currL;

  if (!clicked)
    return;
  if (ImGui::GetIO().WantCaptureMouse)
    return;

  double mx, my;
  glfwGetCursorPos(m_window, &mx, &my);
  if (m_viewGizmoPickRect.valid &&
      m_viewGizmoPickRect.Contains(static_cast<float>(mx), static_cast<float>(my), 2.0f))
    return;

  Ray ray = ScreenToRay(static_cast<float>(mx), static_cast<float>(my), screenW, screenH, cam);

  float bestT = std::numeric_limits<float>::max();
  int bestIdx = -1;

  for (int i = 0; i < static_cast<int>(m_document.objects.size()); ++i) {
    const auto& obj = m_document.objects[i];
    Vec3 center = obj.position;
    Vec3 half = {std::max(obj.scale.x, 0.25f), std::max(obj.scale.y, 0.25f),
                 std::max(obj.scale.z, 0.25f)};
    if (m_liveRegistry && TryPropWorldAabb(*m_liveRegistry, obj, center, half)) {
      half.x = std::max(half.x, 0.25f);
      half.y = std::max(half.y, 0.25f);
      half.z = std::max(half.z, 0.25f);
    }
    float t = RayVsAABB(ray, center, half);
    if (t >= 0.0f && t < bestT) {
      bestT = t;
      bestIdx = i;
    }
  }

  bool shiftHeld = glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                   glfwGetKey(m_window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

  if (bestIdx >= 0) {
    if (shiftHeld)
      ToggleSelect(bestIdx);
    else
      m_selectedIndices = {bestIdx};
  } else if (!shiftHeld) {
    m_selectedIndices.clear();
  }
}

void EditorLayer::DrawSelectionHighlight() {
  const int n = static_cast<int>(m_document.objects.size());
  for (int i : m_selectedIndices) {
    if (i < 0 || i >= n)
      continue;
    const auto& obj = m_document.objects[i];

    if (obj.type == SceneObjectType::Camera) {
      const Vec4 color = {0.2f, 0.7f, 1.0f, 1.0f};
      const float yawRad = ToRadians(obj.yaw);
      const float pitchRad = ToRadians(std::max(-89.0f, std::min(89.0f, obj.pitch)));
      const Vec3 forward = {-std::sin(yawRad) * std::cos(pitchRad),
                            std::sin(pitchRad),
                            -std::cos(yawRad) * std::cos(pitchRad)};

      Vec3 right = Vec3::Cross(forward, Vec3::Up());
      if (right.LengthSq() < 1e-5f)
        right = {1.0f, 0.0f, 0.0f};
      else
        right = right.Normalized();
      Vec3 up = Vec3::Cross(right, forward).Normalized();

      const Vec3 tip = obj.position;
      const Vec3 baseCenter = tip + forward * 0.55f;
      const float baseHalfW = 0.24f;
      const float baseHalfH = 0.16f;

      const Vec3 b0 = baseCenter + right * baseHalfW + up * baseHalfH;
      const Vec3 b1 = baseCenter - right * baseHalfW + up * baseHalfH;
      const Vec3 b2 = baseCenter - right * baseHalfW - up * baseHalfH;
      const Vec3 b3 = baseCenter + right * baseHalfW - up * baseHalfH;

      DebugDraw::Line(tip, b0, color);
      DebugDraw::Line(tip, b1, color);
      DebugDraw::Line(tip, b2, color);
      DebugDraw::Line(tip, b3, color);
      DebugDraw::Line(b0, b1, color);
      DebugDraw::Line(b1, b2, color);
      DebugDraw::Line(b2, b3, color);
      DebugDraw::Line(b3, b0, color);

      const Vec3 dirTip = tip + forward * 0.78f;
      DebugDraw::Line(tip, dirTip, {1.0f, 0.55f, 0.1f, 1.0f});
      continue;
    }

    Vec3 center = obj.position;
    Vec3 half = obj.scale;
    if (!m_liveRegistry || !TryPropWorldAabb(*m_liveRegistry, obj, center, half))
      half = {std::max(half.x, 0.25f), std::max(half.y, 0.25f), std::max(half.z, 0.25f)};
    else {
      half.x = std::max(half.x, 0.25f);
      half.y = std::max(half.y, 0.25f);
      half.z = std::max(half.z, 0.25f);
    }
    DebugDraw::Box(center, half, {0.2f, 0.7f, 1.0f, 1.0f});
  }
}

void EditorLayer::ApplyPendingViewSnap(Camera& cam) {
  if (m_pendingViewSnap == ViewSnap::None)
    return;

  Vec3 pivot = Vec3::Zero();
  float extent = 1.0f;
  const int idx = PrimaryIdx();
  if (idx >= 0 && idx < static_cast<int>(m_document.objects.size())) {
    const SceneObject& obj = m_document.objects[idx];
    pivot = obj.position;
    extent = std::max(obj.scale.x, std::max(obj.scale.y, obj.scale.z));
  }

  const float distance = std::max(2.0f, extent * 3.0f + 1.0f);

  cam.target = pivot;
  cam.up = {0.0f, 1.0f, 0.0f};

  switch (m_pendingViewSnap) {
    case ViewSnap::Top:
      cam.position = pivot + Vec3{0.0f, distance, 0.0f};
      cam.up = {0.0f, 0.0f, -1.0f};
      break;
    case ViewSnap::Bottom:
      cam.position = pivot + Vec3{0.0f, -distance, 0.0f};
      cam.up = {0.0f, 0.0f, 1.0f};
      break;
    case ViewSnap::Left:
      cam.position = pivot + Vec3{-distance, 0.0f, 0.0f};
      break;
    case ViewSnap::Right:
      cam.position = pivot + Vec3{distance, 0.0f, 0.0f};
      break;
    case ViewSnap::Front:
      cam.position = pivot + Vec3{0.0f, 0.0f, distance};
      break;
    case ViewSnap::Back:
      cam.position = pivot + Vec3{0.0f, 0.0f, -distance};
      break;
    case ViewSnap::None:
      break;
  }

  m_flyCamInitialized = false;
  m_pendingViewSnap = ViewSnap::None;
}

// ---- Helpers -----------------------------------------------------------------

bool EditorLayer::IsSelected(int i) const {
  return std::find(m_selectedIndices.begin(), m_selectedIndices.end(), i) !=
         m_selectedIndices.end();
}

int EditorLayer::PrimaryIdx() const {
  return m_selectedIndices.empty() ? -1 : m_selectedIndices.back();
}

void EditorLayer::ToggleSelect(int i) {
  auto it = std::find(m_selectedIndices.begin(), m_selectedIndices.end(), i);
  if (it != m_selectedIndices.end())
    m_selectedIndices.erase(it);
  else
    m_selectedIndices.push_back(i);
}

void EditorLayer::TriggerReload() {
  m_pendingDoc = m_document;
  m_wantsReload = true;
}

void EditorLayer::ProcessMcpCommands() {
  m_mcpController.DrainCommands([this](const std::string& toolName, const nlohmann::json& arguments) {
    return ExecuteMcpCommand(toolName, arguments);
  });
}

void EditorLayer::PublishMcpSnapshot() {
  Mcp::McpEditorSnapshot snapshot;
  snapshot.editorActive = m_active;
  snapshot.playMode = m_playMode;
  snapshot.dirty = m_document.dirty;
  snapshot.reloadPending = m_wantsReload;
  snapshot.sceneId = m_document.sceneId;
  snapshot.sceneName = m_document.sceneName;
  snapshot.sceneFilePath = m_document.filePath;
  snapshot.selectedAssetId = m_selectedAssetId;

  for (int idx : m_selectedIndices) {
    if (idx >= 0 && idx < static_cast<int>(m_document.objects.size()))
      snapshot.selectedObjectIds.push_back(m_document.objects[static_cast<size_t>(idx)].id);
  }

  snapshot.objects.reserve(m_document.objects.size());
  for (const SceneObject& object : m_document.objects) {
    Mcp::McpObjectSnapshot entry;
    entry.id = object.id;
    entry.type = SceneObjectTypeToString(object.type);
    entry.position = object.position;
    entry.scale = object.scale;
    entry.yaw = object.yaw;
    entry.pitch = object.pitch;
    entry.roll = object.roll;
    entry.assetId = object.assetId;
    entry.props = object.props;
    for (const ComponentDesc& component : object.components) {
      Mcp::McpComponentSnapshot componentEntry;
      componentEntry.type = component.type;
      componentEntry.props = component.props;
      entry.components.push_back(std::move(componentEntry));
    }
    snapshot.objects.push_back(std::move(entry));
  }

  std::vector<std::string> assetIds;
  assetIds.reserve(m_document.assets.size());
  for (const auto& entry : m_document.assets)
    assetIds.push_back(entry.first);
  std::sort(assetIds.begin(), assetIds.end());
  for (const std::string& assetId : assetIds) {
    const AssetDef& asset = m_document.assets.at(assetId);
    snapshot.assets.push_back(Mcp::McpAssetSnapshot{assetId, asset.mesh, asset.renderScale,
                                                    asset.albedoMap});
  }

  std::vector<LogLine> lines;
  LogBuffer::Instance().CopyLinesTo(&lines);
  const size_t start = lines.size() > 50 ? lines.size() - 50 : 0;
  for (size_t i = start; i < lines.size(); ++i) {
    char timeBuf[32];
    FormatLogTime(lines[i], timeBuf, sizeof(timeBuf));
    snapshot.consoleEntries.push_back(Mcp::McpConsoleEntry{
        timeBuf,
        lines[i].level == LogLevel::Info ? "INFO" : lines[i].level == LogLevel::Warn ? "WARN" : "ERR",
        lines[i].message,
    });
  }

  m_mcpController.PublishSnapshot(snapshot);
}

Mcp::McpCommandResult EditorLayer::ExecuteMcpCommand(const std::string& toolName,
                                                     const nlohmann::json& arguments) {
  using json = nlohmann::json;

  auto parseVec3 = [](const json& value, Vec3* out) -> bool {
    if (!out || !value.is_array() || value.size() != 3)
      return false;
    if (!value[0].is_number() || !value[1].is_number() || !value[2].is_number())
      return false;
    out->x = value[0].get<float>();
    out->y = value[1].get<float>();
    out->z = value[2].get<float>();
    return true;
  };

  auto parseProps = [](const json& value) {
    std::unordered_map<std::string, std::string> props;
    if (!value.is_object())
      return props;
    for (auto it = value.begin(); it != value.end(); ++it) {
      props[it.key()] = it.value().is_string() ? it.value().get<std::string>() : it.value().dump();
    }
    return props;
  };

  auto parseComponents = [&](const json& value, std::vector<ComponentDesc>* out) -> bool {
    if (!out)
      return false;
    if (!value.is_array())
      return false;
    std::vector<ComponentDesc> components;
    for (const json& item : value) {
      if (!item.is_object() || !item.contains("type") || !item["type"].is_string())
        return false;
      ComponentDesc component;
      component.type = item["type"].get<std::string>();
      component.props = parseProps(item.value("props", json::object()));
      components.push_back(std::move(component));
    }
    *out = std::move(components);
    return true;
  };

  auto findIndexById = [&](const std::string& id) -> int {
    for (int i = 0; i < static_cast<int>(m_document.objects.size()); ++i) {
      if (m_document.objects[static_cast<size_t>(i)].id == id)
        return i;
    }
    return -1;
  };

  auto summarizeObject = [&](const SceneObject& object) {
    json out = json::object();
    out["id"] = object.id;
    out["type"] = SceneObjectTypeToString(object.type);
    out["position"] = json::array({object.position.x, object.position.y, object.position.z});
    out["scale"] = json::array({object.scale.x, object.scale.y, object.scale.z});
    out["yaw"] = object.yaw;
    out["pitch"] = object.pitch;
    out["roll"] = object.roll;
    out["assetId"] = object.assetId;
    return out;
  };

  if (toolName == "editor.select") {
    std::vector<std::string> ids;
    if (arguments.contains("id") && arguments["id"].is_string())
      ids.push_back(arguments["id"].get<std::string>());
    if (arguments.contains("ids") && arguments["ids"].is_array()) {
      for (const json& item : arguments["ids"]) {
        if (item.is_string())
          ids.push_back(item.get<std::string>());
      }
    }
    m_selectedIndices.clear();
    for (const std::string& id : ids) {
      const int idx = findIndexById(id);
      if (idx >= 0)
        m_selectedIndices.push_back(idx);
    }
    return Mcp::McpCommandResult{true, json{{"selectedObjectIds", ids}}, std::string()};
  }

  if (toolName == "editor.create_object") {
    SceneObjectType type = SceneObjectType::Panel;
    if (!ParseSceneObjectType(arguments.value("type", std::string()), &type))
      return Mcp::McpCommandResult{false, json::object(), "Invalid object type."};

    SceneObject object;
    object.type = type;
    ApplySchemaDefaults(object);
    object.id = arguments.value("id", std::string());
    if (object.id.empty())
      object.id = type == SceneObjectType::Camera ? GenerateCameraId(m_document) : GenerateId(m_document);
    if (findIndexById(object.id) >= 0)
      return Mcp::McpCommandResult{false, json::object(), "Object id already exists."};

    if (arguments.contains("position") && !parseVec3(arguments["position"], &object.position))
      return Mcp::McpCommandResult{false, json::object(), "position must be [x,y,z]."};
    if (arguments.contains("scale") && !parseVec3(arguments["scale"], &object.scale))
      return Mcp::McpCommandResult{false, json::object(), "scale must be [x,y,z]."};
    object.yaw = arguments.value("yaw", object.yaw);
    object.pitch = arguments.value("pitch", object.pitch);
    object.roll = arguments.value("roll", object.roll);
    object.assetId = arguments.value("assetId", std::string());
    if (arguments.contains("props"))
      object.props = parseProps(arguments["props"]);
    if (arguments.contains("components") && !parseComponents(arguments["components"], &object.components))
      return Mcp::McpCommandResult{false, json::object(), "components must be an array of objects."};
    const std::string parentId = arguments.value("parentId", std::string());
    if (!parentId.empty())
      object.props["parentId"] = parentId;

    m_document.objects.push_back(std::move(object));
    m_selectedIndices = {static_cast<int>(m_document.objects.size()) - 1};
    m_document.dirty = true;
    TriggerReload();
    return Mcp::McpCommandResult{true,
                                 json{{"created", summarizeObject(m_document.objects.back())}},
                                 std::string()};
  }

  if (toolName == "editor.update_object" || toolName == "editor.transform") {
    const std::string id = arguments.value("id", std::string());
    const int idx = findIndexById(id);
    if (idx < 0)
      return Mcp::McpCommandResult{false, json::object(), "Object not found."};

    SceneObject& object = m_document.objects[static_cast<size_t>(idx)];
    if (arguments.contains("position") && !parseVec3(arguments["position"], &object.position))
      return Mcp::McpCommandResult{false, json::object(), "position must be [x,y,z]."};
    if (arguments.contains("scale") && !parseVec3(arguments["scale"], &object.scale))
      return Mcp::McpCommandResult{false, json::object(), "scale must be [x,y,z]."};
    if (arguments.contains("yaw"))
      object.yaw = arguments["yaw"].get<float>();
    if (arguments.contains("pitch"))
      object.pitch = arguments["pitch"].get<float>();
    if (arguments.contains("roll"))
      object.roll = arguments["roll"].get<float>();
    if (toolName == "editor.update_object") {
      if (arguments.contains("assetId"))
        object.assetId = arguments["assetId"].is_null() ? std::string() : arguments["assetId"].get<std::string>();
      if (arguments.contains("props")) {
        const auto props = parseProps(arguments["props"]);
        for (const auto& entry : props)
          object.props[entry.first] = entry.second;
      }
      if (arguments.contains("components") &&
          !parseComponents(arguments["components"], &object.components)) {
        return Mcp::McpCommandResult{false, json::object(), "components must be an array of objects."};
      }
    }
    m_document.dirty = true;
    if (m_transformCb)
      m_transformCb(object);
    TriggerReload();
    return Mcp::McpCommandResult{true, json{{"updated", summarizeObject(object)}}, std::string()};
  }

  if (toolName == "editor.duplicate") {
    const std::string id = arguments.value("id", std::string());
    const int idx = findIndexById(id);
    if (idx < 0)
      return Mcp::McpCommandResult{false, json::object(), "Object not found."};
    const int count = std::max(1, std::min(8, arguments.value("count", 1)));
    json created = json::array();
    for (int i = 0; i < count; ++i) {
      SceneObject clone = DuplicateObject(m_document, m_document.objects[static_cast<size_t>(idx)]);
      clone.position.x += static_cast<float>(i + 1);
      clone.position.z += static_cast<float>(i + 1);
      m_document.objects.push_back(std::move(clone));
      created.push_back(summarizeObject(m_document.objects.back()));
    }
    m_selectedIndices = {static_cast<int>(m_document.objects.size()) - 1};
    m_document.dirty = true;
    TriggerReload();
    return Mcp::McpCommandResult{true, json{{"duplicates", std::move(created)}}, std::string()};
  }

  if (toolName == "editor.delete") {
    std::vector<int> indices;
    if (arguments.contains("id") && arguments["id"].is_string()) {
      const int idx = findIndexById(arguments["id"].get<std::string>());
      if (idx >= 0)
        indices.push_back(idx);
    }
    if (arguments.contains("ids") && arguments["ids"].is_array()) {
      for (const json& item : arguments["ids"]) {
        if (!item.is_string())
          continue;
        const int idx = findIndexById(item.get<std::string>());
        if (idx >= 0)
          indices.push_back(idx);
      }
    }
    if (indices.empty())
      return Mcp::McpCommandResult{false, json::object(), "No matching objects to delete."};

    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    std::sort(indices.rbegin(), indices.rend());
    for (int idx : indices)
      m_document.objects.erase(m_document.objects.begin() + idx);
    m_selectedIndices.clear();
    m_document.dirty = true;
    TriggerReload();
    return Mcp::McpCommandResult{true,
                                 json{{"deletedCount", static_cast<int>(indices.size())}},
                                 std::string()};
  }

  if (toolName == "editor.save_scene") {
    std::string saveError;
    if (!SaveDocument(&saveError))
      return Mcp::McpCommandResult{false, json::object(), saveError};
    return Mcp::McpCommandResult{true, json{{"saved", true}, {"filePath", m_document.filePath}},
                                 std::string()};
  }

  if (toolName == "editor.reload_scene") {
    TriggerReload();
    return Mcp::McpCommandResult{true, json{{"reloadPending", true}}, std::string()};
  }

  return Mcp::McpCommandResult{false, json::object(), "Unsupported MCP command."};
}

bool EditorLayer::SaveDocument(std::string* outError) {
  if (outError)
    outError->clear();

  std::string path = m_document.filePath.empty() ? "assets/scenes/scene.json" : m_document.filePath;
  m_document.filePath = path;

  LOG_INFO("[Editor] Saving scene to: %s", path.c_str());

  try {
    SceneSerializer::SaveToFile(m_document, path);
    m_document.dirty = false;
    m_lastSavedDocument = m_document;
    LOG_INFO("[Editor] Scene saved OK");
    TriggerReload();  // rebuild scene so changes are immediately visible
    return true;
  } catch (const std::exception& e) {
    LOG_ERROR("[Editor] Save failed: %s", e.what());
    if (outError)
      *outError = e.what();
    return false;
  }
}

void EditorLayer::DiscardUnsavedChanges() {
  if (!m_document.dirty)
    return;

  m_document = m_lastSavedDocument;
  m_selectedIndices.clear();
  m_selectedAssetId.clear();
  TriggerReload();
}

void EditorLayer::RequestDeleteSelectedObjects() {
  if (m_selectedIndices.empty())
    return;

  m_pendingDeleteObjectIndices = m_selectedIndices;
  m_confirmDeleteObjectsOpen = true;
}

void EditorLayer::RequestDeleteAsset(const std::string& assetId) {
  if (assetId.empty())
    return;
  if (m_document.assets.find(assetId) == m_document.assets.end())
    return;

  m_pendingDeleteAssetId = assetId;
  m_confirmDeleteAssetOpen = true;
}

void EditorLayer::OpenRenameObjectModal(int index) {
  if (index < 0 || index >= static_cast<int>(m_document.objects.size()))
    return;
  m_renameObjectIndex = index;
  m_renameObjectDraft = m_document.objects[static_cast<size_t>(index)].id;
  m_renameObjectError.clear();
  m_renameObjectOpen = true;
}

void EditorLayer::AddObject(SceneObjectType type, const std::string& parentId) {
  SceneObject obj;
  obj.id = (type == SceneObjectType::Camera) ? GenerateCameraId(m_document) : GenerateId(m_document);
  obj.type = type;
  ApplySchemaDefaults(obj);
  if (type == SceneObjectType::Camera) {
    obj.props["fov"] = "60";
    obj.props["nearClip"] = "0.1";
    obj.props["farClip"] = "500";
    obj.props["followTargetId"] = "";
  }
  if (!parentId.empty())
    obj.props["parentId"] = parentId;

  m_document.objects.push_back(std::move(obj));
  m_selectedIndices = {static_cast<int>(m_document.objects.size()) - 1};
  m_document.dirty = true;
  TriggerReload();
}

void EditorLayer::AddObjectFromSelectedAsset(const std::string& parentId) {
  const auto it = m_document.assets.find(m_selectedAssetId);
  if (m_selectedAssetId.empty() || it == m_document.assets.end())
    return;

  SceneObject obj = MakeObjectFromAsset(m_document, m_selectedAssetId, m_schema);
  if (!parentId.empty())
    obj.props["parentId"] = parentId;
  m_document.objects.push_back(std::move(obj));
  m_selectedIndices = {static_cast<int>(m_document.objects.size()) - 1};
  m_document.dirty = true;
  TriggerReload();
}

void EditorLayer::DuplicatePrimarySelection() {
  const int primaryIdx = PrimaryIdx();
  if (primaryIdx < 0 || primaryIdx >= static_cast<int>(m_document.objects.size()))
    return;
  SceneObject clone = DuplicateObject(m_document, m_document.objects[static_cast<size_t>(primaryIdx)]);
  clone.position.x += 1.0f;
  clone.position.z += 1.0f;
  m_document.objects.push_back(std::move(clone));
  m_selectedIndices = {static_cast<int>(m_document.objects.size()) - 1};
  m_document.dirty = true;
  TriggerReload();
}

SceneObject EditorLayer::MakeObjectFromAsset(const SceneDocument& doc,
                                             const std::string& assetId,
                                             const EditorSchema& schema) {
  SceneObject obj;
  obj.id = GenerateId(doc);
  obj.type = SceneObjectType::Prop;
  obj.assetId = assetId;

  const TypeSchema* typeSchema = schema.GetSchema(obj.type);
  if (typeSchema) {
    for (const auto& fd : typeSchema->fields)
      obj.props[fd.key] = fd.defaultValue;
  }

  return obj;
}

SceneObject EditorLayer::DuplicateObject(const SceneDocument& doc, const SceneObject& src) {
  SceneObject clone = src;
  clone.id = GenerateId(doc);
  clone.props.erase("_eid");
  return clone;
}

std::string EditorLayer::BuildSelectionRefCode(const SceneObject& obj, int idx) const {
  auto typeToStr = [](SceneObjectType t) {
    switch (t) {
      case SceneObjectType::Panel:
        return "Panel";
      case SceneObjectType::Prop:
        return "Prop";
      case SceneObjectType::Light:
        return "Light";
      case SceneObjectType::Camera:
        return "Camera";
    }
    return "Unknown";
  };

  auto getProp = [&](const char* key) -> std::string {
    auto it = obj.props.find(key);
    return (it != obj.props.end()) ? it->second : "";
  };

  std::ostringstream ss;
  ss.setf(std::ios::fixed);
  ss.precision(4);
  const std::string scenePath = m_document.filePath.empty() ? "assets/scenes/scene.json" : m_document.filePath;
  ss << "EDITOR_REF"
     << " scene=\"" << scenePath << "\""
     << " id=" << obj.id
     << " idx=" << idx
     << " type=" << typeToStr(obj.type)
     << " pos=(" << obj.position.x << "," << obj.position.y << "," << obj.position.z << ")"
     << " scale=(" << obj.scale.x << "," << obj.scale.y << "," << obj.scale.z << ")"
     << " yaw=" << obj.yaw;

  const std::string mesh = getProp("mesh");
  if (!mesh.empty())
    ss << " mesh=\"" << mesh << "\"";

  const std::string eid = getProp("_eid");
  if (!eid.empty())
    ss << " _eid=" << eid;

  return ss.str();
}

std::string EditorLayer::GenerateId(const SceneDocument& doc) {
  std::unordered_set<std::string> existingIds;
  existingIds.reserve(doc.objects.size() * 2);
  for (const auto& obj : doc.objects)
    existingIds.insert(obj.id);

  for (int i = 0; i < 1000000; ++i) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "obj_%03d", i);
    if (existingIds.find(buf) == existingIds.end())
      return buf;
  }
  return "obj_new";
}

std::string EditorLayer::GenerateCameraId(const SceneDocument& doc) {
  std::unordered_set<std::string> existingIds;
  existingIds.reserve(doc.objects.size() * 2);
  for (const auto& obj : doc.objects)
    existingIds.insert(obj.id);

  for (int i = 0; i < 1000000; ++i) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "cam_%03d", i);
    if (existingIds.find(buf) == existingIds.end())
      return buf;
  }
  return "cam_new";
}

// ---- Fly camera --------------------------------------------------------------

void EditorLayer::ToggleFlyMode(Camera& cam) {
  m_flyMode = !m_flyMode;
  m_flyCamInitialized = false;
  m_prevCursorInit = false;
  glfwSetInputMode(m_window, GLFW_CURSOR,
                   m_flyMode ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
  (void)cam;  // camera sync happens lazily in UpdateFlyCamera
}

void EditorLayer::UpdateFlyCamera(float dt, Camera& cam) {
  // Fly mode always uses world-up to avoid inverted controls after view snaps.
  cam.up = Vec3::Up();

  // --- Sync yaw/pitch from live camera on first frame ---
  if (!m_flyCamInitialized) {
    Vec3 dir = cam.target - cam.position;
    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len > 0.001f) {
      dir.x /= len;
      dir.y /= len;
      dir.z /= len;
    }
    m_flyPitch = std::asin(std::max(-1.0f, std::min(1.0f, dir.y))) * (180.0f / PI);

    // If the camera is nearly vertical (Top/Bottom snap), yaw is ill-defined.
    // Keep previous yaw to avoid sudden axis flips when entering fly mode.
    const float horizLen = std::sqrt(dir.x * dir.x + dir.z * dir.z);
    if (horizLen > 0.0001f)
      m_flyYaw = -std::atan2(dir.x, -dir.z) * (180.0f / PI);

    m_flyCamInitialized = true;
  }

  // --- Mouse look ---
  double cx, cy;
  glfwGetCursorPos(m_window, &cx, &cy);
  if (!m_prevCursorInit) {
    m_prevCursorX = cx;
    m_prevCursorY = cy;
    m_prevCursorInit = true;
  }
  const float MOUSE_SENS = 0.15f;
  m_flyYaw -= static_cast<float>(cx - m_prevCursorX) * MOUSE_SENS;
  m_flyPitch -= static_cast<float>(cy - m_prevCursorY) * MOUSE_SENS;
  m_flyPitch = std::max(-89.0f, std::min(89.0f, m_flyPitch));
  m_prevCursorX = cx;
  m_prevCursorY = cy;

  // --- Compute forward/right from yaw/pitch ---
  const float yawRad = m_flyYaw * (PI / 180.0f);
  const float pitchRad = m_flyPitch * (PI / 180.0f);
  Vec3 forward = {-std::sin(yawRad) * std::cos(pitchRad), std::sin(pitchRad),
                  -std::cos(yawRad) * std::cos(pitchRad)};
  Vec3 right = Vec3::Cross(forward, {0.0f, 1.0f, 0.0f});
  float rLen = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
  if (rLen > 0.001f) {
    right.x /= rLen;
    right.y /= rLen;
    right.z /= rLen;
  }

  // --- WASD movement ---
  const float FLY_SPEED = 8.0f;
  Vec3 move = {0.0f, 0.0f, 0.0f};
  if (glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS)
    move = {move.x + forward.x, move.y + forward.y, move.z + forward.z};
  if (glfwGetKey(m_window, GLFW_KEY_S) == GLFW_PRESS)
    move = {move.x - forward.x, move.y - forward.y, move.z - forward.z};
  if (glfwGetKey(m_window, GLFW_KEY_A) == GLFW_PRESS)
    move = {move.x - right.x, move.y - right.y, move.z - right.z};
  if (glfwGetKey(m_window, GLFW_KEY_D) == GLFW_PRESS)
    move = {move.x + right.x, move.y + right.y, move.z + right.z};

  float mLen = std::sqrt(move.x * move.x + move.y * move.y + move.z * move.z);
  if (mLen > 0.001f) {
    cam.position.x += (move.x / mLen) * FLY_SPEED * dt;
    cam.position.y += (move.y / mLen) * FLY_SPEED * dt;
    cam.position.z += (move.z / mLen) * FLY_SPEED * dt;
  }
  cam.target = {cam.position.x + forward.x, cam.position.y + forward.y,
                cam.position.z + forward.z};
}

void EditorLayer::ApplySchemaDefaults(SceneObject& obj) const {
  const TypeSchema* schema = m_schema.GetSchema(obj.type);
  if (!schema)
    return;
  for (const auto& fd : schema->fields)
    if (obj.props.find(fd.key) == obj.props.end())
      obj.props[fd.key] = fd.defaultValue;
}

// ---- Wireframe overlay -------------------------------------------------------

void EditorLayer::DrawWireframeOverlay(const Camera& cam) {
  if (!m_wireframeMode || !m_wireframeShader.IsValid())
    return;

  // Clear the solid scene so only wireframe edges are visible
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  Renderer::BeginScene(cam);
  for (const auto& obj : m_document.objects) {
    if (obj.type != SceneObjectType::Prop)
      continue;

    // Resolve mesh path from assetId or inline "mesh" prop
    std::string meshPath;
    if (!obj.assetId.empty()) {
      const auto assetIt = m_document.assets.find(obj.assetId);
      if (assetIt == m_document.assets.end())
        continue;
      meshPath = assetIt->second.mesh;
    } else {
      const auto propIt = obj.props.find("mesh");
      if (propIt == obj.props.end())
        continue;
      meshPath = propIt->second;
    }
    if (meshPath.empty())
      continue;

    // Reuse the thumbnail mesh cache (already loaded for previews)
    auto* meshEntry = TryLoadAssetMesh(meshPath);
    if (!meshEntry || !meshEntry->mesh)
      continue;

    // Build model matrix: T * Ry(yaw)
    const float yawRad = obj.yaw * (3.14159265f / 180.0f);
    const Mat4 model =
        Mat4::Translate(obj.position) * Mat4::RotateY(yawRad);

    Renderer::SubmitWireframe(*meshEntry->mesh, model, m_wireframeShader, 0.3f, 0.85f, 0.3f);
  }
}

// ---- Viewport drag-drop target -----------------------------------------------

void EditorLayer::DrawViewportDropTarget(const Camera& cam, int screenW, int screenH) {
  // Only show when an ASSET_ID drag-drop is actively in progress.
  // This prevents the full-screen window from intercepting normal mouse clicks.
  const ImGuiPayload* activeDrag = ImGui::GetDragDropPayload();
  if (!activeDrag || !activeDrag->IsDataType("ASSET_ID"))
    return;

  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(static_cast<float>(screenW), static_cast<float>(screenH)));
  ImGui::SetNextWindowBgAlpha(0.0f);
  // No NoInputs — drop target must be hoverable to accept the payload
  ImGui::Begin("##viewport_drop", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings);

  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
      const std::string assetId(static_cast<const char*>(payload->Data));
      const ImVec2 mp = io.MousePos;

      // Ray-cast to y=0 floor plane
      Ray ray = ScreenToRay(mp.x, mp.y, screenW, screenH, cam);
      Vec3 hitPos = cam.position;
      hitPos.y = 0.0f;
      if (std::abs(ray.direction.y) > 1e-5f) {
        const float t = -ray.origin.y / ray.direction.y;
        if (t > 0.0f)
          hitPos = {ray.origin.x + ray.direction.x * t, 0.0f,
                    ray.origin.z + ray.direction.z * t};
      }

      SceneObject obj = MakeObjectFromAsset(m_document, assetId, m_schema);
      obj.position = hitPos;
      m_document.objects.push_back(std::move(obj));
      m_selectedIndices = {static_cast<int>(m_document.objects.size()) - 1};
      m_document.dirty = true;
      TriggerReload();
    }
    ImGui::EndDragDropTarget();
  }

  ImGui::End();
}

}  // namespace Editor
}  // namespace Monolith
