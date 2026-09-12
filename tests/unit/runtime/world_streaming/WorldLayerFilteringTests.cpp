#include "Horo/WorldStreaming/WorldLayerFiltering.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingTestUtils.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <type_traits>
#include <utility>

namespace Horo::WorldStreaming {
    namespace {
        using TestSupport::IdentityFrom;
        using TestSupport::Layer;
        using TestSupport::RequireError;
        using TestSupport::StreamingLayerOwner;
        using TestSupport::World;
        using TestSupport::WorldOwner;

        WorldLayerControlOwner EditorOwner() {
            return {.world = WorldOwner(),
                    .kind = WorldLayerControlOwnerKind::EditorDocument,
                    .authority = IdentityFrom<WorldLayerControlOwnerId>(8),
                    .generation = IdentityFrom<WorldLayerControlOwnerGeneration>(1)};
        }

        WorldLayerFilterCandidate Candidate(const std::uint16_t layer, const WorldLayerResidencyPolicy residency,
                                            const WorldLayerAudience audience, const WorldLayerFlags flags) {
            return {.ownership = {.layer = Layer(layer),
                                  .revision = IdentityFrom<WorldLayerRevision>(layer + 1),
                                  .placement = WorldLayerPlacement::Spatial,
                                  .residency = residency,
                                  .audience = audience,
                                  .owner = audience == WorldLayerAudience::EditorOnly ? EditorOwner() : StreamingLayerOwner()},
                    .flags = flags};
        }

        WorldLayerFilterPolicy Policy(const WorldLayerExecutionTarget target,
                                      const WorldLayerOptionalPolicy optional = WorldLayerOptionalPolicy::Include,
                                      const std::uint64_t revision = 3) {
            return {.id = IdentityFrom<WorldLayerFilterPolicyId>(7),
                    .revision = IdentityFrom<WorldLayerFilterPolicyRevision>(revision),
                    .target = target,
                    .optional = optional};
        }

        WorldLayerFilterContext Context(const StreamingRuntimeOwnerToken world = WorldOwner()) {
            return {.expectedWorld = world,
                    .expectedPolicy = IdentityFrom<WorldLayerFilterPolicyId>(7),
                    .expectedPolicyRevision = IdentityFrom<WorldLayerFilterPolicyRevision>(3),
                    .maximumCandidates = 8,
                    .authorityState = WorldLayerFilterAuthorityState::Active};
        }

        std::array<WorldLayerFilterCandidate, 5> Candidates() {
            return {Candidate(1, WorldLayerResidencyPolicy::Persistent, WorldLayerAudience::Runtime, WorldLayerFlags::Persistent),
                    Candidate(2, WorldLayerResidencyPolicy::Streamed, WorldLayerAudience::EditorOnly, WorldLayerFlags::None),
                    Candidate(3, WorldLayerResidencyPolicy::Streamed, WorldLayerAudience::Runtime,
                              WorldLayerFlags::Optional | WorldLayerFlags::ServerOnly),
                    Candidate(4, WorldLayerResidencyPolicy::Streamed, WorldLayerAudience::Runtime, WorldLayerFlags::ClientOnly),
                    Candidate(5, WorldLayerResidencyPolicy::Streamed, WorldLayerAudience::Runtime, WorldLayerFlags::Optional)};
        }

        WorldLayerFilterDecision SentinelDecision() {
            return {.world = WorldOwner(),
                    .layer = Layer(99),
                    .ownershipRevision = IdentityFrom<WorldLayerRevision>(99),
                    .disposition = WorldLayerFilterDisposition::Included};
        }

        std::array<WorldLayerFilterDecision, 5> SentinelDecisions() {
            std::array<WorldLayerFilterDecision, 5> decisions{};
            decisions.fill(SentinelDecision());
            return decisions;
        }

        void BindWorld(std::array<WorldLayerFilterCandidate, 5> &candidates, const StreamingRuntimeOwnerToken world) {
            for (auto &candidate : candidates)
                candidate.ownership.owner.world = world;
        }

