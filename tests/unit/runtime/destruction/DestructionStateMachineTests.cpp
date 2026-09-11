#include "Horo/Destruction/DestructionStateMachine.h"
#include "support/AllocationProbe.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <future>
#include <limits>
#include <string_view>
#include <utility>

namespace Horo::Destruction {
    namespace {
        template <typename Identity> Identity Id(const std::uint64_t value) {
            return Identity::Create(value).Value();
        }

        FractureArtifactContentIdentity Content(const std::uint8_t suffix = 1) {
            std::array<std::uint8_t, 16> assetBytes{};
            assetBytes.front() = suffix;
            const auto asset = FractureAssetId::Create(Assets::AssetId::FromBytes(assetBytes)).Value();
            Sha256Digest digest{};
            digest.bytes.front() = suffix;
            return FractureArtifactContentIdentity::Create(asset, Id<FractureContentRevision>(suffix), digest).Value();
        }

        DestructibleDescriptor Descriptor(const std::uint8_t content = 1, const std::uint64_t configurationRevision = 3) {
            const auto profile = GetDestructionTierProfile(DestructionFeatureTier::Baseline).Value();
            DestructibleDescriptorData data{.destructible = Id<DestructibleId>(11),
                                            .content = Content(content),
                                            .configurationRevision = Id<DestructionConfigurationRevision>(configurationRevision),
                                            .features = {.required = {.bits = DestructionFeatureBit<DestructionFeature::PreCookedFracture> |
                                                                              DestructionFeatureBit<DestructionFeature::CookedSupport>}},
                                            .limits = profile.limits};
            auto descriptor = DestructibleDescriptor::Create(data);
            REQUIRE(descriptor.HasValue());
            return descriptor.Value();
        }

        DestructionHandle Handle(const std::uint64_t generation = 1, const std::uint64_t world = 7, const std::uint64_t destructible = 11) {
            return {Id<DestructionWorldId>(world), Id<DestructibleId>(destructible), Id<DestructionGeneration>(generation)};
        }

        DestructionCommandId CommandId(const std::uint64_t value, const DestructionHandle target = Handle()) {
            return {target, Id<DestructionCommandValue>(value)};
        }

        DestructionStateMachine Machine(const DestructionHandle target = Handle(), const std::uint64_t revision = 1) {
            auto machine = DestructionStateMachine::Create(Descriptor(), target, Id<DestructionStateRevision>(revision));
            REQUIRE(machine.HasValue());
            return machine.Value();
        }

        template <typename Value> void CheckError(const Result<Value> &result, const ErrorCodeDescriptor &expected) {
            REQUIRE(result.HasError());
            CHECK((result.ErrorValue().domain.Value() == expected.domain.Value() &&
                   result.ErrorValue().code.Value() == expected.code.Value()));
        }

        DestructionStateMachine CommitDamage(const DestructionStateMachine &machine, const std::uint64_t commandValue, const float damage) {
            auto command =
                DestructionStateCommand::Damage(CommandId(commandValue, machine.Snapshot().target), machine.Snapshot().revision, damage);
            REQUIRE(command.HasValue());
            auto transition = machine.Prepare(command.Value());
            REQUIRE(transition.HasValue());
            auto committed = machine.Commit(transition.Value());
            REQUIRE(committed.HasValue());
            return committed.Value();
        }
    }  // namespace

    TEST_CASE("Destruction state creation captures exact descriptor and generation evidence", "[unit][destruction][state]") {
        const auto descriptor = Descriptor(4, 9);
        const auto target = Handle(2);
        const auto machine = DestructionStateMachine::Create(descriptor, target, Id<DestructionStateRevision>(6));
        REQUIRE(machine.HasValue());

        const auto &snapshot = machine.Value().Snapshot();
        CHECK(snapshot.target == target);
        CHECK(snapshot.content == descriptor.Data().content);
        CHECK(snapshot.configurationRevision == descriptor.Data().configurationRevision);
        CHECK(snapshot.effectiveFeatures == descriptor.EffectiveFeatures());
        CHECK(snapshot.revision == Id<DestructionStateRevision>(6));
        CHECK(snapshot.phase == DestructionStatePhase::Intact);
        CHECK(snapshot.health == descriptor.Data().health.maximumHealth);
        CHECK(machine.Value().IsAdmissionOpen());
    }

