#include "Horo/Destruction/DestructionCommand.h"
#include "support/AllocationProbe.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>

namespace Horo::Destruction {
    namespace {
        template <typename T> T Id(std::uint64_t value) {
            return T::Create(value).Value();
        }

        DestructionHandle Handle(std::uint64_t generation = 1) {
            return {Id<DestructionWorldId>(7), Id<DestructibleId>(11), Id<DestructionGeneration>(generation)};
        }

        constexpr std::uint32_t AllCapabilities = DestructionCommandCapabilityBit<DestructionCommandCapability::Damage> |
                                                  DestructionCommandCapabilityBit<DestructionCommandCapability::Impact> |
                                                  DestructionCommandCapabilityBit<DestructionCommandCapability::Explosion> |
                                                  DestructionCommandCapabilityBit<DestructionCommandCapability::Collision> |
                                                  DestructionCommandCapabilityBit<DestructionCommandCapability::Script> |
                                                  DestructionCommandCapabilityBit<DestructionCommandCapability::ExplicitFracture>;

        DestructionCommandHeader Header(std::uint64_t value = 1, DestructionHandle target = Handle(), std::uint64_t revision = 1) {
            return {.id = {target, Id<DestructionCommandValue>(value)},
                    .expectedRevision = Id<DestructionStateRevision>(revision),
                    .capabilityRevision = Id<DestructionCapabilityRevision>(3),
                    .eligibleSimulationTick = 20,
                    .authority = {.authority = Id<DestructionAuthorityId>(5),
                                  .revision = Id<DestructionAuthorityRevision>(2),
                                  .capabilities = {.bits = AllCapabilities}}};
        }

        FractureArtifactContentIdentity Content() {
            std::array<std::uint8_t, 16> bytes{};
            bytes.front() = 1;
            Sha256Digest digest{};
            digest.bytes.front() = 1;
            return FractureArtifactContentIdentity::Create(FractureAssetId::Create(Assets::AssetId::FromBytes(bytes)).Value(),
                                                           Id<FractureContentRevision>(1), digest)
                .Value();
        }

        DestructibleDescriptor Descriptor(DestructionTriggerPolicy trigger = DestructionTriggerPolicy::DamageAndContact) {
            auto data = DestructibleDescriptorData{};
            data.destructible = Id<DestructibleId>(11);
            data.content = Content();
            data.configurationRevision = Id<DestructionConfigurationRevision>(4);
            data.features.required.bits =
                DestructionFeatureBit<DestructionFeature::PreCookedFracture> | DestructionFeatureBit<DestructionFeature::CookedSupport>;
            data.limits = GetDestructionTierProfile(DestructionFeatureTier::Baseline).Value().limits;
            data.behavior.trigger = trigger;
            return DestructibleDescriptor::Create(data).Value();
        }

        DestructionStateSnapshot Snapshot(const DestructibleDescriptor &descriptor) {
            return {.target = Handle(),
                    .content = descriptor.Data().content,
                    .configurationRevision = descriptor.Data().configurationRevision,
                    .effectiveFeatures = descriptor.EffectiveFeatures(),
                    .revision = Id<DestructionStateRevision>(1),
                    .phase = DestructionStatePhase::Intact,
                    .health = descriptor.Data().health.maximumHealth};
        }

        template <typename T> void ErrorIs(const Result<T> &result, const ErrorCodeDescriptor &expected) {
            if (result.HasValue()) {
                FAIL("Expected a typed destruction command error");
                return;
            }
            CHECK(expected.code.Value() == result.ErrorValue().code.Value());
        }
    }  // namespace

    TEST_CASE("Damage and fracture command kinds preserve fixed typed payloads", "[destruction][command]") {
        const auto impact = DestructionCommand::Impact(Header(), {1, 2, 3}, {0, 1, 0}, 25, 10).Value();
        CHECK(impact.Kind() == DestructionCommandKind::Impact);
        CHECK(impact.Payload().position == Math::Vec3{1, 2, 3});
        CHECK(impact.RequiredCapability() == DestructionCommandCapability::Impact);
        CHECK(DestructionCommand::Explosion(Header(2), {}, 7, 8, DestructionExplosionFalloff::Linear).Value().RequiredCapability() ==
              DestructionCommandCapability::Explosion);
        CHECK(DestructionCommand::Collision(Header(3), {}, {0, 0, 1}, 11, 12).Value().RequiredCapability() ==
              DestructionCommandCapability::Collision);
        CHECK(DestructionCommand::Damage(Header(4), 13).Value().RequiredCapability() == DestructionCommandCapability::Damage);
        CHECK(DestructionCommand::Script(Header(5), DestructionScriptIntent::ApplyDamage, 14).Value().RequiredCapability() ==
              DestructionCommandCapability::Damage);
        CHECK(DestructionCommand::Script(Header(6), DestructionScriptIntent::Fracture).Value().RequiredCapability() ==
              DestructionCommandCapability::ExplicitFracture);
    }