        void RequireWorldScopedResult(const StreamingRuntimeOwnerToken world) {
            const auto baselineCandidates = Candidates();
            auto candidates = baselineCandidates;
            BindWorld(candidates, world);
            std::array<WorldLayerFilterDecision, 5> decisions{};
            const auto result = FilterWorldLayers(Policy(WorldLayerExecutionTarget::Editor), candidates, Context(world), decisions);

            REQUIRE(result.HasValue());
            REQUIRE(result.Value().world == world);
            REQUIRE(decisions[0].world == world);
            REQUIRE(decisions[0].world != baselineCandidates[0].ownership.owner.world);
            REQUIRE(decisions[0].layer == baselineCandidates[0].ownership.layer);
            REQUIRE(decisions[0].ownershipRevision == baselineCandidates[0].ownership.revision);
        }

        TEST_CASE("Editor filtering preserves every authored stable layer identity", "[unit][world_streaming][layer_filter][editor]") {
            const auto candidates = Candidates();
            std::array<WorldLayerFilterDecision, 5> decisions{};
            const auto result = FilterWorldLayers(Policy(WorldLayerExecutionTarget::Editor), candidates, Context(), decisions);

            REQUIRE(result.HasValue());
            REQUIRE(result.Value().world == Context().expectedWorld);
            REQUIRE(result.Value().decisionCount == candidates.size());
            REQUIRE(result.Value().includedCount == candidates.size());
            for (std::size_t index = 0; index < candidates.size(); ++index) {
                REQUIRE(decisions[index].IsIncluded());
                REQUIRE(decisions[index].world == candidates[index].ownership.owner.world);
                REQUIRE(decisions[index].layer == candidates[index].ownership.layer);
                REQUIRE(decisions[index].ownershipRevision == candidates[index].ownership.revision);
            }
            static_assert(std::is_trivially_copyable_v<WorldLayerFilterDecision>);
        }

        TEST_CASE("Default layer-filter decisions are unresolved", "[unit][world_streaming][layer_filter][default]") {
            const WorldLayerFilterDecision decision{};
            REQUIRE_FALSE(decision.IsIncluded());
            REQUIRE(decision.disposition == WorldLayerFilterDisposition::Unresolved);
        }

        TEST_CASE("Filter results distinguish reused layers across runtime owners", "[unit][world_streaming][layer_filter][world_fence]") {
            RequireWorldScopedResult(WorldOwner(6));
        }

        TEST_CASE("Filter results distinguish reused layers across partition epochs",
                  "[unit][world_streaming][layer_filter][world_fence]") {
            RequireWorldScopedResult(WorldOwner(5, 2));
        }

        TEST_CASE("Filter results distinguish reused layers across world partitions",
                  "[unit][world_streaming][layer_filter][world_fence]") {
            auto world = WorldOwner();
            world.partition = World(2);
            RequireWorldScopedResult(world);
        }

        TEST_CASE("Client filtering excludes editor server and unsupported optional layers with typed reasons",
                  "[unit][world_streaming][layer_filter][client]") {
            const auto candidates = Candidates();
            std::array<WorldLayerFilterDecision, 5> decisions{};
            const auto result = FilterWorldLayers(Policy(WorldLayerExecutionTarget::ClientRuntime, WorldLayerOptionalPolicy::Exclude),
                                                  candidates, Context(), decisions);

            REQUIRE(result.HasValue());
            REQUIRE(result.Value().includedCount == 2);
            REQUIRE(decisions[0].disposition == WorldLayerFilterDisposition::Included);
            REQUIRE(decisions[1].disposition == WorldLayerFilterDisposition::ExcludedEditorOnly);
            REQUIRE(decisions[2].disposition == WorldLayerFilterDisposition::ExcludedServerOnly);
            REQUIRE(decisions[3].disposition == WorldLayerFilterDisposition::Included);
            REQUIRE(decisions[4].disposition == WorldLayerFilterDisposition::ExcludedOptional);
        }