    TEST_CASE("Damage advances one revision and crosses exact semantic boundaries", "[unit][destruction][state]") {
        auto machine = Machine();
        machine = CommitDamage(machine, 1, 24.0F);
        CHECK(machine.Snapshot().phase == DestructionStatePhase::Intact);
        CHECK(machine.Snapshot().health == 76.0F);
        CHECK(machine.Snapshot().revision == Id<DestructionStateRevision>(2));

        machine = CommitDamage(machine, 2, 1.0F);
        CHECK(machine.Snapshot().phase == DestructionStatePhase::Damaged);
        CHECK(machine.Snapshot().health == 75.0F);

        machine = CommitDamage(machine, 3, 75.0F);
        CHECK(machine.Snapshot().phase == DestructionStatePhase::Destroyed);
        CHECK(machine.Snapshot().health == 0.0F);
        CHECK(machine.Snapshot().revision == Id<DestructionStateRevision>(4));
    }

    TEST_CASE("Explicit destruction is atomic and terminal", "[unit][destruction][state]") {
        const auto machine = Machine();
        const auto destroy = DestructionStateCommand::Destroy(CommandId(1), machine.Snapshot().revision).Value();
        const auto transition = machine.Prepare(destroy);
        REQUIRE(transition.HasValue());
        CHECK(transition.Value().Successor().phase == DestructionStatePhase::Destroyed);
        CHECK(transition.Value().Successor().health == 0.0F);

        const auto destroyed = machine.Commit(transition.Value());
        REQUIRE(destroyed.HasValue());
        const auto later = DestructionStateCommand::Destroy(CommandId(2), destroyed.Value().Snapshot().revision).Value();
        CheckError(destroyed.Value().Prepare(later), DestructionErrors::StateTerminal);
    }

