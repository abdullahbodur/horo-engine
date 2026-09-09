#include "Horo/Audio/AudioEventQueue.h"

#include <atomic>
#include <bit>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace Horo::Audio {
    namespace {
        static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
        static_assert(std::atomic<bool>::is_always_lock_free);
        static_assert(std::is_trivially_copyable_v<AudioControlEventRecord>);
        static_assert(sizeof(AudioControlEventRecord) <= 256);
        static_assert(alignof(AudioControlEventRecord) <= AudioMemoryAlignment);

        /** @brief Checks a command scope without consulting live registries. */
        bool ValidScope(const AudioCommandScope &scope) noexcept {
            return scope.owner.IsValid() && scope.epoch != 0 && scope.scene.IsValid() && scope.scene.owner == scope.owner;
        }

        /** @brief Checks all immutable queue dimensions before allocation. */
        bool ValidDescriptor(const AudioEventQueueDescriptor &descriptor) noexcept {
            const bool identityValid = descriptor.owner.IsValid() && descriptor.storageIdentity.IsValid() && descriptor.commandEpoch != 0 &&
                                       descriptor.clockDomain != 0;
            const bool epochValid = MatchesAudioDeviceEpoch(descriptor.callbackEpoch, descriptor.callbackEpoch) &&
                                    descriptor.callbackEpoch.device.owner == descriptor.owner;
            const bool capacityValid = descriptor.slots >= 2 && descriptor.slots <= MaximumAudioEventSlots &&
                                       std::has_single_bit(descriptor.slots) && descriptor.criticalSlots > 0 &&
                                       descriptor.criticalSlots < descriptor.slots;
            return identityValid && epochValid && capacityValid;
        }

        /** @brief Projects admitted raw EventStorage into initialized queue records. */
        std::span<AudioControlEventRecord> RecordSpan(const std::span<std::byte> allocation, const std::uint32_t slots) noexcept {
            return {static_cast<AudioControlEventRecord *>(static_cast<void *>(allocation.data())), slots};
        }

        /** @brief Checks the closed terminal reason vocabulary. */
        bool ValidReason(const AudioVoiceTerminalReason reason) noexcept {
            return reason >= AudioVoiceTerminalReason::Finished && reason <= AudioVoiceTerminalReason::Failed;
        }

        /** @brief Checks an exact pool handle representation. */
        bool ValidStorage(const AudioMemoryHandle &storage) noexcept {
            return storage.owner.IsValid() && storage.pool.IsValid() && storage.slot != 0 && storage.generation != 0;
        }

        /** @brief Validates exact correlation between a terminal value and its exclusive publication right. */
        bool ValidTerminal(const AudioCompletionToken &token, const AudioTerminalEvent &event) noexcept {
            return std::visit([&token](const auto &value) {
                if (value.scope != token.Scope() || value.acceptedSequence != token.AcceptedSequence())
                    return false;
                using Value = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Value, AudioVoiceTerminalEvent>)
                    return value.voice.IsValid() && value.voice.owner == value.scope.owner && ValidReason(value.reason);
                else
                    return ValidStorage(value.storage) && value.storage.owner == value.scope.owner && value.completedEpoch != 0;
            }, event);
        }

        /** @brief Increments one single-producer observable counter without wrap. */
        void Increment(std::atomic<std::uint32_t> &counter) noexcept {
            const auto value = counter.load(std::memory_order_relaxed);
            if (value != std::numeric_limits<std::uint32_t>::max())
                counter.store(value + 1, std::memory_order_relaxed);
        }
    }  // namespace

    /** @copydoc AudioCompletionToken::Create */
    Result<AudioCompletionToken> AudioCompletionToken::Create(AudioCommandScope scope, const std::uint64_t acceptedSequence) {
        if (!ValidScope(scope) || acceptedSequence == 0)
            return Result<AudioCompletionToken>::Failure(MakeError(AudioErrors::EventQueueInvalid));
        return Result<AudioCompletionToken>::Success(AudioCompletionToken{std::move(scope), acceptedSequence});
    }

    /** @copydoc AudioCompletionToken::AudioCompletionToken */
    AudioCompletionToken::AudioCompletionToken(AudioCommandScope scope, const std::uint64_t acceptedSequence) noexcept
        : scope_(std::move(scope)), acceptedSequence_(acceptedSequence), state_(AudioCompletionTokenState::Pending) {}

    /** @copydoc AudioCompletionToken::AudioCompletionToken */
    AudioCompletionToken::AudioCompletionToken(AudioCompletionToken &&other) noexcept
        : scope_(std::move(other.scope_)), acceptedSequence_(other.acceptedSequence_), state_(other.state_) {
        other.scope_ = {};
        other.acceptedSequence_ = 0;
        other.state_ = AudioCompletionTokenState::Inactive;
    }

    /** @copydoc AudioCompletionToken::Scope */
    const AudioCommandScope &AudioCompletionToken::Scope() const noexcept {
        return scope_;
    }

    /** @copydoc AudioCompletionToken::AcceptedSequence */
    std::uint64_t AudioCompletionToken::AcceptedSequence() const noexcept {
        return acceptedSequence_;
    }

    /** @copydoc AudioCompletionToken::State */
    AudioCompletionTokenState AudioCompletionToken::State() const noexcept {
        return state_;
    }

    /** @copydoc AudioCompletionToken::MarkPublished */
    void AudioCompletionToken::MarkPublished() noexcept {
        state_ = AudioCompletionTokenState::Published;
    }

    /** @brief Producer owns write/counters; consumer owns read; release/acquire pairs protect fixed ring slots. */
    struct AudioEventQueue::State final {
        AudioEventQueueDescriptor descriptor;
        AudioMemoryPool storage;
        std::span<AudioControlEventRecord> records;
        alignas(AudioMemoryAlignment) std::atomic<std::uint32_t> write{};
        alignas(AudioMemoryAlignment) std::atomic<std::uint32_t> read{};
        std::atomic<bool> closed{};
        std::atomic<std::uint32_t> terminalEvents{};
        std::atomic<std::uint32_t> deviceEvents{};
        std::atomic<std::uint32_t> droppedTelemetry{};
        std::atomic<std::uint32_t> criticalRetries{};
        std::atomic<std::uint32_t> duplicateTerminals{};

        /** @brief Initializes every record lifetime in fully admitted EventStorage. */
        State(const AudioEventQueueDescriptor &description, AudioMemoryPool pool, const std::span<std::byte> allocation)
            : descriptor(description), storage(std::move(pool)), records(RecordSpan(allocation, description.slots)) {
            std::ranges::uninitialized_value_construct(records);
        }

        /** @brief Ends record lifetimes only after the host has joined both SPSC participants. */
        ~State() {
            std::ranges::destroy(records);
        }

        /** @brief Publishes one already validated fixed-size value when its reservation class has capacity. */
        AudioEventPublishStatus Publish(const AudioControlEvent &event, const bool critical) noexcept {
            using enum AudioEventPublishStatus;
            if (closed.load(std::memory_order_relaxed))
                return Closed;
            const auto producer = write.load(std::memory_order_relaxed);
            const auto consumer = read.load(std::memory_order_acquire);
            const auto limit = critical ? descriptor.slots : descriptor.slots - descriptor.criticalSlots;
            if (producer - consumer >= limit)
                return critical ? CriticalRetry : TelemetryDropped;
            records[producer & (descriptor.slots - 1)].event = event;
            write.store(producer + 1, std::memory_order_release);
            return Published;
        }
    };

    /** @copydoc AudioEventQueue::Create */
    Result<AudioEventQueue> AudioEventQueue::Create(const AudioEventQueueDescriptor &descriptor) {
        using enum AudioMemoryStatus;
        if (!ValidDescriptor(descriptor))
            return Result<AudioEventQueue>::Failure(MakeError(AudioErrors::EventQueueInvalid));
        auto pool = AudioMemoryPool::Create({.owner = descriptor.owner,
                                             .identity = descriptor.storageIdentity,
                                             .purpose = AudioMemoryPurpose::EventStorage,
                                             .slots = 1,
                                             .blockBytes = sizeof(AudioControlEventRecord) * descriptor.slots,
                                             .budgetBytes = descriptor.budgetBytes});
        if (!pool.HasValue())
            return Result<AudioEventQueue>::Failure(pool.ErrorValue());
        auto storage = std::move(pool).Value();
        const auto allocation = storage.Acquire();
        if (const auto requiredBytes = sizeof(AudioControlEventRecord) * descriptor.slots;
            allocation.status != Ok || allocation.bytes.size() < requiredBytes)
            return Result<AudioEventQueue>::Failure(MakeError(AudioErrors::MemoryAllocationFailed));
        try {
            return Result<AudioEventQueue>::Success(
                AudioEventQueue{std::make_unique<State>(descriptor, std::move(storage), allocation.bytes)});
        } catch (const std::bad_alloc &) {
            return Result<AudioEventQueue>::Failure(MakeError(AudioErrors::MemoryAllocationFailed));
        }
    }

    /** @copydoc AudioEventQueue::AudioEventQueue */
    AudioEventQueue::AudioEventQueue(std::unique_ptr<State> state) : state_(std::move(state)) {}

    /** @copydoc AudioEventQueue::~AudioEventQueue */
    AudioEventQueue::~AudioEventQueue() = default;
    /** @copydoc AudioEventQueue::AudioEventQueue */
    AudioEventQueue::AudioEventQueue(AudioEventQueue &&) noexcept = default;
    /** @copydoc AudioEventQueue::operator= */
    AudioEventQueue &AudioEventQueue::operator=(AudioEventQueue &&) noexcept = default;

    /** @copydoc AudioEventQueue::TryPublishTerminal */
    AudioEventPublishStatus AudioEventQueue::TryPublishTerminal(AudioCompletionToken &token, const AudioTerminalEvent &event) noexcept {
        using enum AudioEventPublishStatus;
        if (!state_)
            return Inactive;
        if (token.State() == AudioCompletionTokenState::Published) {
            Increment(state_->duplicateTerminals);
            return DuplicateTerminal;
        }
        if (token.State() != AudioCompletionTokenState::Pending || token.Scope().owner != state_->descriptor.owner ||
            token.Scope().epoch != state_->descriptor.commandEpoch || !ValidTerminal(token, event))
            return InvalidEvent;

        const auto published = state_->Publish(std::visit(
                                                   [](const auto &value) -> AudioControlEvent {
            return value;
        }, event),
                                               true);
        if (published == CriticalRetry) {
            Increment(state_->criticalRetries);
            return published;
        }
        if (published == Published) {
            token.MarkPublished();
            Increment(state_->terminalEvents);
        }
        return published;
    }

    /** @copydoc AudioEventQueue::TryPublishDevice */
    AudioEventPublishStatus AudioEventQueue::TryPublishDevice(const AudioCallbackEvent &event) noexcept {
        using enum AudioEventPublishStatus;
        if (!state_)
            return Inactive;
        if (!ValidateAudioCallbackEvent(event, state_->descriptor.callbackEpoch, state_->descriptor.clockDomain))
            return InvalidEvent;
        const bool critical = IsCriticalAudioCallbackFact(event.fact);
        const auto published = state_->Publish(AudioControlEvent{event}, critical);
        if (published == CriticalRetry)
            Increment(state_->criticalRetries);
        else if (published == TelemetryDropped)
            Increment(state_->droppedTelemetry);
        else if (published == Published)
            Increment(state_->deviceEvents);
        return published;
    }

    /** @copydoc AudioEventQueue::TryConsume */
    bool AudioEventQueue::TryConsume(AudioControlEventRecord &record) noexcept {
        if (!state_)
            return false;
        const auto consumer = state_->read.load(std::memory_order_relaxed);
        if (consumer == state_->write.load(std::memory_order_acquire))
            return false;
        record = state_->records[consumer & (state_->descriptor.slots - 1)];
        state_->read.store(consumer + 1, std::memory_order_release);
        return true;
    }

    /** @copydoc AudioEventQueue::Close */
    void AudioEventQueue::Close() noexcept {
        if (state_)
            state_->closed.store(true, std::memory_order_release);
    }

    /** @copydoc AudioEventQueue::Stats */
    AudioEventQueueStats AudioEventQueue::Stats() const noexcept {
        if (!state_)
            return {};
        return {.terminalEvents = state_->terminalEvents.load(std::memory_order_acquire),
                .deviceEvents = state_->deviceEvents.load(std::memory_order_acquire),
                .droppedTelemetry = state_->droppedTelemetry.load(std::memory_order_acquire),
                .criticalRetries = state_->criticalRetries.load(std::memory_order_acquire),
                .duplicateTerminals = state_->duplicateTerminals.load(std::memory_order_acquire)};
    }

    /** @copydoc AudioEventQueue::IsDrained */
    bool AudioEventQueue::IsDrained() const noexcept {
        return !state_ || (state_->closed.load(std::memory_order_acquire) &&
                           state_->read.load(std::memory_order_relaxed) == state_->write.load(std::memory_order_acquire));
    }
}  // namespace Horo::Audio
