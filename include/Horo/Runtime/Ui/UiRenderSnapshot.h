#pragma once

/**
 * @file UiRenderSnapshot.h
 * @brief Immutable backend-neutral Runtime UI render extraction values.
 */

#include "Horo/Assets/AssetId.h"
#include "Horo/Runtime/Ui/UiElementTree.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <variant>

namespace Horo::Runtime::Ui {
    /** @brief Sentinel for an absent optional table reference. */
    inline constexpr std::uint32_t NoUiRenderIndex = std::numeric_limits<std::uint32_t>::max();
    inline constexpr std::uint32_t MaximumUiRenderCommands = 16'384;
    inline constexpr std::uint32_t MaximumUiRenderTextRuns = 4'096;
    inline constexpr std::uint32_t MaximumUiRenderGlyphs = 65'536;
    inline constexpr std::uint32_t MaximumUiRenderClips = 4'096;
    inline constexpr std::uint32_t MaximumUiRenderMasks = 1'024;
    inline constexpr std::uint32_t MaximumUiRenderTransforms = 4'096;
    inline constexpr std::uint32_t MaximumUiRenderResources = 4'096;

    struct UiRenderViewHandleTag;
    struct UiRenderSnapshotRevisionTag;
    struct UiRenderResourceRevisionTag;
    /** @brief Exact Horo-owned view incarnation; never a native surface or backend handle. */
    using UiRenderViewId = UiRuntimeHandle<UiRenderViewHandleTag>;
    /** @brief Monotonic generation of one extracted immutable render snapshot. */
    using UiRenderSnapshotRevision = UiRevision<UiRenderSnapshotRevisionTag>;
    /** @brief Exact Horo resource-source generation expected by extraction. */
    using UiRenderResourceRevision = UiRevision<UiRenderResourceRevisionTag>;

    /** @brief Signed logical point in deterministic 1/64-DIP units. */
    struct UiLogicalPoint final {
        std::int32_t x{}; /**< Horizontal 1/64-DIP coordinate. */
        std::int32_t y{}; /**< Vertical 1/64-DIP coordinate. */
        [[nodiscard]] auto operator<=>(const UiLogicalPoint &) const noexcept = default;
    };

    /** @brief Non-negative logical extent in deterministic 1/64-DIP units. */
    struct UiLogicalExtent final {
        std::int32_t width{};  /**< Non-negative 1/64-DIP width. */
        std::int32_t height{}; /**< Non-negative 1/64-DIP height. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] auto operator<=>(const UiLogicalExtent &) const noexcept = default;
    };

    /** @brief Logical rectangle produced upstream by layout. */
    struct UiLogicalRect final {
        UiLogicalPoint origin;  /**< Signed logical origin. */
        UiLogicalExtent extent; /**< Non-negative logical extent. */
        [[nodiscard]] auto operator<=>(const UiLogicalRect &) const noexcept = default;
    };

    /** @brief Finite affine logical transform; translation uses logical DIP units. */
    struct UiLogicalTransform final {
        std::array<float, 6> values{1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F}; /**< Row-major 2x3 affine values. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] auto operator<=>(const UiLogicalTransform &) const noexcept = default;
    };

    /** @brief Finite normalized linear RGBA color. */
    struct UiLinearColor final {
        float red{};       /**< Linear red in [0, 1]. */
        float green{};     /**< Linear green in [0, 1]. */
        float blue{};      /**< Linear blue in [0, 1]. */
        float alpha{1.0F}; /**< Linear alpha in [0, 1]. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] auto operator<=>(const UiLinearColor &) const noexcept = default;
    };

    /** @brief Semantic realization role for one stable asset reference. */
    enum class UiRenderResourceRole : std::uint8_t {
        Image,
        FontFace,
        Material,
        Mask,
    };

    /** @brief Stable Horo resource identity plus exact source generation; never a GPU handle. */
    struct UiRenderResourceReference final {
        Assets::AssetId asset;                                  /**< Stable asset identity. */
        UiRenderResourceRevision revision;                      /**< Exact immutable source generation. */
        UiRenderResourceRole role{UiRenderResourceRole::Image}; /**< Required semantic realization role. */
    };

