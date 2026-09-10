#include "Horo/Cinematic/SequencePlayer.h"
#include "Horo/Cinematic/SequencePlayerErrors.h"
#include "support/AllocationProbe.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <set>
#include <string_view>
#include <type_traits>

namespace Horo::Cinematic {
    namespace {
        template <typename T> void RequireError(const Result<T> &result, const ErrorCodeDescriptor &descriptor) {
            REQUIRE(result.HasError());
            CHECK(result.ErrorValue().domain.Value() == descriptor.domain.Value());
            CHECK(result.ErrorValue().code.Value() == descriptor.code.Value());
        }

        [[nodiscard]] constexpr SequencePlayerHandle Handle(const std::uint32_t sessionGeneration = 2,
                                                            const std::uint32_t playerGeneration = 4) noexcept {
            return {{71, sessionGeneration}, {93, playerGeneration}};
        }

        [[nodiscard]] SequencePlayer Player(const SequenceTime position = 0, const SequencePlaybackRate rate = {}) {
            auto player = SequencePlayer::Create({Handle(), 1'000, position, rate});
            REQUIRE(player.HasValue());
            return std::move(player).Value();
        }

        void CheckSignal(const Result<SequencePlayerTransition> &result, const SequencePlaybackState previous,
                         const SequencePlaybackState current, const SequencePlaybackSignal signal,
                         const SequenceEventTransitionPolicy eventPolicy = SequenceEventTransitionPolicy::Unchanged) {
            REQUIRE(result.HasValue());
            CHECK(result.Value().previousState == previous);
            CHECK(result.Value().state == current);
            CHECK(result.Value().signal == signal);
            CHECK(result.Value().eventPolicy == eventPolicy);
        }
    }  // namespace

    TEST_CASE("Sequence player validates immutable construction bounds", "[unit][cinematic][player][validation]") {
        CHECK(Player().Snapshot() == SequencePlayerSnapshot{Handle(), SequencePlaybackState::Ready, 0, 1'000, {1, 1}, 1});
        RequireError(SequencePlayer::Create({{}, 10, 0, {1, 1}}), SequencePlayerErrors::HandleInvalid);
        RequireError(SequencePlayer::Create({Handle(), 0, 0, {1, 1}}), SequencePlayerErrors::TimeInvalid);
        RequireError(SequencePlayer::Create({Handle(), 10, -1, {1, 1}}), SequencePlayerErrors::TimeInvalid);
        RequireError(SequencePlayer::Create({Handle(), 10, 11, {1, 1}}), SequencePlayerErrors::TimeInvalid);
        RequireError(SequencePlayer::Create({Handle(), 10, 0, {1, 0}}), SequencePlayerErrors::RateInvalid);
        RequireError(SequencePlayer::Create({Handle(), 10, 0, {1025, 1}}), SequencePlayerErrors::RateInvalid);
        CHECK(SequencePlayer::Create({Handle(), 10, 0, {-1024, 1}}).HasValue());
        static_assert(std::is_trivially_copyable_v<SequencePlayerHandle>);
    }

    TEST_CASE("Sequence player play pause and resume publish exact signals", "[unit][cinematic][player][state]") {
        auto player = Player(20, {2, 1});
        CheckSignal(player.Play(Handle()), SequencePlaybackState::Ready, SequencePlaybackState::Playing, SequencePlaybackSignal::Started);
        const auto playRevision = player.Snapshot().controlRevision;
        CheckSignal(player.Play(Handle()), SequencePlaybackState::Playing, SequencePlaybackState::Playing, SequencePlaybackSignal::None);
        CHECK(player.Snapshot().controlRevision == playRevision);
        CheckSignal(player.Pause(Handle()), SequencePlaybackState::Playing, SequencePlaybackState::Paused, SequencePlaybackSignal::Paused);
        CHECK(player.Snapshot().rate == SequencePlaybackRate{2, 1});
        CheckSignal(player.Pause(Handle()), SequencePlaybackState::Paused, SequencePlaybackState::Paused, SequencePlaybackSignal::None);
        CheckSignal(player.Play(Handle()), SequencePlaybackState::Paused, SequencePlaybackState::Playing, SequencePlaybackSignal::Resumed);

        auto ready = Player();
        RequireError(ready.Pause(Handle()), SequencePlayerErrors::TransitionInvalid);
        CHECK(ready.Snapshot().state == SequencePlaybackState::Ready);
    }

    TEST_CASE("Sequence stop during play closes admission and drains admitted events", "[unit][cinematic][player][stop]") {
        auto player = Player(400);
        REQUIRE(player.Play(Handle()).HasValue());
        CheckSignal(player.Stop(Handle()), SequencePlaybackState::Playing, SequencePlaybackState::Stopping,
                    SequencePlaybackSignal::StopRequested, SequenceEventTransitionPolicy::CloseAndDrainAdmitted);
        CheckSignal(player.Stop(Handle()), SequencePlaybackState::Stopping, SequencePlaybackState::Stopping, SequencePlaybackSignal::None);
        RequireError(player.Seek(Handle(), 200), SequencePlayerErrors::TransitionInvalid);
        RequireError(player.SetPlaybackSpeed(Handle(), {2, 1}), SequencePlayerErrors::TransitionInvalid);
        CheckSignal(player.FinishStop(Handle()), SequencePlaybackState::Stopping, SequencePlaybackState::Stopped,
                    SequencePlaybackSignal::Stopped);
        RequireError(player.Play(Handle()), SequencePlayerErrors::TransitionInvalid);
        RequireError(player.Stop(Handle()), SequencePlayerErrors::TransitionInvalid);

        for (const SequencePlaybackState source : {SequencePlaybackState::Ready, SequencePlaybackState::Paused}) {
            auto candidate = Player();
            if (source == SequencePlaybackState::Paused) {
                REQUIRE(candidate.Play(Handle()).HasValue());
                REQUIRE(candidate.Pause(Handle()).HasValue());
            }
            CheckSignal(candidate.Stop(Handle()), source, SequencePlaybackState::Stopping, SequencePlaybackSignal::StopRequested,
                        SequenceEventTransitionPolicy::CloseAndDrainAdmitted);
        }
    }

    TEST_CASE("Sequence seek is atomic random access and never dispatches crossed events", "[unit][cinematic][player][seek]") {
        auto player = Player(100);
        const auto forward = player.Seek(Handle(), 900);
        CheckSignal(forward, SequencePlaybackState::Ready, SequencePlaybackState::Ready, SequencePlaybackSignal::Seeked,
                    SequenceEventTransitionPolicy::ResetWithoutDispatch);
        CHECK(forward.Value().previousPosition == 100);
        CHECK(forward.Value().position == 900);
        const auto backward = player.Seek(Handle(), 25);
        CheckSignal(backward, SequencePlaybackState::Ready, SequencePlaybackState::Ready, SequencePlaybackSignal::Seeked,
                    SequenceEventTransitionPolicy::ResetWithoutDispatch);
        CHECK(backward.Value().previousPosition == 900);
        CHECK(backward.Value().position == 25);

        const auto revision = player.Snapshot().controlRevision;
        CheckSignal(player.Seek(Handle(), 25), SequencePlaybackState::Ready, SequencePlaybackState::Ready, SequencePlaybackSignal::None);
        CHECK(player.Snapshot().controlRevision == revision);
        RequireError(player.Seek(Handle(), -1), SequencePlayerErrors::TimeInvalid);
        RequireError(player.Seek(Handle(), 1'001), SequencePlayerErrors::TimeInvalid);
        CHECK(player.Snapshot().position == 25);

        REQUIRE(player.Play(Handle()).HasValue());
        REQUIRE(player.Seek(Handle(), 700).HasValue());
        REQUIRE(player.Pause(Handle()).HasValue());
        REQUIRE(player.Seek(Handle(), 0).HasValue());
        CHECK(player.Snapshot().state == SequencePlaybackState::Paused);
        CHECK(player.Snapshot().position == 0);
    }

    TEST_CASE("Zero reverse and fractional speed remain distinct from pause", "[unit][cinematic][player][rate]") {
        auto player = Player();
        REQUIRE(player.Play(Handle()).HasValue());
        CheckSignal(player.SetPlaybackSpeed(Handle(), {0, 1}), SequencePlaybackState::Playing, SequencePlaybackState::Playing,
                    SequencePlaybackSignal::RateChanged);
        CHECK(player.Snapshot().state == SequencePlaybackState::Playing);
        CHECK(player.Snapshot().rate == SequencePlaybackRate{0, 1});
        REQUIRE(player.Pause(Handle()).HasValue());
        CHECK(player.Snapshot().state == SequencePlaybackState::Paused);
        CHECK(player.Snapshot().rate == SequencePlaybackRate{0, 1});
        REQUIRE(player.SetPlaybackSpeed(Handle(), {-3, 2}).HasValue());
        CHECK(player.Snapshot().state == SequencePlaybackState::Paused);
        REQUIRE(player.Play(Handle()).HasValue());
        CHECK(player.Snapshot().rate == SequencePlaybackRate{-3, 2});
        RequireError(player.SetPlaybackSpeed(Handle(), {1, 0}), SequencePlayerErrors::RateInvalid);
        RequireError(player.SetPlaybackSpeed(Handle(), {std::numeric_limits<std::int32_t>::max(), 1}), SequencePlayerErrors::RateInvalid);
    }

    TEST_CASE("Player generations and revisions fence replacement and late work", "[unit][cinematic][player][lifecycle]") {
        auto player = Player();
        const auto readyFence = player.CaptureFence();
        CHECK(player.ValidateFence(readyFence).HasValue());
        REQUIRE(player.Play(Handle()).HasValue());
        RequireError(player.ValidateFence(readyFence), SequencePlayerErrors::HandleStale);
        CHECK(player.ValidateFence(player.CaptureFence()).HasValue());

        auto staleSession = Handle(1, 4);
        auto stalePlayer = Handle(2, 3);
        auto unknownSession = Handle();
        unknownSession.session.stableValue = 72;
        RequireError(player.Pause({}), SequencePlayerErrors::HandleInvalid);
        RequireError(player.Pause(unknownSession), SequencePlayerErrors::HandleUnknown);
        RequireError(player.Pause(staleSession), SequencePlayerErrors::HandleStale);
        RequireError(player.Pause(stalePlayer), SequencePlayerErrors::HandleStale);
        CHECK(player.Snapshot().state == SequencePlaybackState::Playing);
    }

    TEST_CASE("Close failure and terminal states enforce shutdown semantics", "[unit][cinematic][player][shutdown]") {
        for (const SequencePlaybackState source : {SequencePlaybackState::Ready, SequencePlaybackState::Playing,
                                                   SequencePlaybackState::Paused, SequencePlaybackState::Stopping}) {
            auto player = Player();
            if (source != SequencePlaybackState::Ready)
                REQUIRE(player.Play(Handle()).HasValue());
            if (source == SequencePlaybackState::Paused)
                REQUIRE(player.Pause(Handle()).HasValue());
            if (source == SequencePlaybackState::Stopping)
                REQUIRE(player.Stop(Handle()).HasValue());
            CheckSignal(player.Close(Handle()), source, SequencePlaybackState::Closing, SequencePlaybackSignal::Closing,
                        SequenceEventTransitionPolicy::CloseAndDiscardPending);
            CheckSignal(player.Close(Handle()), SequencePlaybackState::Closing, SequencePlaybackState::Closing,
                        SequencePlaybackSignal::None);
            CheckSignal(player.FinishClose(Handle()), SequencePlaybackState::Closing, SequencePlaybackState::Stopped,
                        SequencePlaybackSignal::Stopped);
            RequireError(player.Close(Handle()), SequencePlayerErrors::TransitionInvalid);
            RequireError(player.Fail(Handle()), SequencePlayerErrors::TransitionInvalid);
        }

        auto failed = Player();
        CheckSignal(failed.Fail(Handle()), SequencePlaybackState::Ready, SequencePlaybackState::Failed, SequencePlaybackSignal::Failed,
                    SequenceEventTransitionPolicy::CloseAndDiscardPending);
        CheckSignal(failed.Fail(Handle()), SequencePlaybackState::Failed, SequencePlaybackState::Failed, SequencePlaybackSignal::None);
        RequireError(failed.Play(Handle()), SequencePlayerErrors::TransitionInvalid);
        RequireError(failed.Close(Handle()), SequencePlayerErrors::TransitionInvalid);
    }

    TEST_CASE("Successful player commands allocate no heap memory", "[unit][cinematic][player][allocation]") {
        auto player = Player();
        const auto before = Tests::AllocationProbe::Count();
        REQUIRE(player.Play(Handle()).HasValue());
        REQUIRE(player.Seek(Handle(), 500).HasValue());
        REQUIRE(player.SetPlaybackSpeed(Handle(), {-1, 2}).HasValue());
        REQUIRE(player.Pause(Handle()).HasValue());
        const auto after = Tests::AllocationProbe::Count();
        CHECK(after == before);
    }

    TEST_CASE("Player error descriptors are stable and unique", "[unit][cinematic][player][errors]") {
        const std::array descriptors{&SequencePlayerErrors::HandleInvalid,    &SequencePlayerErrors::HandleUnknown,
                                     &SequencePlayerErrors::HandleStale,      &SequencePlayerErrors::TransitionInvalid,
                                     &SequencePlayerErrors::TimeInvalid,      &SequencePlayerErrors::RateInvalid,
                                     &SequencePlayerErrors::RevisionExhausted};
        std::set<std::string_view> codes;
        for (const auto *descriptor : descriptors) {
            CHECK(descriptor->domain.Value() == "horo.cinematic.player");
            CHECK(codes.insert(descriptor->code.Value()).second);
            CHECK_FALSE(descriptor->summary.empty());
            CHECK_FALSE(descriptor->remediationHint.empty());
        }
    }
}  // namespace Horo::Cinematic
