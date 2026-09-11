#include "Horo/Destruction/DestructionComposition.h"

#include <array>
#include <utility>

namespace Horo::Destruction {
    namespace {
        using Requirement = DestructionCapabilityRequirement;

        [[nodiscard]] constexpr bool IsKnown(const DestructionHostCapability value) noexcept {
            return value < DestructionHostCapability::Count;
        }

        [[nodiscard]] constexpr bool IsKnown(const DestructionCapabilityAvailability value) noexcept {
            return value < DestructionCapabilityAvailability::Count;
        }

        [[nodiscard]] constexpr bool IsKnown(const DestructionCompositionLifecycle value) noexcept {
            return value < DestructionCompositionLifecycle::Count;
        }

        [[nodiscard]] constexpr std::array<Requirement, DestructionHostCapabilityCount> Requirements(
            const Requirement physics, const Requirement vfx, const Requirement audio, const Requirement networking) noexcept {
            return {physics, vfx, audio, networking};
        }

        [[nodiscard]] constexpr DestructionProductProfilePolicy Policy(
            const DestructionProductProfile profile, const bool enabled, const DestructionFeatureTier tier,
            const DestructionReplicationIntent replication,
            const std::array<Requirement, DestructionHostCapabilityCount> &requirements) noexcept {
            return {profile, enabled, tier, replication, requirements};
        }

        [[nodiscard]] Result<std::array<DestructionCapabilityFact, DestructionHostCapabilityCount>> ValidateAndOrderFacts(
            const std::span<const DestructionCapabilityFact> facts) {
            if (facts.size() != DestructionHostCapabilityCount)
                return Result<std::array<DestructionCapabilityFact, DestructionHostCapabilityCount>>::Failure(
                    MakeError(DestructionErrors::CompositionInvalid));

            std::array<DestructionCapabilityFact, DestructionHostCapabilityCount> ordered{};
            std::array<bool, DestructionHostCapabilityCount> seen{};
            for (const DestructionCapabilityFact &fact : facts) {
                if (!IsKnown(fact.capability) || !IsKnown(fact.availability))
                    return Result<std::array<DestructionCapabilityFact, DestructionHostCapabilityCount>>::Failure(
                        MakeError(DestructionErrors::CompositionInvalid));
                const auto index = static_cast<std::size_t>(fact.capability);
                if (seen[index])
                    return Result<std::array<DestructionCapabilityFact, DestructionHostCapabilityCount>>::Failure(
                        MakeError(DestructionErrors::CompositionInvalid));
                const bool available = fact.availability == DestructionCapabilityAvailability::Available;
                if (available != fact.revision.IsValid())
                    return Result<std::array<DestructionCapabilityFact, DestructionHostCapabilityCount>>::Failure(
                        MakeError(DestructionErrors::CompositionInvalid));
                seen[index] = true;
                ordered[index] = fact;
            }
            return Result<std::array<DestructionCapabilityFact, DestructionHostCapabilityCount>>::Success(ordered);
        }
    }  // namespace

    /** @copydoc GetDestructionProductProfilePolicy */
    Result<DestructionProductProfilePolicy> GetDestructionProductProfilePolicy(const DestructionProductProfile profile) {
        using enum DestructionCapabilityRequirement;
        switch (profile) {
            case DestructionProductProfile::Null:
                return Result<DestructionProductProfilePolicy>::Success(Policy(profile, false, DestructionFeatureTier::Baseline,
                                                                               DestructionReplicationIntent::LocalAuthority,
                                                                               Requirements(Omitted, Omitted, Omitted, Omitted)));
            case DestructionProductProfile::Headless:
                return Result<DestructionProductProfilePolicy>::Success(Policy(profile, true, DestructionFeatureTier::Standard,
                                                                               DestructionReplicationIntent::LocalAuthority,
                                                                               Requirements(Required, Omitted, Omitted, Omitted)));
            case DestructionProductProfile::Editor:
                return Result<DestructionProductProfilePolicy>::Success(Policy(profile, true, DestructionFeatureTier::High,
                                                                               DestructionReplicationIntent::LocalAuthority,
                                                                               Requirements(Optional, Optional, Optional, Omitted)));
            case DestructionProductProfile::Standalone:
                return Result<DestructionProductProfilePolicy>::Success(Policy(profile, true, DestructionFeatureTier::Standard,
                                                                               DestructionReplicationIntent::LocalAuthority,
                                                                               Requirements(Required, Optional, Optional, Omitted)));
            case DestructionProductProfile::Client:
                return Result<DestructionProductProfilePolicy>::Success(Policy(profile, true, DestructionFeatureTier::Standard,
                                                                               DestructionReplicationIntent::ServerAuthoritative,
                                                                               Requirements(Required, Optional, Optional, Required)));
            case DestructionProductProfile::Server:
                return Result<DestructionProductProfilePolicy>::Success(Policy(profile, true, DestructionFeatureTier::Standard,
                                                                               DestructionReplicationIntent::ServerAuthoritative,
                                                                               Requirements(Required, Omitted, Omitted, Required)));
            case DestructionProductProfile::Count:
                break;
        }
        return Result<DestructionProductProfilePolicy>::Failure(MakeError(DestructionErrors::CompositionInvalid));
    }

