#include "Horo/Navigation/NavigationProjectProfiles.h"

#include "Horo/Navigation/NavigationErrors.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <limits>
#include <ranges>
#include <type_traits>

namespace Horo::Navigation {
    namespace {
        template <typename T> bool CheckedMultiply(const T left, const T right, T &product) noexcept {
            if (left != 0 && right > std::numeric_limits<T>::max() / left)
                return false;
            product = left * right;
            return true;
        }

        bool IsPositive(const NavigationCapacityLimits &limits) noexcept {
            return limits.maximumAgents > 0 && limits.maximumSurfaces > 0 && limits.maximumResidentTiles > 0 &&
                   limits.maximumConcurrentQueries > 0 && limits.maximumBytesPerResidentTile > 0 && limits.maximumResidentMemoryBytes > 0 &&
                   limits.maximumWorkUnitsPerTick > 0;
        }

        bool IsKnownRequirement(const NavigationCapabilityRequirement requirement) noexcept {
            return requirement >= NavigationCapabilityRequirement::Optional && requirement < NavigationCapabilityRequirement::Count;
        }

        bool IsKnownQueryRequirement(const NavigationQueryRequirement &requirement) noexcept {
            return requirement.query < NavigationQueryKind::Count && requirement.quality < NavigationQualityLevel::Count &&
                   requirement.limits.maximumNodeExpansions > 0 && requirement.limits.maximumResultPoints > 0 &&
                   requirement.limits.maximumSearchDistanceMeters > 0.0F &&
                   requirement.limits.maximumSearchDistanceMeters <= std::numeric_limits<float>::max();
        }

        bool AggregateEnvelopeFits(const NavigationProjectProfileInput &input) noexcept {
            std::uint64_t residentBytes{};
            if (!CheckedMultiply(static_cast<std::uint64_t>(input.capacities.maximumResidentTiles),
                                 input.capacities.maximumBytesPerResidentTile, residentBytes) ||
                residentBytes > input.capacities.maximumResidentMemoryBytes)
                return false;

            std::uint64_t queryWork{};
            return CheckedMultiply(static_cast<std::uint64_t>(input.capacities.maximumConcurrentQueries),
                                   static_cast<std::uint64_t>(input.maximumQuery.limits.maximumNodeExpansions), queryWork) &&
                   queryWork <= input.capacities.maximumWorkUnitsPerTick;
        }

        void HashByte(std::uint64_t &hash, const std::uint8_t value) noexcept {
            constexpr std::uint64_t Prime = 1'099'511'628'211ULL;
            hash ^= value;
            hash *= Prime;
        }

        template <typename T> void HashInteger(std::uint64_t &hash, const T value) noexcept {
            using Unsigned = std::make_unsigned_t<T>;
            const auto unsignedValue = static_cast<Unsigned>(value);
            for (std::size_t index = 0; index < sizeof(T); ++index)
                HashByte(hash, static_cast<std::uint8_t>(unsignedValue >> (index * 8U)));
        }

        NavigationProjectProfileFingerprint ComputeFingerprint(const NavigationProjectProfileInput &input) {
            std::uint64_t hash = 14'695'981'039'346'656'037ULL;
            HashInteger(hash, input.id.Value());
            HashInteger(hash, input.revision.Value());
            HashInteger(hash, input.capacities.maximumAgents);
            HashInteger(hash, input.capacities.maximumSurfaces);
            HashInteger(hash, input.capacities.maximumResidentTiles);
            HashInteger(hash, input.capacities.maximumConcurrentQueries);
            HashInteger(hash, input.capacities.maximumBytesPerResidentTile);
            HashInteger(hash, input.capacities.maximumResidentMemoryBytes);
            HashInteger(hash, input.capacities.maximumWorkUnitsPerTick);
            HashInteger(hash, static_cast<std::uint8_t>(input.maximumQuery.query));
            HashInteger(hash, static_cast<std::uint8_t>(input.maximumQuery.quality));
            HashInteger(hash, input.maximumQuery.limits.maximumNodeExpansions);
            HashInteger(hash, input.maximumQuery.limits.maximumResultPoints);
            HashInteger(hash, std::bit_cast<std::uint32_t>(input.maximumQuery.limits.maximumSearchDistanceMeters));
            for (const auto requirement : input.capabilities)
                HashInteger(hash, static_cast<std::uint8_t>(requirement));
            if (hash == 0)
                hash = 1;
            return NavigationProjectProfileFingerprint::Create(hash).Value();
        }

