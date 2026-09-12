#include "Horo/WorldStreaming/WorldLayerFiltering.h"

#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingInternal.h"

#include <limits>

namespace Horo::WorldStreaming {
    namespace {
        constexpr std::uint32_t KnownFlags =
            static_cast<std::uint32_t>(WorldLayerFlags::Persistent) | static_cast<std::uint32_t>(WorldLayerFlags::Optional) |
            static_cast<std::uint32_t>(WorldLayerFlags::ServerOnly) | static_cast<std::uint32_t>(WorldLayerFlags::ClientOnly);

        template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Internal::Failure<T>(descriptor);
        }

        [[nodiscard]] bool IsKnown(const WorldLayerExecutionTarget value) noexcept {
            return value >= WorldLayerExecutionTarget::Editor && value <= WorldLayerExecutionTarget::DedicatedServerRuntime;
        }

        [[nodiscard]] bool IsKnown(const WorldLayerOptionalPolicy value) noexcept {
            return value >= WorldLayerOptionalPolicy::Include && value <= WorldLayerOptionalPolicy::Exclude;
        }

        [[nodiscard]] bool IsKnown(const WorldLayerFilterAuthorityState value) noexcept {
            return value >= WorldLayerFilterAuthorityState::Active && value <= WorldLayerFilterAuthorityState::Closed;
        }

