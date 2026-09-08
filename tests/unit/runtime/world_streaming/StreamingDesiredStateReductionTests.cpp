#include "Horo/WorldStreaming/StreamingDesiredStateReduction.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingTestUtils.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace Horo::WorldStreaming {
    namespace {
        using TestSupport::IdentityFrom;
        using TestSupport::RequireError;

        StreamingSourceDescriptor Source(const std::uint64_t id, const std::uint64_t revision = 1,
                                         const StreamingSourceOwnerToken owner = TestSupport::Owner(), const float priority = 1.0F) {
            return {.id = IdentityFrom<StreamingSourceId>(id),
                    .owner = owner,
                    .intent = StreamingSourceIntent::Camera,
                    .priority = StreamingSourcePriority::Create(priority).Value(),
                    .revision = IdentityFrom<StreamingSourceRevision>(revision)};
        }

        StreamingSourceDesiredState Desired(const std::uint64_t id, const StreamingDesiredResidency residency,
                                            const StreamingRetention retention = StreamingRetention::Releasable,
                                            const std::uint64_t revision = 1, const StreamingSourceOwnerToken owner = TestSupport::Owner(),
                                            const float priority = 1.0F) {
            auto result = StreamingSourceDesiredState::Create(Source(id, revision, owner, priority), residency, retention);
            REQUIRE(result.HasValue());
            return std::move(result).Value();
        }

        StreamingDesiredStateReductionContext ReductionContext(const PartitionEpoch epoch = IdentityFrom<PartitionEpoch>(1),
                                                               const WorldPartitionId partition = TestSupport::World()) {
            return {.partition = partition, .epoch = epoch, .cell = {4, -2, 7, 0, StreamingLayerId::Create(0).Value()}};
        }

        constexpr StreamingDesiredStateReductionLimits Limits{8};

        void CheckResidency(const Result<StreamingDesiredStateReduction> &result, const StreamingDesiredResidency effective,
                            const std::optional<StreamingDesiredResidency> pinnedFloor) {
            REQUIRE(result.HasValue());
            CHECK(result.Value().EffectiveResidency() == effective);
            CHECK(result.Value().PinnedResidencyFloor() == pinnedFloor);
        }

        TEST_CASE("Desired-state reduction represents empty and single-source demand explicitly",
                  "[unit][world_streaming][source_reduction]") {
            auto empty = StreamingDesiredStateReduction::Create(ReductionContext(), {}, Limits);
            REQUIRE(empty.HasValue());
            CHECK(empty.Value().EffectiveResidency() == StreamingDesiredResidency::Unloaded);
            CHECK_FALSE(empty.Value().PinnedResidencyFloor().has_value());
            CHECK(empty.Value().Contributors().empty());

            const std::array states{Desired(3, StreamingDesiredResidency::Loaded, StreamingRetention::Pinned)};
            auto single = StreamingDesiredStateReduction::Create(ReductionContext(), states, Limits);
            REQUIRE(single.HasValue());
            CHECK(single.Value().EffectiveResidency() == StreamingDesiredResidency::Loaded);
            REQUIRE(single.Value().PinnedResidencyFloor().has_value());
            CHECK(*single.Value().PinnedResidencyFloor() == StreamingDesiredResidency::Loaded);
            REQUIRE(single.Value().Contributors().size() == 1);
            CHECK(single.Value().Contributors().front().source == states.front().Source().id);
            static_assert(!std::is_copy_constructible_v<StreamingDesiredStateReduction>);
            static_assert(std::is_move_constructible_v<StreamingDesiredStateReduction>);
        }

        TEST_CASE("Desired-state reduction is independent of registration and iteration order",
                  "[unit][world_streaming][source_reduction]") {
            const std::array forward{
                Desired(1, StreamingDesiredResidency::Loaded, StreamingRetention::Pinned),
                Desired(2, StreamingDesiredResidency::Activated),
                Desired(3, StreamingDesiredResidency::Loaded),
            };
            const std::array reverse{forward[2], forward[1], forward[0]};

            auto first = StreamingDesiredStateReduction::Create(ReductionContext(), forward, Limits);
            auto second = StreamingDesiredStateReduction::Create(ReductionContext(), reverse, Limits);
            REQUIRE(first.HasValue());
            REQUIRE(second.HasValue());
            CHECK(first.Value().EffectiveResidency() == second.Value().EffectiveResidency());
            CHECK(first.Value().PinnedResidencyFloor() == second.Value().PinnedResidencyFloor());
            CHECK(std::ranges::equal(first.Value().Contributors(), second.Value().Contributors()));
            REQUIRE(first.Value().Contributors().size() == 3);
            CHECK(first.Value().Contributors()[0].source == forward[0].Source().id);
            CHECK(first.Value().Contributors()[1].source == forward[1].Source().id);
            CHECK(first.Value().Contributors()[2].source == forward[2].Source().id);
        }

        TEST_CASE("Desired-state reduction keeps effective residency separate from the pinned floor",
                  "[unit][world_streaming][source_reduction]") {
            const std::array states{
                Desired(1, StreamingDesiredResidency::Loaded, StreamingRetention::Pinned),
                Desired(2, StreamingDesiredResidency::Activated, StreamingRetention::Releasable),
            };

            auto result = StreamingDesiredStateReduction::Create(ReductionContext(), states, Limits);
            CheckResidency(result, StreamingDesiredResidency::Activated, StreamingDesiredResidency::Loaded);

            const std::array strongerPins{
                Desired(1, StreamingDesiredResidency::Loaded, StreamingRetention::Pinned),
                Desired(2, StreamingDesiredResidency::Activated, StreamingRetention::Pinned),
            };
            auto pinned = StreamingDesiredStateReduction::Create(ReductionContext(), strongerPins, Limits);
            REQUIRE(pinned.HasValue());
            REQUIRE(pinned.Value().PinnedResidencyFloor().has_value());
            CHECK(*pinned.Value().PinnedResidencyFloor() == StreamingDesiredResidency::Activated);
        }

        TEST_CASE("Desired-state reduction recomputes removal and replacement snapshots without hidden state",
                  "[unit][world_streaming][source_reduction]") {
            const std::array initial{Desired(1, StreamingDesiredResidency::Loaded), Desired(2, StreamingDesiredResidency::Activated)};
            auto combined = StreamingDesiredStateReduction::Create(ReductionContext(), initial, Limits);
            REQUIRE(combined.HasValue());
            CHECK(combined.Value().EffectiveResidency() == StreamingDesiredResidency::Activated);

            const std::array afterRemoval{initial.front()};
            auto lowered = StreamingDesiredStateReduction::Create(ReductionContext(), afterRemoval, Limits);
            REQUIRE(lowered.HasValue());
            CHECK(lowered.Value().EffectiveResidency() == StreamingDesiredResidency::Loaded);

            const std::array replacement{Desired(1, StreamingDesiredResidency::Activated, StreamingRetention::Releasable, 2)};
            auto replaced = StreamingDesiredStateReduction::Create(ReductionContext(), replacement, Limits);
            REQUIRE(replaced.HasValue());
            CHECK(replaced.Value().Contributors().front().revision == replacement.front().Source().revision);
            CHECK(replaced.Value().EffectiveResidency() == StreamingDesiredResidency::Activated);
        }

        TEST_CASE("Desired-state reduction rejects duplicate source identities at any revision",
                  "[unit][world_streaming][source_reduction]") {
            const std::array sameRevision{Desired(1, StreamingDesiredResidency::Loaded), Desired(1, StreamingDesiredResidency::Activated)};
            RequireError(StreamingDesiredStateReduction::Create(ReductionContext(), sameRevision, Limits),
                         WorldStreamingErrors::SourceReductionIdentityConflict);

            const std::array competingRevisions{Desired(1, StreamingDesiredResidency::Loaded, StreamingRetention::Releasable, 1),
                                                Desired(1, StreamingDesiredResidency::Activated, StreamingRetention::Releasable, 2)};
            RequireError(StreamingDesiredStateReduction::Create(ReductionContext(), competingRevisions, Limits),
                         WorldStreamingErrors::SourceReductionIdentityConflict);
        }

        TEST_CASE("Desired-state reduction rejects foreign partition owner lifetimes",
                  "[unit][world_streaming][source_reduction][lifecycle]") {
            auto staleEpochOwner = TestSupport::Owner(1, IdentityFrom<PartitionEpoch>(2));
            const std::array staleEpoch{Desired(1, StreamingDesiredResidency::Loaded, StreamingRetention::Releasable, 1, staleEpochOwner)};
            RequireError(StreamingDesiredStateReduction::Create(ReductionContext(), staleEpoch, Limits),
                         WorldStreamingErrors::SourceOwnerStale);

            auto foreignOwner = TestSupport::Owner();
            foreignOwner.partition = TestSupport::World(2);
            const std::array foreignPartition{
                Desired(1, StreamingDesiredResidency::Loaded, StreamingRetention::Releasable, 1, foreignOwner)};
            RequireError(StreamingDesiredStateReduction::Create(ReductionContext(), foreignPartition, Limits),
                         WorldStreamingErrors::SourceOwnerStale);
        }

        TEST_CASE("Desired-state reduction validates scope and contributor capacity transactionally",
                  "[unit][world_streaming][source_reduction]") {
            const std::array states{Desired(1, StreamingDesiredResidency::Loaded), Desired(2, StreamingDesiredResidency::Activated)};
            auto invalid = ReductionContext();
            invalid.partition = {};
            RequireError(StreamingDesiredStateReduction::Create(invalid, states, Limits), WorldStreamingErrors::SourceReductionInvalid);
            invalid = ReductionContext();
            invalid.epoch = {};
            RequireError(StreamingDesiredStateReduction::Create(invalid, states, Limits), WorldStreamingErrors::SourceReductionInvalid);
            invalid = ReductionContext();
            invalid.cell.layer = {};
            RequireError(StreamingDesiredStateReduction::Create(invalid, states, Limits), WorldStreamingErrors::SourceReductionInvalid);
            RequireError(StreamingDesiredStateReduction::Create(ReductionContext(), states, {0}),
                         WorldStreamingErrors::SourceReductionInvalid);
            RequireError(StreamingDesiredStateReduction::Create(ReductionContext(), states, {1}),
                         WorldStreamingErrors::SourceReductionCapacityExceeded);
            REQUIRE(StreamingDesiredStateReduction::Create(ReductionContext(), states, {2}).HasValue());
        }

        TEST_CASE("Desired-state reduction excludes priority from state algebra and preserves caller input",
                  "[unit][world_streaming][source_reduction]") {
            std::vector states{Desired(2, StreamingDesiredResidency::Loaded, StreamingRetention::Pinned, 1, TestSupport::Owner(), 99.0F),
                               Desired(1, StreamingDesiredResidency::Activated, StreamingRetention::Releasable, 1, TestSupport::Owner(),
                                       0.25F)};
            const auto firstSource = states.front().Source();
            const auto secondSource = states.back().Source();

            auto result = StreamingDesiredStateReduction::Create(ReductionContext(), states, Limits);
            CheckResidency(result, StreamingDesiredResidency::Activated, StreamingDesiredResidency::Loaded);
            CHECK(states.front().Source() == firstSource);
            CHECK(states.back().Source() == secondSource);
            CHECK(states.front().Residency() == StreamingDesiredResidency::Loaded);
            CHECK(states.back().Residency() == StreamingDesiredResidency::Activated);
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
