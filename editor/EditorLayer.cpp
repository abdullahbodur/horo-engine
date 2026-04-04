#include "editor/EditorLayer.h"

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

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "core/Logger.h"
#include "editor/EditorAssetImport.h"
#include "editor/EditorSearch.h"
#include "editor/EditorUiLogic.h"
#include "editor/Raycaster.h"
#include "math/Mat4.h"
#include "math/MathUtils.h"
#include "math/Transform.h"
#include "editor/SceneSerializer.h"
#include "renderer/DebugDraw.h"
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
constexpr float kLeftDockW = 308.0f;

// Split Objects (top) vs Assets (bottom) within [toolbar .. status]. Small gap so they
// never z-fight at the seam.
static void ComputeLeftColumnLayout(const ImGuiIO& io,
                                    float* outObjectsTop,
                                    float* outObjectsH,
                                    float* outAssetsTop,
                                    float* outAssetsH) {
  const float workTop = kEditorToolbarH;
  const float workBottom = io.DisplaySize.y - kEditorStatusH;
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
                                             const std::function<void(const SceneObject&)>& transformCb) {
  if (parentIdx < 0 || parentIdx >= static_cast<int>(doc.objects.size()))
    return;

  const Quaternion deltaRot = newParentRot * oldParentRot.Inverse();
  for (int i = 0; i < static_cast<int>(doc.objects.size()); ++i) {
    if (i == parentIdx || !IsDescendantOf(doc, i, parentIdx))
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
static std::string ImportTextureToAssetsModels(const std::string& pickedPath, std::string* outError) {
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

  const fs::path destDir("assets/models");
  fs::create_directories(destDir, ec);
  if (ec) {
    if (outError)
      *outError = "Cannot create assets/models: " + ec.message();
    return {};
  }

  const fs::path dest = destDir / src.filename();
  fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    if (outError)
      *outError = "Copy failed: " + ec.message();
    return {};
  }
  return dest.generic_string();
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

// Copy picked .obj into assets/models (and companion MTL/textures). Returns project-relative
// mesh path (e.g. assets/models/foo.obj) or empty on failure.
static std::string ImportObjFileIntoAssetsModels(const std::string& pickedPath, std::string* outError) {
  namespace fs = std::filesystem;
  if (pickedPath.empty())
    return {};
  if (!IsObjFilePath(pickedPath)) {
    if (outError)
      *outError = "Selected file is not .obj";
    return {};
  }
  const fs::path src(pickedPath);
  const fs::path destDir("assets/models");
  std::error_code ec;
  fs::create_directories(destDir, ec);
  if (ec) {
    if (outError)
      *outError = "Cannot create assets/models: " + ec.message();
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
  return MeshTagFromImportedPath(pickedPath);
}

}  // namespace

// ---- Lifecycle ---------------------------------------------------------------

void EditorLayer::Init(GLFWwindow* window) {
  m_window = window;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGui::GetIO().IniFilename = nullptr;  // don't write imgui.ini

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 410");

  m_schema.LoadFromFile("assets/editor_schema.json");
}

void EditorLayer::Shutdown() {
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
}

void EditorLayer::Toggle() {
  m_active = !m_active;
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
}

void EditorLayer::LoadDocument(SceneDocument doc) {
  if (doc.filePath.empty())
    doc.filePath = "assets/scenes/dungeon.json";

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
      const std::string rel = ImportTextureToAssetsModels(path, &err);
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
        const std::string rel = ImportTextureToAssetsModels(path, &err);
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
    const std::string meshTag = ImportObjFileIntoAssetsModels(path, &err);
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
    if (ShouldFinalizeEditorClose(m_closeRequested, m_wantsReload)) {
      Toggle();
      return false;
    }

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
    if (ShouldHandleEditorEscape(currEsc, m_prevEsc, io.WantTextInput, ImGui::IsAnyItemActive(),
                                 hasBlockingPopup)) {
      if (ResolveEditorExitDecision(m_document.dirty) ==
          EditorExitDecision::PromptUnsavedConfirm) {
        m_confirmExitOpen = true;
        m_exitConfirmError.clear();
      } else {
        m_closeRequested = true;
      }
    }
    m_prevEsc = currEsc;

    // Tab toggles fly mode
    bool currTab = glfwGetKey(m_window, GLFW_KEY_TAB) == GLFW_PRESS;
    if (currTab && !m_prevTab)
      ToggleFlyMode(cam);
    m_prevTab = currTab;

    if (m_flyMode) {
      UpdateFlyCamera(dt, cam);
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

      HandlePicking(cam, screenW, screenH);

      // Del key — delete all selected objects immediately
      bool currDel = glfwGetKey(m_window, GLFW_KEY_DELETE) == GLFW_PRESS;
      if (ShouldRequestDeleteSelection(currDel, m_prevDel, !m_selectedIndices.empty()))
        RequestDeleteSelectedObjects();
      m_prevDel = currDel;
    }

    ApplyPendingViewSnap(cam);
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
      const std::string meshTag = ImportObjFileIntoAssetsModels(chosen, &err);
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
      const std::string rel = ImportTextureToAssetsModels(PickTextureFilePath(), &err);
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
      const std::string rel = ImportTextureToAssetsModels(PickTextureFilePath(), &err);
      if (!rel.empty()) {
        m_document.assets[id].albedoMap = rel;
        m_document.dirty = true;
      } else if (!err.empty())
        LOG_WARN("Texture browse: %s", err.c_str());
      break;
    }
  }
}

