#include "Horo/Audio/AudioDeviceNegotiation.h"

#include <algorithm>
#include <array>
#include <functional>

namespace Horo::Audio {
    namespace {  // NOSONAR(cpp:S1000) - File-local validation helpers intentionally have internal linkage.

        /** @brief Ensures a bounded nonempty processing interval and an in-range preference. */
        bool ValidPeriodRequest(const AudioDevicePeriodRequest &period) noexcept {
            return period.minimumFrames > 0 && period.maximumFrames <= 16'384 && period.minimumFrames <= period.preferredFrames &&
                   period.preferredFrames <= period.maximumFrames;
        }

        /** @brief A changed preference requires a known explanation; unchanged facts cannot claim a deviation. */
        bool ValidDeviation(const bool changed, const AudioFormatDeviationReason reason) noexcept {
            using enum AudioFormatDeviationReason;
            if (!changed)
                return reason == None;
            return reason == DeviceConstraint || reason == HostPolicy;
        }

        /** @brief Matches complete rate/layout tuples rather than independently mixing whitelist fields. */
        bool AdmittedFormat(const AudioDeviceFormatRequest &request, const AudioProcessingFormat &effective) noexcept {
            return effective == request.preferred || std::ranges::any_of(request.allowedAlternatives, [&effective](const auto &allowed) {
                return effective == allowed;
            });
        }

        /** @brief Equal semantic channel sets require an exact role-preserving permutation. */
        bool ValidChannelMap(const AudioNegotiatedDeviceFormat &candidate) noexcept {
            const auto &horo = candidate.effective.layout;
            const auto &native = candidate.nativeSignal.layout;
            if (const std::array shapeMatches{horo.kind == native.kind, horo.ambisonic == native.ambisonic,
                                              horo.orderedChannels.size() == native.orderedChannels.size(),
                                              candidate.nativeChannelForHoro.size() == horo.orderedChannels.size()};
                !std::ranges::all_of(shapeMatches, std::identity{}))
                return false;
            for (std::size_t index = 0; index < candidate.nativeChannelForHoro.size(); ++index) {
                const auto mapped = candidate.nativeChannelForHoro[index];
                if (mapped >= native.orderedChannels.size() || horo.orderedChannels[index] != native.orderedChannels[mapped])
                    return false;
            }
            return true;
        }

        /** @brief Rate adaptation must be both permitted and explicitly reported before resources are prepared. */
        bool ValidRateConversion(const AudioDeviceFormatRequest &request, const AudioNegotiatedDeviceFormat &candidate) noexcept {
            if (candidate.effective.sampleRate == candidate.nativeSignal.sampleRate)
                return candidate.rateConversion == AudioDeviceRateConversion::None;
            return request.nativeRatePolicy == AudioDeviceRatePolicy::AllowPreparedResampler &&
                   candidate.rateConversion == AudioDeviceRateConversion::PreparedResampler;
        }

        /** @brief Callback bounds use the effective rate; an optional native period must be positive. */
        bool ValidReportedPeriod(const AudioDevicePeriodRequest &request, const AudioNegotiatedDeviceFormat &candidate) noexcept {
            return candidate.callbackFrames >= request.minimumFrames && candidate.callbackFrames <= request.maximumFrames &&
                   (!candidate.nativePeriodFrames.has_value() || *candidate.nativePeriodFrames != 0);
        }

        /** @brief Checks explanations against the original preferences, never against a rewritten request. */
        bool ValidDeviations(const AudioDeviceFormatRequest &request, const AudioNegotiatedDeviceFormat &candidate) noexcept {
            return ValidDeviation(candidate.effective.sampleRate != request.preferred.sampleRate, candidate.deviations.sampleRate) &&
                   ValidDeviation(candidate.effective.layout != request.preferred.layout, candidate.deviations.layout) &&
                   ValidDeviation(candidate.callbackFrames != request.period.preferredFrames, candidate.deviations.callbackPeriod);
        }

        /** @brief Both canonical and native facts must be well formed before any channel indexing. */
        bool ValidFormats(const AudioNegotiatedDeviceFormat &candidate) noexcept {
            const std::array valid{ValidateAudioProcessingFormat(candidate.effective),
                                   ValidateAudioProcessingFormat(candidate.nativeSignal), ValidateAudioPcmFormat(candidate.nativePcm)};
            return std::ranges::all_of(valid, std::identity{});
        }

        /** @brief Validates bounded candidate data without publication or native side effects. */
        AudioDeviceNegotiationStatus ValidateCandidate(const AudioDeviceFormatRequest &request,
                                                       const AudioNegotiatedDeviceFormat &candidate) noexcept {
            using enum AudioDeviceNegotiationStatus;
            if (!ValidFormats(candidate))
                return InvalidFormat;
            if (!AdmittedFormat(request, candidate.effective))
                return UnadmittedFormat;
            if (!ValidChannelMap(candidate))
                return InvalidChannelMap;
            if (!ValidReportedPeriod(request.period, candidate))
                return InvalidPeriod;
            if (!ValidRateConversion(request, candidate))
                return UnsupportedRateConversion;
            if (!ValidDeviations(request, candidate))
                return UndeclaredDeviation;
            return Accepted;
        }
    }  // namespace

    /** @copydoc ValidateAudioDeviceFormatRequest */
    bool ValidateAudioDeviceFormatRequest(const AudioDeviceFormatRequest &request) noexcept {
        if (!ValidateAudioProcessingFormat(request.preferred) || request.allowedAlternatives.size() > 8 ||
            !ValidPeriodRequest(request.period))
            return false;
        if (request.nativeRatePolicy != AudioDeviceRatePolicy::Exact &&
            request.nativeRatePolicy != AudioDeviceRatePolicy::AllowPreparedResampler)
            return false;
        return std::ranges::all_of(request.allowedAlternatives, [](const auto &format) {
            return ValidateAudioProcessingFormat(format);
        });
    }

    /** @copydoc ValidateAudioDeviceNegotiation */
    AudioDeviceNegotiationResult ValidateAudioDeviceNegotiation(const AudioDeviceFormatRequest &request,
                                                                const AudioDeviceSnapshot &snapshot,
                                                                const AudioNegotiatedDeviceFormat &candidate) noexcept {
        if (!ValidateAudioDeviceFormatRequest(request))
            return {};
        const auto selection = ResolveAudioDevice(snapshot, request.device);
        if (selection.status != AudioDeviceResolutionStatus::Resolved)
            return {.status = AudioDeviceNegotiationStatus::SelectionFailed, .selection = selection};
        if (candidate.device != selection.device || candidate.discoveryRevision != selection.revision || candidate.formatRevision == 0)
            return {.status = AudioDeviceNegotiationStatus::StaleDevice, .selection = selection};
        return {.status = ValidateCandidate(request, candidate), .selection = selection};
    }
}  // namespace Horo::Audio
