#include "Horo/PlatformServices/PlatformUserSession.h"

#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <string>
#include <type_traits>

namespace Horo::PlatformServices {
    namespace {
        PlatformSubjectNonce Nonce(const std::byte marker) {
            PlatformSubjectNonce nonce;
            nonce.bytes.front() = marker;
            nonce.bytes.back() = std::byte{0xa5};
            return nonce;
        }

        PlatformSessionCandidate ActiveCandidate(const std::uint64_t generation = 1, const std::uint64_t accessRevision = 1,
                                                 const std::byte marker = std::byte{1}) {
            PlatformSessionCandidate candidate;
            candidate.phase = PlatformSessionPhase::Active;
            candidate.generation = {generation};
            candidate.providerGeneration = {7};
            candidate.accessRevision = {accessRevision};
            candidate.subjectNonce = Nonce(marker);
            candidate.capabilities.services.fill(PlatformSessionAccessState::Granted);
            return candidate;
        }

        PlatformSessionCandidate InactiveCandidate(const PlatformSessionPhase phase, const std::uint64_t generation,
                                                   const std::uint64_t accessRevision, const PlatformSessionReason reason) {
            PlatformSessionCandidate candidate;
            candidate.phase = phase;
            candidate.generation = {generation};
            candidate.providerGeneration = {7};
            candidate.accessRevision = {accessRevision};
            candidate.reason = reason;
            return candidate;
        }

        template <typename T> void CheckError(const Result<T> &result, const ErrorCodeDescriptor &expected) {
            REQUIRE(result.HasError());
            CHECK(result.ErrorValue().code.Value() == expected.code.Value());
        }
    }  // namespace

    TEST_CASE("Platform subject handles are opaque equality-only live capabilities", "[platform-services][session][identity]") {
        static_assert(!std::is_aggregate_v<PlatformSubjectHandle>);
        static_assert(!std::is_convertible_v<PlatformSubjectHandle, std::string>);
        static_assert(std::is_copy_constructible_v<PlatformSubjectHandle>);
        static_assert(std::is_nothrow_destructible_v<PlatformSubjectHandle>);

        CHECK_FALSE(PlatformSubjectNonce{}.IsValid());
        CHECK(Nonce(std::byte{1}).IsValid());
        CHECK_FALSE(PlatformSubjectHandle{}.IsValid());

        const auto first = BuildPlatformSessionSnapshot(ActiveCandidate());
        const auto same = BuildPlatformSessionSnapshot(ActiveCandidate());
        const auto other = BuildPlatformSessionSnapshot(ActiveCandidate(1, 1, std::byte{2}));
        REQUIRE(first.HasValue());
        REQUIRE(same.HasValue());
        REQUIRE(other.HasValue());
        REQUIRE(first.Value().Subject());
        CHECK(first.Value().Subject()->IsValid());
        CHECK(*first.Value().Subject() == *same.Value().Subject());
        CHECK_FALSE(*first.Value().Subject() == *other.Value().Subject());
        CHECK(first.Value().Subject()->ProviderGeneration() == PlatformProviderGeneration{7});
        CHECK(first.Value().Subject()->SessionGeneration() == PlatformSessionGeneration{1});
    }