        NavigationCapacityLimits Clamp(const NavigationCapacityLimits &requested, const NavigationCapacityLimits &authority) noexcept {
            return {
                .maximumAgents = std::min(requested.maximumAgents, authority.maximumAgents),
                .maximumSurfaces = std::min(requested.maximumSurfaces, authority.maximumSurfaces),
                .maximumResidentTiles = std::min(requested.maximumResidentTiles, authority.maximumResidentTiles),
                .maximumConcurrentQueries = std::min(requested.maximumConcurrentQueries, authority.maximumConcurrentQueries),
                .maximumBytesPerResidentTile = std::min(requested.maximumBytesPerResidentTile, authority.maximumBytesPerResidentTile),
                .maximumResidentMemoryBytes = std::min(requested.maximumResidentMemoryBytes, authority.maximumResidentMemoryBytes),
                .maximumWorkUnitsPerTick = std::min(requested.maximumWorkUnitsPerTick, authority.maximumWorkUnitsPerTick),
            };
        }
    }  // namespace

    /** @copydoc NavigationProjectProfile::Create */
    Result<NavigationProjectProfile> NavigationProjectProfile::Create(const NavigationProjectProfileInput &input) {
        if (!input.id.IsValid() || !input.revision.IsValid() || !IsPositive(input.capacities) ||
            !IsKnownQueryRequirement(input.maximumQuery) || !std::ranges::all_of(input.capabilities, IsKnownRequirement))
            return Result<NavigationProjectProfile>::Failure(MakeError(NavigationErrors::ProjectProfileInvalid));
        if (!AggregateEnvelopeFits(input))
            return Result<NavigationProjectProfile>::Failure(MakeError(NavigationErrors::ProjectProfileCapacityExceeded));
        return Result<NavigationProjectProfile>::Success(NavigationProjectProfile{input, ComputeFingerprint(input)});
    }

    /** @copydoc NavigationProjectProfile::Replace */
    Result<NavigationProjectProfile> NavigationProjectProfile::Replace(const NavigationProjectProfile &previous,
                                                                       const NavigationProjectProfileInput &input) {
        if (input.id != previous.Id() || !input.revision.IsValid() || input.revision.Value() <= previous.Revision().Value())
            return Result<NavigationProjectProfile>::Failure(MakeError(NavigationErrors::ProjectProfileStale));
        return Create(input);
    }

    /** @copydoc NavigationProjectProfile::NavigationProjectProfile */
    NavigationProjectProfile::NavigationProjectProfile(const NavigationProjectProfileInput &input,
                                                       const NavigationProjectProfileFingerprint fingerprint) noexcept
        : input_(input), fingerprint_(fingerprint) {}

    /** @copydoc NavigationProjectProfile::Id */
    NavigationProjectProfileId NavigationProjectProfile::Id() const noexcept {
        return input_.id;
    }

    /** @copydoc NavigationProjectProfile::Revision */
    NavigationProjectProfileRevision NavigationProjectProfile::Revision() const noexcept {
        return input_.revision;
    }

    /** @copydoc NavigationProjectProfile::Fingerprint */
    NavigationProjectProfileFingerprint NavigationProjectProfile::Fingerprint() const noexcept {
        return fingerprint_;
    }

    /** @copydoc NavigationProjectProfile::Capacities */
    const NavigationCapacityLimits &NavigationProjectProfile::Capacities() const noexcept {
        return input_.capacities;
    }

    /** @copydoc NavigationProjectProfile::MaximumQuery */
    const NavigationQueryRequirement &NavigationProjectProfile::MaximumQuery() const noexcept {
        return input_.maximumQuery;
    }

