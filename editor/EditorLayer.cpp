#include "editor/EditorLayer.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

#include "core/Logger.h"
#include "editor/Raycaster.h"
#include "editor/SceneSerializer.h"
#include "math/MathUtils.h"
#include "math/Vec4.h"
#include "renderer/DebugDraw.h"

namespace Monolith {
namespace Editor {

namespace {
const char* kDefaultScenePath = "assets/scenes/dungeon.json";
}

const char* EditorLayer::TypeToLabel(SceneObjectType type) {
  switch (type) {
    case SceneObjectType::Prop:
      return "Prop";
    case SceneObjectType::Light:
      return "Light";
    default:
      return "Panel";
  }
}

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
  glfwSetInputMode(m_window, GLFW_CURSOR, m_active ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
  m_prevMouseL = false;
  m_selectedIndices.clear();
}

void EditorLayer::LoadDocument(SceneDocument doc) {
  if (doc.filePath.empty())
    doc.filePath = kDefaultScenePath;
  m_document = std::move(doc);
  m_selectedIndices.clear();
  if (!m_document.assets.empty())
    m_selectedAssetId = m_document.assets.begin()->first;
  else
    m_selectedAssetId.clear();
}

// ---- Per-frame update --------------------------------------------------------

bool EditorLayer::OnUpdate(float dt, Camera& cam, int screenW, int screenH) {
  if (m_clipboardToastTime > 0.0f)
    m_clipboardToastTime = std::max(0.0f, m_clipboardToastTime - dt);

  if (m_active) {
    ImGuiIO& io = ImGui::GetIO();

    // Tab toggles fly mode
    bool currTab = glfwGetKey(m_window, GLFW_KEY_TAB) == GLFW_PRESS;
    if (currTab && !m_prevTab)
      ToggleFlyMode(cam);
    m_prevTab = currTab;

    if (m_flyMode) {
      UpdateFlyCamera(dt, cam);
    } else {
      // Ctrl/Cmd + Shift + C copies selected object reference code to clipboard.
      bool accelHeld = glfwGetKey(m_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                       glfwGetKey(m_window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS ||
                       glfwGetKey(m_window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
                       glfwGetKey(m_window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;
      bool shiftHeld = glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                       glfwGetKey(m_window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
      bool currCopyRef = accelHeld && shiftHeld && glfwGetKey(m_window, GLFW_KEY_C) == GLFW_PRESS;
      if (currCopyRef && !m_prevCopyRef && !io.WantTextInput && !ImGui::IsAnyItemActive()) {
        const int idx = PrimaryIdx();
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
      if (currDel && !m_prevDel && !m_selectedIndices.empty()) {
        std::vector<int> sorted = m_selectedIndices;
        std::sort(sorted.rbegin(), sorted.rend());
        for (int i : sorted)
          m_document.objects.erase(m_document.objects.begin() + i);
        m_selectedIndices.clear();
        m_document.dirty = true;
        TriggerReload();
      }
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

void EditorLayer::Render(const Camera& cam) {
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  if (m_active) {
    DrawToolbar();
    DrawViewGimbal();
    DrawObjectList();
    DrawAssetRegistryPanel();
    DrawPropertiesPanel();
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
  ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, 36));
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
    }
    ImGui::SameLine();
  };

  addObj(SceneObjectType::Panel, "+ Panel");
  addObj(SceneObjectType::Prop, "+ Prop");
  addObj(SceneObjectType::Light, "+ Light");

  if (!m_selectedAssetId.empty()) {
    if (ImGui::Button("Place Selected Asset"))
      CreateObjectFromAsset(m_selectedAssetId);
    ImGui::SameLine();
  }

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
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("WASD + mouse  |  Tab to exit");
  }
  ImGui::SameLine();
  ImGui::TextDisabled("Ctrl/Cmd+Shift+C copy ref");
  ImGui::SameLine();

  const float rightW = 260.0f;
  ImGui::SetCursorPosX(io.DisplaySize.x - rightW);

  if (ImGui::Button("Load")) {
    std::string path = m_document.filePath.empty() ? kDefaultScenePath : m_document.filePath;
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
      std::string path = m_document.filePath.empty() ? kDefaultScenePath : m_document.filePath;
      m_document.filePath = path;
      try {
        SceneSerializer::SaveToFile(m_document, path);
      } catch (...) {
      }
      m_document.dirty = false;
      TriggerReload();
    }
    if (!canSave)
      ImGui::EndDisabled();
  }
  ImGui::SameLine();

  if (ImGui::Button("Exit [F10]"))
    Toggle();

  ImGui::End();
}

void EditorLayer::DrawViewGimbal() {
  ImGuiIO& io = ImGui::GetIO();
  const float panelW = 280.0f;
  const float size = 150.0f;
  const float x = io.DisplaySize.x - panelW - size - 10.0f;
  const float y = 42.0f;

  ImGui::SetNextWindowPos(ImVec2(x, y));
  ImGui::SetNextWindowSize(ImVec2(size, size));
  ImGui::SetNextWindowBgAlpha(0.75f);
  ImGui::Begin("##view_gimbal",
               nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus);

  ImGui::TextUnformatted("View");

  const int idx = PrimaryIdx();
  const bool hasSelection = (idx >= 0 && idx < static_cast<int>(m_document.objects.size()));
  if (!hasSelection)
    ImGui::BeginDisabled();

  auto snapBtn = [&](const char* label, ViewSnap snap, float w = 40.0f) {
    if (ImGui::Button(label, ImVec2(w, 22.0f)))
      m_pendingViewSnap = snap;
  };

  ImGui::Dummy(ImVec2(0, 2));
  ImGui::SetCursorPosX(55.0f);
  snapBtn("Top", ViewSnap::Top);

  snapBtn("Left", ViewSnap::Left);
  ImGui::SameLine();
  snapBtn("Front", ViewSnap::Front);
  ImGui::SameLine();
  snapBtn("Right", ViewSnap::Right);

  ImGui::SetCursorPosX(55.0f);
  snapBtn("Back", ViewSnap::Back);

  ImGui::SetCursorPosX(55.0f);
  snapBtn("Bottom", ViewSnap::Bottom);

  if (!hasSelection) {
    ImGui::EndDisabled();
    ImGui::TextDisabled("Select object");
  }

  ImGui::End();
}

// ---- Object list panel -------------------------------------------------------

void EditorLayer::DrawObjectList() {
  ImGuiIO& io = ImGui::GetIO();
  const float W = 260.0f;
  const float Y = 36.0f;
  const float H = (io.DisplaySize.y - Y) * 0.52f;

  ImGui::SetNextWindowPos(ImVec2(0, Y));
  ImGui::SetNextWindowSize(ImVec2(W, H));
  ImGui::SetNextWindowBgAlpha(0.85f);
  ImGui::Begin(
      "Objects", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);

  for (int i = 0; i < static_cast<int>(m_document.objects.size()); ++i) {
    auto& obj = m_document.objects[i];
    const char* tag = (obj.type == SceneObjectType::Prop)    ? "P"
                      : (obj.type == SceneObjectType::Light) ? "L"
                                                             : "B";

    std::string assetSuffix;
    if (!obj.assetId.empty())
      assetSuffix = " @" + obj.assetId;

    char label[196];
    std::snprintf(label, sizeof(label), "[%s] %s%s##%d", tag, obj.id.c_str(), assetSuffix.c_str(), i);

    if (ImGui::Selectable(label, IsSelected(i))) {
      if (ImGui::GetIO().KeyShift)
        ToggleSelect(i);
      else
        m_selectedIndices = {i};
    }
  }

  ImGui::End();
}

void EditorLayer::DrawAssetRegistryPanel() {
  ImGuiIO& io = ImGui::GetIO();
  const float W = 260.0f;
  const float Y = 36.0f + (io.DisplaySize.y - 36.0f) * 0.52f;
  const float H = io.DisplaySize.y - Y;

  ImGui::SetNextWindowPos(ImVec2(0, Y));
  ImGui::SetNextWindowSize(ImVec2(W, H));
  ImGui::SetNextWindowBgAlpha(0.85f);
  ImGui::Begin(
      "Assets", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);

  ImGui::InputText("ID", m_newAssetId, sizeof(m_newAssetId));
  ImGui::InputText("Mesh", m_newAssetMesh, sizeof(m_newAssetMesh));
  ImGui::InputText("Render Scale", m_newAssetScale, sizeof(m_newAssetScale));

  if (ImGui::Button("Add Asset")) {
    const std::string id = m_newAssetId;
    if (!id.empty()) {
      AssetDef def;
      def.mesh = m_newAssetMesh;
      def.renderScale = m_newAssetScale;
      m_document.assets[id] = def;
      m_selectedAssetId = id;
      m_document.dirty = true;
    }
  }
  ImGui::SameLine();
  if (!m_selectedAssetId.empty()) {
    if (ImGui::Button("Place Asset"))
      CreateObjectFromAsset(m_selectedAssetId);
  }

  ImGui::Separator();
  ImGui::Text("Registered assets: %d", static_cast<int>(m_document.assets.size()));

  std::vector<std::string> ids;
  ids.reserve(m_document.assets.size());
  for (const auto& kv : m_document.assets)
    ids.push_back(kv.first);
  std::sort(ids.begin(), ids.end());

  for (const auto& id : ids) {
    const AssetDef& asset = m_document.assets.at(id);
    const bool selected = (m_selectedAssetId == id);
    std::string label = id + "##asset_" + id;
    if (ImGui::Selectable(label.c_str(), selected))
      m_selectedAssetId = id;
    ImGui::TextDisabled("mesh: %s", asset.mesh.empty() ? "<none>" : asset.mesh.c_str());
    if (!asset.renderScale.empty())
      ImGui::TextDisabled("scale: %s", asset.renderScale.c_str());
    ImGui::Separator();
  }

  if (!m_selectedAssetId.empty()) {
    auto it = m_document.assets.find(m_selectedAssetId);
    if (it != m_document.assets.end()) {
      ImGui::Text("Edit asset");
      ImGui::LabelText("Selected", "%s", m_selectedAssetId.c_str());

      char meshBuf[256] = {};
      std::snprintf(meshBuf, sizeof(meshBuf), "%s", it->second.mesh.c_str());
      if (ImGui::InputText("Asset Mesh", meshBuf, sizeof(meshBuf))) {
        it->second.mesh = meshBuf;
        m_document.dirty = true;
      }

      char scaleBuf[64] = {};
      std::snprintf(scaleBuf, sizeof(scaleBuf), "%s", it->second.renderScale.c_str());
      if (ImGui::InputText("Asset Scale", scaleBuf, sizeof(scaleBuf))) {
        it->second.renderScale = scaleBuf;
        m_document.dirty = true;
      }

      if (ImGui::Button("Delete Asset")) {
        for (auto& obj : m_document.objects) {
          if (obj.assetId == m_selectedAssetId)
            obj.assetId.clear();
        }
        m_document.assets.erase(it);
        m_selectedAssetId.clear();
        m_document.dirty = true;
      }
    }
  }

  ImGui::End();
}

// ---- Properties panel --------------------------------------------------------

void EditorLayer::DrawPropertiesPanel() {
  ImGuiIO& io = ImGui::GetIO();
  const float W = 300.0f;
  const float Y = 36.0f;

  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - W, Y));
  ImGui::SetNextWindowSize(ImVec2(W, io.DisplaySize.y - Y));
  ImGui::SetNextWindowBgAlpha(0.85f);
  ImGui::Begin(
      "Properties", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);

  if (m_selectedIndices.size() > 1) {
    ImGui::Text("%d objects selected", static_cast<int>(m_selectedIndices.size()));
    ImGui::Separator();
    if (ImGui::Button("Delete Selected")) {
      std::vector<int> sorted = m_selectedIndices;
      std::sort(sorted.rbegin(), sorted.rend());
      for (int i : sorted)
        m_document.objects.erase(m_document.objects.begin() + i);
      m_selectedIndices.clear();
      m_document.dirty = true;
      TriggerReload();
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

  ImGui::LabelText("ID", "%s", obj.id.c_str());
  ImGui::LabelText("Type", "%s", TypeToLabel(obj.type));
  ImGui::Separator();

  float pos[3] = {obj.position.x, obj.position.y, obj.position.z};
  if (ImGui::DragFloat3("Position", pos, 0.05f)) {
    obj.position = {pos[0], pos[1], pos[2]};
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

  if (ImGui::DragFloat("Yaw", &obj.yaw, 1.0f, -360.0f, 360.0f)) {
    m_document.dirty = true;
    if (m_transformCb)
      m_transformCb(obj);
  }

  ImGui::Separator();
  ImGui::Text("Asset Binding");

  std::vector<std::string> assetIds;
  assetIds.push_back("<Inline>");
  for (const auto& kv : m_document.assets)
    assetIds.push_back(kv.first);
  std::sort(assetIds.begin() + 1, assetIds.end());

  int currentAssetIndex = 0;
  if (!obj.assetId.empty()) {
    for (int i = 1; i < static_cast<int>(assetIds.size()); ++i) {
      if (assetIds[i] == obj.assetId) {
        currentAssetIndex = i;
        break;
      }
    }
  }

  std::string comboItems;
  for (const auto& id : assetIds) {
    comboItems += id;
    comboItems.push_back('\0');
  }
  comboItems.push_back('\0');

  if (ImGui::Combo("Asset", &currentAssetIndex, comboItems.c_str())) {
    obj.assetId = (currentAssetIndex == 0) ? "" : assetIds[static_cast<size_t>(currentAssetIndex)];
    if (!obj.assetId.empty())
      m_selectedAssetId = obj.assetId;
    m_document.dirty = true;
  }

  if (!obj.assetId.empty()) {
    auto it = m_document.assets.find(obj.assetId);
    if (it != m_document.assets.end()) {
      ImGui::TextDisabled("mesh: %s", it->second.mesh.empty() ? "<none>" : it->second.mesh.c_str());
      ImGui::TextDisabled("renderScale: %s",
                          it->second.renderScale.empty() ? "<none>" : it->second.renderScale.c_str());
    }
  }

  ImGui::Separator();
  ImGui::Text("Props");

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

  ImGui::Separator();
  if (ImGui::Button("Delete")) {
    m_document.objects.erase(m_document.objects.begin() + primaryIdx);
    m_selectedIndices.clear();
    m_document.dirty = true;
    TriggerReload();
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

  Ray ray = ScreenToRay(static_cast<float>(mx), static_cast<float>(my), screenW, screenH, cam);

  float bestT = std::numeric_limits<float>::max();
  int bestIdx = -1;

  for (int i = 0; i < static_cast<int>(m_document.objects.size()); ++i) {
    const auto& obj = m_document.objects[i];
    Vec3 half = {
        std::max(obj.scale.x, 0.25f), std::max(obj.scale.y, 0.25f), std::max(obj.scale.z, 0.25f)};
    float t = RayVsAABB(ray, obj.position, half);
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
    DebugDraw::Box(obj.position, obj.scale, {0.2f, 0.7f, 1.0f, 1.0f});
  }
}

void EditorLayer::ApplyPendingViewSnap(Camera& cam) {
  if (m_pendingViewSnap == ViewSnap::None)
    return;

  const int idx = PrimaryIdx();
  if (idx < 0 || idx >= static_cast<int>(m_document.objects.size())) {
    m_pendingViewSnap = ViewSnap::None;
    return;
  }

  const SceneObject& obj = m_document.objects[idx];
  const float extent = std::max(obj.scale.x, std::max(obj.scale.y, obj.scale.z));
  const float distance = std::max(2.0f, extent * 3.0f + 1.0f);

  cam.target = obj.position;
  cam.up = {0.0f, 1.0f, 0.0f};

  switch (m_pendingViewSnap) {
    case ViewSnap::Top:
      cam.position = obj.position + Vec3{0.0f, distance, 0.0f};
      cam.up = {0.0f, 0.0f, -1.0f};
      break;
    case ViewSnap::Bottom:
      cam.position = obj.position + Vec3{0.0f, -distance, 0.0f};
      cam.up = {0.0f, 0.0f, 1.0f};
      break;
    case ViewSnap::Left:
      cam.position = obj.position + Vec3{-distance, 0.0f, 0.0f};
      break;
    case ViewSnap::Right:
      cam.position = obj.position + Vec3{distance, 0.0f, 0.0f};
      break;
    case ViewSnap::Front:
      cam.position = obj.position + Vec3{0.0f, 0.0f, distance};
      break;
    case ViewSnap::Back:
      cam.position = obj.position + Vec3{0.0f, 0.0f, -distance};
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

void EditorLayer::CreateObjectFromAsset(const std::string& assetId) {
  auto it = m_document.assets.find(assetId);
  if (it == m_document.assets.end())
    return;

  SceneObject obj;
  obj.id = GenerateId(m_document);
  obj.type = SceneObjectType::Prop;
  obj.assetId = assetId;
  ApplySchemaDefaults(obj);

  if (!it->second.mesh.empty())
    obj.props["mesh"] = it->second.mesh;
  if (!it->second.renderScale.empty())
    obj.props["renderScale"] = it->second.renderScale;

  m_document.objects.push_back(std::move(obj));
  m_selectedIndices = {static_cast<int>(m_document.objects.size()) - 1};
  m_document.dirty = true;
  TriggerReload();
}

std::string EditorLayer::BuildSelectionRefCode(const SceneObject& obj, int idx) const {
  auto getProp = [&](const char* key) -> std::string {
    auto it = obj.props.find(key);
    return (it != obj.props.end()) ? it->second : "";
  };

  std::ostringstream ss;
  ss.setf(std::ios::fixed);
  ss.precision(4);
  const std::string scenePath = m_document.filePath.empty() ? kDefaultScenePath : m_document.filePath;
  ss << "EDITOR_REF"
     << " scene=\"" << scenePath << "\""
     << " id=" << obj.id
     << " idx=" << idx
     << " type=" << TypeToLabel(obj.type)
     << " pos=(" << obj.position.x << "," << obj.position.y << "," << obj.position.z << ")"
     << " scale=(" << obj.scale.x << "," << obj.scale.y << "," << obj.scale.z << ")"
     << " yaw=" << obj.yaw;

  if (!obj.assetId.empty())
    ss << " asset=\"" << obj.assetId << "\"";

  const std::string mesh = getProp("mesh");
  if (!mesh.empty())
    ss << " mesh=\"" << mesh << "\"";

  const std::string eid = getProp("_eid");
  if (!eid.empty())
    ss << " _eid=" << eid;

  return ss.str();
}

std::string EditorLayer::GenerateId(const SceneDocument& doc) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "obj_%03d", static_cast<int>(doc.objects.size()));
  return buf;
}

// ---- Fly camera --------------------------------------------------------------

void EditorLayer::ToggleFlyMode(Camera& cam) {
  m_flyMode = !m_flyMode;
  m_flyCamInitialized = false;
  m_prevCursorInit = false;
  glfwSetInputMode(m_window, GLFW_CURSOR,
                   m_flyMode ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
  (void)cam;
}

void EditorLayer::UpdateFlyCamera(float dt, Camera& cam) {
  cam.up = Vec3::Up();

  if (!m_flyCamInitialized) {
    Vec3 dir = cam.target - cam.position;
    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len > 0.001f) {
      dir.x /= len;
      dir.y /= len;
      dir.z /= len;
    }
    m_flyPitch = std::asin(std::max(-1.0f, std::min(1.0f, dir.y))) * (180.0f / PI);

    const float horizLen = std::sqrt(dir.x * dir.x + dir.z * dir.z);
    if (horizLen > 0.0001f)
      m_flyYaw = -std::atan2(dir.x, -dir.z) * (180.0f / PI);

    m_flyCamInitialized = true;
  }

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
