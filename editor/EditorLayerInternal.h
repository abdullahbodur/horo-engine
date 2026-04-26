#pragma once
// Private shared helpers used across EditorLayer's split translation units.
// Must not be included by any public header.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "core/LogBuffer.h"
#include "core/ProjectPath.h"
#include "editor/EditorUiLogic.h"
#include "editor/SceneDocument.h"

namespace Horo::Editor {
namespace Internal {
// ---- Layout constants ----
// Must match DrawToolbar / DrawStatusBar so panels do not overlap.
constexpr float kEditorToolbarH = 36.0f;
constexpr float kEditorStatusH = 24.0f;
constexpr char kEditorPropertiesWindow[] = "Properties";

constexpr ImGuiWindowFlags kMainPanelWindowFlags =
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

constexpr size_t kMaxEditorHistorySnapshots = 128;

// ---- Document utilities ----

inline SceneObject &ObjectAt(SceneDocument &doc, int idx) {
  return doc.objects[static_cast<size_t>(idx)];
}

inline const SceneObject &ObjectAt(const SceneDocument &doc, int idx) {
  return doc.objects[static_cast<size_t>(idx)];
}

// Keeps every object's "_assetRenderScale" prop in sync with its asset def.
inline void SyncAssetScaleMetadata(SceneDocument *doc) {
  if (!doc)
    return;
  for (SceneObject &obj : doc->objects) {
    if (obj.assetId.empty()) {
      obj.props.erase("_assetRenderScale");
      continue;
    }
    const auto assetIt = doc->assets.find(obj.assetId);
    if (assetIt == doc->assets.end()) {
      obj.props.erase("_assetRenderScale");
      continue;
    }
    obj.props["_assetRenderScale"] = assetIt->second.renderScale.empty()
                                         ? "1.0000,1.0000,1.0000"
                                         : assetIt->second.renderScale;
  }
}

inline Vec3 ResolveObjectPlacementHalfExtents(const SceneObject &obj) {
  Vec3 assetRenderScale = Vec3::One();
  if (const auto assetScaleIt = obj.props.find("_assetRenderScale");
      assetScaleIt != obj.props.end())
    TryParseVec3Csv(assetScaleIt->second, &assetRenderScale);

  return {std::max(std::abs(obj.scale.x * assetRenderScale.x), 0.01f),
          std::max(std::abs(obj.scale.y * assetRenderScale.y), 0.01f),
          std::max(std::abs(obj.scale.z * assetRenderScale.z), 0.01f)};
}

inline float ProjectHalfExtentOntoNormal(const Vec3 &halfExtents,
                                         const Vec3 &normal) {
  return std::abs(normal.x) * halfExtents.x +
         std::abs(normal.y) * halfExtents.y +
         std::abs(normal.z) * halfExtents.z;
}

inline float DistSqPointSegment2D(float px, float py, float ax, float ay,
                                 float bx, float by) {
  const float abx = bx - ax;
  const float aby = by - ay;
  const float apx = px - ax;
  const float apy = py - ay;
  const float abLen2 = abx * abx + aby * aby;
  if (abLen2 < 1e-8f)
    return apx * apx + apy * apy;
  float t = (apx * abx + apy * aby) / abLen2;
  t = std::clamp(t, 0.f, 1.f);
  const float cx = ax + t * abx;
  const float cy = ay + t * aby;
  const float dx = px - cx;
  const float dy = py - cy;
  return dx * dx + dy * dy;
}

inline bool IsTextureFilePath(std::string_view path) {
  if (path.empty())
    return false;
  namespace fs = std::filesystem;
  std::string ext = fs::path(path).extension().string();
  std::ranges::transform(ext, ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
         ext == ".bmp" || ext == ".tga" || ext == ".webp" ||
         ext == ".hdr";
}

inline std::string ToLowerAscii(std::string s) {
  std::ranges::transform(s, s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

inline std::filesystem::path
ResolveProjectRelativeOrAbsolutePath(std::string_view rawPath) {
  if (rawPath.empty())
    return {};
  namespace fs = std::filesystem;
  fs::path p(rawPath);
  if (p.is_absolute())
    return p;
  return ProjectPath::Root() / p;
}

// ---- UI utilities ----

// Parses a comma-separated "r,g,b" string into a float[3] array.
inline void ParseRGBString(std::string_view s, float col[3]) {
  col[0] = col[1] = col[2] = 1.0f;
  if (s.empty())
    return;
  const char *p = s.data();
  char *end = nullptr;
  col[0] = std::strtof(p, &end);
  if (end && *end)
    p = end + 1;
  col[1] = std::strtof(p, &end);
  if (end && *end)
    p = end + 1;
  col[2] = std::strtof(p, nullptr);
}

// Returns the index of val in options, or 0 if not found.
inline int FindEnumOptionIndex(const std::vector<std::string> &options,
                               std::string_view val) {
  for (int i = 0; i < static_cast<int>(options.size()); ++i)
    if (options[i] == val)
      return i;
  return 0;
}

// Builds a null-separated, double-null-terminated string for ImGui::Combo.
inline std::string
BuildImGuiComboItems(const std::vector<std::string> &options) {
  std::string items;
  for (const auto &opt : options) {
    items += opt;
    items += '\0';
  }
  items += '\0';
  return items;
}

// Shows a disabled button with a tooltip explaining the platform limitation.
inline void DrawUnavailableTextureDialogButton(const char *buttonId) {
  // NOSONAR: cpp:S1172 used in platform-conditional UI code
  ImGui::BeginDisabled();
  ImGui::Button(buttonId, ImVec2(-1.0f, 0.0f));
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("Texture file dialog is not available on this platform.");
}

// Checks if a schema's appliesTo list includes the given object type.
inline bool SchemaAppliesToObjectType(const std::vector<std::string> &appliesTo,
                                      SceneObjectType objectType) {
  using enum SceneObjectType;
  if (appliesTo.empty())
    return true;
  std::string typeName = "panel";
  switch (objectType) {
  case Prop:
    typeName = "prop";
    break;
  case Light:
    typeName = "light";
    break;
  case Camera:
    typeName = "camera";
    break;
  case Panel:
  default:
    typeName = "panel";
    break;
  }
  return std::ranges::any_of(appliesTo, [&](std::string entry) {
    std::ranges::transform(entry, entry.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    return entry == typeName;
  });
}

// ---- Type-string converters (used in both EditorLayer.cpp and
// EditorMcpHandlers.cpp) ----

inline const char *SceneObjectTypeToString(SceneObjectType type) {
  using enum SceneObjectType;
  switch (type) {
  case Panel:
    return "Panel";
  case Prop:
    return "Prop";
  case Light:
    return "Light";
  case Camera:
    return "Camera";
  }
  return "Panel";
}

inline bool ParseSceneObjectType(std::string_view raw,
                                 SceneObjectType *outType) {
  using enum SceneObjectType;
  if (!outType)
    return false;
  std::string value(raw);
  std::ranges::transform(value, value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  if (value == "panel") {
    *outType = Panel;
    return true;
  }
  if (value == "prop") {
    *outType = Prop;
    return true;
  }
  if (value == "light") {
    *outType = Light;
    return true;
  }
  if (value == "camera") {
    *outType = Camera;
    return true;
  }
  return false;
}

// Formats the wall-clock time of a LogLine as HH:MM:SS into buf.
inline void FormatLogTime(const LogLine &entry, char *buf, size_t bufSize) {
  using clock = std::chrono::system_clock;
  const std::time_t t = clock::to_time_t(entry.time);
  std::tm tmBuf{};
#ifdef _WIN32
  localtime_s(&tmBuf, &t);
#else
  localtime_r(&t, &tmBuf);
#endif
  if (!buf || bufSize == 0)
    return;
  const auto out = std::format_to_n(buf, bufSize - 1, "{:02d}:{:02d}:{:02d}",
                                    tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec);
  buf[std::min(static_cast<size_t>(out.size), bufSize - 1)] = '\0';
}
} // namespace Internal

using Internal::BuildImGuiComboItems;
using Internal::DrawUnavailableTextureDialogButton;
using Internal::FindEnumOptionIndex;
using Internal::FormatLogTime;
using Internal::DistSqPointSegment2D;
using Internal::kEditorPropertiesWindow;
using Internal::kEditorStatusH;
using Internal::kEditorToolbarH;
using Internal::kMainPanelWindowFlags;
using Internal::kMaxEditorHistorySnapshots;
using Internal::ObjectAt;
using Internal::IsTextureFilePath;
using Internal::ProjectHalfExtentOntoNormal;
using Internal::ParseRGBString;
using Internal::ParseSceneObjectType;
using Internal::ResolveObjectPlacementHalfExtents;
using Internal::ResolveProjectRelativeOrAbsolutePath;
using Internal::SceneObjectTypeToString;
using Internal::SchemaAppliesToObjectType;
using Internal::ToLowerAscii;
using Internal::SyncAssetScaleMetadata;
} // namespace Horo::Editor
