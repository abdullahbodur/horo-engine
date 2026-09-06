#include "Horo/Network/ProtocolIdentityRegistry.h"

#include "Horo/Network/NetworkErrors.h"

#include <algorithm>
#include <new>
#include <tuple>
#include <utility>

namespace Horo::Network {
    namespace {
        template <typename T> [[nodiscard]] Result<T> Fail(const ErrorCodeDescriptor &code) {
            return Result<T>::Failure(MakeError(code));
        }

        [[nodiscard]] bool ValidLimits(const ProtocolIdentityRegistryLimits &limits) noexcept {
            return limits.maximumProtocols > 0;
        }

        [[nodiscard]] bool WithinCapacity(const ProtocolIdentityContributions &contributions,
                                          const ProtocolIdentityRegistryLimits &limits) noexcept {
            return contributions.protocols.size() <= limits.maximumProtocols && contributions.messages.size() <= limits.maximumMessages &&
                   contributions.features.size() <= limits.maximumFeatures &&
                   contributions.closeReasons.size() <= limits.maximumCloseReasons;
        }

        template <typename Identity> [[nodiscard]] bool SameNamespace(const ProtocolId protocol, const Identity child) noexcept {
            return protocol.Namespace() == child.Namespace();
        }

        [[nodiscard]] auto MessageKey(const MessageIdentityDescriptor &value) noexcept {
            return std::tuple{value.protocol.Value(), value.id.Value()};
        }

        [[nodiscard]] auto FeatureKey(const FeatureIdentityDescriptor &value) noexcept {
            return std::tuple{value.protocol.Value(), value.id.Value()};
        }

        [[nodiscard]] auto CloseReasonKey(const CloseReasonIdentityDescriptor &value) noexcept {
            return std::tuple{value.protocol.Value(), value.id.Value()};
        }

        [[nodiscard]] const ProtocolIdentityDescriptor *FindProtocol(const std::vector<ProtocolIdentityDescriptor> &protocols,
                                                                     const ProtocolId id) noexcept {
            const auto found = std::ranges::lower_bound(protocols, id, {}, &ProtocolIdentityDescriptor::id);
            return found != protocols.end() && found->id == id ? std::to_address(found) : nullptr;
        }

        [[nodiscard]] bool ValidProtocols(const std::vector<ProtocolIdentityDescriptor> &protocols) noexcept {
            return std::ranges::all_of(protocols, [](const ProtocolIdentityDescriptor &value) {
                return value.id.IsValid() && value.versions.IsValid();
            });
        }

        [[nodiscard]] bool ValidMessages(const std::vector<ProtocolIdentityDescriptor> &protocols,
                                         const std::vector<MessageIdentityDescriptor> &messages) noexcept {
            return std::ranges::all_of(messages, [&](const MessageIdentityDescriptor &value) {
                return value.protocol.IsValid() && value.id.IsValid() && value.schema.IsValid() && value.version.IsValid() &&
                       SameNamespace(value.protocol, value.id) && SameNamespace(value.protocol, value.schema) &&
                       FindProtocol(protocols, value.protocol) != nullptr;
            });
        }

        [[nodiscard]] bool ValidFeatures(const std::vector<ProtocolIdentityDescriptor> &protocols,
                                         const std::vector<FeatureIdentityDescriptor> &features) noexcept {
            return std::ranges::all_of(features, [&](const FeatureIdentityDescriptor &value) {
                const auto *protocol = FindProtocol(protocols, value.protocol);
                return protocol != nullptr && value.id.IsValid() && SameNamespace(value.protocol, value.id) &&
                       protocol->versions.Contains(value.introduced);
            });
        }

        [[nodiscard]] bool ValidCloseReasons(const std::vector<ProtocolIdentityDescriptor> &protocols,
                                             const std::vector<CloseReasonIdentityDescriptor> &closeReasons) noexcept {
            return std::ranges::all_of(closeReasons, [&](const CloseReasonIdentityDescriptor &value) {
                return FindProtocol(protocols, value.protocol) != nullptr && value.id.IsValid() &&
                       SameNamespace(value.protocol, value.id) && value.kind < CloseReasonKind::Count;
            });
        }

        template <typename Range, typename Projection>
        [[nodiscard]] bool HasAdjacentDuplicate(const Range &range, Projection projection) noexcept {
            return std::ranges::adjacent_find(range, {}, projection) != range.end();
        }

        [[nodiscard]] bool HasDuplicateIdentity(const std::vector<ProtocolIdentityDescriptor> &protocols,
                                                const std::vector<MessageIdentityDescriptor> &messages,
                                                const std::vector<FeatureIdentityDescriptor> &features,
                                                const std::vector<CloseReasonIdentityDescriptor> &closeReasons) noexcept {
            return HasAdjacentDuplicate(protocols, &ProtocolIdentityDescriptor::id) || HasAdjacentDuplicate(messages, MessageKey) ||
                   HasAdjacentDuplicate(features, FeatureKey) || HasAdjacentDuplicate(closeReasons, CloseReasonKey);
        }
    }  // namespace

