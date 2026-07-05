/** @file EditorFileBrowser.h
 *  @brief Reusable file browser component with thumbnail grid for the Import Asset modal.
 *
 *  Displays a directory's contents as a thumbnail grid with navigation, search,
 *  and drag-and-drop support. Uses the engine's existing thumbnail rendering
 *  pipeline for mesh and texture previews.
 */
#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

namespace Horo::Editor {

/** @brief Single entry in the file browser grid. */
struct FileBrowserEntry {
    std::string name;           /**< Display name (file or directory name). */
    std::string fullPath;       /**< Absolute filesystem path. */
    uint64_t sizeBytes = 0;     /**< File size in bytes (0 for directories). */
    int itemCount = -1;         /**< Number of items in directory (-1 = not a directory / not counted). */
    bool isDirectory = false;   /**< True for directories. */
    bool isSelected = false;    /**< True when this entry is the current selection. */
    std::string extension;      /**< Lowercase extension including dot (e.g. ".fbx"). */
};

/** @brief Type filter applied to file browser entries. */
enum class FileBrowserTypeFilter {
    Folders,
    Meshes,
    Textures,
    Scenes,
    Scripts,
    Shaders,
    Code,
    Count,
};

inline constexpr size_t kFileBrowserTypeFilterCount =
    static_cast<size_t>(FileBrowserTypeFilter::Count);

/** @brief Mutable state for the file browser component. */
struct FileBrowserState {
    std::filesystem::path currentDir;       /**< Currently displayed directory. */
    std::filesystem::path rootDir;          /**< Project root — navigation boundary (can't go above). */
    std::vector<FileBrowserEntry> entries;  /**< All entries in currentDir (unfiltered). */
    std::string searchQuery;                /**< Current search filter text. */
    std::array<bool, kFileBrowserTypeFilterCount> typeFilters{}; /**< Active file type filters; none selected means all files. */
    std::vector<FileBrowserEntry> filteredEntries; /**< Entries matching searchQuery. */
    std::string selectedFilePath;           /**< Absolute path of the selected file (empty = none). */
    bool hasSelection = false;              /**< True when a file (not directory) is selected. */
};

/** @brief File browser component with thumbnail grid, navigation, and drag-and-drop.
 *
 *  Usage:
 *  @code
 *  EditorFileBrowser browser;
 *  browser.SetRootPath(projectRoot);
 *  browser.NavigateTo(projectRoot / "assets");
 *  if (browser.Draw()) {
 *      // Selection changed — use browser.GetSelectedFilePath()
 *  }
 *  @endcode
 */
class EditorFileBrowser {
public:
    /** @brief Sets the root directory that navigation cannot go above.
     *  @param root Absolute path to the project root.
     */
    void SetRootPath(const std::filesystem::path& root);

    /** @brief Navigates to a directory and refreshes the entry list.
     *  @param dir Absolute path to the target directory.
     */
    void NavigateTo(const std::filesystem::path& dir);

    /** @brief Navigates one level up (clamped to rootDir). */
    void NavigateUp();

    /** @brief Refreshes the current directory listing. */
    void Refresh();

    /** @brief Draws the file browser UI (nav bar, search, thumbnail grid).
     *  @param gridHeight Fixed height for the grid area. If > 0, the grid scrolls internally
     *                    while nav/search stay fixed. If 0, grid expands naturally.
     *  @return True when the file selection changed this frame. */
    bool Draw(float gridHeight = 0.0f);

    /** @brief Draws the status bar (file/directory count).
     *  Call this separately when you want to pin it to a fixed position. */
    void DrawStatusBar() const;

    /** @brief Returns the absolute path of the currently selected file.
     *  @return Empty string when no file is selected.
     */
    const std::string& GetSelectedFilePath() const { return m_state.selectedFilePath; }

    /** @brief Returns true when a file (not directory) is currently selected. */
    bool HasSelection() const { return m_state.hasSelection; }

