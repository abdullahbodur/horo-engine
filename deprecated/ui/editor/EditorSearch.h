/** @file EditorSearch.h
 *  @brief Search, filtering, and command-palette data structures and query helpers for the editor UI. */
#pragma once

#include <span>
#include <string>

#include "ui/editor/SceneDocument.h"

namespace Horo::Editor {
    /** @brief A single row in the keyboard-shortcuts reference table. */
    struct ShortcutRow {
        const char *category; /**< Shortcut category label (e.g. "Scene"). */
        const char *command;  /**< Human-readable command name. */
        const char *keys;     /**< Key-combination string (e.g. "Ctrl+Z"). */
    };

    /** @brief A single entry in the command palette. */
    struct CommandPaletteRow {
        const char *id;      /**< Unique command identifier passed to ExecuteCommandPaletteAction. */
        const char *command; /**< Human-readable label shown in the palette. */
        const char *keys;    /**< Optional keyboard-shortcut hint displayed alongside the command. */
    };

    /** @brief State of a filtered list widget after applying a search query. */
    enum class FilteredListState {
        None,       /**< At least one item passed the filter; the list is populated normally. */
        EmptyData,  /**< The underlying data set is empty regardless of the query. */
        NoMatches,  /**< Data is non-empty but no item matched the query string. */
    };

    /** @brief Returns a human-readable label for a scene object type (e.g. "Prop", "Light").
     *  @param type The scene object type to stringify. */
    const char *ObjectTypeLabel(SceneObjectType type);

    /** @brief Returns true when @p textRaw contains @p queryRaw (case-insensitive ASCII comparison).
     *  @param textRaw  Source text to search within.
     *  @param queryRaw Search string. An empty query always returns true. */
    bool ContainsCaseInsensitive(const std::string &textRaw,
                                 const std::string &queryRaw);

    /** @brief Returns true when a shortcut row's category, command, or keys match the query.
     *  @param row      Shortcut row to test.
     *  @param queryRaw Case-insensitive search query. */
    bool MatchesShortcutQuery(const ShortcutRow &row, const std::string &queryRaw);

    /** @brief Returns true when a command-palette row's command or keys match the query.
     *  @param row      Command palette row to test.
     *  @param queryRaw Case-insensitive search query. */
    bool MatchesCommandPaletteQuery(const CommandPaletteRow &row,
                                    const std::string &queryRaw);

    /** @brief Returns the full static table of editor keyboard shortcuts. */
    std::span<const ShortcutRow> GetEditorShortcuts();

    /** @brief Returns the full static table of command-palette entries. */
    std::span<const CommandPaletteRow> GetEditorCommands();

    /** @brief Returns true when @p obj should appear in quick-open results for @p queryRaw.
     *  @param obj      Scene object to test.
     *  @param queryRaw Case-insensitive search query. */
    bool ObjectMatchesQuickOpenQuery(const SceneObject &obj,
                                     const std::string &queryRaw);

    /** @brief Returns true when the asset identified by @p assetId matches @p queryRaw.
     *  @param assetId  Asset identifier string.
     *  @param asset    Asset definition whose display name and fields are searched.
     *  @param queryRaw Case-insensitive search query. */
    bool AssetMatchesQuickOpenQuery(const std::string &assetId,
                                    const AssetDef &asset,
                                    const std::string &queryRaw);

    /** @brief Determines the filtered-list state after applying a search query.
     *  @param totalCount Total number of items before filtering.
     *  @param shownCount Number of items that passed the filter.
     *  @param queryRaw   The search query string (may be empty).
     *  @return The appropriate FilteredListState value. */
    FilteredListState EvaluateFilteredListState(size_t totalCount, int shownCount,
                                                std::string_view queryRaw);
} // namespace Horo::Editor
