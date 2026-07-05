/** @file EditorAssetsPanel.h
 *  @brief Asset browser panel with grid view, drag-drop zones, and asset creation. */
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ui/editor/SceneDocument.h"

namespace Horo {
    class Registry;
    struct RenderTargetHandle;
}

namespace Horo::Editor {
    struct EditorComponentContext;
    class AssetImportService;

    /** @brief Screen-space rectangle used as a drop target for drag-and-drop operations. */
    struct ScreenRectDropZone {
        bool valid = false;   /**< True when the zone is active and should receive drops. */
        float minX = 0.0f;   /**< Left edge of the drop zone in screen pixels. */
        float minY = 0.0f;   /**< Top edge of the drop zone in screen pixels. */
        float maxX = 0.0f;   /**< Right edge of the drop zone in screen pixels. */
        float maxY = 0.0f;   /**< Bottom edge of the drop zone in screen pixels. */

        /** @brief Resets the zone to an inactive state. */
        void Clear() noexcept { valid = false; }

        /** @brief Returns true if the point (x, y) falls within the zone, expanded by paddingPx.
         *  @param x         Screen X coordinate to test.
         *  @param y         Screen Y coordinate to test.
         *  @param paddingPx Outward expansion in pixels applied to all four sides.
         *  @return True when the point is inside the padded rectangle and the zone is valid. */
        bool Contains(float x, float y, float paddingPx) const noexcept {
            if (!valid)
                return false;
            const float p = paddingPx;
            return x >= minX - p && x <= maxX + p && y >= minY - p && y <= maxY + p;
        }
    };

    /** @brief Callbacks supplied by EditorLayer to drive asset-panel operations. */
    struct EditorAssetsPanelCallbacks {
        // Asset operations
        std::function<void(std::string_view)> requestDeleteAsset;           /**< Request deletion of the asset with the given identifier. */
        std::function<void()> markDirtyAndReload;                           /**< Mark the scene dirty and trigger an asset reload. */
        std::function<SceneObject(std::string_view)> makeObjectFromAsset; /**< Construct a default scene object from the named asset. */

        // File operations
        std::function<void(int)> setDeferredFilePick; /**< Request a deferred file-picker with the given DeferredFilePick enum value. */

        // Deferred state callbacks
        std::function<void()> openSettingsModal;   /**< Open the editor settings modal. */
        std::function<void()> openImportAssetModal; /**< Open the Import Asset modal to import an asset from a source file. */
    };

    /** @brief Mutable editor state accessed by the assets panel during rendering. */
    struct EditorAssetsPanelState {
        // Selection
        std::string* selectedAssetId = nullptr;        /**< Pointer to the currently selected asset identifier. */
        std::vector<int>* selectedIndices = nullptr;   /**< Pointer to the list of selected scene-object indices. */

        // Drop zones
        ScreenRectDropZone* albedoSelDrop = nullptr;   /**< Drop zone for the albedo map on the selected-asset inspector. */

        // Search state
        bool* assetSearchOpen = nullptr;          /**< Pointer to the flag controlling search bar visibility. */
        std::string* assetSearchQuery = nullptr;  /**< Pointer to the current search query string. */

        // Document and systems
        SceneDocument* document = nullptr;                  /**< Non-owning pointer to the active scene document. */
        AssetImportService* assetImportService = nullptr;   /**< Non-owning pointer to the asset import service. */

        // Live scene
        Registry* liveRegistry = nullptr; /**< Non-owning pointer to the live ECS registry; may be null outside play mode. */
    };

    /** @brief Pixel dimensions for a single asset tile. */
    struct AssetTileDimensions {
        float tileW;     /**< Tile width in pixels. */
        float tileH;     /**< Tile height in pixels. */
        float thumbPad;  /**< Padding between the tile border and the thumbnail. */
        float thumbSize; /**< Size of the thumbnail image in pixels. */
    };

    /** @brief Draws the asset browser panel, including the grid, spotlight popup, and create-asset modal. */
    class EditorAssetsPanel {
    public:
        /** @brief Draws the full asset panel inside its own ImGui window.
         *  @param ctx       Shared editor component context.
         *  @param callbacks Callable table for asset operations.
         *  @param state     Mutable editor state used to read and write panel data. */
        void Draw(const EditorComponentContext& ctx,
                  const EditorAssetsPanelCallbacks& callbacks,
                  const EditorAssetsPanelState& state) const;

        /** @brief Renders only the asset grid content inside an already-open ImGui window.
         *
         *  Used when embedding the asset panel as a tab inside another panel.
         *  @param callbacks Callable table for asset operations.
         *  @param state     Mutable editor state used to read and write panel data. */
        void DrawContent(const EditorAssetsPanelCallbacks& callbacks,
                         const EditorAssetsPanelState& state) const;

    private:
        /** @brief Draws the asset spotlight popup (quick-search overlay).
         *  @param state    Current panel state.
         *  @param assetIds Ordered list of asset identifiers to display. */
        void DrawAssetSpotlightPopup(const EditorAssetsPanelState& state,
                                     const std::vector<std::string>& assetIds) const;

        /** @brief Draws the main asset grid with one tile per asset.
         *  @param state     Current panel state.
         *  @param assetIds  Ordered list of asset identifiers to display.
         *  @param callbacks Callable table for asset operations. */
        void DrawAssetGrid(const EditorAssetsPanelState& state,
                           const std::vector<std::string>& assetIds,
                           const EditorAssetsPanelCallbacks& callbacks) const;

        /** @brief Draws a single asset tile inside the grid.
         *  @param state     Current panel state.
         *  @param assetId   Unique identifier of the asset for this tile.
         *  @param asset     Asset definition data (name, mesh, textures, etc.).
         *  @param dims      Tile pixel dimensions.
         *  @param callbacks Callable table for asset operations. */
        void DrawAssetTile(const EditorAssetsPanelState& state,
                          const std::string& assetId,
                          const AssetDef& asset,
                          const AssetTileDimensions& dims,
                          const EditorAssetsPanelCallbacks& callbacks) const;

        /** @brief Draws the special "Add asset" placeholder tile at the end of the grid.
         *  @param callbacks Callable table for asset operations.
         *  @param dims      Tile pixel dimensions. */
        void DrawAddAssetTile(const EditorAssetsPanelCallbacks& callbacks,
                              const AssetTileDimensions& dims) const;
    };
}