    /** @copydoc DestructionComposition::Create */
    Result<DestructionComposition> DestructionComposition::Create(const DestructionCompositionRequest &request) {
        if (request.contractVersion != CurrentDestructionCompositionContractVersion || !request.revision.IsValid())
            return Result<DestructionComposition>::Failure(MakeError(DestructionErrors::CompositionInvalid));
        auto policy = GetDestructionProductProfilePolicy(request.profile);
        if (policy.HasError())
            return Result<DestructionComposition>::Failure(policy.ErrorValue());
        auto facts = ValidateAndOrderFacts(request.capabilities);
        if (facts.HasError())
            return Result<DestructionComposition>::Failure(facts.ErrorValue());

        std::array<DestructionCapabilityResolution, DestructionHostCapabilityCount> resolutions{};
        for (std::size_t index = 0; index < resolutions.size(); ++index) {
            const DestructionCapabilityFact &fact = facts.Value()[index];
            const Requirement requirement = policy.Value().requirements[index];
            if (requirement == Requirement::Required && fact.availability == DestructionCapabilityAvailability::Unavailable)
                return Result<DestructionComposition>::Failure(MakeError(DestructionErrors::CompositionCapabilityUnavailable));

            DestructionCapabilityResolutionState state = DestructionCapabilityResolutionState::Unavailable;
            DestructionHostCapabilityRevision revision{};
            if (requirement == Requirement::Omitted) {
                state = DestructionCapabilityResolutionState::Omitted;
            } else if (fact.availability == DestructionCapabilityAvailability::Available) {
                state = DestructionCapabilityResolutionState::Bound;
                revision = fact.revision;
            }
            resolutions[index] = {fact.capability, requirement, state, revision};
        }
        return Result<DestructionComposition>::Success(DestructionComposition{policy.Value(), request.revision, resolutions});
    }

    DestructionComposition::DestructionComposition(
        DestructionProductProfilePolicy policy, const DestructionCompositionRevision revision,
        std::array<DestructionCapabilityResolution, DestructionHostCapabilityCount> capabilities) noexcept
        : policy_(std::move(policy)), revision_(revision), capabilities_(std::move(capabilities)) {}

    /** @copydoc DestructionComposition::Policy */
    const DestructionProductProfilePolicy &DestructionComposition::Policy() const noexcept {
        return policy_;
    }

    /** @copydoc DestructionComposition::Revision */
    DestructionCompositionRevision DestructionComposition::Revision() const noexcept {
        return revision_;
    }

    /** @copydoc DestructionComposition::Capabilities */
    const std::array<DestructionCapabilityResolution, DestructionHostCapabilityCount> &DestructionComposition::Capabilities()
        const noexcept {
        return capabilities_;
    }

    /** @copydoc DestructionComposition::Resolve */
    Result<DestructionCapabilityResolution> DestructionComposition::Resolve(const DestructionHostCapability capability) const {
        if (!IsKnown(capability))
            return Result<DestructionCapabilityResolution>::Failure(MakeError(DestructionErrors::CompositionInvalid));
        return Result<DestructionCapabilityResolution>::Success(capabilities_[static_cast<std::size_t>(capability)]);
    }

    /** @copydoc ValidateDestructionCompositionAdmission */
    Result<void> ValidateDestructionCompositionAdmission(const DestructionComposition &composition,
                                                         const DestructionCompositionRevision currentRevision,
                                                         const DestructionCompositionLifecycle lifecycle) {
        if (!currentRevision.IsValid() || !IsKnown(lifecycle))
            return Result<void>::Failure(MakeError(DestructionErrors::CompositionInvalid));
        if (composition.Revision() != currentRevision)
            return Result<void>::Failure(MakeError(DestructionErrors::CompositionStale));
        if (lifecycle == DestructionCompositionLifecycle::Cancelling)
            return Result<void>::Failure(MakeError(DestructionErrors::CancelledBeforeCommit));
        if (lifecycle == DestructionCompositionLifecycle::ShuttingDown || lifecycle == DestructionCompositionLifecycle::Closed)
            return Result<void>::Failure(MakeError(DestructionErrors::ShutdownInProgress));
        return Result<void>::Success();
    }
}  // namespace Horo::Destruction
