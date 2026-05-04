#pragma once

#include <span>
#include <string>

#include "ui/editor/SceneDocument.h"

namespace Horo::Editor {
    struct ShortcutRow {
        const char *category;
        const char *command;
        const char *keys;
    };

    struct CommandPaletteRow {
        const char *id;
        const char *command;
        const char *keys;
    };

    enum class FilteredListState {
        None,
        EmptyData,
        NoMatches,
    };

    const char *ObjectTypeLabel(SceneObjectType type);

    bool ContainsCaseInsensitive(const std::string &textRaw,
                                 const std::string &queryRaw);

    bool MatchesShortcutQuery(const ShortcutRow &row, const std::string &queryRaw);

    bool MatchesCommandPaletteQuery(const CommandPaletteRow &row,
                                    const std::string &queryRaw);

    std::span<const ShortcutRow> GetEditorShortcuts();

    std::span<const CommandPaletteRow> GetEditorCommands();

    bool ObjectMatchesQuickOpenQuery(const SceneObject &obj,
                                     const std::string &queryRaw);

    bool AssetMatchesQuickOpenQuery(const std::string &assetId,
                                    const AssetDef &asset,
                                    const std::string &queryRaw);

    FilteredListState EvaluateFilteredListState(size_t totalCount, int shownCount,
                                                std::string_view queryRaw);
} // namespace Horo::Editor
