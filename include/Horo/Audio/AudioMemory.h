#pragma once

/** @file AudioMemory.h
 * @brief Explicitly owned, bounded real-time audio memory and allocation-free admission facts.
 */

#include "Horo/Audio/AudioIdentity.h"

#include <cstddef>
#include <memory>
#include <span>

namespace Horo::Audio {
    inline constexpr std::size_t AudioMemoryAlignment = 64;
    inline constexpr std::size_t MaximumAudioMemoryBytes = 256U * 1024U * 1024U;

    /** @brief Fixed-size callback outcomes; general diagnostic construction belongs on the control thread. */
    enum class AudioMemoryStatus : std::uint8_t {
        Ok,
        Exhausted,
        InvalidRequest,
        InvalidHandle,
        InvalidEpoch,
        Inactive
    };

    /** @brief Allocation-free telemetry snapshot; byte counts include alignment padding. */
    struct AudioMemoryStats {
        std::uint64_t reservedBytes{};
        std::uint64_t usedBytes{};
        std::uint64_t peakBytes{};
        std::uint64_t failedAllocations{}; /**< Saturating lifetime count; epoch reset does not erase pressure evidence. */
    };

    /** @brief A scratch borrow is invalidated by the next successful BeginEpoch or owner destruction. */
    struct AudioScratchAllocation {
        AudioMemoryStatus status{AudioMemoryStatus::Inactive};
        std::span<std::byte> bytes;
        std::uint64_t epoch{};
    };

    struct AudioMemoryPoolIdentityTag;
    /** @brief Host-assigned identity, never reused within the same runtime generation. */
    using AudioMemoryPoolId = AudioStableIdentity<AudioMemoryPoolIdentityTag>;

    /** @brief Storage ownership category; does not instantiate voices, graphs or queues. */
    enum class AudioMemoryPurpose : std::uint8_t {
        VoiceState,
        GraphState,
        CommandStorage,
        EventStorage
    };

    /** @brief Preparation-only fixed block reservation; metadata is included in the byte budget. */
    struct AudioMemoryPoolDescriptor {
        AudioRuntimeId owner;
        AudioMemoryPoolId identity;
        AudioMemoryPurpose purpose{AudioMemoryPurpose::VoiceState};
        std::uint32_t slots{};
        std::size_t blockBytes{};
        std::size_t budgetBytes{};
    };

    /** @brief Pool-scoped non-wrapping handle; retired handles cannot resolve, even before reclamation. */
    struct AudioMemoryHandle {
        AudioRuntimeId owner;
        AudioMemoryPoolId pool;
        std::uint32_t slot{}; /**< One-based index. */
        std::uint64_t generation{};
        auto operator<=>(const AudioMemoryHandle &) const noexcept = default;
    };

    /** @brief Raw storage borrow; the host owns object lifetime and must end it before reclamation. */
    struct AudioMemoryAllocation {
        AudioMemoryStatus status{AudioMemoryStatus::Inactive};
        AudioMemoryHandle handle;
        std::span<std::byte> bytes;
    };

    /** @brief Fixed-size bounded-reclamation outcome for control-owner telemetry. */
    struct AudioMemoryReclamation {
        AudioMemoryStatus status{AudioMemoryStatus::Inactive};
        std::uint32_t visited{};
        std::uint32_t reclaimed{};
    };

    /** @brief Exclusively owned fixed pool with explicit deferred reuse; prepared and destroyed off-callback. */
    class AudioMemoryPool final {
    public:
        /**
         * @brief Reserve all payload and slot metadata without publishing partially prepared state.
         * @param descriptor Valid owner, unique identity, purpose, 1..MaximumAudioHandleSlots slots,
         * positive payload size and budget no greater than MaximumAudioMemoryBytes.
         * @return Prepared pool or a typed invalid/budget/allocation error.
         */
        [[nodiscard]] static Result<AudioMemoryPool> Create(const AudioMemoryPoolDescriptor &descriptor);
        /** @brief Destroy off-callback only after all borrowed storage is quiescent. */
        ~AudioMemoryPool();
        /** @brief Transfer quiescent ownership. @param other Source, left inert. */
        AudioMemoryPool(AudioMemoryPool &&other) noexcept;
        /** @brief Replace quiescent storage off-callback. @param other Source. @return This pool. */
        AudioMemoryPool &operator=(AudioMemoryPool &&other) noexcept;
        AudioMemoryPool(const AudioMemoryPool &) = delete;
        AudioMemoryPool &operator=(const AudioMemoryPool &) = delete;

