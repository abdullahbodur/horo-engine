#pragma once

/**
 * @file XRViewRenderPlan.h
 * @brief Bounded typed XR views, configurations, and runtime-owned external render targets.
 */

#include "Horo/Runtime/Render/RenderResourceDescriptors.h"
#include "Horo/XR/XRCapabilities.h"
#include "Horo/XR/XRSpacePose.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Horo::XR {
    struct XRViewConfigurationRevisionTag;

    /** @brief Monotonic publication revision of one active XR view configuration. */
    using XRViewConfigurationRevision = XRGeneration<XRViewConfigurationRevisionTag>;

    /** @brief Backend-neutral semantic family of a runtime-discovered view configuration. */
    enum class XRViewConfigurationType : std::uint8_t {
        PrimaryMono,
        PrimaryStereo,
        QuadView,
        Other,
        Count
    };

    /** @brief Semantic role of one runtime-ordered view; array position is never eye identity. */
    enum class XRViewRole : std::uint8_t {
        Primary,
        PrimaryLeft,
        PrimaryRight,
        FoveatedInsetLeft,
        FoveatedInsetRight,
        Auxiliary,
        Count
    };

    /** @brief Runtime compositor environment blend behavior independent of native enums. */
    enum class XREnvironmentBlendMode : std::uint8_t {
        Opaque,
        Additive,
        AlphaBlend,
        Count
    };

    /** @brief Explicit implementation admission profile; it never truncates a discovered view set. */
    enum class XRViewAdmissionMode : std::uint8_t {
        SimulatorSingleView,
        PrimaryStereo,
        BoundedNView,
        Count
    };

    /** @brief Runtime-owned external image role presented to the Renderer bridge. */
    enum class XRExternalRenderTargetRole : std::uint8_t {
        ProjectionColor,
        ProjectionDepth,
        Count
    };

    /** @brief Compile-time bounds for frame-hot view and external-target validation. */
    struct XRViewRenderHardLimits final {
        static constexpr std::uint32_t MaximumTargets = XRHardLimits::MaximumViews * 2U; /**< Color plus depth per view. */
        static constexpr std::uint32_t MaximumArrayLayers = 2048; /**< Maximum admitted runtime target array layers. */
        static constexpr std::uint32_t MaximumSampleCount = 64;   /**< Maximum power-of-two multisample count. */
    };

    /** @brief Asymmetric projection tangents in Horo view space. */
    struct XRViewProjection final {
        float leftTangent{};  /**< Negative or zero tangent of the left plane. */
        float rightTangent{}; /**< Positive or zero tangent of the right plane. */
        float downTangent{};  /**< Negative or zero tangent of the bottom plane. */
        float upTangent{};    /**< Positive or zero tangent of the top plane. */

        /** @brief Checks finite, ordered, non-degenerate projection planes. @return True for valid asymmetric projection evidence. */
        [[nodiscard]] bool IsValid() const noexcept;
        constexpr auto operator<=>(const XRViewProjection &) const noexcept = default;
    };

    /** @brief Active pixel rectangle inside a physical runtime-owned allocation. */
    struct XRRenderRectangle final {
        std::uint32_t x{};      /**< Left pixel offset. */
        std::uint32_t y{};      /**< Top pixel offset. */
        std::uint32_t width{};  /**< Non-zero active width. */
        std::uint32_t height{}; /**< Non-zero active height. */

        /**
         * @brief Checks the rectangle against an allocation without overflowing integer arithmetic.
         * @param allocation Physical target allocation extent.
         * @return True when the non-empty rectangle is fully contained.
         */
        [[nodiscard]] bool FitsWithin(Render::FramebufferExtent allocation) const noexcept;
        constexpr auto operator<=>(const XRRenderRectangle &) const noexcept = default;
    };

    /** @brief Exact active runtime view-configuration evidence for one session generation. */
    struct XRViewConfigurationDescriptor final {
        XRContractVersion contractVersion{CurrentXRContractVersion};        /**< Horo XRApi semantic contract version. */
        XRSessionId session;                                                /**< Exact active session owner. */
        XRViewConfigurationId id;                                           /**< Generation-safe active configuration. */
        XRViewConfigurationRevision revision;                               /**< Immutable publication revision. */
        XRViewConfigurationType type{XRViewConfigurationType::PrimaryMono}; /**< Runtime-discovered semantic family. */
        XREnvironmentBlendMode blendMode{XREnvironmentBlendMode::Opaque};   /**< Runtime compositor blend requirement. */
        std::uint32_t viewCount{};                                          /**< Complete runtime-required view count. */
    };

    /** @brief One runtime-ordered view with presentation-purpose pose and asymmetric projection evidence. */
    struct XRViewDescriptor final {
        XRViewId id;                                 /**< Stable live semantic view identity. */
        std::uint32_t order{};                       /**< Zero-based runtime order retained verbatim. */
        XRViewRole role{XRViewRole::Primary};        /**< Semantic role independent of order. */
        XRPoseDescriptor pose;                       /**< Presentation-prediction pose evidence. */
        XRViewProjection projection;                 /**< Backend-neutral asymmetric projection. */
        Render::FramebufferExtent recommendedExtent; /**< Runtime-recommended physical extent. */
        Render::FramebufferExtent maximumExtent;     /**< Runtime-advertised physical maximum. */
    };

    /** @brief One acquired runtime-owned image exposed as an external Renderer target requirement. */
    struct XRExternalRenderTargetDescriptor final {
        XRViewId view;                                                                  /**< Exact view receiving the target. */
        XRExternalRenderTargetRole role{XRExternalRenderTargetRole::ProjectionColor};   /**< Color or optional depth role. */
        XRSwapchainTargetId target;                                                     /**< Runtime-owned swapchain target. */
        XRSwapchainImageId image;                                                       /**< Exact currently acquired image generation. */
        Render::RenderTextureFormat format{Render::RenderTextureFormat::Rgba8Unorm};    /**< Horo render format. */
        Render::RenderTextureUsage usage{Render::RenderTextureUsage::RenderAttachment}; /**< Required renderer uses. */
        Render::FramebufferExtent allocationExtent;                                     /**< Physical allocation extent. */
        XRRenderRectangle renderRectangle;                                              /**< Active render area within the allocation. */
        std::uint32_t arrayLayer{};                                                     /**< Exact array slice used by this view. */
        std::uint32_t arrayLayerCount{1};                                               /**< Physical runtime image array-layer count. */
        std::uint32_t sampleCount{1};                                                   /**< Runtime-selected power-of-two samples. */
    };

    /** @brief Finite implementation admission policy captured before image use. */
    struct XRViewAdmissionPolicy final {
        XRViewAdmissionMode mode{XRViewAdmissionMode::PrimaryStereo}; /**< Explicit supported execution family. */
        std::uint32_t maximumViews{2};                                /**< Implementation ceiling, never a truncation count. */
        bool supportsDepthTargets{false};                             /**< Whether optional projection depth is admitted. */
    };

    /** @brief Borrowed construction input copied into fixed storage only after complete validation. */
    struct XRViewRenderPlanDescriptor final {
        XRViewConfigurationDescriptor configuration;               /**< Exact active configuration publication. */
        XRRenderPredictionTime predictedDisplayTime;               /**< Presentation time shared by every view pose. */
        std::span<const XRViewDescriptor> views;                   /**< Complete runtime-ordered view set. */
        std::span<const XRExternalRenderTargetDescriptor> targets; /**< Complete color and optional depth target set. */
        std::span<const XRSwapchainImageId> acquiredImages;        /**< Unique current acquired images from the runtime owner. */
    };

    /** @brief Immutable fixed-capacity N-view plan safe for frame-hot read access without allocation. */
    class XRViewRenderPlan final {
    public:
        /**
         * @brief Validates and copies one complete view and target plan atomically.
         * @param descriptor Complete runtime evidence and current acquired-image truth.
         * @param activeSession Exact current session, or invalid after shutdown.
         * @param activeConfiguration Exact active configuration generation.
         * @param expectedRevision Configuration revision captured by the coordinator.
         * @param activeOriginRevision Current world-origin adapter revision.
         * @param systemMaximumViews Immutable admitted system view limit.
         * @param policy Explicit implementation admission policy.
         * @return Immutable plan or typed invalid, stale, unsupported, unavailable, incompatible, or capacity failure.
         * @post Success performs no heap allocation, native work, blocking I/O, queue mutation, or CPU-GPU synchronization.
         */
        [[nodiscard]] static Result<XRViewRenderPlan> Create(const XRViewRenderPlanDescriptor &descriptor, const XRSessionId &activeSession,
                                                             const XRViewConfigurationId &activeConfiguration,
                                                             XRViewConfigurationRevision expectedRevision,
                                                             XRWorldOriginRevision activeOriginRevision, std::uint32_t systemMaximumViews,
                                                             const XRViewAdmissionPolicy &policy);

        /** @brief Returns exact immutable configuration evidence. @return Active configuration captured by the plan. */
        [[nodiscard]] const XRViewConfigurationDescriptor &Configuration() const noexcept;
        /** @brief Returns the common render prediction time. @return Exact predicted display time. */
        [[nodiscard]] XRRenderPredictionTime PredictedDisplayTime() const noexcept;
        /** @brief Returns the complete runtime-ordered view set. @return Immutable contiguous views. */
        [[nodiscard]] std::span<const XRViewDescriptor> Views() const noexcept;
        /** @brief Returns the complete canonical external-target set. @return Immutable contiguous target descriptors. */
        [[nodiscard]] std::span<const XRExternalRenderTargetDescriptor> Targets() const noexcept;

    private:
        explicit XRViewRenderPlan(const XRViewRenderPlanDescriptor &descriptor) noexcept;

        XRViewConfigurationDescriptor configuration_;
        XRRenderPredictionTime predictedDisplayTime_;
        std::array<XRViewDescriptor, XRHardLimits::MaximumViews> views_{};
        std::array<XRExternalRenderTargetDescriptor, XRViewRenderHardLimits::MaximumTargets> targets_{};
        std::size_t viewCount_{};
        std::size_t targetCount_{};
    };

    /**
     * @brief Revalidates a retained plan against replacement, shutdown, origin, and acquired-image fences.
     * @param plan Immutable plan retained by a consumer.
     * @param activeSession Exact current session, or invalid after shutdown.
     * @param activeConfiguration Exact current configuration generation.
     * @param expectedRevision Current configuration publication revision.
     * @param activeOriginRevision Current world-origin adapter revision.
     * @param acquiredImages Current acquired-image truth from the runtime owner.
     * @return Success or typed invalid, stale, or unavailable failure before Renderer use.
     * @post Performs bounded work without allocation, native calls, blocking I/O, or CPU-GPU synchronization.
     */
    [[nodiscard]] Result<void> ValidateXRViewRenderPlan(const XRViewRenderPlan &plan, const XRSessionId &activeSession,
                                                        const XRViewConfigurationId &activeConfiguration,
                                                        XRViewConfigurationRevision expectedRevision,
                                                        XRWorldOriginRevision activeOriginRevision,
                                                        std::span<const XRSwapchainImageId> acquiredImages);
}  // namespace Horo::XR
