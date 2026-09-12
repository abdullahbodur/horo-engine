#include "Horo/Network/NetworkErrors.h"
#include "Horo/Network/ReplicationDescriptorRegistry.h"
#include "Horo/Network/ReplicationRoles.h"
#include "ReplicationDescriptorTestSupport.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string_view>
#include <thread>

namespace Horo::Network {
    namespace {
        NetworkSessionGeneration Session(const std::uint64_t value = 1) {
            return NetworkSessionGeneration::Create(value).Value();
        }

        NetworkPeerId Peer(const std::uint64_t value) {
            return NetworkPeerId::Create(value).Value();
        }

        ReplicationRoleRevision Revision(const std::uint64_t value) {
            return ReplicationRoleRevision::Create(value).Value();
        }

        ReplicationConditionId CustomCondition(const std::uint32_t value = 1) {
            return ReplicationConditionId::Create(value).Value();
        }

        NetworkObjectId Object(const std::uint64_t session = 1) {
            return NetworkObjectId::Create(ReplicationAuthorityEpoch::Create(session).Value(), 7, 3).Value();
        }

        ReplicationRoleBinding Server(const std::uint64_t revision = 1) {
            return {Revision(revision), Session(), Object(), TestSupport::SchemaId(11), {2, 4}, ReplicationExecutionRole::AuthorityServer,
                    std::nullopt,       Peer(10)};
        }

        ReplicationRoleBinding Autonomous(const std::uint64_t revision = 1) {
            return {Revision(revision), Session(), Object(), TestSupport::SchemaId(11), {2, 4}, ReplicationExecutionRole::AutonomousClient,
                    Peer(10),           Peer(10)};
        }

        ReplicationRoleBinding Simulated(const std::uint64_t revision = 1) {
            return {Revision(revision), Session(), Object(), TestSupport::SchemaId(11), {2, 4}, ReplicationExecutionRole::SimulatedClient,
                    Peer(20),           Peer(10)};
        }

        template <typename T> std::string_view ResultErrorCode(const Result<T> &result) {
            REQUIRE(result.HasError());
            return result.ErrorValue().code.Value();
        }

        template <typename T> void RequireError(const Result<T> &result, const ErrorCodeDescriptor &expected) {
            CHECK(ResultErrorCode(result) == expected.code.Value());
        }

        bool Visible(const ReplicationCondition condition, const ReplicationRoleBinding &recipient,
                     const ReplicationRecordKind record = ReplicationRecordKind::Update) {
            auto field = TestSupport::Field();
            field.condition = condition;
            const auto result = EvaluateReplicationCondition(field, recipient, record);
            REQUIRE(result.HasValue());
            return result.Value();
        }

        TEST_CASE("Replication conditions use pinned recipient role and record context", "[network][replication][roles]") {
            CHECK(Visible(ReplicationCondition::Always, Autonomous()));
            CHECK(Visible(ReplicationCondition::Always, Simulated()));
            CHECK(Visible(ReplicationCondition::InitialOnly, Autonomous(), ReplicationRecordKind::Spawn));
            CHECK_FALSE(Visible(ReplicationCondition::InitialOnly, Autonomous()));
            CHECK(Visible(ReplicationCondition::OwnerOnly, Autonomous()));
            CHECK_FALSE(Visible(ReplicationCondition::OwnerOnly, Simulated()));
            CHECK_FALSE(Visible(ReplicationCondition::SkipOwner, Autonomous()));
            CHECK(Visible(ReplicationCondition::SkipOwner, Simulated()));
            CHECK_FALSE(Visible(ReplicationCondition::SimulatedOnly, Autonomous()));
            CHECK(Visible(ReplicationCondition::SimulatedOnly, Simulated()));

            auto custom = TestSupport::Field();
            custom.condition = ReplicationCondition::Custom;
            custom.customCondition = CustomCondition();
            auto customResult = EvaluateReplicationCondition(custom, Autonomous(), ReplicationRecordKind::Update,
                                                             ReplicationCustomConditionEvidence{CustomCondition(), true});
            REQUIRE(customResult.HasValue());
            CHECK(customResult.Value());
            customResult = EvaluateReplicationCondition(custom, Autonomous(), ReplicationRecordKind::Update,
                                                        ReplicationCustomConditionEvidence{CustomCondition(), false});
            REQUIRE(customResult.HasValue());
            CHECK_FALSE(customResult.Value());
        }

        TEST_CASE("Custom conditions are typed descriptor and fingerprint inputs", "[network][replication][roles][custom]") {
            auto custom = TestSupport::Field();
            custom.condition = ReplicationCondition::Custom;
            RequireError(ValidateReplicationSchemaDescriptor(TestSupport::Schema(10, {custom}), TestSupport::Limits),
                         NetworkErrors::ReplicationDescriptorInvalid);

            custom.customCondition = CustomCondition(1);
            const std::array firstSchemas{TestSupport::Schema(10, {custom})};
            const auto first = BuildReplicationDescriptorSnapshot(firstSchemas, TestSupport::Limits);
            REQUIRE(first.HasValue());
            custom.customCondition = CustomCondition(2);
            const std::array secondSchemas{TestSupport::Schema(10, {custom})};
            const auto second = BuildReplicationDescriptorSnapshot(secondSchemas, TestSupport::Limits);
            REQUIRE(second.HasValue());
            CHECK(first.Value()->Fingerprint() != second.Value()->Fingerprint());

            RequireError(EvaluateReplicationCondition(custom, Autonomous(), ReplicationRecordKind::Update),
                         NetworkErrors::ReplicationRoleContextInvalid);
            RequireError(EvaluateReplicationCondition(custom, Autonomous(), ReplicationRecordKind::Update,
                                                      ReplicationCustomConditionEvidence{CustomCondition(1), true}),
                         NetworkErrors::ReplicationRoleContextInvalid);
        }