    TEST_CASE("Damage commands reject invalid identity revision and numeric payload", "[unit][destruction][state]") {
        CheckError(DestructionStateCommand::Damage({}, Id<DestructionStateRevision>(1), 1.0F), DestructionErrors::IdentityInvalid);
        CheckError(DestructionStateCommand::Damage(CommandId(1), {}, 1.0F), DestructionErrors::IdentityInvalid);
        for (const float invalid : {0.0F, -1.0F, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
            CheckError(DestructionStateCommand::Damage(CommandId(1), Id<DestructionStateRevision>(1), invalid),
                       DestructionErrors::InvalidDamage);
        }
    }

    TEST_CASE("Exact command retries are idempotent and conflicting reuse is rejected", "[unit][destruction][state]") {
        const auto machine = Machine();
        const auto command = DestructionStateCommand::Damage(CommandId(1), machine.Snapshot().revision, 10.0F).Value();
        const auto transition = machine.Prepare(command).Value();
        const auto committed = machine.Commit(transition).Value();

        const auto duplicate = committed.Prepare(command);
        REQUIRE(duplicate.HasValue());
        CHECK(duplicate.Value().Status() == DestructionTransitionStatus::Duplicate);
        CHECK(duplicate.Value().Successor() == committed.Snapshot());
        const auto repeated = committed.Commit(transition);
        REQUIRE(repeated.HasValue());
        CHECK(repeated.Value().Snapshot() == committed.Snapshot());

        const auto conflicting = DestructionStateCommand::Damage(CommandId(1), command.ExpectedRevision(), 11.0F).Value();
        CheckError(committed.Prepare(conflicting), DestructionErrors::DuplicateCommand);

        const auto second = CommitDamage(committed, 2, 1.0F);
        const auto reused = DestructionStateCommand::Damage(CommandId(1), second.Snapshot().revision, 1.0F).Value();
        CheckError(second.Prepare(reused), DestructionErrors::DuplicateCommand);
    }

    TEST_CASE("Concurrent candidates from one revision cannot both commit", "[unit][destruction][state][concurrency]") {
        const auto machine = Machine();
        const auto first = DestructionStateCommand::Damage(CommandId(1), machine.Snapshot().revision, 10.0F).Value();
        const auto second = DestructionStateCommand::Damage(CommandId(2), machine.Snapshot().revision, 20.0F).Value();

        auto firstFuture = std::async(std::launch::async, [&machine, first] {
            return machine.Prepare(first);
        });
        auto secondFuture = std::async(std::launch::async, [&machine, second] {
            return machine.Prepare(second);
        });
        const auto firstCandidate = firstFuture.get();
        const auto secondCandidate = secondFuture.get();
        REQUIRE(firstCandidate.HasValue());
        REQUIRE(secondCandidate.HasValue());

        const auto committed = machine.Commit(firstCandidate.Value());
        REQUIRE(committed.HasValue());
        CheckError(committed.Value().Commit(secondCandidate.Value()), DestructionErrors::StaleRevision);
        CHECK(committed.Value().Snapshot().health == 90.0F);
    }

    TEST_CASE("Cancellation is idempotent rollback before publication", "[unit][destruction][state][lifecycle]") {
        const auto machine = Machine();
        const auto command = DestructionStateCommand::Damage(CommandId(1), machine.Snapshot().revision, 20.0F).Value();
        const auto candidate = machine.Prepare(command).Value();
        const auto cancelled = candidate.Cancel().Cancel();
        CHECK(cancelled.Status() == DestructionTransitionStatus::Cancelled);
        CheckError(machine.Commit(cancelled), DestructionErrors::CancelledBeforeCommit);
        CHECK(machine.Snapshot().health == 100.0F);
        CHECK(machine.Snapshot().revision == Id<DestructionStateRevision>(1));
    }

    TEST_CASE("Replacement resets semantic state and invalidates old generation completions", "[unit][destruction][state][lifecycle]") {
        const auto machine = CommitDamage(Machine(), 1, 50.0F);
        const auto oldCommand = DestructionStateCommand::Damage(CommandId(2), machine.Snapshot().revision, 10.0F).Value();
        const auto oldCandidate = machine.Prepare(oldCommand).Value();
        const auto replacementHandle = Handle(2);
        const auto replacementDescriptor = Descriptor(5, 4);
        const auto replacement = machine.Replace(replacementHandle, replacementDescriptor);
        REQUIRE(replacement.HasValue());
        CHECK(replacement.Value().Snapshot().target == replacementHandle);
        CHECK(replacement.Value().Snapshot().content == replacementDescriptor.Data().content);
        CHECK(replacement.Value().Snapshot().configurationRevision == replacementDescriptor.Data().configurationRevision);
        CHECK(replacement.Value().Snapshot().revision == Id<DestructionStateRevision>(1));
        CHECK(replacement.Value().Snapshot().phase == DestructionStatePhase::Intact);
        CHECK(replacement.Value().Snapshot().health == replacementDescriptor.Data().health.maximumHealth);
        CheckError(replacement.Value().Commit(oldCandidate), DestructionErrors::StaleGeneration);

        CheckError(machine.Replace(Handle(3), replacementDescriptor), DestructionErrors::StaleGeneration);
        CheckError(machine.Replace(Handle(2, 8), replacementDescriptor), DestructionErrors::IdentityUnknown);
    }

    TEST_CASE("Shutdown closes mutation and replacement while preserving the last snapshot", "[unit][destruction][state][lifecycle]") {
        const auto machine = Machine();
        const auto command = DestructionStateCommand::Damage(CommandId(1), machine.Snapshot().revision, 10.0F).Value();
        const auto candidate = machine.Prepare(command).Value();
        const auto closed = machine.BeginShutdown().BeginShutdown();
        CHECK_FALSE(closed.IsAdmissionOpen());
        CHECK(closed.Snapshot() == machine.Snapshot());
        CheckError(closed.Prepare(command), DestructionErrors::ShutdownInProgress);
        CheckError(closed.Commit(candidate), DestructionErrors::ShutdownInProgress);
        CheckError(closed.Replace(Handle(2), Descriptor(2, 4)), DestructionErrors::ShutdownInProgress);
    }

    TEST_CASE("Stale owner revision and exhausted revision fail without successor state", "[unit][destruction][state][boundary]") {
        const auto machine = Machine();
        const auto foreign = DestructionStateCommand::Damage(CommandId(1, Handle(1, 9)), machine.Snapshot().revision, 1.0F).Value();
        CheckError(machine.Prepare(foreign), DestructionErrors::IdentityUnknown);
        const auto stale = DestructionStateCommand::Damage(CommandId(2), Id<DestructionStateRevision>(2), 1.0F).Value();
        CheckError(machine.Prepare(stale), DestructionErrors::StaleRevision);

        const auto exhausted = Machine(Handle(), std::numeric_limits<std::uint64_t>::max());
        const auto command = DestructionStateCommand::Damage(CommandId(3), exhausted.Snapshot().revision, 1.0F).Value();
        CheckError(exhausted.Prepare(command), DestructionErrors::RevisionExhausted);
    }

    TEST_CASE("Successful steady-state transitions perform fixed work without heap allocation", "[unit][destruction][state][allocation]") {
        auto machine = Machine();
        const auto before = Tests::AllocationProbe::Count();
        bool succeeded = true;
        for (std::uint64_t index = 1; index <= 10; ++index) {
            auto command = DestructionStateCommand::Damage(CommandId(index), machine.Snapshot().revision, 1.0F);
            if (command.HasError()) {
                succeeded = false;
                break;
            }
            auto candidate = machine.Prepare(command.Value());
            if (candidate.HasError()) {
                succeeded = false;
                break;
            }
            auto committed = machine.Commit(candidate.Value());
            if (committed.HasError()) {
                succeeded = false;
                break;
            }
            machine = std::move(committed.Value());
        }
        const auto after = Tests::AllocationProbe::Count();
        CHECK(succeeded);
        CHECK(after == before);
        CHECK(machine.Snapshot().revision == Id<DestructionStateRevision>(11));
    }
}  // namespace Horo::Destruction