        [[nodiscard]] bool HasFlag(const WorldLayerFlags flags, const WorldLayerFlags flag) noexcept {
            return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(flag)) != 0;
        }

        [[nodiscard]] Result<void> ValidateCandidate(const WorldLayerFilterCandidate &candidate,
                                                     const StreamingRuntimeOwnerToken &expectedWorld) {
            if (const auto valid = ValidateWorldLayerOwnershipDescriptor(candidate.ownership); valid.HasError())
                return valid;
            if (candidate.ownership.owner.world != expectedWorld)
                return Failure<void>(WorldStreamingErrors::LayerFilterStale);
            const auto flags = static_cast<std::uint32_t>(candidate.flags);
            if ((flags & ~KnownFlags) != 0)
                return Failure<void>(WorldStreamingErrors::LayerFilterUnsupported);
            if (HasFlag(candidate.flags, WorldLayerFlags::ServerOnly) && HasFlag(candidate.flags, WorldLayerFlags::ClientOnly))
                return Failure<void>(WorldStreamingErrors::LayerFilterUnsupported);
            const bool persistentFlag = HasFlag(candidate.flags, WorldLayerFlags::Persistent);
            const bool persistentPolicy = candidate.ownership.residency == WorldLayerResidencyPolicy::Persistent;
            if (persistentFlag != persistentPolicy)
                return Failure<void>(WorldStreamingErrors::LayerFilterUnsupported);
            return Result<void>::Success();
        }

        [[nodiscard]] WorldLayerFilterDisposition Decide(const WorldLayerFilterPolicy &policy,
                                                         const WorldLayerFilterCandidate &candidate) noexcept {
            using enum WorldLayerExecutionTarget;
            if (policy.target != Editor && candidate.ownership.audience == WorldLayerAudience::EditorOnly)
                return WorldLayerFilterDisposition::ExcludedEditorOnly;
            if (policy.target == ClientRuntime && HasFlag(candidate.flags, WorldLayerFlags::ServerOnly))
                return WorldLayerFilterDisposition::ExcludedServerOnly;
            if (policy.target == DedicatedServerRuntime && HasFlag(candidate.flags, WorldLayerFlags::ClientOnly))
                return WorldLayerFilterDisposition::ExcludedClientOnly;
            if (policy.optional == WorldLayerOptionalPolicy::Exclude && HasFlag(candidate.flags, WorldLayerFlags::Optional))
                return WorldLayerFilterDisposition::ExcludedOptional;
            return WorldLayerFilterDisposition::Included;
        }
    }  // namespace

    /** @copydoc WorldLayerFilterPolicy::IsValid */
    bool WorldLayerFilterPolicy::IsValid() const noexcept {
        return id.IsValid() && revision.IsValid() && IsKnown(target) && IsKnown(optional);
    }

    /** @copydoc FilterWorldLayers */
    Result<WorldLayerFilterResult> FilterWorldLayers(const WorldLayerFilterPolicy &policy,
                                                     const std::span<const WorldLayerFilterCandidate> candidates,
                                                     const WorldLayerFilterContext &context,
                                                     const std::span<WorldLayerFilterDecision> decisions) {
        if (!policy.id.IsValid() || !policy.revision.IsValid() || !context.expectedWorld.IsValid() || !context.expectedPolicy.IsValid() ||
            !context.expectedPolicyRevision.IsValid() || context.maximumCandidates == 0 || !IsKnown(context.authorityState)) {
            return Failure<WorldLayerFilterResult>(WorldStreamingErrors::LayerFilterInvalid);
        }
        if (!IsKnown(policy.target) || !IsKnown(policy.optional))
            return Failure<WorldLayerFilterResult>(WorldStreamingErrors::LayerFilterUnsupported);
        if (policy.id != context.expectedPolicy || policy.revision != context.expectedPolicyRevision)
            return Failure<WorldLayerFilterResult>(WorldStreamingErrors::LayerFilterStale);
        if (context.authorityState != WorldLayerFilterAuthorityState::Active)
            return Failure<WorldLayerFilterResult>(WorldStreamingErrors::LayerFilterLifecycleUnavailable);
        if (candidates.size() > context.maximumCandidates || decisions.size() < candidates.size())
            return Failure<WorldLayerFilterResult>(WorldStreamingErrors::LayerFilterCapacityExceeded);

        for (std::size_t index = 0; index < candidates.size(); ++index) {
            if (const auto valid = ValidateCandidate(candidates[index], context.expectedWorld); valid.HasError())
                return Result<WorldLayerFilterResult>::Failure(valid.ErrorValue());
            if (index == 0)
                continue;
            const auto previous = candidates[index - 1].ownership.layer;
            const auto current = candidates[index].ownership.layer;
            if (current == previous)
                return Failure<WorldLayerFilterResult>(WorldStreamingErrors::LayerFilterIdentityConflict);
            if (current < previous)
                return Failure<WorldLayerFilterResult>(WorldStreamingErrors::LayerFilterInvalid);
        }

        std::size_t includedCount{};
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            const auto disposition = Decide(policy, candidates[index]);
            decisions[index] = {.world = candidates[index].ownership.owner.world,
                                .layer = candidates[index].ownership.layer,
                                .ownershipRevision = candidates[index].ownership.revision,
                                .disposition = disposition};
            includedCount += disposition == WorldLayerFilterDisposition::Included ? 1U : 0U;
        }
        return Result<WorldLayerFilterResult>::Success({.world = context.expectedWorld,
                                                        .policy = policy.id,
                                                        .revision = policy.revision,
                                                        .decisionCount = candidates.size(),
                                                        .includedCount = includedCount});
    }

    /** @copydoc ValidateWorldLayerFilterPolicyReplacement */
    Result<void> ValidateWorldLayerFilterPolicyReplacement(const WorldLayerFilterPolicy &current, const WorldLayerFilterPolicy &replacement,
                                                           const WorldLayerFilterAuthorityState authorityState) {
        if (!current.id.IsValid() || !current.revision.IsValid() || !replacement.id.IsValid() || !replacement.revision.IsValid() ||
            !IsKnown(authorityState)) {
            return Failure<void>(WorldStreamingErrors::LayerFilterInvalid);
        }
        if (!IsKnown(current.target) || !IsKnown(current.optional) || !IsKnown(replacement.target) || !IsKnown(replacement.optional))
            return Failure<void>(WorldStreamingErrors::LayerFilterUnsupported);
        if (authorityState != WorldLayerFilterAuthorityState::Active)
            return Failure<void>(WorldStreamingErrors::LayerFilterLifecycleUnavailable);
        if (replacement.id != current.id)
            return Failure<void>(WorldStreamingErrors::LayerFilterIdentityConflict);
        const auto next = NextWorldLayerFilterPolicyRevision(current.revision);
        if (next.HasError())
            return Result<void>::Failure(next.ErrorValue());
        if (replacement.revision != next.Value())
            return Failure<void>(WorldStreamingErrors::LayerFilterStale);
        return Result<void>::Success();
    }

    /** @copydoc NextWorldLayerFilterPolicyRevision */
    Result<WorldLayerFilterPolicyRevision> NextWorldLayerFilterPolicyRevision(const WorldLayerFilterPolicyRevision current) {
        if (!current.IsValid())
            return Failure<WorldLayerFilterPolicyRevision>(WorldStreamingErrors::IdentityInvalid);
        if (current.Value() == std::numeric_limits<std::uint64_t>::max())
            return Failure<WorldLayerFilterPolicyRevision>(WorldStreamingErrors::GenerationExhausted);
        return WorldLayerFilterPolicyRevision::Create(current.Value() + 1);
    }
}  // namespace Horo::WorldStreaming