    /** @brief One positioned Horo glyph identity in logical 1/64-DIP units. */
    struct UiPositionedGlyph final {
        std::uint32_t glyph{};   /**< Horo glyph identity, never an atlas slot. */
        std::uint32_t cluster{}; /**< Source text cluster index. */
        UiLogicalPoint origin;   /**< Positioned logical origin. */
    };

    /** @brief Immutable glyph range resolved to one exact font resource. */
    struct UiTextRun final {
        std::uint32_t fontResource{}; /**< FontFace resource table index. */
        std::uint32_t firstGlyph{};   /**< First owned glyph table index. */
        std::uint32_t glyphCount{};   /**< Bounded contiguous glyph count. */
        UiLinearColor color;          /**< Resolved linear text color. */
    };

    /** @brief Logical clip node with an optional parent intersection. */
    struct UiClip final {
        UiLogicalRect rect;                    /**< Logical intersection rectangle. */
        std::uint32_t parent{NoUiRenderIndex}; /**< Earlier parent clip or sentinel. */
    };

    /** @brief Logical mask projection backed by one typed mask resource. */
    struct UiMask final {
        UiLogicalRect rect;        /**< Logical mask bounds. */
        std::uint32_t resource{};  /**< Mask resource table index. */
        std::uint32_t transform{}; /**< Transform table index. */
    };

    /** @brief Resolved solid paint payload. */
    struct UiSolidDraw final {
        UiLinearColor color; /**< Resolved linear fill color. */
    };

    /** @brief Resolved border paint payload in logical units. */
    struct UiBorderDraw final {
        UiLinearColor color;  /**< Resolved linear border color. */
        std::int32_t width{}; /**< Non-negative 1/64-DIP width. */
    };

    /** @brief Resolved image paint payload naming one image resource. */
    struct UiImageDraw final {
        std::uint32_t resource{}; /**< Image resource table index. */
        UiLinearColor tint;       /**< Resolved linear tint. */
    };

    /** @brief Resolved text paint payload naming one positioned run. */
    struct UiTextDraw final {
        std::uint32_t run{}; /**< Text run table index. */
    };

    /** @brief Closed backend-neutral draw payload vocabulary. */
    using UiDrawPayload = std::variant<UiSolidDraw, UiBorderDraw, UiImageDraw, UiTextDraw>;

    /** @brief One ordered logical draw emitted for an exact retained element. */
    struct UiDrawCommand final {
        UiElementHandle element;             /**< Exact resident source element. */
        UiLogicalRect rect;                  /**< Resolved logical paint bounds. */
        std::uint32_t transform{};           /**< Required transform table index. */
        std::uint32_t clip{NoUiRenderIndex}; /**< Optional clip table index. */
        std::uint32_t mask{NoUiRenderIndex}; /**< Optional mask table index. */
        float opacity{1.0F};                 /**< Resolved opacity in [0, 1]. */
        UiDrawPayload payload;               /**< Closed resolved paint payload. */
    };

    /** @brief Caller-selected capacities qualified by repository hard ceilings. */
    struct UiRenderSnapshotLimits final {
        std::uint32_t commands{};   /**< Ordered draw command ceiling. */
        std::uint32_t textRuns{};   /**< Positioned text run ceiling. */
        std::uint32_t glyphs{};     /**< Positioned glyph ceiling. */
        std::uint32_t clips{};      /**< Logical clip node ceiling. */
        std::uint32_t masks{};      /**< Logical mask ceiling. */
        std::uint32_t transforms{}; /**< Logical transform ceiling; must be positive. */
        std::uint32_t resources{};  /**< Stable resource reference ceiling. */
        /** @brief Validates every declared ceiling. @return Whether all bounds are supported. */
        [[nodiscard]] bool IsValid() const noexcept;
    };

