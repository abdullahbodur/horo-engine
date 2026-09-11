#include "Horo/Destruction/DestructionComposition.h"
#include "support/AllocationProbe.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <type_traits>

namespace Horo::Destruction {
    namespace {
        template <typename Identity> Identity Id(const std::uint64_t value) {
            auto identity = Identity::Create(value);
            REQUIRE(identity.HasValue());
            return identity.Value();
        }

        std::array<DestructionCapabilityFact, DestructionHostCapabilityCount> Facts(
            const DestructionCapabilityAvailability physics = DestructionCapabilityAvailability::Available,
            const DestructionCapabilityAvailability vfx = DestructionCapabilityAvailability::Available,
            const DestructionCapabilityAvailability audio = DestructionCapabilityAvailability::Available,
            const DestructionCapabilityAvailability networking = DestructionCapabilityAvailability::Available) {
            const std::array availability{physics, vfx, audio, networking};
            std::array<DestructionCapabilityFact, DestructionHostCapabilityCount> facts{};
            for (std::size_t index = 0; index < facts.size(); ++index) {
                const bool available = availability[index] == DestructionCapabilityAvailability::Available;
                facts[index] = {static_cast<DestructionHostCapability>(index), availability[index],
                                available ? Id<DestructionHostCapabilityRevision>(index + 10U) : DestructionHostCapabilityRevision{}};
            }
            return facts;
        }

        DestructionCompositionRequest Request(const DestructionProductProfile profile,
                                              const std::span<const DestructionCapabilityFact> facts, const std::uint64_t revision = 7) {
            return {.revision = Id<DestructionCompositionRevision>(revision), .profile = profile, .capabilities = facts};
        }

        template <typename T> void CheckError(const Result<T> &result, const ErrorCodeDescriptor &expected) {
            REQUIRE(result.HasError());
            const Error &actual = result.ErrorValue();
            CHECK(actual.domain.Value() == expected.domain.Value());
            CHECK(actual.code.Value() == expected.code.Value());
        }
    }  // namespace

    TEST_CASE("Every destruction product profile has an explicit canonical capability policy", "[unit][destruction][composition]") {
        const auto nullPolicy = GetDestructionProductProfilePolicy(DestructionProductProfile::Null).Value();
        CHECK_FALSE(nullPolicy.destructionEnabled);
        CHECK(nullPolicy.requirements[0] == DestructionCapabilityRequirement::Omitted);
        CHECK(nullPolicy.requirements[3] == DestructionCapabilityRequirement::Omitted);

        const auto headless = GetDestructionProductProfilePolicy(DestructionProductProfile::Headless).Value();
        CHECK(headless.destructionEnabled);
        CHECK(headless.requirements[0] == DestructionCapabilityRequirement::Required);
        CHECK(headless.requirements[1] == DestructionCapabilityRequirement::Omitted);
        CHECK(headless.requirements[2] == DestructionCapabilityRequirement::Omitted);

        const auto editor = GetDestructionProductProfilePolicy(DestructionProductProfile::Editor).Value();
        CHECK(editor.tier == DestructionFeatureTier::High);
        CHECK(editor.requirements[0] == DestructionCapabilityRequirement::Optional);
        CHECK(editor.requirements[3] == DestructionCapabilityRequirement::Omitted);

        const auto standalone = GetDestructionProductProfilePolicy(DestructionProductProfile::Standalone).Value();
        CHECK(standalone.replication == DestructionReplicationIntent::LocalAuthority);
        CHECK(standalone.requirements[0] == DestructionCapabilityRequirement::Required);
        CHECK(standalone.requirements[3] == DestructionCapabilityRequirement::Omitted);

        const auto client = GetDestructionProductProfilePolicy(DestructionProductProfile::Client).Value();
        CHECK(client.replication == DestructionReplicationIntent::ServerAuthoritative);
        CHECK(client.requirements[3] == DestructionCapabilityRequirement::Required);

        const auto server = GetDestructionProductProfilePolicy(DestructionProductProfile::Server).Value();
        CHECK(server.requirements[0] == DestructionCapabilityRequirement::Required);
        CHECK(server.requirements[1] == DestructionCapabilityRequirement::Omitted);
        CHECK(server.requirements[2] == DestructionCapabilityRequirement::Omitted);
        CHECK(server.requirements[3] == DestructionCapabilityRequirement::Required);

        CheckError(GetDestructionProductProfilePolicy(DestructionProductProfile::Count), DestructionErrors::CompositionInvalid);
    }

