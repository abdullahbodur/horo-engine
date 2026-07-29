#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct ImDrawList;
struct ImFont;
struct ImVec2;

namespace Horo::Editor {
    class IEditorGuiRenderer;
    struct ContentBrowserDirectory;
    struct ContentBrowserEntry;

    /** @brief Owns preview textures and renders Asset Browser cards. */
    class AssetBrowserCardRenderer {
    public:
        /** @brief Binds the renderer used to create preview textures. */
        void Attach(IEditorGuiRenderer *renderer) noexcept;

        /** @brief Destroys every owned preview texture and releases the renderer binding. */
        void Detach() noexcept;

        /**
         * @brief Releases cached previews that are no longer represented by visible entries.
         * @param directory Current directory projection.
         * @param visibleEntries Indices of entries that remain visible.
         */
        void RetainVisible(const ContentBrowserDirectory &directory, const std::vector<std::size_t> &visibleEntries);

        /** @brief Draws one folder or asset card, resolving its preview texture when available. */
        void Draw(ImDrawList *drawList, ImFont *font, float fontSize, const ContentBrowserEntry &entry, const ImVec2 &cardMin,
                  float cardWidth, bool hovered, bool selected, bool dimmed);

    private:
        [[nodiscard]] std::uintptr_t ResolvePreview(const ContentBrowserEntry &entry);

        IEditorGuiRenderer *m_renderer{nullptr};
        std::unordered_map<std::string, std::pair<std::uint64_t, std::uintptr_t>> m_previewTextures;
    };
}  // namespace Horo::Editor