    TEST_CASE("Session snapshots reject malformed phase generation reason and capability evidence",
              "[platform-services][session][validation]") {
        auto candidate = ActiveCandidate();
        candidate.subjectNonce = PlatformSubjectNonce{};
        CheckError(BuildPlatformSessionSnapshot(candidate), PlatformSessionErrors::InvalidSnapshot);

        candidate = ActiveCandidate();
        candidate.subjectNonce.reset();
        CheckError(BuildPlatformSessionSnapshot(candidate), PlatformSessionErrors::InvalidSnapshot);

        candidate = InactiveCandidate(PlatformSessionPhase::NoSubject, 1, 1, PlatformSessionReason::None);
        candidate.subjectNonce = Nonce(std::byte{1});
        CheckError(BuildPlatformSessionSnapshot(candidate), PlatformSessionErrors::InvalidSnapshot);

        candidate = InactiveCandidate(PlatformSessionPhase::Closing, 1, 1, PlatformSessionReason::None);
        CheckError(BuildPlatformSessionSnapshot(candidate), PlatformSessionErrors::InvalidSnapshot);

        candidate = InactiveCandidate(PlatformSessionPhase::Failed, 1, 1, PlatformSessionReason::AuthenticationFailed);
        candidate.capabilities.services[0] = PlatformSessionAccessState::Granted;
        CheckError(BuildPlatformSessionSnapshot(candidate), PlatformSessionErrors::InvalidSnapshot);

        candidate = ActiveCandidate();
        candidate.generation = {};
        CheckError(BuildPlatformSessionSnapshot(candidate), PlatformSessionErrors::InvalidSnapshot);
        candidate = ActiveCandidate();
        candidate.providerGeneration = {};
        CheckError(BuildPlatformSessionSnapshot(candidate), PlatformSessionErrors::InvalidSnapshot);
        candidate = ActiveCandidate();
        candidate.accessRevision = {};
        CheckError(BuildPlatformSessionSnapshot(candidate), PlatformSessionErrors::InvalidSnapshot);
        candidate = ActiveCandidate();
        candidate.phase = static_cast<PlatformSessionPhase>(255);
        CheckError(BuildPlatformSessionSnapshot(candidate), PlatformSessionErrors::InvalidSnapshot);
        candidate = ActiveCandidate();
        candidate.capabilities.services[0] = static_cast<PlatformSessionAccessState>(255);
        CheckError(BuildPlatformSessionSnapshot(candidate), PlatformSessionErrors::InvalidSnapshot);
    }

    TEST_CASE("Session access requires the exact handle policy revision and granted capability", "[platform-services][session][access]") {
        const auto active = BuildPlatformSessionSnapshot(ActiveCandidate());
        REQUIRE(active.HasValue());
        REQUIRE(active.Value().Subject());
        const auto subject = *active.Value().Subject();

        CHECK(ValidatePlatformSessionAccess(active.Value(), subject, {1}, PlatformServiceKind::Cloud).HasValue());
        CheckError(ValidatePlatformSessionAccess(active.Value(), PlatformSubjectHandle{}, {1}, PlatformServiceKind::Cloud),
                   PlatformSessionErrors::InvalidSnapshot);
        CheckError(ValidatePlatformSessionAccess(active.Value(), subject, {2}, PlatformServiceKind::Cloud),
                   PlatformSessionErrors::StaleAccessPolicy);
        CheckError(ValidatePlatformSessionAccess(active.Value(), subject, {1}, static_cast<PlatformServiceKind>(255)),
                   PlatformSessionErrors::InvalidSnapshot);

        struct AccessCase final {
            PlatformSessionAccessState state;
            const ErrorCodeDescriptor *error;
        };

        const std::array cases{
            AccessCase{PlatformSessionAccessState::Unavailable, &PlatformSessionErrors::AccessUnavailable},
            AccessCase{PlatformSessionAccessState::ConsentRequired, &PlatformSessionErrors::ConsentRequired},
            AccessCase{PlatformSessionAccessState::Denied, &PlatformSessionErrors::AccessDenied},
            AccessCase{PlatformSessionAccessState::Restricted, &PlatformSessionErrors::AccessRestricted},
            AccessCase{PlatformSessionAccessState::Revoked, &PlatformSessionErrors::AccessRevoked},
        };
        for (const auto &testCase : cases) {
            auto deniedCandidate = ActiveCandidate();
            deniedCandidate.capabilities.services[static_cast<std::size_t>(PlatformServiceKind::Friends)] = testCase.state;
            const auto denied = BuildPlatformSessionSnapshot(deniedCandidate);
            REQUIRE(denied.HasValue());
            CheckError(ValidatePlatformSessionAccess(denied.Value(), *denied.Value().Subject(), {1}, PlatformServiceKind::Friends),
                       *testCase.error);
        }
    }

