#pragma once
// Private shared helpers used across EditorLayer's split translation units.
// Must not be included by any public header.

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "editor/SceneDocument.h"

namespace Monolith::Editor {
    namespace {

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
            for (SceneObject &obj: doc->objects) {
                if (obj.assetId.empty()) {
                    obj.props.erase("_assetRenderScale");
                    continue;
                }
                const auto assetIt = doc->assets.find(obj.assetId);
                if (assetIt == doc->assets.end()) {
                    obj.props.erase("_assetRenderScale");
                    continue;
                }
                obj.props["_assetRenderScale"] =
                        assetIt->second.renderScale.empty()
                                ? "1.0000,1.0000,1.0000"
                                : assetIt->second.renderScale;
            }
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
        inline std::string BuildImGuiComboItems(const std::vector<std::string> &options) {
            std::string items;
            for (const auto &opt: options) {
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
                case Prop:    typeName = "prop";   break;
                case Light:   typeName = "light";  break;
                case Camera:  typeName = "camera"; break;
                case Panel:
                default:      typeName = "panel";  break;
            }
            return std::ranges::any_of(appliesTo, [&](std::string entry) {
                std::ranges::transform(entry, entry.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                return entry == typeName;
            });
        }

    } // namespace
} // namespace Monolith::Editor
