#include "Horo/Audio/AudioClock.h"

#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <ranges>

namespace Horo::Audio {
    namespace {
        constexpr std::uint64_t NanosecondsPerSecond = 1'000'000'000;

        /** @brief Validate identities, rates and interval ordering without consulting ambient clocks. */
        bool ValidSnapshot(const AudioClockCorrelationSnapshot &snapshot) noexcept {
            const auto &clock = snapshot.clock;
            const std::array valid{clock.owner.IsValid(),
                                   clock.epoch != 0,
                                   clock.generation != 0,
                                   clock.discontinuityRevision != 0,
                                   clock.sampleRate != 0,
                                   clock.sampleRate <= MaximumAudioSampleRate,
                                   clock.observedAt.clockDomain != 0,
                                   snapshot.producerClockDomain != 0,
                                   snapshot.producerGeneration != 0,
                                   snapshot.validFromNanoseconds <= snapshot.producerNanoseconds,
                                   snapshot.producerNanoseconds <= snapshot.validThroughNanoseconds};
            return std::ranges::all_of(valid, std::identity{});
        }

        /** @brief Convert a nonnegative nanosecond delta to whole frames without overflowing intermediate multiplication. */
        bool DeltaToFrames(const std::uint64_t nanoseconds, const std::uint32_t sampleRate, std::uint64_t &frames) noexcept {
            const auto seconds = nanoseconds / NanosecondsPerSecond;
            const auto remainder = nanoseconds % NanosecondsPerSecond;
            if (seconds > std::numeric_limits<std::uint64_t>::max() / sampleRate) {
                return false;
            }
            const auto whole = seconds * sampleRate;
            const auto fraction = (remainder * sampleRate) / NanosecondsPerSecond;
            if (whole > std::numeric_limits<std::uint64_t>::max() - fraction) {
                return false;
            }
            frames = whole + fraction;
            return true;
        }
    }  // namespace

    /** @copydoc MapAudioProducerTimeToSampleFrame */
    AudioClockMappingResult MapAudioProducerTimeToSampleFrame(const AudioClockCorrelationSnapshot &snapshot,
                                                              const AudioClockExpectation &expectation,
                                                              const std::uint64_t producerNanoseconds) noexcept {
        using enum AudioClockMappingStatus;
        if (!ValidSnapshot(snapshot)) {
            return {.status = InvalidSnapshot};
        }
        const auto &clock = snapshot.clock;
        const std::array current{expectation.owner == clock.owner,
                                 expectation.epoch == clock.epoch,
                                 expectation.clockGeneration == clock.generation,
                                 expectation.discontinuityRevision == clock.discontinuityRevision,
                                 expectation.producerClockDomain == snapshot.producerClockDomain,
                                 expectation.producerGeneration == snapshot.producerGeneration};
        if (!std::ranges::all_of(current, std::identity{})) {
            return {.status = StaleClock};
        }
        if (producerNanoseconds < snapshot.validFromNanoseconds || producerNanoseconds > snapshot.validThroughNanoseconds) {
            return {.status = OutsideCorrelation};
        }
        if (clock.state == AudioSampleClockState::Paused) {
            return {.status = Paused};
        }

        const bool afterAnchor = producerNanoseconds >= snapshot.producerNanoseconds;
        const auto delta =
            afterAnchor ? producerNanoseconds - snapshot.producerNanoseconds : snapshot.producerNanoseconds - producerNanoseconds;
        std::uint64_t frames{};
        if (!DeltaToFrames(delta, clock.sampleRate, frames)) {
            return {.status = Overflow};
        }
        if (afterAnchor) {
            if (clock.sampleFrame > std::numeric_limits<std::uint64_t>::max() - frames) {
                return {.status = Overflow};
            }
            return {.status = Mapped, .sampleFrame = clock.sampleFrame + frames};
        }
        if (frames > clock.sampleFrame) {
            return {.status = Overflow};
        }
        return {.status = Mapped, .sampleFrame = clock.sampleFrame - frames};
    }
}  // namespace Horo::Audio
