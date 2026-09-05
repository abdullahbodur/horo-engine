#include "Horo/Audio/AudioDeviceTiming.h"

#include <algorithm>
#include <array>
#include <functional>

namespace Horo::Audio {
    namespace {  // NOSONAR(cpp:S1000) - File-local validation helpers intentionally have internal linkage.

        /** @brief Requires every independently evaluated invariant to hold. */
        template <std::size_t Count> bool AllTrue(const std::array<bool, Count> &invariants) noexcept {
            return std::ranges::all_of(invariants, std::identity{});
        }

        /** @brief Tests membership in a small, fixed set without embedding another decision chain. */
        template <typename Value, std::size_t Count> bool Contains(const std::array<Value, Count> &values, const Value candidate) noexcept {
            return std::ranges::find(values, candidate) != values.end();
        }

        /** @brief Missing knowledge carries no fabricated numeric evidence. */
        bool EmptyEvidence(const AudioDurationObservation &value) noexcept {
            return AllTrue(std::array{value.source == AudioObservationSource::None, !value.nanoseconds.has_value(),
                                      !value.observedAt.has_value(), !value.windowNanoseconds.has_value(), value.sampleCount == 0});
        }

        /** @brief Numeric zero and time zero remain valid; their optionals and clock identity must be present. */
        bool HasValueAndTime(const AudioDurationObservation &value) noexcept {
            return value.nanoseconds.has_value() && value.observedAt.has_value() && value.observedAt->clockDomain != 0;
        }

        /** @brief API facts and estimates are not measured windows. */
        bool ValidReported(const AudioDurationObservation &value) noexcept {
            return AllTrue(std::array{HasValueAndTime(value), !value.windowNanoseconds.has_value(), value.sampleCount == 0});
        }

        /** @brief A measured mean names a positive window/count and cannot underflow its clock origin. */
        bool ValidMeasured(const AudioDurationObservation &value) noexcept {
            using enum AudioObservationSource;
            if (!HasValueAndTime(value) || !value.windowNanoseconds.has_value())
                return false;
            return AllTrue(std::array{*value.windowNanoseconds != 0, value.sampleCount != 0,
                                      *value.windowNanoseconds <= value.observedAt->nanoseconds,
                                      Contains(std::array{CallbackClock, Loopback, DeterministicClock}, value.source)});
        }

        /** @brief Every numeric observation uses the report's clock and was known by capture time. */
        bool WithinSnapshot(const AudioDurationObservation &value, const AudioMonotonicTimestamp &capturedAt) noexcept {
            if (!ValidateAudioDurationObservation(value))
                return false;
            if (!value.observedAt.has_value())
                return true;
            return AllTrue(std::array{value.observedAt->clockDomain == capturedAt.clockDomain,
                                      value.observedAt->nanoseconds <= capturedAt.nanoseconds});
        }

        /** @brief Reported/estimated values remain labeled; physical measurements require loopback evidence. */
        bool PhysicalEvidence(const AudioDurationObservation &value) noexcept {
            return value.quality != AudioObservationQuality::Measured || value.source == AudioObservationSource::Loopback;
        }

        /** @brief Callback duration/cadence cannot be inferred from a loopback latency measurement. */
        bool CallbackEvidence(const AudioDurationObservation &value) noexcept {
            using enum AudioObservationSource;
            if (value.quality != AudioObservationQuality::Measured)
                return true;
            return Contains(std::array{CallbackClock, DeterministicClock}, value.source);
        }

        /** @brief Null has no hardware/end-to-end physical latency, even if it executes callbacks on a real CPU. */
        bool ValidPhysicalComponents(const AudioDeviceTimingReport &report, const AudioBackendKind backend) noexcept {
            if (backend == AudioBackendKind::NullAudio)
                return AllTrue(std::array{report.hardwareLatency.quality == AudioObservationQuality::Unsupported,
                                          report.endToEndLatency.quality == AudioObservationQuality::Unsupported});
            return AllTrue(std::array{PhysicalEvidence(report.hardwareLatency), PhysicalEvidence(report.endToEndLatency)});
        }
    }  // namespace

    /** @copydoc MatchesAudioDeviceEpoch */
    bool MatchesAudioDeviceEpoch(const AudioDeviceEpoch &expected, const AudioDeviceEpoch &observed) noexcept {
        return AllTrue(
            std::array{expected.device.IsValid(), expected.formatRevision != 0, expected.callbackEpoch != 0, expected == observed});
    }

    /** @copydoc ValidateAudioDurationObservation */
    bool ValidateAudioDurationObservation(const AudioDurationObservation &value) noexcept {
        using enum AudioObservationQuality;
        using enum AudioObservationSource;
        switch (value.quality) {
            case Unknown:
            case Unsupported:
            case Unavailable:
                return EmptyEvidence(value);
            case Estimated:
                return AllTrue(std::array{ValidReported(value), Contains(std::array{BackendApi, Adapter}, value.source)});
            case Reported:
                return AllTrue(std::array{ValidReported(value), value.source == BackendApi});
            case Measured:
                return ValidMeasured(value);
        }
        return false;
    }

    /** @copydoc ValidateAudioDeviceTimingReport */
    bool ValidateAudioDeviceTimingReport(const AudioDeviceTimingReport &report, const AudioDeviceEpoch &expected,
                                         const AudioBackendKind backend) noexcept {
        using enum AudioBackendKind;
        if (constexpr std::array backends{WASAPI, CoreAudio, PipeWire, SDL3Audio, NullAudio};
            !AllTrue(std::array{MatchesAudioDeviceEpoch(expected, report.epoch), report.capturedAt.clockDomain != 0,
                                Contains(backends, backend)}))
            return false;
        const std::array observations{&report.hardwareLatency, &report.adapterLatency,    &report.queuedLatency,
                                      &report.endToEndLatency, &report.callbackExecution, &report.callbackPeriod};
        const bool coherent = std::ranges::all_of(observations, [&report](const auto *value) {
            return WithinSnapshot(*value, report.capturedAt);
        });
        return AllTrue(std::array{coherent, ValidPhysicalComponents(report, backend), CallbackEvidence(report.callbackExecution),
                                  CallbackEvidence(report.callbackPeriod)});
    }
}  // namespace Horo::Audio