    TEST_CASE("Composition resolves bound omitted and explicitly unavailable capabilities without fallback",
              "[unit][destruction][composition]") {
        auto facts = Facts(DestructionCapabilityAvailability::Available, DestructionCapabilityAvailability::Unavailable,
                           DestructionCapabilityAvailability::Available, DestructionCapabilityAvailability::Unavailable);
        const auto composition = DestructionComposition::Create(Request(DestructionProductProfile::Standalone, facts));
        REQUIRE(composition.HasValue());
        CHECK(composition.Value().Revision() == Id<DestructionCompositionRevision>(7));
        CHECK(composition.Value().Resolve(DestructionHostCapability::Physics).Value().state == DestructionCapabilityResolutionState::Bound);
        CHECK(composition.Value().Resolve(DestructionHostCapability::Vfx).Value().state ==
              DestructionCapabilityResolutionState::Unavailable);
        CHECK(composition.Value().Resolve(DestructionHostCapability::Audio).Value().state == DestructionCapabilityResolutionState::Bound);
        const auto networking = composition.Value().Resolve(DestructionHostCapability::Networking).Value();
        CHECK(networking.state == DestructionCapabilityResolutionState::Omitted);
        CHECK_FALSE(networking.sourceRevision.IsValid());

        static_assert(!std::is_default_constructible_v<DestructionComposition>);
        static_assert(std::is_copy_constructible_v<DestructionComposition>);
        static_assert(std::is_trivially_copyable_v<DestructionComposition>);
    }

    TEST_CASE("All canonical product profiles resolve without substituting profile identity", "[unit][destruction][composition]") {
        const auto facts = Facts();
        for (std::uint8_t value = 0; value < static_cast<std::uint8_t>(DestructionProductProfile::Count); ++value) {
            const auto profile = static_cast<DestructionProductProfile>(value);
            const auto composition = DestructionComposition::Create(Request(profile, facts, value + 1U));
            REQUIRE(composition.HasValue());
            CHECK(composition.Value().Policy().profile == profile);
        }
    }

    TEST_CASE("Composition canonicalizes fact order and successful resolution allocates nothing",
              "[unit][destruction][composition][allocation]") {
        auto facts = Facts();
        const std::array shuffled{facts[3], facts[1], facts[0], facts[2]};
        const auto before = Tests::AllocationProbe::Count();
        const auto composition = DestructionComposition::Create(Request(DestructionProductProfile::Client, shuffled));
        const auto after = Tests::AllocationProbe::Count();
        REQUIRE(composition.HasValue());
        CHECK(after == before);
        for (std::size_t index = 0; index < composition.Value().Capabilities().size(); ++index)
            CHECK(composition.Value().Capabilities()[index].capability == static_cast<DestructionHostCapability>(index));
    }

    TEST_CASE("Required unavailable capabilities reject the exact profile instead of selecting another one",
              "[unit][destruction][composition]") {
        auto noPhysics = Facts(DestructionCapabilityAvailability::Unavailable);
        CheckError(DestructionComposition::Create(Request(DestructionProductProfile::Headless, noPhysics)),
                   DestructionErrors::CompositionCapabilityUnavailable);
        CheckError(DestructionComposition::Create(Request(DestructionProductProfile::Standalone, noPhysics)),
                   DestructionErrors::CompositionCapabilityUnavailable);

        auto noNetwork = Facts(DestructionCapabilityAvailability::Available, DestructionCapabilityAvailability::Unavailable,
                               DestructionCapabilityAvailability::Unavailable, DestructionCapabilityAvailability::Unavailable);
        CheckError(DestructionComposition::Create(Request(DestructionProductProfile::Client, noNetwork)),
                   DestructionErrors::CompositionCapabilityUnavailable);
        CheckError(DestructionComposition::Create(Request(DestructionProductProfile::Server, noNetwork)),
                   DestructionErrors::CompositionCapabilityUnavailable);

        const auto nullComposition = DestructionComposition::Create(Request(DestructionProductProfile::Null, noNetwork));
        REQUIRE(nullComposition.HasValue());
        for (const auto &capability : nullComposition.Value().Capabilities())
            CHECK(capability.state == DestructionCapabilityResolutionState::Omitted);
    }

