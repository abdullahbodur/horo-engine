#pragma once

/**
 * @file UiPresentationReceipt.h
 * @brief Renderer-independent Runtime UI presentation evidence and interaction adoption.
 */

#include "Horo/Runtime/Ui/UiRenderSnapshot.h"

#include <cstdint>

namespace Horo::Runtime::Ui {
    /** @brief Terminal presentation disposition for one submitted Runtime UI snapshot. */
    enum class UiPresentationOutcome : std::uint8_t {
        Presented,
        Skipped,
        Failed,
    };

    /** @brief Stable renderer-independent reason for a non-presented Runtime UI snapshot. */
    enum class UiPresentationReason : std::uint8_t {
        None,
        Suppressed,
        OutputUnavailable,
        ViewReplaced,
        ResourceUnavailable,
        ExecutionFailure,
    };

    /** @brief Immutable completion evidence correlated to one exact UI interaction generation. */
    struct UiPresentationReceipt final {
        UiRenderViewId view;
        UiCanvasInstanceId canvas;
        UiInteractionRevision interactionRevision;
        UiRenderSnapshotRevision snapshotRevision;
        UiPresentationOutcome outcome{UiPresentationOutcome::Skipped};
        UiPresentationReason reason{UiPresentationReason::Suppressed};

        /** @brief Validates identity, revisions, outcome, and reason consistency. */
        [[nodiscard]] bool IsValid() const noexcept;
    };

    /**
     * @brief Runtime UI-owned state that adopts interaction geometry only after presentation.
     * @details Skipped and failed receipts consume their snapshot revision but never advance LastPresentedInteraction().
     */
    class UiPresentedInteractionState final {
    public:
        /**
         * @brief Creates an empty tracker for one exact view/canvas incarnation.
         * @param view Exact view owner.
         * @param canvas Exact canvas owner.
         * @return Tracker or a malformed/cross-owner handle failure.
         */
        [[nodiscard]] static Result<UiPresentedInteractionState> Create(UiRenderViewId view, UiCanvasInstanceId canvas);

        /**
         * @brief Applies one strictly newer presentation receipt.
         * @param receipt Exact completion evidence for this tracker.
         * @return True only when interaction eligibility advanced; false for skipped/failed or a repeated interaction revision.
         */
        [[nodiscard]] Result<bool> Apply(const UiPresentationReceipt &receipt);

        /** @brief Returns the last observed snapshot revision, or invalid zero before any receipt. */
        [[nodiscard]] UiRenderSnapshotRevision LastObservedSnapshot() const noexcept;
        /** @brief Returns the last successfully presented interaction revision, or invalid zero before presentation. */
        [[nodiscard]] UiInteractionRevision LastPresentedInteraction() const noexcept;

    private:
        UiPresentedInteractionState(UiRenderViewId view, UiCanvasInstanceId canvas) noexcept;

        UiRenderViewId view_;
        UiCanvasInstanceId canvas_;
        UiRenderSnapshotRevision lastObservedSnapshot_;
        UiInteractionRevision lastPresentedInteraction_;
    };
}  // namespace Horo::Runtime::Ui
