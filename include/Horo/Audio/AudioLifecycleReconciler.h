#pragma once

/**
 * @file AudioLifecycleReconciler.h
 * @brief Control-owner reconciliation for scene unload, device reset, partial startup, and shutdown.
 */

#include "Horo/Audio/AudioEventQueue.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace Horo::Audio {
    /** @brief Hard bound for one runtime's retained operations, terminals, and callback references. */
    inline constexpr std::size_t MaximumAudioLifecycleRecords = 4'096;

    /** @brief Control-owned runtime state relevant to teardown and replacement. */
    enum class AudioLifecycleState : std::uint8_t {
        Initializing,
        Active,
        Resetting,
        Stopping,
        Stopped,
    };

    /** @brief Stable terminal disposition retained before an operation owner disappears. */
    enum class AudioReconciliationReason : std::uint8_t {
        Completed,
        Stopped,
        Failed,
        Cancelled,
        CancelledBySceneUnload,
        CancelledByDeviceReset,
        CancelledByShutdown,
        CancelledByInitializationRollback,
    };

    /** @brief One accepted operation awaiting an exact callback or lifecycle terminal outcome. */
    struct AudioPendingOperation final {
        AudioCommandScope scope;          /**< Exact runtime, command epoch, and scene context. */
        std::uint64_t acceptedSequence{}; /**< Strictly increasing runtime-local admission sequence. */

        constexpr auto operator<=>(const AudioPendingOperation &) const noexcept = default;
    };

    /** @brief Immutable terminal result retained until its control-side owner acknowledges publication. */
    struct AudioReconciledOperation final {
        AudioPendingOperation operation;                                        /**< Original accepted operation identity. */
        AudioReconciliationReason reason{AudioReconciliationReason::Completed}; /**< Exact terminal disposition. */

        constexpr auto operator<=>(const AudioReconciledOperation &) const noexcept = default;
    };

    /** @brief Callback-visible identity that remains tracked until a proven last-use boundary. */
    using AudioCallbackReference = std::variant<AudioVoiceHandle, AudioMemoryHandle>;

    /** @brief Scene-scoped callback-visible reference tracked by the control owner. */
    struct AudioTrackedCallbackReference final {
        AudioSceneContextHandle scene;    /**< Exact owning scene context generation. */
        AudioCallbackReference reference; /**< Voice or prepared storage identity. */

        constexpr auto operator<=>(const AudioTrackedCallbackReference &) const noexcept = default;
    };

    /** @brief Immutable capacities and exact initial runtime/callback generations. */
    struct AudioLifecycleReconcilerDescriptor final {
        AudioRuntimeId owner;                       /**< Exact runtime generation. */
        std::uint64_t commandEpoch{};               /**< Non-zero initial command epoch. */
        AudioDeviceEpoch callbackEpoch;             /**< Initial callback generation when callbackRequired is true. */
        std::uint64_t clockDomain{};                /**< Horo monotonic domain for callback evidence. */
        std::size_t maximumPendingOperations{256};  /**< Bounded unresolved operation capacity. */
        std::size_t maximumTerminalResults{256};    /**< Bounded retained terminal capacity. */
        std::size_t maximumCallbackReferences{256}; /**< Bounded active voice/resource capacity. */
        bool callbackRequired{true};                /**< Whether activation publishes callback-visible state. */
    };

    /** @brief Observable control-owner facts without exposing mutable storage. */
    struct AudioLifecycleReconciliationSnapshot final {
        AudioLifecycleState state{AudioLifecycleState::Initializing}; /**< Current runtime lifecycle. */
        std::uint64_t commandEpoch{};                                 /**< Current command generation. */
        std::size_t pendingOperations{};                              /**< Awaiting terminal result. */
        std::size_t retainedTerminalResults{};                        /**< Awaiting owner acknowledgement. */
        std::size_t callbackReferences{};                             /**< Not yet past a last-use boundary. */
        bool callbackAttached{};                                      /**< Native callback entry may still occur. */
        bool callbackQuiesced{};                                      /**< Matching epoch quiescence was observed. */
    };

    /**
     * @brief Single-control-owner lifecycle and terminal reconciliation authority.
     * @details This value creates no threads and performs no device calls. The host feeds it accepted operations,
     * callback facts, and backend-detachment proof in serialized control order. Scene unload acknowledges its ordered
     * barrier directly; reset and shutdown require matching callback quiescence and native detachment before tracked
     * callback references are released. Terminal results are retained before the corresponding owner may disappear.
     */
    class AudioLifecycleReconciler final {
    public:
        /**
         * @brief Validates dimensions and reserves all bounded control-side storage.
         * @param descriptor Runtime identity, generations, callback policy, and capacities.
         * @return Prepared reconciler or typed identity, capacity, or allocation failure.
         */
        [[nodiscard]] static Result<AudioLifecycleReconciler> Create(const AudioLifecycleReconcilerDescriptor &descriptor);

        AudioLifecycleReconciler(const AudioLifecycleReconciler &) = delete;
        AudioLifecycleReconciler &operator=(const AudioLifecycleReconciler &) = delete;
        /** @brief Transfers sole control ownership and leaves the source Stopped. @param other Source owner. */
        AudioLifecycleReconciler(AudioLifecycleReconciler &&other) noexcept;
        AudioLifecycleReconciler &operator=(AudioLifecycleReconciler &&) = delete;

        /** @brief Records native callback attachment during startup. @return Success or RuntimeInactive. */
        [[nodiscard]] Result<void> MarkCallbackAttached();
        /** @brief Commits successful startup. @return Success or RuntimeInactive when required callback proof is absent. */
        [[nodiscard]] Result<void> Activate();

        /**
         * @brief Retains one accepted operation before callback publication.
         * @param operation Exact scope and strictly increasing non-zero sequence.
         * @return Success or typed inactive, identity, stale, or capacity failure.
         */
        [[nodiscard]] Result<void> Admit(AudioPendingOperation operation);

        /**
         * @brief Tracks one active voice or retained storage generation visible to the callback.
         * @param reference Exact scene-scoped reference.
         * @return Success or typed inactive, identity, duplicate, or capacity failure.
         */
        [[nodiscard]] Result<void> TrackCallbackReference(AudioTrackedCallbackReference reference);

        /**
         * @brief Applies an exact callback terminal and retires its corresponding tracked reference.
         * @param event Terminal event already drained by control.
         * @return Success or typed malformed, stale, duplicate, or capacity failure.
         */
        [[nodiscard]] Result<void> ObserveTerminal(const AudioTerminalEvent &event);

        /**
         * @brief Closes one scene generation and records its ordered unload barrier.
         * @param scene Exact scene context to close.
         * @param barrierSequence Non-zero accepted sequence through which callback work must be applied.
         * @return Success or typed identity/transition failure.
         */
        [[nodiscard]] Result<void> BeginSceneUnload(AudioSceneContextHandle scene, std::uint64_t barrierSequence);

        /**
         * @brief Reconciles one closed scene after callback acknowledgement of its exact barrier.
         * @param scene Exact closing scene context.
         * @param barrierSequence Exact barrier supplied to BeginSceneUnload.
         * @return Success after terminal publication and callback-reference retirement, or a typed failure with no partial change.
         */
        [[nodiscard]] Result<void> AcknowledgeSceneUnload(AudioSceneContextHandle scene, std::uint64_t barrierSequence);

        /** @brief Closes device-dependent admission and begins callback quiescence for reset. @return Success or RuntimeInactive. */
        [[nodiscard]] Result<void> BeginDeviceReset();
        /** @brief Closes all admission and begins host-shutdown or startup-rollback reconciliation. @return Success or prior outcome. */
        [[nodiscard]] Result<void> BeginShutdown();

        /**
         * @brief Accepts matching callback-quiesced evidence for reset or shutdown.
         * @param event Exact callback event from the active queue.
         * @return Success or typed identity/transition failure.
         */
        [[nodiscard]] Result<void> ObserveCallbackQuiesced(const AudioCallbackEvent &event);

        /**
         * @brief Publishes reset cancellations, releases old callback references, and activates new generations.
         * @param nextCommandEpoch Strictly newer non-zero command epoch.
         * @param nextCallbackEpoch Valid fresh callback generation for the same owner.
         * @param backendDetached Whether native callback entry into the old epoch is impossible.
         * @return Success or typed identity, transition, capacity, or detachment failure with no partial change.
         */
        [[nodiscard]] Result<void> CompleteDeviceReset(std::uint64_t nextCommandEpoch, AudioDeviceEpoch nextCallbackEpoch,
                                                       bool backendDetached);

        /**
         * @brief Publishes shutdown/rollback cancellations and commits Stopped after safe callback detachment.
         * @param backendDetached Whether native callback entry is impossible when one was attached.
         * @return Success, repeated success after Stopped, or typed transition/capacity/detachment failure. A capacity failure
         * leaves Stopping and all pending ownership intact so existing terminal results can be acknowledged before retry.
         */
        [[nodiscard]] Result<void> CompleteShutdown(bool backendDetached);

        /** @brief Returns the retained terminal count. @return Number of outcomes awaiting acknowledgement. */
        [[nodiscard]] std::size_t TerminalResultCount() const noexcept;
        /**
         * @brief Copies one retained terminal outcome without exposing invalidatable storage.
         * @param index Zero-based stable admission-order index.
         * @return Owned outcome copy, or null when index is out of range.
         */
        [[nodiscard]] std::optional<AudioReconciledOperation> TerminalResult(std::size_t index) const noexcept;
        /**
         * @brief Releases one terminal only after its external operation owner observed publication.
         * @param operation Exact retained scope and sequence, including its command epoch.
         * @return Whether a matching result was removed, or a typed malformed identity failure.
         */
        [[nodiscard]] Result<bool> AcknowledgeTerminal(const AudioPendingOperation &operation);
        /** @brief Returns current lifecycle and ownership counts. @return Immutable snapshot. */
        [[nodiscard]] AudioLifecycleReconciliationSnapshot Snapshot() const noexcept;

    private:
        /** @brief One closed scene awaiting its exact callback barrier acknowledgement. */
        struct SceneBarrier final {
            AudioSceneContextHandle scene; /**< Exact context generation. */
            std::uint64_t sequence{};      /**< Accepted ordered barrier sequence. */

            constexpr auto operator<=>(const SceneBarrier &) const noexcept = default;
        };

        /** @brief Constructs fully reserved storage after descriptor validation. */
        explicit AudioLifecycleReconciler(AudioLifecycleReconcilerDescriptor descriptor);
        /** @brief Validates and applies one voice terminal from the closed terminal-event variant. */
        [[nodiscard]] Result<void> ObserveTerminalValue(const AudioVoiceTerminalEvent &event);
        /** @brief Validates and applies one resource terminal from the closed terminal-event variant. */
        [[nodiscard]] Result<void> ObserveTerminalValue(const AudioResourceReleaseEvent &event);
        /** @brief Publishes a validated exact terminal and retires its matching callback reference. */
        [[nodiscard]] Result<void> PublishTerminal(AudioPendingOperation operation, AudioTrackedCallbackReference reference,
                                                   AudioReconciliationReason reason);
        /** @brief Atomically moves matching pending operations into the retained terminal store. */
        [[nodiscard]] Result<void> ReconcileMatching(std::optional<AudioSceneContextHandle> scene, AudioReconciliationReason reason);
        /** @brief Checks an operation scope against the active runtime and command epoch. */
        [[nodiscard]] bool ValidScope(const AudioCommandScope &scope) const noexcept;

        AudioLifecycleReconcilerDescriptor descriptor_;
        AudioLifecycleState state_{AudioLifecycleState::Initializing};
        std::vector<AudioPendingOperation> pending_;
        std::vector<AudioReconciledOperation> terminal_;
        std::vector<AudioTrackedCallbackReference> callbackReferences_;
        std::vector<SceneBarrier> sceneBarriers_;
        std::vector<AudioSceneContextHandle> retiredScenes_;
        std::uint64_t lastAcceptedSequence_{};
        bool activated_{};
        bool callbackAttached_{};
        bool callbackQuiesced_{};
    };
}  // namespace Horo::Audio
