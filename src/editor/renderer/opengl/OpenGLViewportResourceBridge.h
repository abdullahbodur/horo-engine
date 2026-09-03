#pragma once

#include "Horo/Runtime/Render/RenderFrontend.h"

namespace Horo::Editor {
    /** @brief Matching editor integration that consumes generic handles without exposing native objects. */
    class OpenGLViewportResourceBridge final {
    public:
        /** @brief Binds a ready generic mesh's backend-private vertex input object. */
        [[nodiscard]] Result<void> BindMesh(const Render::RenderFrontend &frontend, Render::RenderMeshHandle mesh) const;

        /** @brief Binds a ready generic offscreen render target. */
        [[nodiscard]] Result<void> BindRenderTarget(const Render::RenderFrontend &frontend, Render::RenderTargetHandle target) const;

        /** @brief Binds a ready generic texture view to an OpenGL texture unit slot. */
        [[nodiscard]] Result<void> BindTexture(const Render::RenderFrontend &frontend, Render::RenderTextureViewHandle view,
                                               std::uint32_t unit) const;

        /** @brief Returns the opaque GUI identity derived from a ready generic texture view. */
        [[nodiscard]] Result<std::uintptr_t> EditorImageIdentity(const Render::RenderFrontend &frontend,
                                                                 Render::RenderTextureViewHandle view) const;
    };
}  // namespace Horo::Editor