        TEST_CASE("Dedicated server filtering excludes editor and client-only layers without renaming sources",
                  "[unit][world_streaming][layer_filter][server]") {
            const auto candidates = Candidates();
            std::array<WorldLayerFilterDecision, 5> decisions{};
            const auto result =
                FilterWorldLayers(Policy(WorldLayerExecutionTarget::DedicatedServerRuntime), candidates, Context(), decisions);

            REQUIRE(result.HasValue());
            REQUIRE(result.Value().includedCount == 3);
            REQUIRE(decisions[0].disposition == WorldLayerFilterDisposition::Included);
            REQUIRE(decisions[1].disposition == WorldLayerFilterDisposition::ExcludedEditorOnly);
            REQUIRE(decisions[2].disposition == WorldLayerFilterDisposition::Included);
            REQUIRE(decisions[3].disposition == WorldLayerFilterDisposition::ExcludedClientOnly);
            REQUIRE(decisions[4].disposition == WorldLayerFilterDisposition::Included);
            REQUIRE(decisions[2].layer == Layer(3));
        }

        TEST_CASE("Filtering rejects stale world and policy evidence before touching output",
                  "[unit][world_streaming][layer_filter][stale]") {
            const auto candidates = Candidates();
            auto decisions = SentinelDecisions();
            const auto sentinel = decisions;

            auto context = Context();
            context.expectedPolicyRevision = IdentityFrom<WorldLayerFilterPolicyRevision>(4);
            RequireError(FilterWorldLayers(Policy(WorldLayerExecutionTarget::Editor), candidates, context, decisions),
                         WorldStreamingErrors::LayerFilterStale);
            REQUIRE(decisions == sentinel);

            auto staleCandidates = candidates;
            staleCandidates[0].ownership.owner.world = WorldOwner(6);
            RequireError(FilterWorldLayers(Policy(WorldLayerExecutionTarget::Editor), staleCandidates, Context(), decisions),
                         WorldStreamingErrors::LayerFilterStale);
            REQUIRE(decisions == sentinel);
        }

        TEST_CASE("Filtering validates the complete canonical snapshot before publication",
                  "[unit][world_streaming][layer_filter][transaction]") {
            auto candidates = Candidates();
            auto decisions = SentinelDecisions();
            const auto sentinel = decisions;

            candidates[2].ownership.layer = candidates[1].ownership.layer;
            RequireError(FilterWorldLayers(Policy(WorldLayerExecutionTarget::Editor), candidates, Context(), decisions),
                         WorldStreamingErrors::LayerFilterIdentityConflict);
            REQUIRE(decisions == sentinel);

            candidates = Candidates();
            std::swap(candidates[1], candidates[2]);
            RequireError(FilterWorldLayers(Policy(WorldLayerExecutionTarget::Editor), candidates, Context(), decisions),
                         WorldStreamingErrors::LayerFilterInvalid);
            REQUIRE(decisions == sentinel);

            const std::span<WorldLayerFilterDecision> shortOutput{decisions.data(), decisions.size() - 1};
            candidates = Candidates();
            RequireError(FilterWorldLayers(Policy(WorldLayerExecutionTarget::Editor), candidates, Context(), shortOutput),
                         WorldStreamingErrors::LayerFilterCapacityExceeded);
            REQUIRE(decisions == sentinel);
        }

        TEST_CASE("Filtering rejects contradictory flags and classification", "[unit][world_streaming][layer_filter][policy]") {
            auto candidates = Candidates();
            std::array<WorldLayerFilterDecision, 5> decisions{};

            candidates[0].flags = WorldLayerFlags::None;
            RequireError(FilterWorldLayers(Policy(WorldLayerExecutionTarget::Editor), candidates, Context(), decisions),
                         WorldStreamingErrors::LayerFilterUnsupported);

            candidates = Candidates();
            candidates[3].flags = WorldLayerFlags::ServerOnly | WorldLayerFlags::ClientOnly;
            RequireError(FilterWorldLayers(Policy(WorldLayerExecutionTarget::Editor), candidates, Context(), decisions),
                         WorldStreamingErrors::LayerFilterUnsupported);

            candidates = Candidates();
            candidates[3].flags = static_cast<WorldLayerFlags>(1U << 31U);
            RequireError(FilterWorldLayers(Policy(WorldLayerExecutionTarget::Editor), candidates, Context(), decisions),
                         WorldStreamingErrors::LayerFilterUnsupported);

            auto policy = Policy(WorldLayerExecutionTarget::Editor);
            policy.target = static_cast<WorldLayerExecutionTarget>(255);
            RequireError(FilterWorldLayers(policy, candidates, Context(), decisions), WorldStreamingErrors::LayerFilterUnsupported);
        }

