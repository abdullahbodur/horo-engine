#pragma once

/**
 * @file FallbackStreamingProvider.h
 * @brief Explicit single-cell and null composition providers for small and headless worlds.
 */

#include "Horo/WorldStreaming/StreamingSourceDescriptor.h"

#include <array>
#include <optional>
#include <span>

namespace Horo::WorldStreaming {
    /** @brief Closed fallback composition selected before a world-streaming manager is created. */
    enum class FallbackStreamingProviderMode : std::uint8_t {
        SingleCell,
        Null
    };

    /** @brief Owner lifecycle; cancelling and closed providers expose no new cell demand. */
    enum class FallbackStreamingProviderState : std::uint8_t {
        Active,
        Cancelling,
        Closed
    };

    /** @brief Complete candidate configuration validated before publication or replacement. */
    struct FallbackStreamingProviderDescriptor final {
        StreamingSourceOwnerToken owner;           /**< Exact partition and mounted epoch lifetime. */
        StreamingSourceRevision revision;          /**< Strictly increasing immutable configuration revision. */
        FallbackStreamingProviderMode mode{};      /**< Explicit single-cell or feature-absent composition. */
        std::optional<StreamingCellId> singleCell; /**< Present only for SingleCell and never inferred from coordinates. */
        std::uint32_t maximumPublishedCells{};     /**< Mandatory host ceiling; zero is valid only for Null. */
    };

    /**
     * @brief Small value owner for fallback composition demand, not a residency authority or feature-provider hierarchy.
     * The exposed cell is only immutable desired input for the later manager/authority. It never means Loaded, Resident or Active.
     */
    class FallbackStreamingProvider final {
    public:
        FallbackStreamingProvider(const FallbackStreamingProvider &) = delete;
        FallbackStreamingProvider &operator=(const FallbackStreamingProvider &) = delete;

        /** @brief Transfer the unique fallback owner and close the moved-from instance. */
        FallbackStreamingProvider(FallbackStreamingProvider &&other) noexcept;
        FallbackStreamingProvider &operator=(FallbackStreamingProvider &&) = delete;

        /**
         * @brief Validate and publish one initial fallback configuration transactionally.
         * @param descriptor Exact owner, revision, mode, optional cell and capacity ceiling.
         * @return Active provider or a typed invalid, unsupported or capacity error with no partial owner.
         */
        [[nodiscard]] static Result<FallbackStreamingProvider> Create(const FallbackStreamingProviderDescriptor &descriptor);

        /** @brief Return the exact owner lifetime. @return Immutable partition/epoch token. */
        [[nodiscard]] constexpr const StreamingSourceOwnerToken &Owner() const noexcept {
            return descriptor_.owner;
        }

        /** @brief Return the current immutable configuration revision. @return Nonzero admitted revision. */
        [[nodiscard]] constexpr StreamingSourceRevision Revision() const noexcept {
            return descriptor_.revision;
        }

        /** @brief Return the explicit composition mode. @return SingleCell or Null. */
        [[nodiscard]] constexpr FallbackStreamingProviderMode Mode() const noexcept {
            return descriptor_.mode;
        }

        /** @brief Return the lifecycle gate. @return Active, Cancelling or Closed. */
        [[nodiscard]] constexpr FallbackStreamingProviderState State() const noexcept {
            return state_;
        }

        /**
         * @brief Return current desired cells without claiming residency or retaining a caller borrow.
         * @return Exactly one cell for active SingleCell; empty for Null, Cancelling and Closed.
         */
        [[nodiscard]] std::span<const StreamingCellId> DesiredCells() const noexcept;

        /**
         * @brief Replace the active configuration after full candidate validation.
         * @param descriptor Same owner lifetime and a strictly newer revision; mode may change explicitly.
         * @return Success or typed invalid, unsupported, capacity, stale or lifecycle failure without partial mutation.
         */
        [[nodiscard]] Result<void> Replace(const FallbackStreamingProviderDescriptor &descriptor);

        /**
         * @brief Close desired-cell admission for cooperative cancellation.
         * @param owner Exact current owner lifetime. @param revision Exact current configuration revision.
         * @return Success, including an idempotent repeat, or a typed stale/lifecycle error.
         */
        [[nodiscard]] Result<void> RequestCancellation(StreamingSourceOwnerToken owner, StreamingSourceRevision revision) noexcept;

        /**
         * @brief Enter terminal shutdown and expose no desired cells.
         * @param owner Exact current owner lifetime.
         * @return Success, including an idempotent repeat, or a typed stale-owner error.
         */
        [[nodiscard]] Result<void> Shutdown(StreamingSourceOwnerToken owner) noexcept;

    private:
        /** @brief Store an already validated active descriptor without allocation. */
        explicit constexpr FallbackStreamingProvider(const FallbackStreamingProviderDescriptor &descriptor) noexcept
            : descriptor_(descriptor), cell_{descriptor.singleCell.value_or(StreamingCellId{})} {}

        FallbackStreamingProviderDescriptor descriptor_;
        std::array<StreamingCellId, 1> cell_;
        FallbackStreamingProviderState state_{FallbackStreamingProviderState::Active};
    };
}  // namespace Horo::WorldStreaming
