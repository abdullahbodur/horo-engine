#include "Horo/Audio/AudioLifecycleReconciler.h"

#include "Horo/Audio/AudioErrors.h"

#include <algorithm>
#include <new>
#include <ranges>
#include <type_traits>
#include <utility>

namespace Horo::Audio {
    namespace {
        template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] bool ValidCapacity(const std::size_t value) noexcept {
            return value > 0 && value <= MaximumAudioLifecycleRecords;
        }

        [[nodiscard]] bool ValidCallbackEpoch(const AudioDeviceEpoch &epoch, const AudioRuntimeId owner) noexcept {
            return MatchesAudioDeviceEpoch(epoch, epoch) && epoch.device.owner == owner;
        }

        [[nodiscard]] bool ValidMemoryHandle(const AudioMemoryHandle &handle, const AudioRuntimeId owner) noexcept {
            return handle.owner == owner && handle.pool.IsValid() && handle.slot != 0 && handle.generation != 0;
        }

        [[nodiscard]] bool ValidVoiceReason(const AudioVoiceTerminalReason reason) noexcept {
            return reason >= AudioVoiceTerminalReason::Finished && reason <= AudioVoiceTerminalReason::Failed;
        }

        [[nodiscard]] AudioReconciliationReason ReconciliationReason(const AudioVoiceTerminalReason reason) noexcept {
            if (reason == AudioVoiceTerminalReason::Finished)
                return AudioReconciliationReason::Completed;
            if (reason == AudioVoiceTerminalReason::Stopped)
                return AudioReconciliationReason::Stopped;
            if (reason == AudioVoiceTerminalReason::Failed)
                return AudioReconciliationReason::Failed;
            return AudioReconciliationReason::Cancelled;
        }
    }  // namespace

    /** @copydoc AudioLifecycleReconciler::Create */
    Result<AudioLifecycleReconciler> AudioLifecycleReconciler::Create(const AudioLifecycleReconcilerDescriptor &descriptor) {
        if (!descriptor.owner.IsValid() || descriptor.commandEpoch == 0 || !ValidCapacity(descriptor.maximumPendingOperations) ||
            !ValidCapacity(descriptor.maximumTerminalResults) || !ValidCapacity(descriptor.maximumCallbackReferences) ||
            descriptor.maximumTerminalResults < descriptor.maximumPendingOperations ||
            (descriptor.callbackRequired &&
             (descriptor.clockDomain == 0 || !ValidCallbackEpoch(descriptor.callbackEpoch, descriptor.owner))))
            return Failure<AudioLifecycleReconciler>(AudioErrors::IdentityInvalid);
        try {
            return Result<AudioLifecycleReconciler>::Success(AudioLifecycleReconciler{descriptor});
        } catch (const std::bad_alloc &) {
            return Failure<AudioLifecycleReconciler>(AudioErrors::MemoryAllocationFailed);
        }
    }

    /** @copydoc AudioLifecycleReconciler::AudioLifecycleReconciler */
    AudioLifecycleReconciler::AudioLifecycleReconciler(AudioLifecycleReconcilerDescriptor descriptor) : descriptor_(std::move(descriptor)) {
        pending_.reserve(descriptor_.maximumPendingOperations);
        terminal_.reserve(descriptor_.maximumTerminalResults);
        callbackReferences_.reserve(descriptor_.maximumCallbackReferences);
        sceneBarriers_.reserve(descriptor_.maximumCallbackReferences);
        retiredScenes_.reserve(descriptor_.maximumCallbackReferences);
    }

    /** @copydoc AudioLifecycleReconciler::AudioLifecycleReconciler */
    AudioLifecycleReconciler::AudioLifecycleReconciler(AudioLifecycleReconciler &&other) noexcept
        : descriptor_(std::move(other.descriptor_)), state_(other.state_), pending_(std::move(other.pending_)),
          terminal_(std::move(other.terminal_)), callbackReferences_(std::move(other.callbackReferences_)),
          sceneBarriers_(std::move(other.sceneBarriers_)), retiredScenes_(std::move(other.retiredScenes_)),
          lastAcceptedSequence_(other.lastAcceptedSequence_), activated_(other.activated_), callbackAttached_(other.callbackAttached_),
          callbackQuiesced_(other.callbackQuiesced_) {
        other.descriptor_ = {};
        other.state_ = AudioLifecycleState::Stopped;
        other.lastAcceptedSequence_ = 0;
        other.activated_ = false;
        other.callbackAttached_ = false;
        other.callbackQuiesced_ = false;
    }

    /** @copydoc AudioLifecycleReconciler::MarkCallbackAttached */
    Result<void> AudioLifecycleReconciler::MarkCallbackAttached() {
        if (state_ != AudioLifecycleState::Initializing || !descriptor_.callbackRequired || callbackAttached_)
            return Failure<void>(AudioErrors::RuntimeInactive);
        callbackAttached_ = true;
        return Result<void>::Success();
    }

    /** @copydoc AudioLifecycleReconciler::Activate */
    Result<void> AudioLifecycleReconciler::Activate() {
        if (state_ != AudioLifecycleState::Initializing || (descriptor_.callbackRequired && !callbackAttached_))
            return Failure<void>(AudioErrors::RuntimeInactive);
        state_ = AudioLifecycleState::Active;
        activated_ = true;
        return Result<void>::Success();
    }

    /** @copydoc AudioLifecycleReconciler::Admit */
    Result<void> AudioLifecycleReconciler::Admit(AudioPendingOperation operation) {
        if (state_ != AudioLifecycleState::Active)
            return Failure<void>(AudioErrors::RuntimeInactive);
        if (!ValidScope(operation.scope) || operation.acceptedSequence == 0)
            return Failure<void>(AudioErrors::IdentityInvalid);
        if (operation.acceptedSequence <= lastAcceptedSequence_)
            return Failure<void>(AudioErrors::HandleStale);
        if (std::ranges::any_of(sceneBarriers_,
                                [&operation](const SceneBarrier &barrier) {
            return barrier.scene == operation.scope.scene;
        }) ||
            std::ranges::find(retiredScenes_, operation.scope.scene) != retiredScenes_.end())
            return Failure<void>(AudioErrors::RuntimeInactive);
        if (pending_.size() == descriptor_.maximumPendingOperations)
            return Failure<void>(AudioErrors::HandleCapacityExhausted);
        pending_.push_back(std::move(operation));
        lastAcceptedSequence_ = pending_.back().acceptedSequence;
        return Result<void>::Success();
    }

    /** @copydoc AudioLifecycleReconciler::TrackCallbackReference */
    Result<void> AudioLifecycleReconciler::TrackCallbackReference(AudioTrackedCallbackReference reference) {
        if (state_ != AudioLifecycleState::Active)
            return Failure<void>(AudioErrors::RuntimeInactive);
        if (!reference.scene.IsValid() || reference.scene.owner != descriptor_.owner)
            return Failure<void>(AudioErrors::IdentityInvalid);
        if (std::ranges::any_of(sceneBarriers_,
                                [&reference](const SceneBarrier &barrier) {
            return barrier.scene == reference.scene;
        }) ||
            std::ranges::find(retiredScenes_, reference.scene) != retiredScenes_.end())
            return Failure<void>(AudioErrors::RuntimeInactive);
        const bool validReference = std::visit([this](const auto &value) {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, AudioVoiceHandle>)
                return value.IsValid() && value.owner == descriptor_.owner;
            else
                return ValidMemoryHandle(value, descriptor_.owner);
        }, reference.reference);
        if (!validReference)
            return Failure<void>(AudioErrors::IdentityInvalid);
        if (std::ranges::find(callbackReferences_, reference) != callbackReferences_.end())
            return Failure<void>(AudioErrors::HandleStale);
        if (callbackReferences_.size() == descriptor_.maximumCallbackReferences)
            return Failure<void>(AudioErrors::HandleCapacityExhausted);
        callbackReferences_.push_back(std::move(reference));
        return Result<void>::Success();
    }

    /** @copydoc AudioLifecycleReconciler::ObserveTerminal */
    Result<void> AudioLifecycleReconciler::ObserveTerminal(const AudioTerminalEvent &event) {
        return std::visit([this](const auto &value) {
            return ObserveTerminalValue(value);
        }, event);
    }

    /** @copydoc AudioLifecycleReconciler::ObserveTerminalValue */
    Result<void> AudioLifecycleReconciler::ObserveTerminalValue(const AudioVoiceTerminalEvent &event) {
        if (!ValidScope(event.scope) || event.acceptedSequence == 0 || !event.voice.IsValid() || event.voice.owner != descriptor_.owner ||
            !ValidVoiceReason(event.reason))
            return Failure<void>(AudioErrors::EventQueueInvalid);
        return PublishTerminal({event.scope, event.acceptedSequence}, {event.scope.scene, event.voice}, ReconciliationReason(event.reason));
    }

    /** @copydoc AudioLifecycleReconciler::ObserveTerminalValue */
    Result<void> AudioLifecycleReconciler::ObserveTerminalValue(const AudioResourceReleaseEvent &event) {
        if (!ValidScope(event.scope) || event.acceptedSequence == 0 || !ValidMemoryHandle(event.storage, descriptor_.owner) ||
            event.completedEpoch == 0)
            return Failure<void>(AudioErrors::EventQueueInvalid);
        return PublishTerminal({event.scope, event.acceptedSequence}, {event.scope.scene, event.storage},
                               AudioReconciliationReason::Completed);
    }

    /** @copydoc AudioLifecycleReconciler::PublishTerminal */
    Result<void> AudioLifecycleReconciler::PublishTerminal(const AudioPendingOperation operation,
                                                           const AudioTrackedCallbackReference reference,
                                                           const AudioReconciliationReason reason) {
        const auto pending = std::ranges::find(pending_, operation);
        if (pending == pending_.end())
            return Failure<void>(AudioErrors::HandleStale);
        const auto callbackReference = std::ranges::find(callbackReferences_, reference);
        if (callbackReference == callbackReferences_.end())
            return Failure<void>(AudioErrors::HandleStale);
        if (terminal_.size() == descriptor_.maximumTerminalResults)
            return Failure<void>(AudioErrors::HandleCapacityExhausted);

        terminal_.push_back({*pending, reason});
        pending_.erase(pending);
        callbackReferences_.erase(callbackReference);
        return Result<void>::Success();
    }

    /** @copydoc AudioLifecycleReconciler::BeginSceneUnload */
    Result<void> AudioLifecycleReconciler::BeginSceneUnload(const AudioSceneContextHandle scene, const std::uint64_t barrierSequence) {
        if (state_ != AudioLifecycleState::Active)
            return Failure<void>(AudioErrors::RuntimeInactive);
        if (!scene.IsValid() || scene.owner != descriptor_.owner || barrierSequence == 0)
            return Failure<void>(AudioErrors::IdentityInvalid);
        if (barrierSequence <= lastAcceptedSequence_)
            return Failure<void>(AudioErrors::HandleStale);
        if (std::ranges::any_of(sceneBarriers_,
                                [scene](const SceneBarrier &barrier) {
            return barrier.scene == scene;
        }) ||
            std::ranges::find(retiredScenes_, scene) != retiredScenes_.end())
            return Failure<void>(AudioErrors::RuntimeInactive);
        if (sceneBarriers_.size() == descriptor_.maximumCallbackReferences)
            return Failure<void>(AudioErrors::HandleCapacityExhausted);
        sceneBarriers_.push_back({scene, barrierSequence});
        lastAcceptedSequence_ = barrierSequence;
        return Result<void>::Success();
    }

    /** @copydoc AudioLifecycleReconciler::AcknowledgeSceneUnload */
    Result<void> AudioLifecycleReconciler::AcknowledgeSceneUnload(const AudioSceneContextHandle scene,
                                                                  const std::uint64_t barrierSequence) {
        const auto barrier = std::ranges::find(sceneBarriers_, SceneBarrier{scene, barrierSequence});
        if (state_ != AudioLifecycleState::Active || barrier == sceneBarriers_.end())
            return Failure<void>(AudioErrors::HandleStale);
        if (retiredScenes_.size() == descriptor_.maximumCallbackReferences)
            return Failure<void>(AudioErrors::HandleCapacityExhausted);
        if (const auto reconciled = ReconcileMatching(scene, AudioReconciliationReason::CancelledBySceneUnload); reconciled.HasError())
            return reconciled;
        callbackReferences_.erase(std::remove_if(callbackReferences_.begin(), callbackReferences_.end(),
                                                 [scene](const AudioTrackedCallbackReference &reference) {
            return reference.scene == scene;
        }),
                                  callbackReferences_.end());
        sceneBarriers_.erase(barrier);
        retiredScenes_.push_back(scene);
        return Result<void>::Success();
    }

    /** @copydoc AudioLifecycleReconciler::BeginDeviceReset */
    Result<void> AudioLifecycleReconciler::BeginDeviceReset() {
        if (state_ != AudioLifecycleState::Active || !callbackAttached_)
            return Failure<void>(AudioErrors::RuntimeInactive);
        state_ = AudioLifecycleState::Resetting;
        callbackQuiesced_ = false;
        return Result<void>::Success();
    }

    /** @copydoc AudioLifecycleReconciler::BeginShutdown */
    Result<void> AudioLifecycleReconciler::BeginShutdown() {
        if (state_ == AudioLifecycleState::Stopped)
            return Result<void>::Success();
        if (state_ == AudioLifecycleState::Stopping)
            return Result<void>::Success();
        const bool alreadyQuiesced = state_ == AudioLifecycleState::Resetting && callbackQuiesced_;
        state_ = AudioLifecycleState::Stopping;
        callbackQuiesced_ = alreadyQuiesced;
        return Result<void>::Success();
    }

    /** @copydoc AudioLifecycleReconciler::ObserveCallbackQuiesced */
    Result<void> AudioLifecycleReconciler::ObserveCallbackQuiesced(const AudioCallbackEvent &event) {
        if ((state_ != AudioLifecycleState::Resetting && state_ != AudioLifecycleState::Stopping) || !callbackAttached_ ||
            !ValidateAudioCallbackEvent(event, descriptor_.callbackEpoch, descriptor_.clockDomain) ||
            !std::holds_alternative<AudioCallbackQuiesced>(event.fact))
            return Failure<void>(AudioErrors::EventQueueInvalid);
        callbackQuiesced_ = true;
        return Result<void>::Success();
    }

    /** @copydoc AudioLifecycleReconciler::CompleteDeviceReset */
    Result<void> AudioLifecycleReconciler::CompleteDeviceReset(const std::uint64_t nextCommandEpoch,
                                                               const AudioDeviceEpoch nextCallbackEpoch, const bool backendDetached) {
        if (state_ != AudioLifecycleState::Resetting || !callbackQuiesced_ || !backendDetached)
            return Failure<void>(AudioErrors::RuntimeInactive);
        if (nextCommandEpoch <= descriptor_.commandEpoch || !ValidCallbackEpoch(nextCallbackEpoch, descriptor_.owner) ||
            nextCallbackEpoch == descriptor_.callbackEpoch)
            return Failure<void>(AudioErrors::IdentityInvalid);
        if (const auto reconciled = ReconcileMatching(std::nullopt, AudioReconciliationReason::CancelledByDeviceReset);
            reconciled.HasError())
            return reconciled;
        callbackReferences_.clear();
        sceneBarriers_.clear();
        retiredScenes_.clear();
        descriptor_.commandEpoch = nextCommandEpoch;
        descriptor_.callbackEpoch = nextCallbackEpoch;
        lastAcceptedSequence_ = 0;
        callbackQuiesced_ = false;
        callbackAttached_ = true;
        state_ = AudioLifecycleState::Active;
        return Result<void>::Success();
    }

    /** @copydoc AudioLifecycleReconciler::CompleteShutdown */
    Result<void> AudioLifecycleReconciler::CompleteShutdown(const bool backendDetached) {
        if (state_ == AudioLifecycleState::Stopped)
            return Result<void>::Success();
        if (state_ != AudioLifecycleState::Stopping || (callbackAttached_ && (!callbackQuiesced_ || !backendDetached)))
            return Failure<void>(AudioErrors::RuntimeInactive);
        const auto reason =
            activated_ ? AudioReconciliationReason::CancelledByShutdown : AudioReconciliationReason::CancelledByInitializationRollback;
        if (const auto reconciled = ReconcileMatching(std::nullopt, reason); reconciled.HasError())
            return reconciled;
        callbackReferences_.clear();
        sceneBarriers_.clear();
        retiredScenes_.clear();
        callbackAttached_ = false;
        state_ = AudioLifecycleState::Stopped;
        return Result<void>::Success();
    }

    /** @copydoc AudioLifecycleReconciler::TerminalResultCount */
    std::size_t AudioLifecycleReconciler::TerminalResultCount() const noexcept {
        return terminal_.size();
    }

    /** @copydoc AudioLifecycleReconciler::TerminalResult */
    std::optional<AudioReconciledOperation> AudioLifecycleReconciler::TerminalResult(const std::size_t index) const noexcept {
        if (index >= terminal_.size())
            return std::nullopt;
        return terminal_[index];
    }

    /** @copydoc AudioLifecycleReconciler::AcknowledgeTerminal */
    Result<bool> AudioLifecycleReconciler::AcknowledgeTerminal(const AudioPendingOperation &operation) {
        if (!operation.scope.owner.IsValid() || !operation.scope.scene.IsValid() || operation.scope.scene.owner != operation.scope.owner ||
            operation.acceptedSequence == 0)
            return Failure<bool>(AudioErrors::IdentityInvalid);
        const auto found = std::ranges::find(terminal_, operation, &AudioReconciledOperation::operation);
        if (found == terminal_.end())
            return Result<bool>::Success(false);
        terminal_.erase(found);
        return Result<bool>::Success(true);
    }

    /** @copydoc AudioLifecycleReconciler::Snapshot */
    AudioLifecycleReconciliationSnapshot AudioLifecycleReconciler::Snapshot() const noexcept {
        return {.state = state_,
                .commandEpoch = descriptor_.commandEpoch,
                .pendingOperations = pending_.size(),
                .retainedTerminalResults = terminal_.size(),
                .callbackReferences = callbackReferences_.size(),
                .callbackAttached = callbackAttached_,
                .callbackQuiesced = callbackQuiesced_};
    }

    /** @copydoc AudioLifecycleReconciler::ReconcileMatching */
    Result<void> AudioLifecycleReconciler::ReconcileMatching(const std::optional<AudioSceneContextHandle> scene,
                                                             const AudioReconciliationReason reason) {
        const auto matches = [scene](const AudioPendingOperation &operation) {
            return !scene.has_value() || operation.scope.scene == *scene;
        };
        const auto matchCount = static_cast<std::size_t>(std::ranges::count_if(pending_, matches));
        if (matchCount > descriptor_.maximumTerminalResults - terminal_.size())
            return Failure<void>(AudioErrors::HandleCapacityExhausted);
        for (const AudioPendingOperation &operation : pending_) {
            if (matches(operation))
                terminal_.push_back({operation, reason});
        }
        pending_.erase(std::remove_if(pending_.begin(), pending_.end(), matches), pending_.end());
        return Result<void>::Success();
    }

    /** @copydoc AudioLifecycleReconciler::ValidScope */
    bool AudioLifecycleReconciler::ValidScope(const AudioCommandScope &scope) const noexcept {
        return scope.owner == descriptor_.owner && scope.epoch == descriptor_.commandEpoch && scope.scene.IsValid() &&
               scope.scene.owner == descriptor_.owner;
    }
}  // namespace Horo::Audio