    /** @brief Handles a file dropped onto the browser area.
     *  @param x Drop X coordinate in screen space.
     *  @param y Drop Y coordinate in screen space.
     *  @param path Dropped file path.
     *  @return True when the drop was accepted and the browser navigated/selected.
     */
    bool HandleFileDrop(float x, float y, const std::string& path);

    /** @brief Sets the modal content rect for drag-and-drop hit testing.
     *  @param minX,minY,maxX,maxY Screen-space rectangle of the modal.
     */
    void SetModalRect(float minX, float minY, float maxX, float maxY) {
        m_modalMinX = minX; m_modalMinY = minY;
        m_modalMaxX = maxX; m_modalMaxY = maxY;
    }

    /** @brief Returns the current state for external inspection (read-only). */
    const FileBrowserState& State() const { return m_state; }

    /** @brief Sets the large FA font used for thumbnail icon rendering.
     *
     *  This font is only pushed during DrawThumbnailTile and does not affect
     *  toolbar, search, or any other UI icon rendering.  Call once during
     *  editor initialisation after loading the large FA font.
     *  @param font  Standalone FontAwesome font loaded at a larger pixel size.
     */
    static void SetLargeIconFont(ImFont* font);

private:
    /** @brief Reads the current directory and populates m_state.entries. */
    void RefreshEntries();

    /** @brief Applies the current search filter to produce filteredEntries. */
    void ApplySearchFilter();

    /** @brief Draws the navigation bar (back/up, breadcrumb, refresh). */
    void DrawNavBar();

    /** @brief Draws the search bar. */
    void DrawSearchBar();

    /** @brief Draws the thumbnail grid. */
    void DrawThumbnailGrid();

    /** @brief Draws a single thumbnail tile.
     *  @param entry The file/directory entry to render.
     *  @param thumbSize Size of the thumbnail area in pixels.
     *  @param tileH Total tile height in pixels.
     */
    void DrawThumbnailTile(const FileBrowserEntry& entry, float thumbSize, float tileH);
    void DrawThumbnailTileIcon(ImDrawList* dl, const FileBrowserEntry& entry, const ImVec2& thumbMin, const ImVec2& thumbMax);
    void DrawThumbnailTileLabel(ImDrawList* dl, const FileBrowserEntry& entry, float thumbSize, const ImVec2& tileMin, const ImVec2& thumbMax) const;

    /** @brief Draws the drag-and-drop zone at the top of the browser. */
    void DrawDropZone();

    /** @brief Selects a file entry and updates state.
     *  @param entry The entry to select.
     */
    void SelectEntry(const FileBrowserEntry& entry);

    /** @brief Clears the current selection. */
    void ClearSelection();

    FileBrowserState m_state;

    static ImFont* s_largeIconFont;   /**< Standalone FA font for thumbnail icons (not merged). */

    // Drag-and-drop hit testing
    float m_modalMinX = 0;
    float m_modalMinY = 0;
    float m_modalMaxX = 0;
    float m_modalMaxY = 0;
    float m_dropZoneMinY = 0;
    float m_dropZoneMaxY = 0;
    float m_gridMinY = 0;
    float m_gridMaxY = 0;

    // Thumbnail grid layout
    static constexpr int kColumns = 4;
    static constexpr float kTileSpacing = 9.0f;
    static constexpr float kThumbPad = 9.0f;

    // Mesh preview cache keyed by absolute file path
    struct PreviewCacheEntry {
        std::string path;
        ImTextureID textureId = 0;
        bool loaded = false;
        bool failed = false;
    };
    
    struct StringHash {
        using is_transparent = void;
        size_t operator()(std::string_view txt) const {
            return std::hash<std::string_view>{}(txt);
        }
    };
    
    std::unordered_map<std::string, PreviewCacheEntry, StringHash, std::equal_to<>> m_previewCache;
    std::unordered_map<std::string, PreviewCacheEntry, StringHash, std::equal_to<>> m_texturePreviewCache; /**< Cached texture image previews (PNG, JPG, etc.). */

    std::optional<std::filesystem::path> m_pendingNavigate; /**< Deferred directory navigation to avoid invalidating grid iteration. */
};

} // namespace Horo::Editor
