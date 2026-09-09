#include "Horo/Audio/AudioMediaFormatRegistry.h"

#include "Horo/Audio/AudioErrors.h"

#include <algorithm>

namespace Horo::Audio {
    namespace {
        [[nodiscard]] bool IsKnownPayloadKind(const AudioCodecPayloadKind kind) noexcept {
            return kind == AudioCodecPayloadKind::Pcm || kind == AudioCodecPayloadKind::Compressed;
        }

        template <typename Descriptor, typename Id>
        [[nodiscard]] const Descriptor *FindDescriptor(const std::span<const Descriptor> descriptors, const Id id) noexcept {
            const auto found = std::ranges::find(descriptors, id, &Descriptor::id);
            return found == descriptors.end() ? nullptr : &*found;
        }

        template <typename Descriptor> [[nodiscard]] bool HasDuplicateIdentity(const std::span<const Descriptor> descriptors) noexcept {
            for (std::size_t candidate = 0; candidate < descriptors.size(); ++candidate) {
                if (FindDescriptor<Descriptor>(descriptors.first(candidate), descriptors[candidate].id))
                    return true;
            }
            return false;
        }

        [[nodiscard]] bool HasDuplicateBinding(const std::span<const AudioMediaFormatBinding> bindings) noexcept {
            for (std::size_t candidate = 0; candidate < bindings.size(); ++candidate) {
                for (std::size_t prior = 0; prior < candidate; ++prior) {
                    if (bindings[prior] == bindings[candidate])
                        return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool ValidLimits(const AudioMediaFormatRegistryLimits &limits) noexcept {
            return limits.maximumContainers > 0 && limits.maximumCodecs > 0 && limits.maximumBindings > 0 &&
                   limits.maximumDisplayNameBytes > 0;
        }

        [[nodiscard]] bool WithinCapacity(const AudioMediaFormatContributions &contributions,
                                          const AudioMediaFormatRegistryLimits &limits) noexcept {
            return contributions.containers.size() <= limits.maximumContainers && contributions.codecs.size() <= limits.maximumCodecs &&
                   contributions.bindings.size() <= limits.maximumBindings;
        }

        template <typename Descriptor>
        [[nodiscard]] bool ValidDescriptors(const std::span<const Descriptor> descriptors, const std::size_t maximumNameBytes) noexcept {
            return std::ranges::all_of(descriptors, [maximumNameBytes](const Descriptor &descriptor) {
                return descriptor.id.IsValid() && !descriptor.displayName.empty() && descriptor.displayName.size() <= maximumNameBytes;
            });
        }

        [[nodiscard]] Result<void> ValidateBinding(const AudioMediaFormatBinding &binding,
                                                   const std::span<const AudioContainerDescriptor> containers,
                                                   const std::span<const AudioCodecDescriptor> codecs) {
            if (!binding.container.IsValid() || !binding.codec.IsValid())
                return Result<void>::Failure(MakeError(AudioErrors::FormatRegistryInvalid));
            if (!FindDescriptor<AudioContainerDescriptor>(containers, binding.container) ||
                !FindDescriptor<AudioCodecDescriptor>(codecs, binding.codec))
                return Result<void>::Failure(MakeError(AudioErrors::FormatRegistryInvalid));

            const AudioCodecDescriptor *codec = FindDescriptor<AudioCodecDescriptor>(codecs, binding.codec);
            const bool validRepresentation = codec->payloadKind == AudioCodecPayloadKind::Pcm
                                                 ? binding.pcm.has_value() && ValidateAudioPcmFormat(*binding.pcm)
                                                 : !binding.pcm.has_value();
            return validRepresentation ? Result<void>::Success() : Result<void>::Failure(MakeError(AudioErrors::FormatRegistryInvalid));
        }

        [[nodiscard]] bool ValidDescriptorMetadata(const AudioMediaFormatContributions &contributions,
                                                   const AudioMediaFormatRegistryLimits &limits) noexcept {
            return ValidDescriptors(contributions.containers, limits.maximumDisplayNameBytes) &&
                   ValidDescriptors(contributions.codecs, limits.maximumDisplayNameBytes) &&
                   std::ranges::all_of(contributions.codecs, [](const AudioCodecDescriptor &codec) {
                return IsKnownPayloadKind(codec.payloadKind);
            });
        }

        [[nodiscard]] bool HasConflicts(const AudioMediaFormatContributions &contributions) noexcept {
            return HasDuplicateIdentity(contributions.containers) || HasDuplicateIdentity(contributions.codecs) ||
                   HasDuplicateBinding(contributions.bindings);
        }

        [[nodiscard]] Result<void> ValidateContributions(const AudioMediaFormatContributions &contributions,
                                                         const AudioMediaFormatRegistryLimits &limits) {
            if (!ValidLimits(limits))
                return Result<void>::Failure(MakeError(AudioErrors::FormatRegistryInvalid));
            if (!WithinCapacity(contributions, limits))
                return Result<void>::Failure(MakeError(AudioErrors::FormatRegistryCapacityExceeded));
            if (!ValidDescriptorMetadata(contributions, limits))
                return Result<void>::Failure(MakeError(AudioErrors::FormatRegistryInvalid));
            if (HasConflicts(contributions))
                return Result<void>::Failure(MakeError(AudioErrors::FormatRegistryConflict));
            for (const AudioMediaFormatBinding &binding : contributions.bindings) {
                if (const Result<void> valid = ValidateBinding(binding, contributions.containers, contributions.codecs); valid.HasError())
                    return valid;
            }
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc AudioMediaFormatRegistry::Create */
    Result<AudioMediaFormatRegistry> AudioMediaFormatRegistry::Create(const AudioMediaFormatContributions &contributions,
                                                                      const AudioMediaFormatRegistryLimits &limits) {
        if (const Result<void> valid = ValidateContributions(contributions, limits); valid.HasError())
            return Result<AudioMediaFormatRegistry>::Failure(valid.ErrorValue());

        AudioMediaFormatRegistry registry;
        registry.containers_.assign(contributions.containers.begin(), contributions.containers.end());
        registry.codecs_.assign(contributions.codecs.begin(), contributions.codecs.end());
        registry.bindings_.assign(contributions.bindings.begin(), contributions.bindings.end());
        return Result<AudioMediaFormatRegistry>::Success(std::move(registry));
    }

    /** @copydoc AudioMediaFormatRegistry::Containers */
    std::span<const AudioContainerDescriptor> AudioMediaFormatRegistry::Containers() const noexcept {
        return containers_;
    }

    /** @copydoc AudioMediaFormatRegistry::Codecs */
    std::span<const AudioCodecDescriptor> AudioMediaFormatRegistry::Codecs() const noexcept {
        return codecs_;
    }

    /** @copydoc AudioMediaFormatRegistry::Bindings */
    std::span<const AudioMediaFormatBinding> AudioMediaFormatRegistry::Bindings() const noexcept {
        return bindings_;
    }

    /** @copydoc AudioMediaFormatRegistry::Resolve */
    Result<AudioMediaFormatBinding> AudioMediaFormatRegistry::Resolve(const AudioMediaFormatBinding &requested) const {
        if (!requested.container.IsValid() || !requested.codec.IsValid())
            return Result<AudioMediaFormatBinding>::Failure(MakeError(AudioErrors::FormatRegistryInvalid));
        if (!FindDescriptor<AudioContainerDescriptor>(containers_, requested.container))
            return Result<AudioMediaFormatBinding>::Failure(MakeError(AudioErrors::FormatContainerUnknown));

        const AudioCodecDescriptor *codec = FindDescriptor<AudioCodecDescriptor>(codecs_, requested.codec);
        if (!codec)
            return Result<AudioMediaFormatBinding>::Failure(MakeError(AudioErrors::FormatCodecUnknown));
        if (const bool validRepresentation = codec->payloadKind == AudioCodecPayloadKind::Pcm
                                                 ? requested.pcm.has_value() && ValidateAudioPcmFormat(*requested.pcm)
                                                 : !requested.pcm.has_value();
            !validRepresentation)
            return Result<AudioMediaFormatBinding>::Failure(MakeError(AudioErrors::FormatRegistryInvalid));

        const auto found = std::ranges::find(bindings_, requested);
        if (found == bindings_.end())
            return Result<AudioMediaFormatBinding>::Failure(MakeError(AudioErrors::FormatCombinationUnsupported));
        return Result<AudioMediaFormatBinding>::Success(*found);
    }
}  // namespace Horo::Audio