    /** @copydoc NavigationProjectProfile::Requirement */
    NavigationCapabilityRequirement NavigationProjectProfile::Requirement(const NavigationCapability capability) const noexcept {
        const auto index = static_cast<std::size_t>(capability);
        if (index >= input_.capabilities.size())
            return NavigationCapabilityRequirement::Count;
        return input_.capabilities[index];
    }

    /** @copydoc ResolveNavigationProjectProfile */
    Result<ResolvedNavigationProjectProfile> ResolveNavigationProjectProfile(
        const NavigationProjectProfile &project, const std::optional<NavigationDeveloperPreviewPreference> &preview) {
        ResolvedNavigationProjectProfile resolved{
            .id = project.Id(),
            .projectRevision = project.Revision(),
            .projectFingerprint = project.Fingerprint(),
            .capacities = project.Capacities(),
            .maximumQuery = project.MaximumQuery(),
        };
        if (!preview.has_value())
            return Result<ResolvedNavigationProjectProfile>::Success(resolved);
        if (!preview->revision.IsValid() || !IsPositive(preview->requestedMaximums))
            return Result<ResolvedNavigationProjectProfile>::Failure(MakeError(NavigationErrors::ProjectProfileInvalid));
        if (preview->projectRevision != project.Revision())
            return Result<ResolvedNavigationProjectProfile>::Failure(MakeError(NavigationErrors::ProjectProfileStale));
        resolved.previewRevision = preview->revision;
        resolved.capacities = Clamp(preview->requestedMaximums, project.Capacities());
        return Result<ResolvedNavigationProjectProfile>::Success(resolved);
    }

    /** @copydoc AdmitNavigationCapacity */
    Result<void> AdmitNavigationCapacity(const ResolvedNavigationProjectProfile &profile, const NavigationCapacityUsage &usage) {
        const auto &limits = profile.capacities;
        if (usage.agents > limits.maximumAgents || usage.surfaces > limits.maximumSurfaces ||
            usage.residentTiles > limits.maximumResidentTiles || usage.concurrentQueries > limits.maximumConcurrentQueries ||
            usage.residentMemoryBytes > limits.maximumResidentMemoryBytes || usage.workUnitsThisTick > limits.maximumWorkUnitsPerTick)
            return Result<void>::Failure(MakeError(NavigationErrors::ProjectProfileCapacityExceeded));
        return Result<void>::Success();
    }

    /** @copydoc AdmitNavigationProjectProfile */
    Result<void> AdmitNavigationProjectProfile(const NavigationProjectProfile &profile, const NavigationProviderCapabilities &provider,
                                               const std::uint64_t expectedProviderRevision) {
        if (!ValidateNavigationProviderCapabilities(provider))
            return Result<void>::Failure(MakeError(NavigationErrors::CapabilityDescriptorInvalid));
        if (expectedProviderRevision == 0 || provider.revision != expectedProviderRevision)
            return Result<void>::Failure(MakeError(NavigationErrors::CapabilityStale));

        for (std::size_t index = 0; index < static_cast<std::size_t>(NavigationCapability::Count); ++index) {
            if (profile.Requirement(static_cast<NavigationCapability>(index)) != NavigationCapabilityRequirement::Required)
                continue;
            const auto support = QueryNavigationCapability(provider, static_cast<NavigationCapability>(index));
            if (support == NavigationSupport::Unsupported)
                return Result<void>::Failure(MakeError(NavigationErrors::OperationUnsupported));
            if (support != NavigationSupport::Available)
                return Result<void>::Failure(MakeError(NavigationErrors::CapabilityUnavailable));
        }

        if (profile.Capacities().maximumConcurrentQueries > provider.maximumConcurrentQueries)
            return Result<void>::Failure(MakeError(NavigationErrors::ProjectProfileCapacityExceeded));
        return AdmitNavigationQuery(provider, expectedProviderRevision, profile.MaximumQuery());
    }
}  // namespace Horo::Navigation
