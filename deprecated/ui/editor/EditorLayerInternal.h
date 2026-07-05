/** @file EditorLayerInternal.h
 *  @brief Private helpers shared across EditorLayer translation units.
 *  @note Must not be included by any public header. */
#pragma once
// Private shared helpers used across EditorLayer's split translation units.
// Must not be included by any public header.

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "ui/IconsFontAwesome6.h"
#include "core/LogBuffer.h"
#include "core/ProjectPath.h"
#include "core/StringUtils.h"
#include "renderer/Camera.h"
#include "ui/UiComponents.h"
#include "ui/editor/EditorLayer.h"
#include "ui/editor/EditorUiLogic.h"
#include "ui/editor/SceneDocument.h"

namespace Horo::Editor {
namespace Internal {
// Must match DrawToolbar / DrawStatusBar so panels do not overlap.
constexpr float kEditorToolbarH = 36.0f;           /**< Height of the editor toolbar in pixels. */
constexpr float kEditorStatusH = Ui::kEditorStatusBarHeight; /**< Height of the editor status bar in pixels. */
constexpr char kEditorHierarchyWindow[] = "Hierarchy";   /**< ImGui window title for the hierarchy panel. */
constexpr char kEditorPropertiesWindow[] = "Properties"; /**< ImGui window title for the properties panel. */
constexpr char kEditorAssetsWindow[] = "Assets";         /**< ImGui window title for the assets panel. */
constexpr char kEditorWorkspaceWindow[] = "Workspace";   /**< ImGui window title for the project workspace panel. */
constexpr char kEditorViewportWindow[] = "Viewport";     /**< ImGui window title for the 3D viewport panel. */

constexpr float kHierarchySectionRatio = 0.56f; /**< Default fraction of left-dock height allocated to the hierarchy section. */

/** @brief Shared ImGui window flags for all fixed-position editor panels. */
constexpr ImGuiWindowFlags kMainPanelWindowFlags =
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

constexpr size_t kMaxEditorHistorySnapshots = 128; /**< Maximum undo/redo depth before old snapshots are trimmed. */


/** @brief Returns a mutable reference to the scene object at index @p idx.
 *  @param doc Scene document that owns the object list.
 *  @param idx Zero-based index into doc.objects. */
inline SceneObject &ObjectAt(SceneDocument &doc, int idx) {
  return doc.objects[static_cast<size_t>(idx)];
}

/** @brief Returns a const reference to the scene object at index @p idx.
 *  @param doc Scene document that owns the object list.
 *  @param idx Zero-based index into doc.objects. */
inline const SceneObject &ObjectAt(const SceneDocument &doc, int idx) {
  return doc.objects[static_cast<size_t>(idx)];
}

/** @brief Keeps every object's "_assetRenderScale" prop in sync with its asset definition.
 *  @param doc Scene document to update; no-op when null. */
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

/** @brief Computes the world-space half-extents of @p obj for placement snapping.
 *  @param obj Scene object whose scale and asset render scale are used.
 *  @return Half-extents vector; each component is at least 0.01 to prevent degenerate geometry. */
inline Vec3 ResolveObjectPlacementHalfExtents(const SceneObject &obj) {
  Vec3 assetRenderScale = Vec3::One();
  if (const auto assetScaleIt = obj.props.find("_assetRenderScale");
      assetScaleIt != obj.props.end())
    TryParseVec3Csv(assetScaleIt->second, &assetRenderScale);

  return {std::max(std::abs(obj.scale.x * assetRenderScale.x), 0.01f),
          std::max(std::abs(obj.scale.y * assetRenderScale.y), 0.01f),
          std::max(std::abs(obj.scale.z * assetRenderScale.z), 0.01f)};
}

/** @brief Projects an axis-aligned half-extent box onto a unit normal.
 *  @param halfExtents Box half-extents in each axis.
 *  @param normal      Unit normal vector to project onto.
 *  @return The projected half-extent along the given normal. */
inline float ProjectHalfExtentOntoNormal(const Vec3 &halfExtents,
                                         const Vec3 &normal) {
  return std::abs(normal.x) * halfExtents.x +
         std::abs(normal.y) * halfExtents.y +
         std::abs(normal.z) * halfExtents.z;
}

/** @brief Returns the squared 2-D distance from point P to segment AB.
 *  @param px,py  Query point.
 *  @param ax,ay  Segment start.
 *  @param bx,by  Segment end.
 *  @return Squared distance from P to the nearest point on AB. */
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

// Geometry and hit-testing helpers shared between DrawViewGimbal and tests.
// ImU32/ImVec2 are plain POD types that don't require an active ImGui context,
// so these can be used freely in unit tests.

/** @brief Per-axis visual descriptor for the viewport orientation gimbal.
 *  posSnap is applied when the axis faces the camera (viewZ >= 0);
 *  negSnap is applied when the axis points away from it (viewZ < 0). */
struct ViewGimbalAxisDraw {
  ViewSnap posSnap; /**< Snap when axis faces the camera. */
  ViewSnap negSnap; /**< Snap when axis points away from camera. */
  Vec3 worldPlus;                /**< Positive world-space direction of the axis. */
  ImU32 col;                     /**< Full-brightness colour (axis faces viewer). */
  ImU32 colDim;                  /**< Dimmed colour (axis points away). */
  const char *label;             /**< Text label drawn near the arrowhead. */
};

/** @brief Cached per-axis screen-space data computed once per frame. */
struct ViewGimbalAxisCache {
  float dx = 0.0f;    /**< Screen-space X direction (normalized). */
  float dy = 0.0f;    /**< Screen-space Y direction (normalized, ImGui down = +). */
  float viewZ = 0.0f; /**< View-space Z: >= 0 faces camera, < 0 points away. */
  int origIdx = 0;    /**< Index into the kAxes[] draw array. */
};

/** @brief 2D arrow geometry for one gimbal axis. */
struct ViewGimbalArrowGeometry {
  ImVec2 tip;       /**< Arrow tip in screen space (furthest from gimbal centre). */
  ImVec2 headLeft;  /**< Left base vertex of the arrowhead triangle. */
  ImVec2 headRight; /**< Right base vertex of the arrowhead triangle. */
  ImVec2 shaftEnd;  /**< Shaft endpoint where the arrowhead attaches (toward centre). */
};

/** @brief Tunables for gimbal hover hit testing and arrow shape. */
struct ViewGimbalHoverParams {
  float shaftPx = 0.0f;       /**< Length from centre to arrow tip in pixels. */
  float headLength = 0.0f;    /**< Arrowhead triangle length in pixels. */
  float headHalfWidth = 0.0f; /**< Arrowhead half-width in pixels. */
  float hitPxSq = 0.0f;       /**< Maximum squared hit distance from axis shaft. */
};

/** @brief Builds the 2-D arrow geometry for a single gimbal axis.
 *  @param center        Screen-space centre of the gimbal circle.
 *  @param dx,dy         Normalised screen-space direction of the axis.
 *  @param shaftPx       Length from the centre to the arrow tip in pixels.
 *  @param headLength    Length of the arrowhead triangle in pixels.
 *  @param headHalfWidth Half-width of the arrowhead base in pixels.
 *  @return Geometry struct containing tip, head vertices, and shaft end-point. */
inline ViewGimbalArrowGeometry BuildViewGimbalArrow(const ImVec2 &center,
                                                    float dx, float dy,
                                                    float shaftPx,
                                                    float headLength,
                                                    float headHalfWidth) {
  const ImVec2 dir(dx, dy);
  const ImVec2 perp(-dy, dx);
  ViewGimbalArrowGeometry arrow;
  arrow.tip =
      ImVec2(center.x + dir.x * shaftPx, center.y + dir.y * shaftPx);
  arrow.shaftEnd = ImVec2(arrow.tip.x - dir.x * headLength,
                          arrow.tip.y - dir.y * headLength);
  arrow.headLeft = ImVec2(arrow.shaftEnd.x + perp.x * headHalfWidth,
                          arrow.shaftEnd.y + perp.y * headHalfWidth);
  arrow.headRight = ImVec2(arrow.shaftEnd.x - perp.x * headHalfWidth,
                           arrow.shaftEnd.y - perp.y * headHalfWidth);
  return arrow;
}

/** @brief Returns the 2-D cross product of vectors AB and AC.
 *  @param a,b,c Three points in 2-D screen space.
 *  @return Scalar cross product (AB x AC); positive = counter-clockwise winding. */
inline float Cross2D(const ImVec2 &a, const ImVec2 &b, const ImVec2 &c) {
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

/** @brief Returns true when point @p p lies inside triangle ABC (inclusive of edges).
 *  @param p     Query point.
 *  @param a,b,c Triangle vertices in 2-D screen space. */
inline bool PointInTriangle2D(const ImVec2 &p, const ImVec2 &a,
                               const ImVec2 &b, const ImVec2 &c) {
  const float c1 = Cross2D(a, b, p);
  const float c2 = Cross2D(b, c, p);
  const float c3 = Cross2D(c, a, p);
  return (c1 >= 0.0f && c2 >= 0.0f && c3 >= 0.0f) ||
         (c1 <= 0.0f && c2 <= 0.0f && c3 <= 0.0f);
}

/** @brief Normalises the screen-space direction stored in @p axis.
 *  @param axis Axis cache entry whose dx/dy are normalised in-place; reset to (1,0) if near-zero. */
inline void NormalizeViewGimbalAxis(ViewGimbalAxisCache &axis) {
  const float len = std::sqrt(axis.dx * axis.dx + axis.dy * axis.dy);
  if (len < 1e-4f) {
    axis.dx = 1.0f;
    axis.dy = 0.0f;
    return;
  }
  axis.dx /= len;
  axis.dy /= len;
}

/** @brief Overrides the screen-space directions in @p cache so the three axes always
 *  form a symmetric triad (120 degrees apart), anchored to the Y-axis screen angle.
 *  The viewZ values are preserved and still reflect true camera orientation. */
inline void
ArrangeViewGimbalAxesAsTriad(std::array<ViewGimbalAxisCache, 3> &cache) {
  constexpr float kThirdTurn = 2.09439510239f;
  float yAngle = -1.57079632679f;
  for (const ViewGimbalAxisCache &axis : cache) {
    if (axis.origIdx == 1) {
      yAngle = std::atan2(axis.dy, axis.dx);
      break;
    }
  }

  for (ViewGimbalAxisCache &axis : cache) {
    float angle = yAngle;
    if (axis.origIdx == 0)
      angle += kThirdTurn;
    else if (axis.origIdx == 2)
      angle -= kThirdTurn;
    axis.dx = std::cos(angle);
    axis.dy = std::sin(angle);
  }
}

/** @brief Projects a world-space unit vector onto the screen using the camera view
 *  matrix. @p outViewZ receives the view-space Z of the projected vector:
 *  a positive value means the axis faces the camera; negative means it points
 *  away. Callers use this to choose between posSnap and negSnap. */
inline void WorldAxisToScreenDir(const Camera &cam, const Vec3 &worldUnit,
                                 float *outDx, float *outDy,
                                 float *outViewZ = nullptr) {
  const Mat4 view = cam.GetView();
  const Vec3 e = view.TransformVector(worldUnit);
  if (outViewZ)
    *outViewZ = e.z;
  const float dx = e.x;
  const float dy = -e.y; // ImGui Y is down
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len < 1e-4f) {
    *outDx = 1.f;
    *outDy = 0.f;
    return;
  }
  *outDx = dx / len;
  *outDy = dy / len;
}

/** @brief Returns the ViewSnap that should be activated when the mouse hovers over
 *  a gimbal axis. Uses posSnap when the axis faces the camera (viewZ >= 0)
 *  and negSnap when the axis points away from the camera (viewZ < 0), so that
 *  clicking a dimmed axis correctly snaps to the opposite view direction. */
inline ViewSnap
FindViewGimbalHoverSnap(const ImVec2 &mouse, const ImVec2 &center,
                        const std::array<ViewGimbalAxisCache, 3> &cache,
                        const std::array<ViewGimbalAxisDraw, 3> &axes,
                        const ViewGimbalHoverParams &params) {
  float bestD = params.hitPxSq;
  ViewSnap snap = ViewSnap::None;
  for (const ViewGimbalAxisCache &c : cache) {
    const ViewGimbalAxisDraw &ad = axes[c.origIdx];
    const ViewGimbalArrowGeometry arrow =
        BuildViewGimbalArrow(center, c.dx, c.dy, params.shaftPx,
                             params.headLength, params.headHalfWidth);
    // Choose snap direction based on whether the axis faces the viewer.
    const ViewSnap thisSnap =
        c.viewZ >= 0.0f ? ad.posSnap : ad.negSnap;
    if (PointInTriangle2D(mouse, arrow.tip, arrow.headLeft, arrow.headRight))
      return thisSnap;
    const float d1 =
        DistSqPointSegment2D(mouse.x, mouse.y, center.x, center.y,
                             arrow.shaftEnd.x, arrow.shaftEnd.y);
    if (d1 < bestD) {
      bestD = d1;
      snap = thisSnap;
    }
  }
  return snap;
}

/** @brief Positions @p cam to look along the selected axis toward @p pivot.
 *  Sets position, target, and up so the camera is placed at distance
 *  @p distance from @p pivot along the appropriate world axis.
 *  This is the pure positioning kernel extracted from ApplyPendingViewSnap. */
inline void SnapCameraToAxis(Camera &cam, ViewSnap snap,
                             Vec3 pivot, float distance) {
  using enum ViewSnap;
  cam.target = pivot;
  cam.up = {0.0f, 1.0f, 0.0f};
  switch (snap) {
  case Top:
    cam.position = pivot + Vec3{0.0f, distance, 0.0f};
    cam.up = {0.0f, 0.0f, -1.0f};
    break;
  case Bottom:
    cam.position = pivot + Vec3{0.0f, -distance, 0.0f};
    cam.up = {0.0f, 0.0f, 1.0f};
    break;
  case Left:
    cam.position = pivot + Vec3{-distance, 0.0f, 0.0f};
    break;
  case Right:
    cam.position = pivot + Vec3{distance, 0.0f, 0.0f};
    break;
  case Front:
    cam.position = pivot + Vec3{0.0f, 0.0f, distance};
    break;
  case Back:
    cam.position = pivot + Vec3{0.0f, 0.0f, -distance};
    break;
  case None:
    break;
  }
}

/** @brief Returns the Font Awesome icon string for a scene object type.
 *  @param type The scene object type to look up.
 *  @return A null-terminated Font Awesome icon string. */
inline const char *ObjectTypeIcon(SceneObjectType type) {
  using enum SceneObjectType;
  switch (type) {
  case Prop:
    return ICON_FA_CUBE;
  case Light:
    return ICON_FA_SUN;
  case Camera:
    return ICON_FA_VIDEO;
  default:
    return ICON_FA_FOLDER;
  }
}

/** @brief Returns true when @p path has a recognised texture file extension.
 *  @param path File path or filename to test (case-insensitive extension check). */
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

/** @brief Returns a copy of @p s with all ASCII characters converted to lower-case.
 *  @param s Source string.
 *  @note Delegates to Horo::ToLowerAscii; kept here for backward compatibility. */
inline std::string ToLowerAscii(const std::string &s) {
  return Horo::ToLowerAscii(s);
}

/** @brief Resolves @p rawPath to an absolute filesystem path.
 *  @param rawPath A path string that is either already absolute or relative to the project root.
 *  @return The resolved absolute path, or an empty path when @p rawPath is empty. */
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


/** @brief Parses a comma-separated "r,g,b" string into a float[3] array.
 *  @param s   Source string in "r,g,b" format; missing components default to 1.
 *  @param col Output float[3] array receiving the parsed colour components. */
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

/** @brief Returns the index of @p val in @p options, or 0 if not found.
 *  @param options List of option strings to search.
 *  @param val     Value to locate.
 *  @return Zero-based index of the matching element, or 0 when absent. */
inline int FindEnumOptionIndex(const std::vector<std::string> &options,
                               std::string_view val) {
  for (int i = 0; i < static_cast<int>(options.size()); ++i)
    if (options[i] == val)
      return i;
  return 0;
}

/** @brief Builds a null-separated, double-null-terminated string for ImGui::Combo.
 *  @param options Source option strings.
 *  @return String where each option is followed by a null byte and a final extra null terminates the list. */
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

/** @brief Shows a disabled button with a tooltip explaining that the texture file dialog is unavailable on this platform.
 *  @param buttonId ImGui widget ID string for the disabled button. */
inline void DrawUnavailableTextureDialogButton(const char *buttonId) {
  // NOSONAR: cpp:S1172 used in platform-conditional UI code
  ImGui::BeginDisabled();
  ImGui::Button(buttonId, ImVec2(-1.0f, 0.0f));
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("Texture file dialog is not available on this platform.");
}

/** @brief Returns true when @p appliesTo is empty (applies to all types) or contains the type name matching @p objectType.
 *  @param appliesTo  List of type name strings from the schema definition.
 *  @param objectType Scene object type to check against the list.
 *  @return True when the schema applies to the given object type. */
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


/** @brief Converts a SceneObjectType to its canonical display string.
 *  @param type The scene object type to convert.
 *  @return A null-terminated string such as "Panel", "Prop", "Light", or "Camera". */
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

/** @brief Parses a case-insensitive type name into a SceneObjectType.
 *  @param raw     Input string (e.g. "prop", "Prop", "PROP").
 *  @param outType Receives the parsed type on success; must not be null.
 *  @return True when @p raw matched a known type name. */
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

/** @brief Formats the wall-clock timestamp of @p entry as "HH:MM:SS" into @p buf.
 *  @param entry   Log line whose time field is formatted.
 *  @param buf     Output character buffer; must be non-null and at least @p bufSize bytes.
 *  @param bufSize Size of @p buf in bytes including the null terminator. */
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

/** @brief Queries the live ECS registry for the world-space AABB of @p obj.
 *  @param reg       Live ECS registry to query.
 *  @param obj       Scene object whose runtime entity is looked up.
 *  @param outCenter Receives the AABB centre in world space on success.
 *  @param outHalf   Receives the AABB half-extents on success.
 *  @return True when a valid AABB was found for the object's runtime entity. */
bool TryPropWorldAabb(Registry &reg, const SceneObject &obj, Vec3 &outCenter,
                      Vec3 &outHalf);

/** @brief Locates a renderer shader file by searching a prioritised set of candidate directories.
 *  @param fileName Shader filename (e.g. "wireframe.vert") without path prefix.
 *  @return Absolute path to the first matching file; falls back to the first candidate path when none found. */
inline std::filesystem::path ResolvePreviewShaderPath(const char *fileName) {
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

  for (const auto &candidate : candidates) {
    std::error_code ec;
    if (fs::is_regular_file(candidate, ec) && !ec)
      return candidate;
  }

  return candidates.front();
}

}

using Internal::BuildImGuiComboItems;
using Internal::DrawUnavailableTextureDialogButton;
using Internal::FindEnumOptionIndex;
using Internal::FormatLogTime;
using Internal::DistSqPointSegment2D;
using Internal::kEditorAssetsWindow;
using Internal::kEditorHierarchyWindow;
using Internal::kEditorPropertiesWindow;
using Internal::kEditorStatusH;
using Internal::kEditorToolbarH;
using Internal::kEditorViewportWindow;
using Internal::kEditorWorkspaceWindow;
using Internal::kHierarchySectionRatio;
using Internal::kMainPanelWindowFlags;
using Internal::kMaxEditorHistorySnapshots;
using Internal::ObjectAt;
using Internal::IsTextureFilePath;
using Internal::ProjectHalfExtentOntoNormal;
using Internal::ResolveObjectPlacementHalfExtents;
using Internal::ParseRGBString;
using Internal::ParseSceneObjectType;
using Internal::ResolveProjectRelativeOrAbsolutePath;
using Internal::SceneObjectTypeToString;
using Internal::SchemaAppliesToObjectType;
using Internal::ToLowerAscii;
using Internal::SyncAssetScaleMetadata;
using Internal::ObjectTypeIcon;
using Internal::TryPropWorldAabb;
using Internal::ResolvePreviewShaderPath;
using Internal::ViewGimbalAxisDraw;
using Internal::ViewGimbalAxisCache;
using Internal::ViewGimbalArrowGeometry;
using Internal::ViewGimbalHoverParams;
using Internal::BuildViewGimbalArrow;
using Internal::Cross2D;
using Internal::PointInTriangle2D;
using Internal::NormalizeViewGimbalAxis;
using Internal::ArrangeViewGimbalAxesAsTriad;
using Internal::WorldAxisToScreenDir;
using Internal::FindViewGimbalHoverSnap;
using Internal::SnapCameraToAxis;
} // namespace Horo::Editor