    /** @copydoc ProtocolIdentityRegistry::Create */
    Result<ProtocolIdentityRegistry> ProtocolIdentityRegistry::Create(const ProtocolIdentityContributions &contributions,
                                                                      const ProtocolIdentityRegistryLimits &limits) {
        if (!ValidLimits(limits))
            return Fail<ProtocolIdentityRegistry>(NetworkErrors::ProtocolIdentityDescriptorInvalid);
        if (!WithinCapacity(contributions, limits))
            return Fail<ProtocolIdentityRegistry>(NetworkErrors::ProtocolIdentityCapacityExceeded);

        try {
            ProtocolIdentityRegistry result;
            result.protocols_.assign(contributions.protocols.begin(), contributions.protocols.end());
            result.messages_.assign(contributions.messages.begin(), contributions.messages.end());
            result.features_.assign(contributions.features.begin(), contributions.features.end());
            result.closeReasons_.assign(contributions.closeReasons.begin(), contributions.closeReasons.end());
            std::ranges::sort(result.protocols_, {}, &ProtocolIdentityDescriptor::id);
            std::ranges::sort(result.messages_, {}, MessageKey);
            std::ranges::sort(result.features_, {}, FeatureKey);
            std::ranges::sort(result.closeReasons_, {}, CloseReasonKey);

            if (HasDuplicateIdentity(result.protocols_, result.messages_, result.features_, result.closeReasons_))
                return Fail<ProtocolIdentityRegistry>(NetworkErrors::ProtocolIdentityConflict);

            if (!ValidProtocols(result.protocols_) || !ValidMessages(result.protocols_, result.messages_) ||
                !ValidFeatures(result.protocols_, result.features_) || !ValidCloseReasons(result.protocols_, result.closeReasons_))
                return Fail<ProtocolIdentityRegistry>(NetworkErrors::ProtocolIdentityDescriptorInvalid);
            return Result<ProtocolIdentityRegistry>::Success(std::move(result));
        } catch (const std::bad_alloc &) {
            return Fail<ProtocolIdentityRegistry>(NetworkErrors::ProtocolIdentityCapacityExceeded);
        }
    }

    /** @copydoc ProtocolIdentityRegistry::FindProtocol */
    std::optional<ProtocolIdentityDescriptor> ProtocolIdentityRegistry::FindProtocol(const ProtocolId id) const noexcept {
        const auto *found = Horo::Network::FindProtocol(protocols_, id);
        return found == nullptr ? std::nullopt : std::optional{*found};
    }

    /** @copydoc ProtocolIdentityRegistry::FindMessage */
    std::optional<MessageIdentityDescriptor> ProtocolIdentityRegistry::FindMessage(const ProtocolId protocol,
                                                                                   const MessageTypeId id) const noexcept {
        const auto key = std::tuple{protocol.Value(), id.Value()};
        const auto found = std::ranges::lower_bound(messages_, key, {}, MessageKey);
        return found == messages_.end() || MessageKey(*found) != key ? std::nullopt : std::optional{*found};
    }

    /** @copydoc ProtocolIdentityRegistry::FindFeature */
    std::optional<FeatureIdentityDescriptor> ProtocolIdentityRegistry::FindFeature(const ProtocolId protocol,
                                                                                   const ProtocolFeatureId id) const noexcept {
        const auto key = std::tuple{protocol.Value(), id.Value()};
        const auto found = std::ranges::lower_bound(features_, key, {}, FeatureKey);
        return found == features_.end() || FeatureKey(*found) != key ? std::nullopt : std::optional{*found};
    }

    /** @copydoc ProtocolIdentityRegistry::FindCloseReason */
    std::optional<CloseReasonIdentityDescriptor> ProtocolIdentityRegistry::FindCloseReason(const ProtocolId protocol,
                                                                                           const CloseReasonId id) const noexcept {
        const auto key = std::tuple{protocol.Value(), id.Value()};
        const auto found = std::ranges::lower_bound(closeReasons_, key, {}, CloseReasonKey);
        return found == closeReasons_.end() || CloseReasonKey(*found) != key ? std::nullopt : std::optional{*found};
    }

    /** @copydoc ProtocolIdentityRegistry::SelectVersion */
    Result<ProtocolVersion> ProtocolIdentityRegistry::SelectVersion(const ProtocolId protocol, const ProtocolVersionRange &peer) const {
        if (!protocol.IsValid() || !peer.IsValid())
            return Fail<ProtocolVersion>(NetworkErrors::ProtocolIdentityDescriptorInvalid);
        const auto local = FindProtocol(protocol);
        if (!local.has_value())
            return Fail<ProtocolVersion>(NetworkErrors::ProtocolIdentityUnknown);
        if (local->versions.minimum.major != peer.minimum.major)
            return Fail<ProtocolVersion>(NetworkErrors::ProtocolVersionIncompatible);
        const ProtocolVersion minimum = std::max(local->versions.minimum, peer.minimum);
        const ProtocolVersion maximum = std::min(local->versions.maximum, peer.maximum);
        return minimum <= maximum ? Result<ProtocolVersion>::Success(maximum)
                                  : Fail<ProtocolVersion>(NetworkErrors::ProtocolVersionIncompatible);
    }
}  // namespace Horo::Network