    TEST_CASE("Commands reject malformed and non-finite input", "[destruction][command][boundary]") {
        auto header = Header();
        header.eligibleSimulationTick = 0;
        ErrorIs(DestructionCommand::Damage(header, 1), DestructionErrors::CommandInvalid);
        const float inf = std::numeric_limits<float>::infinity();
        ErrorIs(DestructionCommand::Impact(Header(), {}, {}, 1, 1), DestructionErrors::CommandInvalid);
        ErrorIs(DestructionCommand::Impact(Header(), {inf, 0, 0}, {0, 1, 0}, 1, 1), DestructionErrors::CommandInvalid);
        ErrorIs(DestructionCommand::Explosion(Header(), {}, 0, 1, DestructionExplosionFalloff::Linear), DestructionErrors::CommandInvalid);
        ErrorIs(DestructionCommand::Explosion(Header(), {}, 1, 1, static_cast<DestructionExplosionFalloff>(255)),
                DestructionErrors::CommandInvalid);
        ErrorIs(DestructionCommand::Damage(Header(), -1), DestructionErrors::CommandInvalid);
        ErrorIs(DestructionCommand::Damage(Header(), DestructionCommandHardLimits::Damage + 1.0F), DestructionErrors::CommandLimitExceeded);
        ErrorIs(DestructionCommand::Script(Header(), DestructionScriptIntent::Fracture, 1), DestructionErrors::CommandInvalid);
    }

    TEST_CASE("Admission validates exact fences authority policy and bounded input", "[destruction][command][admission]") {
        const auto descriptor = Descriptor();
        const auto snapshot = Snapshot(descriptor);
        const auto authority = Header().authority;
        const DestructionCommandLimits limits{.maximumDamage = 100, .maximumImpulse = 100, .maximumExplosionRadius = 50};
        const auto impact = DestructionCommand::Impact(Header(), {}, {1, 0, 0}, 25, 10).Value();
        CHECK(AdmitDestructionCommand(impact, snapshot, descriptor, Id<DestructionCapabilityRevision>(3), authority, limits, true)
                  .HasValue());
        ErrorIs(AdmitDestructionCommand(DestructionCommand::Damage(Header(2, Handle(), 2), 1).Value(), snapshot, descriptor,
                                        Id<DestructionCapabilityRevision>(3), authority, limits, true),
                DestructionErrors::StaleRevision);
        ErrorIs(AdmitDestructionCommand(DestructionCommand::Damage(Header(2, Handle(2)), 1).Value(), snapshot, descriptor,
                                        Id<DestructionCapabilityRevision>(3), authority, limits, true),
                DestructionErrors::StaleGeneration);
        auto deniedHeader = Header(3);
        deniedHeader.authority.capabilities.bits = DestructionCommandCapabilityBit<DestructionCommandCapability::Damage>;
        ErrorIs(AdmitDestructionCommand(DestructionCommand::Impact(deniedHeader, {}, {1, 0, 0}, 1, 1).Value(), snapshot, descriptor,
                                        Id<DestructionCapabilityRevision>(3), deniedHeader.authority, limits, true),
                DestructionErrors::CommandAuthorityDenied);
        ErrorIs(AdmitDestructionCommand(impact, snapshot, descriptor, Id<DestructionCapabilityRevision>(4), authority, limits, true),
                DestructionErrors::CommandUnsupported);
        auto staleAuthority = authority;
        staleAuthority.revision = Id<DestructionAuthorityRevision>(3);
        ErrorIs(AdmitDestructionCommand(impact, snapshot, descriptor, Id<DestructionCapabilityRevision>(3), staleAuthority, limits, true),
                DestructionErrors::CommandAuthorityDenied);
        auto scriptOnly = Header(4);
        scriptOnly.authority.capabilities.bits = DestructionCommandCapabilityBit<DestructionCommandCapability::Script>;
        ErrorIs(AdmitDestructionCommand(DestructionCommand::Script(scriptOnly, DestructionScriptIntent::ApplyDamage, 1).Value(), snapshot,
                                        descriptor, Id<DestructionCapabilityRevision>(3), scriptOnly.authority, limits, true),
                DestructionErrors::CommandAuthorityDenied);
        auto fractureOnly = Header(5);
        fractureOnly.authority.capabilities.bits = DestructionCommandCapabilityBit<DestructionCommandCapability::ExplicitFracture>;
        ErrorIs(AdmitDestructionCommand(DestructionCommand::Script(fractureOnly, DestructionScriptIntent::Fracture).Value(), snapshot,
                                        descriptor, Id<DestructionCapabilityRevision>(3), fractureOnly.authority, limits, true),
                DestructionErrors::CommandAuthorityDenied);
        ErrorIs(AdmitDestructionCommand(impact, snapshot, descriptor, Id<DestructionCapabilityRevision>(3), authority, limits, false),
                DestructionErrors::ShutdownInProgress);
        const auto large = DestructionCommand::Explosion(Header(4), {}, 51, 1, DestructionExplosionFalloff::Constant).Value();
        ErrorIs(AdmitDestructionCommand(large, snapshot, descriptor, Id<DestructionCapabilityRevision>(3), authority, limits, true),
                DestructionErrors::CommandLimitExceeded);
    }