        TEST_CASE("Client ownership never grants canonical field write authority", "[network][replication][roles][authority]") {
            const auto field = TestSupport::Field();
            CHECK(AuthorizeReplicationFieldWrite(field, Server()).HasValue());
            RequireError(AuthorizeReplicationFieldWrite(field, Autonomous()), NetworkErrors::ReplicationAuthorityDenied);
            RequireError(AuthorizeReplicationFieldWrite(field, Simulated()), NetworkErrors::ReplicationAuthorityDenied);
        }

        TEST_CASE("Malformed role and condition declarations fail deterministically", "[network][replication][roles][validation]") {
            auto binding = Autonomous();
            binding.role = ReplicationExecutionRole::Standalone;
            CHECK_FALSE(binding.IsValid());
            binding = Autonomous();
            binding.autonomousOwner = Peer(20);
            CHECK_FALSE(binding.IsValid());
            binding = Simulated();
            binding.autonomousOwner = binding.localPeer;
            CHECK_FALSE(binding.IsValid());
            binding = Server();
            binding.localPeer = Peer(10);
            CHECK_FALSE(binding.IsValid());
            binding = Server();
            binding.schemaVersion = {};
            CHECK_FALSE(binding.IsValid());

            auto field = TestSupport::Field();
            field.condition = ReplicationCondition::Count;
            RequireError(EvaluateReplicationCondition(field, Autonomous(), ReplicationRecordKind::Update),
                         NetworkErrors::ReplicationRoleContextInvalid);
            RequireError(EvaluateReplicationCondition(TestSupport::Field(), Autonomous(), ReplicationRecordKind::Count),
                         NetworkErrors::ReplicationRoleContextInvalid);
            RequireError(EvaluateReplicationCondition(TestSupport::Field(), Server(), ReplicationRecordKind::Update),
                         NetworkErrors::ReplicationRoleContextInvalid);
        }

        TEST_CASE("Role and ownership changes publish only at the exact safe point", "[network][replication][roles][lifecycle]") {
            auto stateResult = ReplicationRoleState::Create(Autonomous());
            REQUIRE(stateResult.HasValue());
            auto state = std::move(stateResult).Value();
            auto next = Autonomous(2);
            next.role = ReplicationExecutionRole::SimulatedClient;
            next.autonomousOwner = Peer(20);
            REQUIRE(state.Stage({Revision(1), next}).HasValue());
            auto current = state.Snapshot();
            REQUIRE(current.HasValue());
            CHECK(current.Value().role == ReplicationExecutionRole::AutonomousClient);
            RequireError(state.Stage({Revision(1), next}), NetworkErrors::ReplicationRoleTransitionPending);
            RequireError(state.CommitAtSafePoint(Revision(3)), NetworkErrors::ReplicationRoleTransitionStale);
            REQUIRE(state.CommitAtSafePoint(Revision(2)).HasValue());
            current = state.Snapshot();
            REQUIRE(current.HasValue());
            CHECK(current.Value() == next);

            auto foreign = Autonomous(3);
            foreign.session = Session(2);
            RequireError(state.Stage({Revision(2), foreign}), NetworkErrors::ReplicationRoleContextInvalid);
            foreign = Autonomous(3);
            foreign.localPeer = Peer(11);
            foreign.autonomousOwner = foreign.localPeer;
            RequireError(state.Stage({Revision(2), foreign}), NetworkErrors::ReplicationRoleContextInvalid);
            RequireError(state.Stage({Revision(1), Autonomous(3)}), NetworkErrors::ReplicationRoleTransitionStale);
        }

        TEST_CASE("Role state rejects non-owner threads and late shutdown work", "[network][replication][roles][lifecycle]") {
            auto state = std::move(ReplicationRoleState::Create(Autonomous())).Value();
            std::optional<Result<ReplicationRoleBinding>> offThread;
            std::thread worker([&] {
                offThread.emplace(state.Snapshot());
            });
            worker.join();
            REQUIRE(offThread.has_value());
            RequireError(*offThread, NetworkErrors::ReplicationRoleWrongThread);

            REQUIRE(state.Shutdown().HasValue());
            REQUIRE(state.Shutdown().HasValue());
            RequireError(state.Snapshot(), NetworkErrors::ReplicationRoleShuttingDown);
            RequireError(state.Stage({Revision(1), Simulated(2)}), NetworkErrors::ReplicationRoleShuttingDown);
            RequireError(state.CommitAtSafePoint(Revision(2)), NetworkErrors::ReplicationRoleShuttingDown);
        }
    }  // namespace
}  // namespace Horo::Network