        TEST_CASE("Filtering enforces candidate capacity cancellation and shutdown", "[unit][world_streaming][layer_filter][lifecycle]") {
            const auto candidates = Candidates();
            std::array<WorldLayerFilterDecision, 5> decisions{};
            auto context = Context();
            context.maximumCandidates = candidates.size() - 1;
            RequireError(FilterWorldLayers(Policy(WorldLayerExecutionTarget::Editor), candidates, context, decisions),
                         WorldStreamingErrors::LayerFilterCapacityExceeded);

            for (const auto state : {WorldLayerFilterAuthorityState::Cancelling, WorldLayerFilterAuthorityState::Closed}) {
                context = Context();
                context.authorityState = state;
                RequireError(FilterWorldLayers(Policy(WorldLayerExecutionTarget::Editor), candidates, context, decisions),
                             WorldStreamingErrors::LayerFilterLifecycleUnavailable);
            }

            context = Context();
            context.maximumCandidates = 0;
            RequireError(FilterWorldLayers(Policy(WorldLayerExecutionTarget::Editor), candidates, context, decisions),
                         WorldStreamingErrors::LayerFilterInvalid);
        }

        TEST_CASE("Filter policy replacement is identity stable lifecycle gated and non-wrapping",
                  "[unit][world_streaming][layer_filter][replacement]") {
            const auto current = Policy(WorldLayerExecutionTarget::Editor, WorldLayerOptionalPolicy::Include, 8);
            auto replacement = Policy(WorldLayerExecutionTarget::ClientRuntime, WorldLayerOptionalPolicy::Exclude, 9);
            REQUIRE(ValidateWorldLayerFilterPolicyReplacement(current, replacement, WorldLayerFilterAuthorityState::Active).HasValue());

            replacement.revision = IdentityFrom<WorldLayerFilterPolicyRevision>(10);
            RequireError(ValidateWorldLayerFilterPolicyReplacement(current, replacement, WorldLayerFilterAuthorityState::Active),
                         WorldStreamingErrors::LayerFilterStale);
            replacement = Policy(WorldLayerExecutionTarget::ClientRuntime, WorldLayerOptionalPolicy::Exclude, 9);
            replacement.id = IdentityFrom<WorldLayerFilterPolicyId>(8);
            RequireError(ValidateWorldLayerFilterPolicyReplacement(current, replacement, WorldLayerFilterAuthorityState::Active),
                         WorldStreamingErrors::LayerFilterIdentityConflict);
            replacement = Policy(WorldLayerExecutionTarget::ClientRuntime, WorldLayerOptionalPolicy::Exclude, 9);
            RequireError(ValidateWorldLayerFilterPolicyReplacement(current, replacement, WorldLayerFilterAuthorityState::Cancelling),
                         WorldStreamingErrors::LayerFilterLifecycleUnavailable);

            const auto maximum = IdentityFrom<WorldLayerFilterPolicyRevision>(std::numeric_limits<std::uint64_t>::max());
            auto exhausted = current;
            exhausted.revision = maximum;
            replacement.revision = IdentityFrom<WorldLayerFilterPolicyRevision>(1);
            RequireError(ValidateWorldLayerFilterPolicyReplacement(exhausted, replacement, WorldLayerFilterAuthorityState::Active),
                         WorldStreamingErrors::GenerationExhausted);
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