    TEST_CASE("Policy refresh preserves subject generation while identity lifecycle advances monotonically",
              "[platform-services][session][replacement]") {
        const auto active = BuildPlatformSessionSnapshot(ActiveCandidate());
        REQUIRE(active.HasValue());
        const auto oldSubject = *active.Value().Subject();

        auto refresh = ActiveCandidate(1, 2);
        refresh.capabilities.services[static_cast<std::size_t>(PlatformServiceKind::Friends)] = PlatformSessionAccessState::Revoked;
        const auto refreshed = BuildPlatformSessionSnapshotReplacement(active.Value(), refresh);
        REQUIRE(refreshed.HasValue());
        CHECK(*refreshed.Value().Subject() == oldSubject);
        CheckError(ValidatePlatformSessionAccess(refreshed.Value(), oldSubject, {1}, PlatformServiceKind::Cloud),
                   PlatformSessionErrors::StaleAccessPolicy);

        const auto closingCandidate = InactiveCandidate(PlatformSessionPhase::Closing, 2, 3, PlatformSessionReason::UserSignedOut);
        const auto closing = BuildPlatformSessionSnapshotReplacement(refreshed.Value(), closingCandidate);
        REQUIRE(closing.HasValue());
        CHECK_FALSE(closing.Value().Subject());
        CheckError(ValidatePlatformSessionAccess(closing.Value(), oldSubject, {3}, PlatformServiceKind::Cloud),
                   PlatformSessionErrors::Closing);

        const auto noSubject =
            BuildPlatformSessionSnapshotReplacement(closing.Value(), InactiveCandidate(PlatformSessionPhase::NoSubject, 3, 4,
                                                                                       PlatformSessionReason::UserSignedOut));
        REQUIRE(noSubject.HasValue());
        const auto authenticating =
            BuildPlatformSessionSnapshotReplacement(noSubject.Value(), InactiveCandidate(PlatformSessionPhase::Authenticating, 4, 5,
                                                                                         PlatformSessionReason::None));
        REQUIRE(authenticating.HasValue());
        const auto rebound = BuildPlatformSessionSnapshotReplacement(authenticating.Value(), ActiveCandidate(5, 6, std::byte{2}));
        REQUIRE(rebound.HasValue());
        CheckError(ValidatePlatformSessionAccess(rebound.Value(), oldSubject, {6}, PlatformServiceKind::Cloud),
                   PlatformSessionErrors::StaleSession);
        CHECK(ValidatePlatformSessionAccess(rebound.Value(), *rebound.Value().Subject(), {6}, PlatformServiceKind::Cloud).HasValue());
    }

    TEST_CASE("Invalid replacements fail without changing the prior immutable snapshot", "[platform-services][session][lifecycle]") {
        const auto active = BuildPlatformSessionSnapshot(ActiveCandidate());
        REQUIRE(active.HasValue());
        const auto subject = *active.Value().Subject();

        CheckError(BuildPlatformSessionSnapshotReplacement(active.Value(), ActiveCandidate(1, 1)),
                   PlatformSessionErrors::InvalidTransition);
        CheckError(BuildPlatformSessionSnapshotReplacement(active.Value(), ActiveCandidate(3, 2, std::byte{2})),
                   PlatformSessionErrors::InvalidTransition);
        auto changedProvider = ActiveCandidate(1, 2);
        changedProvider.providerGeneration = {8};
        CheckError(BuildPlatformSessionSnapshotReplacement(active.Value(), changedProvider), PlatformSessionErrors::InvalidTransition);
        CheckError(BuildPlatformSessionSnapshotReplacement(active.Value(), InactiveCandidate(PlatformSessionPhase::NoSubject, 2, 2,
                                                                                             PlatformSessionReason::UserSignedOut)),
                   PlatformSessionErrors::InvalidTransition);

        CHECK(active.Value().Phase() == PlatformSessionPhase::Active);
        CHECK((active.Value().Subject() && *active.Value().Subject() == subject));

        auto exhaustedCandidate = ActiveCandidate(std::numeric_limits<std::uint64_t>::max(), 9);
        const auto exhausted = BuildPlatformSessionSnapshot(exhaustedCandidate);
        REQUIRE(exhausted.HasValue());
        CheckError(BuildPlatformSessionSnapshotReplacement(exhausted.Value(), InactiveCandidate(PlatformSessionPhase::Closing, 1, 10,
                                                                                                PlatformSessionReason::Shutdown)),
                   PlatformSessionErrors::GenerationExhausted);
    }
}  // namespace Horo::PlatformServices
