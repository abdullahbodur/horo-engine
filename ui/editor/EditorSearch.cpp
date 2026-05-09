/**
 * @file EditorSearch.cpp
 * @brief Implementation for EditorSearch editor functionality.
 */
#include "ui/editor/EditorSearch.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace Horo::Editor {
    namespace {
        constexpr std::array<ShortcutRow, 17> kEditorShortcuts = {
            {
                {"Editor", "Run or stop game in viewport", "Toolbar: Play / Stop"},
                {"Editor", "Toggle shortcuts help", "? or F1"},
                {"Editor", "Quick open", "Ctrl/Cmd + P"},
                {"Editor", "Command palette", "Ctrl/Cmd + Shift + P"},
                {"Editor", "Undo last scene change", "Ctrl/Cmd + Z"},
                {"Editor", "Redo last scene change", "Ctrl/Cmd + Shift + Z / Ctrl+Y"},
                {"Camera", "Toggle fly mode", "Tab"},
                {"Camera", "Move in fly mode", "W A S D"},
                {"Camera", "Look around in fly mode", "Mouse"},
                {"Selection", "Select object", "Left click"},
                {"Selection", "Multi-select", "Shift + Left click"},
                {"Selection", "Delete selected object(s)", "Delete"},
                {"Selection", "Duplicate selected object", "Toolbar: Duplicate"},
                {"Scene", "Load scene", "Toolbar: Load"},
                {"Scene", "Save scene", "Toolbar: Save"},
                {"Assets", "Add prop from selected asset", "Toolbar: + Prop from Asset"},
                {"Clipboard", "Copy selected object reference", "Ctrl/Cmd + Shift + C"}
            }
        };

        constexpr std::array<CommandPaletteRow, 10> kEditorCommands = {
            {
                {"undo", "Undo", "Ctrl/Cmd + Z"},
                {"redo", "Redo", "Ctrl/Cmd + Shift + Z / Ctrl+Y"},
                {"new_scene", "New Scene", "File"},
                {"open_scene", "Open Scene...", "File"},
                {"load_scene", "Load Scene", "Toolbar"},
                {"reload_scene", "Reload Scene", "Palette"},
                {"save_scene", "Save Scene", "Toolbar"},
                {"reset_layout", "Reset Layout", "View"},
                {"quick_open", "Quick Open", "Ctrl/Cmd + P"},
                {"close_editor", "Close Editor", "Toolbar"}
            }
        };

        std::string ToLower(std::string text) {
            std::ranges::transform(text, text.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return text;
        }
    } // namespace

    const char *ObjectTypeLabel(SceneObjectType type) {
        using enum SceneObjectType;
        switch (type) {
            case Prop:
                return "prop";
            case Light:
                return "light";
            case Panel:
                return "board";
            case Camera:
                return "[cam]";
            default:
                return "unknown";
        }
    }

    bool ContainsCaseInsensitive(const std::string &textRaw,
                                 const std::string &queryRaw) {
        if (queryRaw.empty())
            return true;
        return ToLower(textRaw).find(ToLower(queryRaw)) != std::string::npos;
    }

    bool MatchesShortcutQuery(const ShortcutRow &row, const std::string &queryRaw) {
        if (queryRaw.empty())
            return true;

        return ContainsCaseInsensitive(row.category, queryRaw) ||
               ContainsCaseInsensitive(row.command, queryRaw) ||
               ContainsCaseInsensitive(row.keys, queryRaw);
    }

    bool MatchesCommandPaletteQuery(const CommandPaletteRow &row,
                                    const std::string &queryRaw) {
        if (queryRaw.empty())
            return true;

        return ContainsCaseInsensitive(row.command, queryRaw) ||
               ContainsCaseInsensitive(row.keys, queryRaw) ||
               ContainsCaseInsensitive(row.id, queryRaw);
    }

    std::span<const ShortcutRow> GetEditorShortcuts() { return kEditorShortcuts; }

    std::span<const CommandPaletteRow> GetEditorCommands() {
        return kEditorCommands;
    }

    bool ObjectMatchesQuickOpenQuery(const SceneObject &obj,
                                     const std::string &queryRaw) {
        const char *typeName = ObjectTypeLabel(obj.type);
        return ContainsCaseInsensitive(obj.id + " " + typeName + " " + obj.assetId,
                                       queryRaw);
    }

    bool AssetMatchesQuickOpenQuery(const std::string &assetId,
                                    const AssetDef &asset,
                                    const std::string &queryRaw) {
        return ContainsCaseInsensitive(
            assetId + " " + asset.mesh + " " + asset.albedoMap, queryRaw);
    }

    FilteredListState EvaluateFilteredListState(size_t totalCount, int shownCount,
                                                std::string_view queryRaw) {
        using enum FilteredListState;
        if (shownCount > 0)
            return None;
        if (totalCount == 0)
            return EmptyData;
        if (!queryRaw.empty())
            return NoMatches;
        return None;
    }
} // namespace Horo::Editor
