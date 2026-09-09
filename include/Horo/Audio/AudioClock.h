#pragma once

/** @file AudioClock.h
 * @brief Generation-scoped sample clock and producer-time correlation contracts.
 */

#include "Horo/Audio/AudioDeviceTiming.h"
#include "Horo/Audio/AudioFormat.h"

#include <cstdint>

namespace Horo::Audio {
    /** @brief Whether producer time may advance through the current sample-clock snapshot. */
    enum class AudioSampleClockState : std::uint8_t {
        Running,
        Paused
    };

    /** @brief Immutable control-owned observation of one callback sample-clock generation. */
    struct AudioSampleClock final {
        AudioRuntimeId owner;
        std::uint64_t epoch{};
        std::uint64_t generation{};
        std::uint64_t discontinuityRevision{};
        std::uint64_t sampleFrame{};
        std::uint32_t sampleRate{};
        AudioMonotonicTimestamp observedAt;
        AudioSampleClockState state{AudioSampleClockState::Running};
    };

    /** @brief One producer clock identity and bounded interval correlated to an Audio sample-clock anchor. */
    struct AudioClockCorrelationSnapshot final {
        AudioSampleClock clock;
        std::uint64_t producerClockDomain{};
        std::uint64_t producerGeneration{};
        std::uint64_t producerNanoseconds{};
        std::uint64_t validFromNanoseconds{};
        std::uint64_t validThroughNanoseconds{};
    };

    /** @brief Exact identity expected by a scheduling caller; stale generations never map by coincidence. */
    struct AudioClockExpectation final {
        AudioRuntimeId owner;
        std::uint64_t epoch{};
        std::uint64_t clockGeneration{};
        std::uint64_t discontinuityRevision{};
        std::uint64_t producerClockDomain{};
        std::uint64_t producerGeneration{};
    };

    /** @brief Fixed-size mapping result; sampleFrame is assigned only for Mapped. */
    enum class AudioClockMappingStatus : std::uint8_t {
        Mapped,
        InvalidSnapshot,
        StaleClock,
        OutsideCorrelation,
        Paused,
        Overflow
    };

    /** @brief One allocation-free mapping outcome and its sample frame when successful. */
    struct AudioClockMappingResult final {
        AudioClockMappingStatus status{AudioClockMappingStatus::InvalidSnapshot};
        std::uint64_t sampleFrame{};
    };

    /**
     * @brief Map one producer timestamp to the current sample epoch using checked integer arithmetic.
     * @param snapshot Immutable correlation with a bounded inclusive producer-time interval.
     * @param expectation Exact runtime, epoch, clock/discontinuity and producer generation expected by the caller.
     * @param producerNanoseconds Timestamp in expectation.producerClockDomain; never wall time by implication.
     * @return Exact sample frame or a typed invalid, stale, paused, range or overflow result.
     * @note Mapping truncates sub-frame fractions toward the correlation anchor. A paused clock never advances or schedules work.
     */
    [[nodiscard]] AudioClockMappingResult MapAudioProducerTimeToSampleFrame(const AudioClockCorrelationSnapshot &snapshot,
                                                                            const AudioClockExpectation &expectation,
                                                                            std::uint64_t producerNanoseconds) noexcept;
}  // namespace Horo::Audio