void EditorLayer::Render(const Camera& cam) {
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
    DrawToolbar();
    DrawViewGimbal(cam);
    DrawObjectList();
    DrawAssetsPanel();
    DrawPropertiesPanel();
    DrawStatusBar();
    DrawHelpPopup();
    DrawQuickOpenPopup();
    DrawDeleteConfirmModals();
    DrawExitConfirmModal();
    DrawSelectionHighlight();  // queues to DebugDraw
  }
  DrawHotReloadOverlay();
  DrawClipboardToast();

  // Flush any queued debug primitives (selection box, etc.) before ImGui
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

  auto addObj = [&](SceneObjectType type, const char* label) {
    if (ImGui::Button(label)) {
      SceneObject o;
      o.id = GenerateId(m_document);
      o.type = type;
      ApplySchemaDefaults(o);
      m_document.objects.push_back(std::move(o));
      m_selectedIndices = {static_cast<int>(m_document.objects.size()) - 1};
      m_document.dirty = true;
      TriggerReload();
    }
    ImGui::SameLine();
  };

  addObj(SceneObjectType::Panel, "+ Panel");
  addObj(SceneObjectType::Prop, "+ Prop");
  addObj(SceneObjectType::Light, "+ Light");

  if (ImGui::Button("+ Camera")) {
    SceneObject o;
    o.id = GenerateCameraId(m_document);
    o.type = SceneObjectType::Camera;
    o.props["fov"] = "60";
    o.props["nearClip"] = "0.1";
    o.props["farClip"] = "500";
    o.props["followTargetId"] = "";
    m_document.objects.push_back(std::move(o));
    m_selectedIndices = {static_cast<int>(m_document.objects.size()) - 1};
    m_document.dirty = true;
    TriggerReload();
  }
  ImGui::SameLine();

  const bool hasSelectedAsset = !m_selectedAssetId.empty() &&
                                m_document.assets.find(m_selectedAssetId) != m_document.assets.end();
  if (!hasSelectedAsset)
    ImGui::BeginDisabled();
  if (ImGui::Button("+ Prop from Asset")) {
    SceneObject obj = MakeObjectFromAsset(m_document, m_selectedAssetId, m_schema);
    m_document.objects.push_back(std::move(obj));
    m_selectedIndices = {static_cast<int>(m_document.objects.size()) - 1};
    m_document.dirty = true;
    TriggerReload();
  }
  if (!hasSelectedAsset)
    ImGui::EndDisabled();
  ImGui::SameLine();

  const int primaryIdx = PrimaryIdx();
  const bool canDuplicate = primaryIdx >= 0 && primaryIdx < static_cast<int>(m_document.objects.size());
  if (!canDuplicate)
    ImGui::BeginDisabled();
  if (ImGui::Button("Duplicate")) {
    SceneObject clone = DuplicateObject(m_document, m_document.objects[primaryIdx]);
    clone.position.x += 1.0f;
    clone.position.z += 1.0f;
    m_document.objects.push_back(std::move(clone));
    m_selectedIndices = {static_cast<int>(m_document.objects.size()) - 1};
    m_document.dirty = true;
    TriggerReload();
  }
  if (!canDuplicate)
    ImGui::EndDisabled();
  ImGui::SameLine();

  // Fly camera toggle — green when active, Tab also toggles
  const bool flyActiveNow = m_flyMode;  // capture before button may flip it
  if (flyActiveNow)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.65f, 0.15f, 1.0f));
  if (ImGui::Button(flyActiveNow ? "Fly [ON]" : "Fly")) {
    m_flyMode = !m_flyMode;
    m_flyCamInitialized = false;
    m_prevCursorInit = false;
    glfwSetInputMode(m_window, GLFW_CURSOR,
                     m_flyMode ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
  }
  if (flyActiveNow) {
    ImGui::PopStyleColor();  // always balanced with the push above
    ImGui::SameLine();
    ImGui::TextDisabled("WASD + mouse  |  Tab to exit");
  }
  ImGui::SameLine();
  ImGui::TextDisabled("Ctrl/Cmd+Shift+C copy ref");
  ImGui::SameLine();
  ImGui::TextDisabled("Help: ? / F1");
  ImGui::SameLine();
  ImGui::TextDisabled("Quick Open: Ctrl/Cmd+P");
  ImGui::SameLine();

  // Right-aligned controls
  const float rightW = 260.0f;
  ImGui::SetCursorPosX(io.DisplaySize.x - rightW);

  if (ImGui::Button("Load")) {
    std::string path =
        m_document.filePath.empty() ? "assets/scenes/dungeon.json" : m_document.filePath;
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

  if (ImGui::Button("Exit [F10]")) {
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

void EditorLayer::DrawViewGimbal(const Camera& cam) {
  ImGuiIO& io = ImGui::GetIO();
  constexpr float kPanelW = 280.0f;
  constexpr float kWinW = 128.0f;
  constexpr float kWinH = 138.0f;
  const float wx = io.DisplaySize.x - kPanelW - kWinW - 10.0f;
  const float wy = 42.0f;

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

// ---- Object list panel -------------------------------------------------------

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
      "Objects", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);

  char searchBuf[256] = {};
  std::snprintf(searchBuf, sizeof(searchBuf), "%s", m_objectSearchQuery.c_str());
  ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, true);
  if (ImGui::InputTextWithHint("##object_search", "Search objects...", searchBuf, sizeof(searchBuf)))
    m_objectSearchQuery = searchBuf;
  ImGui::PopItemFlag();
  ImGui::Separator();

  int shownObjectCount = 0;
  if (!m_objectSearchQuery.empty()) {
    for (int i = 0; i < static_cast<int>(m_document.objects.size()); ++i) {
      ImGui::PushID(i);
      auto& obj = m_document.objects[i];
      if (!ObjectMatchesQuickOpenQuery(obj, m_objectSearchQuery))
      {
        ImGui::PopID();
        continue;
      }
      const char* typeName = ObjectTypeLabel(obj.type);

      char selectableId[32];
      std::snprintf(selectableId, sizeof(selectableId), "##obj_%d", i);

      const float lineH = ImGui::GetTextLineHeight();
      const float rowH = obj.assetId.empty() ? lineH : lineH * 2.0f + 2.0f;

      if (ImGui::Selectable(selectableId, IsSelected(i), 0, ImVec2(0, rowH))) {
        if (ImGui::GetIO().KeyShift)
          ToggleSelect(i);
        else
          m_selectedIndices = {i};
      }
      ImGui::SameLine();
      {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::BeginGroup();
        ImGui::TextDisabled("%s", typeName);
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::Text("%s", obj.id.c_str());
        if (!obj.assetId.empty()) {
          ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + lineH + 2.0f));
          ImGui::TextDisabled("> %s", obj.assetId.c_str());
        }
        ImGui::EndGroup();
      }

      if (ImGui::BeginPopupContextItem("obj_ctx")) {
        if (ImGui::MenuItem("Rename...")) {
          m_renameObjectIndex = i;
          m_renameObjectDraft = obj.id;
          m_renameObjectError.clear();
          m_renameObjectOpen = true;
        }

        if (ImGui::MenuItem("Duplicate")) {
          SceneObject clone = DuplicateObject(m_document, obj);
          clone.position.x += 1.0f;
          clone.position.z += 1.0f;
          m_document.objects.push_back(std::move(clone));
          m_selectedIndices = {static_cast<int>(m_document.objects.size()) - 1};
          m_document.dirty = true;
          TriggerReload();
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
  } else {
    std::unordered_map<std::string, int> idToIndex;
    idToIndex.reserve(m_document.objects.size());
    for (int i = 0; i < static_cast<int>(m_document.objects.size()); ++i)
      idToIndex[m_document.objects[static_cast<size_t>(i)].id] = i;

    std::vector<std::vector<int>> children(m_document.objects.size());
    std::vector<int> roots;
    roots.reserve(m_document.objects.size());
    for (int i = 0; i < static_cast<int>(m_document.objects.size()); ++i) {
      const SceneObject& obj = m_document.objects[static_cast<size_t>(i)];
      int p = -1;
      const auto pIt = idToIndex.find(GetParentId(obj));
      if (pIt != idToIndex.end())
        p = pIt->second;
      if (p >= 0 && p != i)
        children[static_cast<size_t>(p)].push_back(i);
      else
        roots.push_back(i);
    }

    std::function<void(int, int)> drawNode;
    drawNode = [this, &drawNode, &children, &shownObjectCount](int idx, int depth) {
      if (idx < 0 || idx >= static_cast<int>(m_document.objects.size()))
        return;
      SceneObject& obj = m_document.objects[static_cast<size_t>(idx)];
      ++shownObjectCount;

      ImGui::PushID(idx);
      const float treeIndent = 14.0f;
      if (depth > 0)
        ImGui::Indent(treeIndent);
      const bool hasChildren = !children[static_cast<size_t>(idx)].empty();
      ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
      if (!hasChildren)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
      if (IsSelected(idx))
        flags |= ImGuiTreeNodeFlags_Selected;

      const char* typeName = ObjectTypeLabel(obj.type);
      std::string label = std::string(typeName) + " " + obj.id;
      const bool open = ImGui::TreeNodeEx("##obj_tree", flags, "%s", label.c_str());
      if (ImGui::IsItemClicked()) {
        if (ImGui::GetIO().KeyShift)
          ToggleSelect(idx);
        else
          m_selectedIndices = {idx};
      }

      if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("SCENE_OBJECT_INDEX", &idx, sizeof(int));
        ImGui::TextUnformatted(obj.id.c_str());
        ImGui::EndDragDropSource();
      }
      if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT_INDEX")) {
          if (payload->DataSize == sizeof(int)) {
            const int src = *static_cast<const int*>(payload->Data);
            const bool valid = src >= 0 && src < static_cast<int>(m_document.objects.size()) &&
                               src != idx && !IsDescendantOf(m_document, idx, src);
            if (valid) {
              m_document.objects[static_cast<size_t>(src)].props["parentId"] = obj.id;
              m_document.dirty = true;
            }
          }
        }
        ImGui::EndDragDropTarget();
      }

      if (ImGui::BeginPopupContextItem("obj_ctx")) {
        if (ImGui::MenuItem("Rename...")) {
          m_renameObjectIndex = idx;
          m_renameObjectDraft = obj.id;
          m_renameObjectError.clear();
          m_renameObjectOpen = true;
        }

        if (ImGui::MenuItem("Duplicate")) {
          SceneObject clone = DuplicateObject(m_document, obj);
          clone.position.x += 1.0f;
          clone.position.z += 1.0f;
          m_document.objects.push_back(std::move(clone));
          m_selectedIndices = {static_cast<int>(m_document.objects.size()) - 1};
          m_document.dirty = true;
          TriggerReload();
        }

        if (ImGui::MenuItem("Delete")) {
          m_selectedIndices = {idx};
          RequestDeleteSelectedObjects();
        }

        ImGui::Separator();
        const bool hasParent = !GetParentId(obj).empty();
        if (ImGui::MenuItem("Unparent", nullptr, false, hasParent)) {
          obj.props.erase("parentId");
          m_document.dirty = true;
        }
        ImGui::EndPopup();
      }

      if (open && hasChildren) {
        for (int childIdx : children[static_cast<size_t>(idx)])
          drawNode(childIdx, depth + 1);
        ImGui::TreePop();
      }

      if (depth > 0)
        ImGui::Unindent(treeIndent);
      ImGui::PopID();
    };

    for (int rootIdx : roots)
      drawNode(rootIdx, 0);
  }

  const FilteredListState objectState =
      EvaluateFilteredListState(m_document.objects.size(), shownObjectCount, m_objectSearchQuery);
  if (objectState != FilteredListState::None) {
    ImGui::Spacing();
    if (objectState == FilteredListState::EmptyData) {
      ImGui::TextDisabled("No objects in scene");
      ImGui::TextDisabled("Tip: add from '+ Prop from Asset' or create one from Assets panel.");
    } else if (objectState == FilteredListState::NoMatches) {
      ImGui::TextDisabled("No objects match '%s'", m_objectSearchQuery.c_str());
      if (ImGui::Button("Clear Object Search"))
        m_objectSearchQuery.clear();
    }
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

  ImGui::TextDisabled("Registry");
  ImGui::SameLine();
  ImGui::SetCursorPosX(kLeftDockW - 74.0f);
  if (ImGui::Button("Search", ImVec2(64.0f, 0.0f))) {
    m_assetSearchOpen = true;
    m_assetSearchQuery.clear();
  }
  ImGui::Separator();

  const float belowHeader = ImGui::GetContentRegionAvail().y;
  const float footerMinH = std::clamp(belowHeader * 0.36f, 150.0f, 280.0f);
  const float listH = std::max(48.0f, belowHeader - footerMinH);

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

  bool openNewAssetSection = m_openNewAssetHeader;
  if (m_openNewAssetHeader)
    m_openNewAssetHeader = false;

  ImGui::BeginChild("##asset_registry_scroll", ImVec2(0, listH), true);

  int shownAssetCount = 0;
  for (const auto& assetId : assetIds) {
    const auto assetIt = m_document.assets.find(assetId);
    if (assetIt == m_document.assets.end())
      continue;
    const auto& asset = assetIt->second;

    if (!AssetMatchesQuickOpenQuery(assetId, asset, m_assetSearchQuery))
      continue;

    ++shownAssetCount;

    ImGui::PushID(assetId.c_str());
    const bool isSelectedAsset = (m_selectedAssetId == assetId);

    // Tint the row background when selected
    if (isSelectedAsset)
      ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.45f, 0.70f, 0.55f));
    if (ImGui::Selectable("##row", isSelectedAsset, ImGuiSelectableFlags_SpanAllColumns))
      m_selectedAssetId = isSelectedAsset ? std::string() : assetId;
    if (isSelectedAsset)
      ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::Text("%s", assetId.c_str());

    // Expanded inspector — vertical stack below the list row (no label-on-right cramming)
    if (isSelectedAsset) {
      ImGui::SameLine();
      ImGui::TextDisabled("[x]");
      if (ImGui::IsItemClicked())
        m_selectedAssetId.clear();

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 8.0f));
      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.12f, 0.16f, 0.95f));
      ImGui::BeginChild("##asset_detail", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders,
                        ImGuiWindowFlags_NoScrollbar);

      const float innerW = ImGui::GetContentRegionAvail().x;
      ImGui::PushItemWidth(innerW);

      ImGui::TextDisabled("Asset");
      ImGui::TextUnformatted(assetId.c_str());
      if (ImGui::Button("Deselect##sel_asset_close", ImVec2(innerW, 0.0f)))
        m_selectedAssetId.clear();

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      ImGui::TextDisabled("Mesh path");
      char meshEditBuf[512] = {};
      std::snprintf(meshEditBuf, sizeof(meshEditBuf), "%s", assetIt->second.mesh.c_str());
      if (ImGui::InputText("##sel_mesh", meshEditBuf, sizeof(meshEditBuf))) {
        m_document.assets[assetId].mesh = meshEditBuf;
        m_document.dirty = true;
      }

      ImGui::TextDisabled("Render scale (x, y, z)");
      char scaleEditBuf[128] = {};
      std::snprintf(scaleEditBuf, sizeof(scaleEditBuf), "%s", assetIt->second.renderScale.c_str());
      if (ImGui::InputText("##sel_scale", scaleEditBuf, sizeof(scaleEditBuf))) {
        m_document.assets[assetId].renderScale = scaleEditBuf;
        m_document.dirty = true;
        TriggerReload();
      }

      ImGui::TextDisabled("Albedo map (optional)");
      const ImVec2 selAlbLabelMin = ImGui::GetItemRectMin();
      const ImVec2 selAlbLabelMax = ImGui::GetItemRectMax();
      char albBuf[512] = {};
      std::snprintf(albBuf, sizeof(albBuf), "%s", assetIt->second.albedoMap.c_str());
      if (ImGui::InputText("##sel_alb", albBuf, sizeof(albBuf))) {
        m_document.assets[assetId].albedoMap = albBuf;
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
      if (ImGui::Button("Browse texture...##alb_pick_asset", ImVec2(innerW, 0.0f))) {
        m_deferredFilePick = DeferredFilePick::SelectedAssetAlbedo;
      }
#else
      ImGui::BeginDisabled();
      ImGui::Button("Browse texture...##alb_pick_asset", ImVec2(innerW, 0.0f));
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Texture file dialog is not available on this platform.");
#endif

      ImGui::Spacing();
      const float gap = ImGui::GetStyle().ItemSpacing.x;
      const float btnW = std::max(60.0f, (innerW - gap) * 0.5f);
      if (ImGui::Button("Add Prop##sel_add", ImVec2(btnW, 0.0f))) {
        SceneObject obj = MakeObjectFromAsset(m_document, assetId, m_schema);
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
        RequestDeleteAsset(assetId);
      ImGui::PopStyleColor(3);

      ImGui::PopItemWidth();
      ImGui::EndChild();
      ImGui::PopStyleColor();
      ImGui::PopStyleVar();
    }

    ImGui::Spacing();
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
        openNewAssetSection = true;
    } else if (assetState == FilteredListState::NoMatches) {
      ImGui::TextDisabled("No assets match '%s'", m_assetSearchQuery.c_str());
      if (ImGui::Button("Clear Asset Search"))
        m_assetSearchQuery.clear();
    }
  }

  ImGui::EndChild();

  ImGui::Separator();

  const float footerH = ImGui::GetContentRegionAvail().y;
  ImGui::BeginChild("##new_asset_footer", ImVec2(0, footerH), true);

  const float footerInnerW = ImGui::GetContentRegionAvail().x;
  // Use almost full footer width so labels + fields are not clipped (was capped ~228px).
  const float blockW = std::max(160.0f, footerInnerW - 12.0f);
  const float padX = std::max(0.0f, (footerInnerW - blockW) * 0.5f);
  ImGui::Dummy(ImVec2(0.0f, 6.0f));
  ImGui::Dummy(ImVec2(padX, 0.0f));
  ImGui::SameLine(0.0f, 0.0f);
  ImGui::BeginGroup();
  ImGui::PushItemWidth(blockW);

  if (openNewAssetSection)
    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
  if (ImGui::CollapsingHeader("+ New Asset")) {
    // -- Import from file -------------------------------------------------------
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

    // -- Manual fields (label above field: avoids clipped right-side labels in narrow panel)
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
    if (ImGui::Button("Browse texture...##alb_pick_draft", ImVec2(blockW, 0.0f))) {
      m_deferredFilePick = DeferredFilePick::NewAssetAlbedo;
    }
#else
    ImGui::BeginDisabled();
    ImGui::Button("Browse texture...##alb_pick_draft", ImVec2(blockW, 0.0f));
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("Texture file dialog is not available on this platform.");
#endif

    const bool canCreate = !m_assetDraftId.empty() && !m_assetDraftMesh.empty();
    if (!canCreate)
      ImGui::BeginDisabled();
    if (ImGui::Button("Create Asset", ImVec2(blockW, 0.0f))) {
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
    }
    if (!canCreate)
      ImGui::EndDisabled();
  }

  ImGui::PopItemWidth();
  ImGui::EndGroup();

  ImGui::EndChild();

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
  const float workBottom = io.DisplaySize.y - kEditorStatusH;

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
    ImGui::TextDisabled("No selection");
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

    // Remove button on the same line, right-aligned
    float btnW = ImGui::CalcTextSize("x").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - btnW);
    if (ImGui::SmallButton("x"))
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

bool EditorLayer::SaveDocument(std::string* outError) {
  if (outError)
    outError->clear();

  std::string path = m_document.filePath.empty() ? "assets/scenes/dungeon.json" : m_document.filePath;
  m_document.filePath = path;

  try {
    SceneSerializer::SaveToFile(m_document, path);
    m_document.dirty = false;
    m_lastSavedDocument = m_document;
    TriggerReload();  // rebuild scene so changes are immediately visible
    return true;
  } catch (const std::exception& e) {
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
  const std::string scenePath = m_document.filePath.empty() ? "assets/scenes/dungeon.json" : m_document.filePath;
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

}  // namespace Editor
}  // namespace Monolith