    /** @brief Exact immutable evidence captured for one per-view extraction. */
    struct UiRenderSnapshotDescriptor final {
        RuntimeUiInstanceId instance;              /**< Exact runtime document instance. */
        UiCanvasInstanceId canvas;                 /**< Exact canvas incarnation. */
        UiDocumentId document;                     /**< Stable source document. */
        UiDocumentRevision documentRevision;       /**< Exact authored source revision. */
        UiRuntimeTreeRevision treeRevision;        /**< Exact retained tree revision. */
        UiInteractionRevision interactionRevision; /**< Exact published layout/interaction generation. */
        UiRenderSnapshotRevision snapshotRevision; /**< Exact extracted snapshot generation. */
        UiRenderViewId view;                       /**< Exact Horo view incarnation. */
        UiRenderSnapshotLimits limits;             /**< Fixed transaction bounds. */
    };

    /** @brief Non-owning complete projection submitted as one extraction transaction. */
    struct UiRenderProjection final {
        std::span<const UiDrawCommand> commands;              /**< Stable paint-order commands. */
        std::span<const UiTextRun> textRuns;                  /**< Positioned text run table. */
        std::span<const UiPositionedGlyph> glyphs;            /**< Positioned glyph table. */
        std::span<const UiClip> clips;                        /**< Logical clip table. */
        std::span<const UiMask> masks;                        /**< Logical mask table. */
        std::span<const UiLogicalTransform> transforms;       /**< Logical transform table. */
        std::span<const UiRenderResourceReference> resources; /**< Stable resource reference table. */
    };

    /**
     * @brief Owning immutable per-view Runtime UI render projection.
     * @details The snapshot owns every array and can outlive the tree and caller inputs. Renderer may realize resources and batch
     *          equivalent paint, but cannot mutate these values or recover a live Runtime UI owner from them.
     */
    class UiRenderSnapshot final {
    public:
        /**
         * @brief Validates and copies one complete ordered extraction transaction.
         * @param tree Exact active retained tree supplying identity and residency evidence.
         * @param descriptor Exact view, source revisions, output revision, and bounds.
         * @param projection Complete non-owning projection copied by the transaction.
         * @return Complete owning snapshot or a typed validation/capacity/lifecycle failure.
         */
        [[nodiscard]] static Result<UiRenderSnapshot> Extract(const UiElementTree &tree, const UiRenderSnapshotDescriptor &descriptor,
                                                              const UiRenderProjection &projection);

        /** @brief Returns exact extraction evidence. @return Borrowed immutable descriptor. */
        [[nodiscard]] const UiRenderSnapshotDescriptor &Descriptor() const noexcept;
        /** @brief Returns stable paint-order commands. @return Borrowed span owned by this snapshot. */
        [[nodiscard]] std::span<const UiDrawCommand> Commands() const noexcept;
        /** @brief Returns positioned text runs. @return Borrowed span owned by this snapshot. */
        [[nodiscard]] std::span<const UiTextRun> TextRuns() const noexcept;
        /** @brief Returns positioned glyph identities. @return Borrowed span owned by this snapshot. */
        [[nodiscard]] std::span<const UiPositionedGlyph> Glyphs() const noexcept;
        /** @brief Returns logical clip nodes. @return Borrowed span owned by this snapshot. */
        [[nodiscard]] std::span<const UiClip> Clips() const noexcept;
        /** @brief Returns logical mask projections. @return Borrowed span owned by this snapshot. */
        [[nodiscard]] std::span<const UiMask> Masks() const noexcept;
        /** @brief Returns logical transforms. @return Borrowed span owned by this snapshot. */
        [[nodiscard]] std::span<const UiLogicalTransform> Transforms() const noexcept;
        /** @brief Returns stable Horo resource references. @return Borrowed span owned by this snapshot. */
        [[nodiscard]] std::span<const UiRenderResourceReference> Resources() const noexcept;

    private:
        struct Storage;
        /** @brief Adopts validated immutable storage. */
        explicit UiRenderSnapshot(std::shared_ptr<const Storage> storage) noexcept;
        std::shared_ptr<const Storage> storage_;
    };
}  // namespace Horo::Runtime::Ui