    TEST_CASE("Trigger policy has no fallback and commands lower without identity loss", "[destruction][command][policy]") {
        const auto accumulated = Descriptor(DestructionTriggerPolicy::AccumulatedDamage);
        const auto collision = DestructionCommand::Collision(Header(), {}, {0, 1, 0}, 5, 2).Value();
        ErrorIs(AdmitDestructionCommand(collision, Snapshot(accumulated), accumulated, Id<DestructionCapabilityRevision>(3),
                                        Header().authority, {}, true),
                DestructionErrors::CommandUnsupported);
        const auto explicitOnly = Descriptor(DestructionTriggerPolicy::ExplicitOnly);
        const auto damage = DestructionCommand::Damage(Header(), 2).Value();
        ErrorIs(AdmitDestructionCommand(damage, Snapshot(explicitOnly), explicitOnly, Id<DestructionCapabilityRevision>(3),
                                        Header().authority, {}, true),
                DestructionErrors::CommandUnsupported);
        const auto fracture = DestructionCommand::Script(Header(2), DestructionScriptIntent::Fracture).Value();
        REQUIRE(AdmitDestructionCommand(fracture, Snapshot(explicitOnly), explicitOnly, Id<DestructionCapabilityRevision>(3),
                                        Header().authority, {}, true)
                    .HasValue());
        CHECK(ToDestructionStateCommand(fracture).Value().Kind() == DestructionStateCommandKind::Destroy);
        CHECK(ToDestructionStateCommand(DestructionCommand::Damage(Header(3), 7).Value()).Value().Id() == Header(3).id);
    }

    TEST_CASE("Terminal outcomes remain distinct and stale completions require rollback", "[destruction][command][lifecycle]") {
        const auto command = DestructionCommand::Damage(Header(), 1).Value();
        const auto id = command.Header().id;
        const auto source = Id<DestructionStateRevision>(1);
        const auto next = Id<DestructionStateRevision>(2);
        const std::array cases{std::pair{DestructionCommandOutcome::Rejected, DestructionCommandTerminalReason::AuthorityDenied},
                               std::pair{DestructionCommandOutcome::Cancelled, DestructionCommandTerminalReason::Rollback},
                               std::pair{DestructionCommandOutcome::Unsupported, DestructionCommandTerminalReason::CapabilityUnavailable},
                               std::pair{DestructionCommandOutcome::Failed, DestructionCommandTerminalReason::ExecutionFailure}};
        for (const auto &[outcome, reason] : cases) {
            const auto result = DestructionCommandResult::Terminated(id, source, next, outcome, reason).Value();
            CHECK(result.Outcome() == outcome);
            CHECK(result.RequiresRollback());
        }
        CHECK_FALSE(DestructionCommandResult::Succeeded(id, source, next).Value().RequiresRollback());
        ErrorIs(DestructionCommandResult::Terminated(id, source, next, DestructionCommandOutcome::Unsupported,
                                                     DestructionCommandTerminalReason::ExecutionFailure),
                DestructionErrors::CommandResultInvalid);
        CHECK(ResolveDestructionCommandCompletion(command, source, next, Handle(2), source, true).Value().Reason() ==
              DestructionCommandTerminalReason::Replaced);
        CHECK(ResolveDestructionCommandCompletion(command, source, next, Handle(), next, true).Value().Reason() ==
              DestructionCommandTerminalReason::StaleRevision);
        CHECK(ResolveDestructionCommandCompletion(command, source, next, Handle(), source, false).Value().Outcome() ==
              DestructionCommandOutcome::Cancelled);
        CHECK(ResolveDestructionCommandCompletion(command, source, next, Handle(), source, true).Value().Outcome() ==
              DestructionCommandOutcome::Succeeded);
    }

    TEST_CASE("Steady command path allocates nothing", "[destruction][command][allocation]") {
        const auto descriptor = Descriptor();
        const auto snapshot = Snapshot(descriptor);
        const auto before = Tests::AllocationProbe::Count();
        const auto command = DestructionCommand::Damage(Header(), 10);
        REQUIRE(command.HasValue());
        REQUIRE(AdmitDestructionCommand(command.Value(), snapshot, descriptor, Id<DestructionCapabilityRevision>(3), Header().authority, {},
                                        true)
                    .HasValue());
        REQUIRE(ResolveDestructionCommandCompletion(command.Value(), snapshot.revision, Id<DestructionStateRevision>(2), snapshot.target,
                                                    snapshot.revision, true)
                    .HasValue());
        CHECK(Tests::AllocationProbe::Count() == before);
    }
}  // namespace Horo::Destruction
