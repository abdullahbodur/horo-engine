#include "Horo/WorldStreaming/NetworkStreamingAuthority.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingTestUtils.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace Horo::WorldStreaming {
    namespace {
        using TestSupport::IdentityFrom;
        using TestSupport::RequireError;

        StreamingCellId Cell(const std::int32_t x = 4) {
            return {x, -2, 7, 0, TestSupport::Layer()};
        }

        StreamingRuntimeOwnerToken Owner(const PartitionEpoch epoch = IdentityFrom<PartitionEpoch>(3),
                                         const WorldPartitionId partition = TestSupport::World()) {
            return {.partition = partition, .epoch = epoch, .owner = IdentityFrom<StreamingRuntimeOwnerId>(9)};
        }

        NetworkStreamingAuthorityConfig Config(const std::uint32_t capacity = 2) {
            return {.session = IdentityFrom<NetworkStreamingPeerSessionId>(11), .localOwner = Owner(), .maximumTrackedCells = capacity};
        }

        NetworkStreamingIntentCommand Command(const std::uint64_t sequence, const StreamingCellId cell = Cell(),
                                              const NetworkStreamingServerIntent intent = NetworkStreamingServerIntent::RequireActive) {
            return {.session = Config().session,
                    .sequence = IdentityFrom<NetworkStreamingCommandSequence>(sequence),
                    .partition = Owner().partition,
                    .cell = cell,
                    .intent = intent};
        }

        StreamingFence Fence(const StreamingCellId cell = Cell(), const PartitionEpoch epoch = Owner().epoch,
                             const std::uint64_t generation = 5) {
            return {.partition = Owner().partition,
                    .epoch = epoch,
                    .cell = cell,
                    .generation = IdentityFrom<StreamingGeneration>(generation)};
        }

        NetworkStreamingReadinessReport Report(const std::uint64_t sequence, const NetworkStreamingClientReadiness readiness,
                                               const StreamingCellId cell = Cell()) {
            return {.session = Config().session,
                    .sequence = IdentityFrom<NetworkStreamingCommandSequence>(sequence),
                    .partition = Owner().partition,
                    .cell = cell,
                    .readiness = readiness};
        }

        NetworkStreamingAuthority Authority(const std::uint32_t capacity = 2) {
            auto result = NetworkStreamingAuthority::Create(Config(capacity));
            REQUIRE(result.HasValue());
            return std::move(result).Value();
        }

        TEST_CASE("Server relevance intent waits for client-local residency readiness",
                  "[unit][world_streaming][network_authority][headless]") {
            auto authority = Authority();
            auto admitted = authority.ApplyServerIntent(Command(1));
            REQUIRE(admitted.HasValue());
            REQUIRE(admitted.Value().has_value());
            CHECK(admitted.Value()->readiness == NetworkStreamingClientReadiness::Pending);
            CHECK_FALSE(admitted.Value()->localFence.has_value());
            CHECK(authority.Snapshot().front().command.intent == NetworkStreamingServerIntent::RequireActive);

            auto ready = Report(1, NetworkStreamingClientReadiness::Ready);
            ready.localFence = Fence();
            ready.localState = StreamingCellState::Active;
            auto published = authority.ApplyClientReadiness(Owner(), ready);
            REQUIRE(published.HasValue());
            CHECK(published.Value().readiness == NetworkStreamingClientReadiness::Ready);
            REQUIRE(published.Value().localFence.has_value());
            CHECK(published.Value().localFence->epoch == Owner().epoch);
            CHECK(authority.Snapshot().size() == 1);
        }

        TEST_CASE("Ready proof must satisfy the exact client-local owner fence and requested state",
                  "[unit][world_streaming][network_authority][headless]") {
            auto authority = Authority();
            REQUIRE(authority.ApplyServerIntent(Command(1)).HasValue());
            const auto before = authority.Snapshot().front();

            auto insufficient = Report(1, NetworkStreamingClientReadiness::Ready);
            insufficient.localFence = Fence();
            insufficient.localState = StreamingCellState::Resident;
            RequireError(authority.ApplyClientReadiness(Owner(), insufficient), WorldStreamingErrors::NetworkStreamingReadinessInvalid);

            auto staleEpoch = insufficient;
            staleEpoch.localState = StreamingCellState::Active;
            staleEpoch.localFence = Fence(Cell(), IdentityFrom<PartitionEpoch>(2));
            RequireError(authority.ApplyClientReadiness(Owner(), staleEpoch), WorldStreamingErrors::NetworkStreamingAuthorityStale);

            auto foreignCell = insufficient;
            foreignCell.localState = StreamingCellState::Active;
            foreignCell.localFence = Fence(Cell(8));
            RequireError(authority.ApplyClientReadiness(Owner(), foreignCell), WorldStreamingErrors::NetworkStreamingAuthorityStale);
            CHECK(authority.Snapshot().front() == before);
        }

        TEST_CASE("Peer-global sequences replace atomically and stale traffic cannot mutate the snapshot",
                  "[unit][world_streaming][network_authority][headless]") {
            auto authority = Authority();
            REQUIRE(authority.ApplyServerIntent(Command(1)).HasValue());

            auto unavailable = Report(1, NetworkStreamingClientReadiness::Unavailable);
            REQUIRE(authority.ApplyClientReadiness(Owner(), unavailable).HasValue());
            CHECK(authority.Snapshot().front().readiness == NetworkStreamingClientReadiness::Unavailable);

            auto replacement = Command(2, Cell(), NetworkStreamingServerIntent::RequireLoaded);
            REQUIRE(authority.ApplyServerIntent(replacement).HasValue());
            CHECK(authority.Snapshot().front().command == replacement);
            CHECK(authority.Snapshot().front().readiness == NetworkStreamingClientReadiness::Pending);

            const auto before = authority.Snapshot().front();
            RequireError(authority.ApplyServerIntent(Command(4, Cell(2))), WorldStreamingErrors::NetworkStreamingAuthorityStale);
            RequireError(authority.ApplyClientReadiness(Owner(), unavailable), WorldStreamingErrors::NetworkStreamingAuthorityStale);
            CHECK(authority.Snapshot().front() == before);
            REQUIRE(authority.LastSequence().has_value());
            CHECK(authority.LastSequence()->Value() == 2);
        }

        TEST_CASE("Capacity denial and unsupported values preserve the complete bounded snapshot",
                  "[unit][world_streaming][network_authority][headless]") {
            auto authority = Authority(1);
            REQUIRE(authority.ApplyServerIntent(Command(1)).HasValue());
            const auto before = authority.Snapshot().front();

            RequireError(authority.ApplyServerIntent(Command(2, Cell(2))), WorldStreamingErrors::NetworkStreamingAuthorityCapacityExceeded);
            CHECK(authority.Snapshot().size() == 1);
            CHECK(authority.Snapshot().front() == before);
            CHECK(authority.LastSequence()->Value() == 1);

            auto unsupported = Command(2);
            unsupported.intent = static_cast<NetworkStreamingServerIntent>(255);
            RequireError(authority.ApplyServerIntent(unsupported), WorldStreamingErrors::NetworkStreamingAuthorityUnsupported);
            CHECK(authority.Snapshot().front() == before);

            auto pending = Report(1, NetworkStreamingClientReadiness::Pending);
            RequireError(authority.ApplyClientReadiness(Owner(), pending), WorldStreamingErrors::NetworkStreamingAuthorityUnsupported);
            CHECK(authority.Snapshot().front() == before);
        }

        TEST_CASE("Release failure cancellation and shutdown stay protocol-local and bounded",
                  "[unit][world_streaming][network_authority][headless]") {
            auto authority = Authority();
            REQUIRE(authority.ApplyServerIntent(Command(1)).HasValue());

            auto failed = Report(1, NetworkStreamingClientReadiness::Failed);
            REQUIRE(authority.ApplyClientReadiness(Owner(), failed).HasValue());
            CHECK(authority.Snapshot().front().readiness == NetworkStreamingClientReadiness::Failed);

            auto release = authority.ApplyServerIntent(Command(2, Cell(), NetworkStreamingServerIntent::Release));
            REQUIRE(release.HasValue());
            CHECK_FALSE(release.Value().has_value());
            CHECK(authority.Snapshot().empty());

            REQUIRE(authority.ApplyServerIntent(Command(3, Cell(3))).HasValue());
            REQUIRE(authority.RequestCancellation(Owner()).HasValue());
            REQUIRE(authority.RequestCancellation(Owner()).HasValue());
            CHECK(authority.Lifecycle() == NetworkStreamingAuthorityLifecycle::Cancelling);
            RequireError(authority.ApplyServerIntent(Command(4, Cell(4))),
                         WorldStreamingErrors::NetworkStreamingAuthorityLifecycleUnavailable);
            CHECK(authority.Snapshot().size() == 1);

            REQUIRE(authority.Shutdown(Owner()).HasValue());
            REQUIRE(authority.Shutdown(Owner()).HasValue());
            CHECK(authority.Lifecycle() == NetworkStreamingAuthorityLifecycle::Closed);
            CHECK(authority.Snapshot().empty());
            RequireError(authority.ApplyClientReadiness(Owner(), Report(3, NetworkStreamingClientReadiness::Unavailable, Cell(3))),
                         WorldStreamingErrors::NetworkStreamingAuthorityLifecycleUnavailable);
        }

        TEST_CASE("Foreign peer world owner and malformed construction are rejected",
                  "[unit][world_streaming][network_authority][headless]") {
            auto invalid = Config();
            invalid.maximumTrackedCells = 0;
            RequireError(NetworkStreamingAuthority::Create(invalid), WorldStreamingErrors::NetworkStreamingAuthorityInvalid);
            invalid = Config(NetworkStreamingAuthorityConfig::MaximumTrackedCells + 1);
            RequireError(NetworkStreamingAuthority::Create(invalid), WorldStreamingErrors::NetworkStreamingAuthorityInvalid);

            auto authority = Authority();
            auto foreignPeer = Command(1);
            foreignPeer.session = IdentityFrom<NetworkStreamingPeerSessionId>(12);
            RequireError(authority.ApplyServerIntent(foreignPeer), WorldStreamingErrors::NetworkStreamingAuthorityStale);
            auto foreignWorld = Command(1);
            foreignWorld.partition = TestSupport::World(2);
            RequireError(authority.ApplyServerIntent(foreignWorld), WorldStreamingErrors::NetworkStreamingAuthorityStale);
            CHECK(authority.Snapshot().empty());
            CHECK_FALSE(authority.LastSequence().has_value());
        }

        TEST_CASE("Peer command sequence has one namespace and never wraps", "[unit][world_streaming][network_authority][headless]") {
            auto next = NextNetworkStreamingCommandSequence(IdentityFrom<NetworkStreamingCommandSequence>(41));
            REQUIRE(next.HasValue());
            CHECK(next.Value().Value() == 42);
            RequireError(NextNetworkStreamingCommandSequence(
                             IdentityFrom<NetworkStreamingCommandSequence>(std::numeric_limits<std::uint64_t>::max())),
                         WorldStreamingErrors::GenerationExhausted);

            auto authority = Authority();
            RequireError(authority.ApplyServerIntent(Command(2)), WorldStreamingErrors::NetworkStreamingAuthorityStale);
            CHECK_FALSE(authority.LastSequence().has_value());
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
