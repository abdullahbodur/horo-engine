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

    struct ScreenRectDropZone {
        bool valid = false;
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
        void Clear() noexcept { valid = false; }

        bool Contains(float x, float y, float paddingPx) const noexcept {
            if (!valid)
                return false;
            const float p = paddingPx;
            return x >= minX - p && x <= maxX + p && y >= minY - p && y <= maxY + p;
        }
    };

    struct EditorAssetsPanelCallbacks {
        // Asset operations
        std::function<void(std::string_view)> requestDeleteAsset;
        std::function<void()> markDirtyAndReload;
        std::function<SceneObject(const std::string&)> makeObjectFromAsset;

        // File operations
        std::function<void(int)> setDeferredFilePick;  // param: DeferredFilePick enum value

        // Deferred state callbacks
        std::function<void()> openSettingsModal;
    };

    struct EditorAssetsPanelState {
        // Selection
        std::string* selectedAssetId = nullptr;
        std::vector<int>* selectedIndices = nullptr;

        // Asset draft state
        std::string* assetDraftId = nullptr;
        std::string* assetDraftGuid = nullptr;
        std::string* assetDraftDisplayName = nullptr;
        std::string* assetDraftMesh = nullptr;
        std::string* assetDraftRenderScale = nullptr;
        std::string* assetDraftAlbedoMap = nullptr;
        std::string* assetImportError = nullptr;

        // Modal state
        bool* openNewAssetHeader = nullptr;

        // Drop zones
        ScreenRectDropZone* albedoDraftDrop = nullptr;
        ScreenRectDropZone* albedoSelDrop = nullptr;

        // Search state
        bool* assetSearchOpen = nullptr;
        std::string* assetSearchQuery = nullptr;

        // Document and systems
        SceneDocument* document = nullptr;
        AssetImportService* assetImportService = nullptr;

        // Live scene
        Registry* liveRegistry = nullptr;
    };

    class EditorAssetsPanel {
    public:
        void Draw(const EditorComponentContext& ctx,
                  const EditorAssetsPanelCallbacks& callbacks,
                  const EditorAssetsPanelState& state);

    private:
        void DrawAssetSpotlightPopup(const EditorAssetsPanelState& state,
                                     const std::vector<std::string>& assetIds);

        void DrawAssetGrid(const EditorAssetsPanelState& state,
                          const std::vector<std::string>& assetIds,
                          bool& openNewAssetModal,
                          const EditorAssetsPanelCallbacks& callbacks);

        void DrawAssetTile(const EditorAssetsPanelState& state,
                          const std::string& assetId,
                          const AssetDef& asset,
                          float tileW,
                          float tileH,
                          float thumbPad,
                          float thumbSize,
                          const EditorAssetsPanelCallbacks& callbacks);

        void DrawCreateAssetModal(const EditorAssetsPanelState& state,
                                  bool openModal,
                                  const EditorAssetsPanelCallbacks& callbacks);

        void DrawCreateAssetModalContent(const EditorAssetsPanelState& state,
                                        const EditorAssetsPanelCallbacks& callbacks);
    };
}