        /** @brief Claim and zero one block including padding in bounded time, without heap allocation.
         * @return Handle and exact payload span, or empty typed exhaustion/inactive failure.
         */
        [[nodiscard]] AudioMemoryAllocation Acquire() noexcept;
        /** @brief Resolve a currently live handle on the owning thread.
         * @param handle Exact pool/runtime/slot/generation identity.
         * @return Borrow valid until reclamation or destruction, or an empty span for invalid/retired handles.
         */
        [[nodiscard]] std::span<std::byte> Resolve(const AudioMemoryHandle &handle) noexcept;
        /** @brief Retire without recycling or destroying bytes; prior borrowers may still be running.
         * @param handle Live block identity.
         * @param lastUseEpoch Nonzero last-use epoch, strictly newer than the acknowledged watermark.
         * @return Ok, InvalidHandle, InvalidEpoch or Inactive; rejected calls do not mutate the pool.
         */
        [[nodiscard]] AudioMemoryStatus Retire(const AudioMemoryHandle &handle, std::uint64_t lastUseEpoch) noexcept;
        /**
         * @brief Inspect at most visitBudget slots round-robin and recycle completed retired blocks.
         * @param owner Runtime generation whose completion was established by the caller.
         * @param completedEpoch Nonzero nondecreasing completion watermark; repeated values permit incremental scans.
         * @param visitBudget Positive bound, clamped to the pool's slot count.
         * @return Status, visited count and reusable block count. Generation exhaustion permanently retires a slot.
         * @pre The host has joined all work through completedEpoch and ended nontrivial object lifetimes.
         * This is a control-owner operation outside the callback after an explicit quiescent ownership handoff.
         * All pool methods require exclusive ownership; this method is not a synchronization barrier.
         */
        [[nodiscard]] AudioMemoryReclamation Reclaim(AudioRuntimeId owner, std::uint64_t completedEpoch,
                                                     std::uint32_t visitBudget) noexcept;
        /** @brief Snapshot owning-thread metrics. @return Reserved payload/metadata, occupied payload, peak and failures.
         * Retired blocks remain occupied until safely reclaimed. Fixed owner-object overhead is excluded.
         */
        [[nodiscard]] AudioMemoryStats Stats() const noexcept;

    private:
        struct State;
        /** @brief Publish fully prepared state. */
        explicit AudioMemoryPool(std::unique_ptr<State> state);
        std::unique_ptr<State> state_;
    };

    /** @brief Single-owner bump arena prepared off-callback; no atomics, locks or fallback allocation on hot paths. */
    class AudioScratchArena final {
    public:
        /**
         * @brief Reserve aligned scratch storage outside the callback, initially without an active epoch.
         * @param owner Valid runtime generation that owns mutation and all borrows.
         * @param capacityBytes Positive multiple of 64, no greater than MaximumAudioMemoryBytes.
         * @return Prepared arena or a typed invalid/budget/allocation failure. No partial arena escapes.
         */
        [[nodiscard]] static Result<AudioScratchArena> Create(AudioRuntimeId owner, std::size_t capacityBytes);
        /** @brief Release storage only off-callback after the owner proves all borrows quiescent. */
        ~AudioScratchArena();
        /** @brief Transfer quiescent ownership; the source becomes inert. @param other Source arena. */
        AudioScratchArena(AudioScratchArena &&other) noexcept;
        /** @brief Replace quiescent storage off-callback. @param other Source arena. @return This arena. */
        AudioScratchArena &operator=(AudioScratchArena &&other) noexcept;
        AudioScratchArena(const AudioScratchArena &) = delete;
        AudioScratchArena &operator=(const AudioScratchArena &) = delete;

        /**
         * @brief Reset the bump cursor after all previous-epoch borrows have ended.
         * @param owner Runtime identity attached to the actual completion boundary.
         * @param epoch Strictly increasing nonzero epoch; wrap and repeated/stale epochs are rejected.
         * @return Ok, Inactive or InvalidEpoch; rejected calls preserve storage, cursor and epoch.
         * @pre Exactly one owner invokes all methods. The caller has joined all work using old scratch.
         * The epoch value records that proof; this arena does not create a synchronization barrier.
         */
        [[nodiscard]] AudioMemoryStatus BeginEpoch(AudioRuntimeId owner, std::uint64_t epoch) noexcept;
        /**
         * @brief Borrow zeroed bytes at a 64-byte-aligned start without growing the arena.
         * @param bytes Positive requested payload size; its rounded-up stride is charged to the reservation.
         * @return Exact requested span and epoch, or an empty typed failure. Padding is also zeroed.
         * Failed calls do not advance the cursor; only the saturating failed-allocation metric changes.
         * Returned bytes own no nontrivial object lifetime and must not escape the epoch.
         */
        [[nodiscard]] AudioScratchAllocation Allocate(std::size_t bytes) noexcept;
        /** @brief Read metrics on the owning thread. @return Reserved, consumed, peak and failed counts. */
        [[nodiscard]] AudioMemoryStats Stats() const noexcept;

    private:
        struct State;
        /** @brief Publish only fully allocated state. */
        explicit AudioScratchArena(std::unique_ptr<State> state);
        std::unique_ptr<State> state_;
    };
}  // namespace Horo::Audio
