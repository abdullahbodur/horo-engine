#pragma once

/**
 * @file StreamingDesiredState.h
 * @brief Typed source residency and retention intent for world streaming.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/WorldStreaming/StreamingSourceDescriptor.h"

#include <cstdint>

namespace Horo::WorldStreaming {
    /**
     * @brief Ordered residency requested by one streaming source.
     * @details Each value includes the guarantees of the preceding value: Activated requires Loaded, while Unloaded requests no cell
     * residency. This order describes one source request and does not combine requests from multiple sources.
     */
    enum class StreamingDesiredResidency : std::uint8_t {
        Unloaded = 0,
        Loaded = 1,
        Activated = 2,
    };

    /**
     * @brief Retention policy requested independently of residency.
     * @details Pinned retains the requested residency until explicit replacement or owner teardown. It neither promotes residency nor
     * bypasses capacity or budget admission.
     */
    enum class StreamingRetention : std::uint8_t {
        Releasable = 0,
        Pinned = 1,
    };

    /** @brief Validated inert desired-state metadata for one stable streaming source. */
    class StreamingSourceDesiredState final {
    public:
        /**
         * @brief Creates a source desired state without registration or ambient mutation.
         * @param source Stable source identity, owner lifetime, intent, priority, and revision.
         * @param residency Ordered residency requested by the source.
         * @param retention Retention policy kept separate from residency.
         * @return Validated value, a source descriptor error, SourceDesiredStateInvalid, or SourceDesiredStateUnsupported.
         */
        [[nodiscard]] static Result<StreamingSourceDesiredState> Create(const StreamingSourceDescriptor &source,
                                                                        StreamingDesiredResidency residency, StreamingRetention retention);

        /** @brief Returns the stable source descriptor. @return Descriptor owned by this value. */
        [[nodiscard]] constexpr const StreamingSourceDescriptor &Source() const noexcept {
            return source_;
        }

        /** @brief Returns the requested residency without applying retention. @return Exact validated residency. */
        [[nodiscard]] constexpr StreamingDesiredResidency Residency() const noexcept {
            return residency_;
        }

        /** @brief Returns the independently requested retention. @return Exact validated retention. */
        [[nodiscard]] constexpr StreamingRetention Retention() const noexcept {
            return retention_;
        }

        /** @brief Tests whether the request needs resident content. @return True for Loaded or Activated. */
        [[nodiscard]] constexpr bool RequiresLoading() const noexcept {
            return residency_ != StreamingDesiredResidency::Unloaded;
        }

        /** @brief Tests whether the request needs published active content. @return True only for Activated. */
        [[nodiscard]] constexpr bool RequiresActivation() const noexcept {
            return residency_ == StreamingDesiredResidency::Activated;
        }

        /** @brief Tests whether the exact requested residency must be retained. @return True only for Pinned. */
        [[nodiscard]] constexpr bool IsPinned() const noexcept {
            return retention_ == StreamingRetention::Pinned;
        }

    private:
        constexpr StreamingSourceDesiredState(const StreamingSourceDescriptor &source, const StreamingDesiredResidency residency,
                                              const StreamingRetention retention) noexcept
            : source_(source), residency_(residency), retention_(retention) {}

        StreamingSourceDescriptor source_;
        StreamingDesiredResidency residency_;
        StreamingRetention retention_;
    };

    /**
     * @brief Validates admission of one desired-state revision against an immutable source snapshot.
     * @param desiredState Structurally validated desired-state value.
     * @param context Current source owner, revision, capacity, and lifecycle snapshot.
     * @return Insert or Replace, or the existing typed stale, capacity, lifecycle, or descriptor error.
     * @details Successful validation authorizes only source admission. It does not reserve budget, mutate cell state, combine multiple
     * source requests, or register ambient state.
     */
    [[nodiscard]] Result<StreamingSourceAdmissionKind> ValidateStreamingSourceDesiredStateAdmission(
        const StreamingSourceDesiredState &desiredState, const StreamingSourceAdmissionContext &context);
}  // namespace Horo::WorldStreaming
