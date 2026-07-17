#pragma once

/**
 * @file EditorViewportRenderer.h
 * @brief Backend-neutral editor viewport presentation contract.
 */

#include "editor/renderer/EditorViewportScene.h"
#include "Horo/Foundation/Result.h"
#include "Horo/Runtime/Render/RenderBackend.h"

namespace Horo::Editor
{
    /** @brief Pixel extent requested by the editor viewport panel. */
    struct EditorViewportExtent
    {
        std::uint32_t width{0};
        std::uint32_t height{0};

        /** @brief Reports whether the extent can back a render target. */
        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return width > 0 && height > 0;
        }
    };

    /** @brief Backend-supplied GUI texture identity and normalized presentation coordinates. */
    struct EditorViewportTextureView
    {
        std::uintptr_t textureId{0};
        float u0{0.0F};
        float v0{0.0F};
        float u1{1.0F};
        float v1{1.0F};

        /** @brief Reports whether the matching GUI bridge can consume this view. */
        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return textureId != 0;
        }
    };

    /**
     * @brief Editor-private adapter that renders one viewport image with the selected graphics backend.
     *
     * Implementations are selected by the editor composition root together with the renderer backend.
     * The returned texture identity is consumed only by the matching editor GUI renderer bridge; it is
     * not a public RHI resource handle. Calls are serialized on the render-capable UI thread.
     */
    class IEditorViewportRenderer : public Render::IStaticMeshPassExecutor
    {
    public:
        virtual ~IEditorViewportRenderer() = default;

        /** @brief Records the panel's desired render-target extent for the current frame. */
        virtual void RequestExtent(EditorViewportExtent extent) noexcept = 0;

        /** @brief Returns the most recent panel extent request for static-mesh pass construction. */
        [[nodiscard]] virtual EditorViewportExtent RequestedExtent() const noexcept = 0;

        /** @brief Returns the matching GUI bridge texture view, including backend-specific orientation. */
        [[nodiscard]] virtual EditorViewportTextureView TextureView() const noexcept = 0;

        /** @brief Reports whether a complete viewport image is available for presentation. */
        [[nodiscard]] virtual bool IsReady() const noexcept = 0;
    };
} // namespace Horo::Editor