    TEST_CASE("Composition validation rejects malformed bounded capability snapshots atomically", "[unit][destruction][composition]") {
        auto facts = Facts();
        auto request = Request(DestructionProductProfile::Standalone, facts);
        request.contractVersion++;
        CheckError(DestructionComposition::Create(request), DestructionErrors::CompositionInvalid);
        request = Request(DestructionProductProfile::Standalone, facts);
        request.revision = {};
        CheckError(DestructionComposition::Create(request), DestructionErrors::CompositionInvalid);

        request =
            Request(DestructionProductProfile::Standalone, std::span<const DestructionCapabilityFact>{facts.data(), facts.size() - 1U});
        CheckError(DestructionComposition::Create(request), DestructionErrors::CompositionInvalid);

        facts[1].capability = DestructionHostCapability::Physics;
        CheckError(DestructionComposition::Create(Request(DestructionProductProfile::Standalone, facts)),
                   DestructionErrors::CompositionInvalid);
        facts = Facts();
        facts[0].availability = DestructionCapabilityAvailability::Unavailable;
        CheckError(DestructionComposition::Create(Request(DestructionProductProfile::Standalone, facts)),
                   DestructionErrors::CompositionInvalid);
        facts = Facts();
        facts[0].revision = {};
        CheckError(DestructionComposition::Create(Request(DestructionProductProfile::Standalone, facts)),
                   DestructionErrors::CompositionInvalid);
        facts = Facts();
        facts[0].capability = DestructionHostCapability::Count;
        CheckError(DestructionComposition::Create(Request(DestructionProductProfile::Standalone, facts)),
                   DestructionErrors::CompositionInvalid);
        facts = Facts();
        facts[0].availability = DestructionCapabilityAvailability::Count;
        CheckError(DestructionComposition::Create(Request(DestructionProductProfile::Standalone, facts)),
                   DestructionErrors::CompositionInvalid);
    }

    TEST_CASE("Replacement cancellation and shutdown fences reject stale or closed admission", "[unit][destruction][composition]") {
        const auto facts = Facts();
        const auto oldComposition = DestructionComposition::Create(Request(DestructionProductProfile::Standalone, facts, 41)).Value();
        const auto current = Id<DestructionCompositionRevision>(42);

        CheckError(ValidateDestructionCompositionAdmission(oldComposition, current, DestructionCompositionLifecycle::Active),
                   DestructionErrors::CompositionStale);
        CHECK(ValidateDestructionCompositionAdmission(oldComposition, oldComposition.Revision(), DestructionCompositionLifecycle::Active)
                  .HasValue());
        CheckError(ValidateDestructionCompositionAdmission(oldComposition, oldComposition.Revision(),
                                                           DestructionCompositionLifecycle::Cancelling),
                   DestructionErrors::CancelledBeforeCommit);
        CheckError(ValidateDestructionCompositionAdmission(oldComposition, oldComposition.Revision(),
                                                           DestructionCompositionLifecycle::ShuttingDown),
                   DestructionErrors::ShutdownInProgress);
        CheckError(ValidateDestructionCompositionAdmission(oldComposition, oldComposition.Revision(),
                                                           DestructionCompositionLifecycle::Closed),
                   DestructionErrors::ShutdownInProgress);
        CheckError(ValidateDestructionCompositionAdmission(oldComposition, {}, DestructionCompositionLifecycle::Active),
                   DestructionErrors::CompositionInvalid);
        CheckError(ValidateDestructionCompositionAdmission(oldComposition, oldComposition.Revision(),
                                                           DestructionCompositionLifecycle::Count),
                   DestructionErrors::CompositionInvalid);
        CheckError(oldComposition.Resolve(DestructionHostCapability::Count), DestructionErrors::CompositionInvalid);
    }
}  // namespace Horo::Destruction
