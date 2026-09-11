#pragma once

/**
 * @file UiRenderComposition.h
 * @brief Backend-neutral admission contract for Runtime UI render-graph composition.
 */

#include "Horo/Runtime/Render/RenderGraph.h"
#include "Horo/Runtime/Render/RenderResourceDescriptors.h"
#include "Horo/Runtime/Ui/UiRenderSnapshot.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Horo::Render {
    /** @brief Repository hard ceiling for Runtime UI passes admitted to one view. */
    inline constexpr std::size_t MaximumUiRenderCompositionPasses = 256;

    /** @brief Semantic canvas projection consumed by one Runtime UI render pass. */
    enum class UiRenderCompositionSpace : std::uint8_t {
        World,
        Camera,
        Screen,
    };

    /** @brief Stable scene/display point at which one Runtime UI pass is composed. */
    enum class UiRenderCompositionPoint : std::uint8_t {
        SceneBeforePostProcess,
        SceneAfterPostProcess,
        DisplayOverlay,
    };

    /** @brief Fixed semantic ordering band within one composition point. */
    enum class UiRenderPresentationBand : std::uint8_t {
        World,
        Hud,
        Screen,
        Overlay,
        Modal,
        Loading,
        Debug,
    };

    /** @brief Exact graph resource and backend-neutral texture structure used by a UI pass. */
    struct UiRenderCompositionTarget final {
        RenderGraphResourceId resource;  /**< Graph-local logical texture identity. */
        RenderTextureDescriptor texture; /**< Exact admitted texture shape and format. */
    };

    /**
     * @brief Borrowed inputs for one declared Runtime UI render-graph pass.
     * @details The snapshot pointer is borrowed only for synchronous validation. After validation, the frame owner must retain its
     *          own UiRenderSnapshot lease until render completion. No backend or native object is retained here.
     */
    struct UiRenderCompositionPass final {
        const Runtime::Ui::UiRenderSnapshot *snapshot{}; /**< Immutable per-view projection; never a live Runtime UI tree. */
        RenderGraphPassRef pass;                         /**< Canonical graph pass authored by the renderer frontend. */
        UiRenderCompositionSpace space{UiRenderCompositionSpace::Screen};
        UiRenderCompositionPoint point{UiRenderCompositionPoint::DisplayOverlay};
        UiRenderPresentationBand band{UiRenderPresentationBand::Screen};
        UiRenderCompositionTarget colorInput;           /**< Scene/display color read by the pass. */
        UiRenderCompositionTarget colorOutput;          /**< Scene/display color written by the pass. */
        std::optional<UiRenderCompositionTarget> depth; /**< Required only for world-space composition. */
    };

    /** @brief Per-view bounded admission policy for a borrowed Runtime UI composition request. */
    struct UiRenderCompositionLimits final {
        std::size_t maximumPasses{64}; /**< Finite maximum entries scanned for one view. */

        /** @brief Reports whether the bound is nonzero and within the repository ceiling. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return maximumPasses > 0 && maximumPasses <= MaximumUiRenderCompositionPasses;
        }
    };

    /** @brief Complete allocation-free Runtime UI composition admission request for one exact view and graph. */
    struct UiRenderCompositionRequest final {
        Runtime::Ui::UiRenderViewId view; /**< Exact Runtime UI/renderer view incarnation. */
        RenderGraphOwnerId graphOwner;    /**< One graph owner shared by every pass and target reference. */
        FramebufferExtent outputExtent;   /**< Exact output extent all declared targets must match. */
        UiRenderCompositionLimits limits;
        std::span<const UiRenderCompositionPass> passes; /**< Already ordered, caller-owned entries. */
    };

    /**
     * @brief Validates one bounded per-view Runtime UI composition request without allocation.
     * @param request Borrowed request whose snapshots and spans remain live for this call.
     * @return Success or a stable capacity, identity, target, ordering, or compatibility failure.
     */
    [[nodiscard]] Result<void> ValidateUiRenderComposition(const UiRenderCompositionRequest &request);

}  // namespace Horo::Render
