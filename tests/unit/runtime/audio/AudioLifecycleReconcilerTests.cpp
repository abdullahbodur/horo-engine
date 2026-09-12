#include "Horo/Audio/AudioLifecycleReconciler.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <utility>

namespace Horo::Audio {
    namespace {
        [[nodiscard]] AudioRuntimeId Owner(const std::uint64_t value = 41) {
            return AudioRuntimeId::Create(value).Value();
        }

        [[nodiscard]] AudioSceneContextHandle Scene(const std::uint32_t slot = 3) {
            return {Owner(), slot, 1};
        }

        [[nodiscard]] AudioDeviceEpoch CallbackEpoch(const std::uint64_t callbackEpoch = 13) {
            return {.device = {Owner(), 2, 1}, .formatRevision = 5, .callbackEpoch = callbackEpoch};
        }

        [[nodiscard]] AudioLifecycleReconcilerDescriptor Descriptor(const bool callbackRequired = true) {
            return {.owner = Owner(),
                    .commandEpoch = 7,
                    .callbackEpoch = callbackRequired ? CallbackEpoch() : AudioDeviceEpoch{},
                    .clockDomain = callbackRequired ? 17U : 0U,
                    .maximumPendingOperations = 8,
                    .maximumTerminalResults = 8,
                    .maximumCallbackReferences = 8,
                    .callbackRequired = callbackRequired};
        }

        [[nodiscard]] AudioCommandScope Scope(const AudioSceneContextHandle scene = Scene(), const std::uint64_t epoch = 7) {
            return {.owner = Owner(), .epoch = epoch, .scene = scene};
        }

        [[nodiscard]] AudioCallbackEvent Quiesced(const AudioDeviceEpoch epoch = CallbackEpoch()) {
            return {.epoch = epoch, .sampleFrame = 64, .timestamp = {17, 2'000}, .fact = AudioCallbackQuiesced{}};
        }

        [[nodiscard]] AudioVoiceHandle Voice(const std::uint32_t slot = 4) {
            return {Owner(), slot, 1};
        }

        [[nodiscard]] AudioMemoryHandle Storage(const std::uint32_t slot = 5) {
            return {Owner(), AudioMemoryPoolId::Create(9).Value(), slot, 1};
        }

        [[nodiscard]] AudioLifecycleReconciler Prepared(const AudioLifecycleReconcilerDescriptor &descriptor = Descriptor()) {
            auto created = AudioLifecycleReconciler::Create(descriptor);
            REQUIRE(created.HasValue());
            return std::move(created).Value();
        }

        [[nodiscard]] AudioLifecycleReconciler Active(const AudioLifecycleReconcilerDescriptor &descriptor = Descriptor()) {
            auto reconciler = Prepared(descriptor);
            if (descriptor.callbackRequired)
                REQUIRE(reconciler.MarkCallbackAttached().HasValue());
            REQUIRE(reconciler.Activate().HasValue());
            return reconciler;
        }

        template <typename T> void ExpectError(const Result<T> &result, const ErrorCodeDescriptor &error) {
            REQUIRE(result.HasError());
            CHECK(result.ErrorValue().code.Value() == error.code.Value());
        }

        TEST_CASE("Audio lifecycle startup requires explicit callback attachment", "[unit][audio][lifecycle]") {
            auto reconciler = Prepared();
            ExpectError(reconciler.Activate(), AudioErrors::RuntimeInactive);
            REQUIRE(reconciler.MarkCallbackAttached().HasValue());
            ExpectError(reconciler.MarkCallbackAttached(), AudioErrors::RuntimeInactive);
            REQUIRE(reconciler.Activate().HasValue());
            CHECK(reconciler.Snapshot().state == AudioLifecycleState::Active);
            CHECK(reconciler.Snapshot().callbackAttached);
        }

        TEST_CASE("Audio scene unload publishes terminals before retiring callback references", "[unit][audio][lifecycle][scene]") {
            auto reconciler = Active();
            const auto otherScene = Scene(8);
            REQUIRE(reconciler.Admit({Scope(), 1}).HasValue());
            REQUIRE(reconciler.Admit({Scope(otherScene), 2}).HasValue());
            REQUIRE(reconciler.TrackCallbackReference({Scene(), Voice()}).HasValue());
            REQUIRE(reconciler.TrackCallbackReference({Scene(), Storage()}).HasValue());
            REQUIRE(reconciler.TrackCallbackReference({otherScene, Voice(7)}).HasValue());

            REQUIRE(reconciler.BeginSceneUnload(Scene(), 3).HasValue());
            ExpectError(reconciler.Admit({Scope(), 4}), AudioErrors::RuntimeInactive);
            REQUIRE(reconciler.Admit({Scope(otherScene), 4}).HasValue());
            ExpectError(reconciler.AcknowledgeSceneUnload(Scene(), 5), AudioErrors::HandleStale);
            CHECK(reconciler.Snapshot().callbackReferences == 3);
            CHECK(reconciler.TerminalResults().empty());

            REQUIRE(reconciler.AcknowledgeSceneUnload(Scene(), 3).HasValue());
            CHECK(reconciler.Snapshot().pendingOperations == 2);
            CHECK(reconciler.Snapshot().callbackReferences == 1);
            REQUIRE(reconciler.TerminalResults().size() == 1);
            CHECK(reconciler.TerminalResults().front().operation.acceptedSequence == 1);
            CHECK(reconciler.TerminalResults().front().reason == AudioReconciliationReason::CancelledBySceneUnload);
            ExpectError(reconciler.Admit({Scope(), 5}), AudioErrors::RuntimeInactive);
            REQUIRE(reconciler.Admit({Scope(otherScene), 5}).HasValue());
        }

        TEST_CASE("Audio callback terminals reconcile once and retire exact references", "[unit][audio][lifecycle][terminal]") {
            auto reconciler = Active();
            REQUIRE(reconciler.Admit({Scope(), 1}).HasValue());
            REQUIRE(reconciler.TrackCallbackReference({Scene(), Voice()}).HasValue());
            const AudioTerminalEvent wrongVoice = AudioVoiceTerminalEvent{Scope(), 1, Voice(9), AudioVoiceTerminalReason::Finished};
            ExpectError(reconciler.ObserveTerminal(wrongVoice), AudioErrors::HandleStale);
            const AudioTerminalEvent finished = AudioVoiceTerminalEvent{Scope(), 1, Voice(), AudioVoiceTerminalReason::Finished};
            REQUIRE(reconciler.ObserveTerminal(finished).HasValue());
            CHECK(reconciler.Snapshot().pendingOperations == 0);
            CHECK(reconciler.Snapshot().callbackReferences == 0);
            CHECK(reconciler.TerminalResults().front().reason == AudioReconciliationReason::Completed);
            ExpectError(reconciler.ObserveTerminal(finished), AudioErrors::HandleStale);
            CHECK(reconciler.AcknowledgeTerminal({Scope(), 1}).Value());
            CHECK_FALSE(reconciler.AcknowledgeTerminal({Scope(), 1}).Value());

            REQUIRE(reconciler.Admit({Scope(), 2}).HasValue());
            REQUIRE(reconciler.TrackCallbackReference({Scene(), Storage()}).HasValue());
            const AudioTerminalEvent released = AudioResourceReleaseEvent{Scope(), 2, Storage(), 13};
            REQUIRE(reconciler.ObserveTerminal(released).HasValue());
            CHECK(reconciler.Snapshot().callbackReferences == 0);
            CHECK(reconciler.TerminalResults().front().reason == AudioReconciliationReason::Completed);
        }

        TEST_CASE("Audio device reset requires matching quiescence and detachment before replacement", "[unit][audio][lifecycle][reset]") {
            auto reconciler = Active();
            REQUIRE(reconciler.Admit({Scope(), 1}).HasValue());
            REQUIRE(reconciler.Admit({Scope(Scene(8)), 2}).HasValue());
            REQUIRE(reconciler.TrackCallbackReference({Scene(), Voice()}).HasValue());
            REQUIRE(reconciler.TrackCallbackReference({Scene(8), Storage()}).HasValue());
            REQUIRE(reconciler.BeginDeviceReset().HasValue());
            ExpectError(reconciler.CompleteDeviceReset(8, CallbackEpoch(14), true), AudioErrors::RuntimeInactive);
            ExpectError(reconciler.ObserveCallbackQuiesced(Quiesced(CallbackEpoch(99))), AudioErrors::EventQueueInvalid);
            REQUIRE(reconciler.ObserveCallbackQuiesced(Quiesced()).HasValue());
            ExpectError(reconciler.CompleteDeviceReset(8, CallbackEpoch(14), false), AudioErrors::RuntimeInactive);
            CHECK(reconciler.Snapshot().pendingOperations == 2);
            CHECK(reconciler.Snapshot().callbackReferences == 2);

            REQUIRE(reconciler.CompleteDeviceReset(8, CallbackEpoch(14), true).HasValue());
            const auto snapshot = reconciler.Snapshot();
            CHECK(snapshot.state == AudioLifecycleState::Active);
            CHECK(snapshot.commandEpoch == 8);
            CHECK(snapshot.pendingOperations == 0);
            CHECK(snapshot.callbackReferences == 0);
            REQUIRE(reconciler.TerminalResults().size() == 2);
            CHECK(reconciler.TerminalResults()[0].reason == AudioReconciliationReason::CancelledByDeviceReset);
            REQUIRE(reconciler.Admit({Scope(Scene(), 8), 1}).HasValue());
        }

        TEST_CASE("Audio shutdown reconciles before Stopped and remains idempotent", "[unit][audio][lifecycle][shutdown]") {
            auto reconciler = Active();
            REQUIRE(reconciler.Admit({Scope(), 1}).HasValue());
            REQUIRE(reconciler.TrackCallbackReference({Scene(), Voice()}).HasValue());
            REQUIRE(reconciler.BeginShutdown().HasValue());
            REQUIRE(reconciler.BeginShutdown().HasValue());
            ExpectError(reconciler.CompleteShutdown(true), AudioErrors::RuntimeInactive);
            REQUIRE(reconciler.ObserveCallbackQuiesced(Quiesced()).HasValue());
            ExpectError(reconciler.CompleteShutdown(false), AudioErrors::RuntimeInactive);
            REQUIRE(reconciler.CompleteShutdown(true).HasValue());
            REQUIRE(reconciler.CompleteShutdown(true).HasValue());
            CHECK(reconciler.Snapshot().state == AudioLifecycleState::Stopped);
            CHECK(reconciler.Snapshot().pendingOperations == 0);
            CHECK(reconciler.Snapshot().callbackReferences == 0);
            CHECK_FALSE(reconciler.Snapshot().callbackAttached);
            CHECK(reconciler.TerminalResults().front().reason == AudioReconciliationReason::CancelledByShutdown);
        }

        TEST_CASE("Audio partial startup and omitted callback compositions shut down safely", "[unit][audio][lifecycle][partial_start]") {
            auto beforeCallback = Prepared();
            REQUIRE(beforeCallback.BeginShutdown().HasValue());
            REQUIRE(beforeCallback.CompleteShutdown(false).HasValue());

            auto afterCallback = Prepared();
            REQUIRE(afterCallback.MarkCallbackAttached().HasValue());
            REQUIRE(afterCallback.BeginShutdown().HasValue());
            ExpectError(afterCallback.CompleteShutdown(true), AudioErrors::RuntimeInactive);
            REQUIRE(afterCallback.ObserveCallbackQuiesced(Quiesced()).HasValue());
            REQUIRE(afterCallback.CompleteShutdown(true).HasValue());

            auto omitted = Active(Descriptor(false));
            REQUIRE(omitted.Admit({Scope(), 1}).HasValue());
            REQUIRE(omitted.BeginShutdown().HasValue());
            REQUIRE(omitted.CompleteShutdown(false).HasValue());
            CHECK(omitted.TerminalResults().front().reason == AudioReconciliationReason::CancelledByShutdown);
        }

        TEST_CASE("Audio reconciliation capacity failure preserves pending ownership", "[unit][audio][lifecycle][capacity]") {
            auto descriptor = Descriptor();
            descriptor.maximumTerminalResults = 1;
            auto reconciler = Active(descriptor);
            REQUIRE(reconciler.Admit({Scope(), 1}).HasValue());
            REQUIRE(reconciler.Admit({Scope(Scene(8)), 2}).HasValue());
            REQUIRE(reconciler.BeginShutdown().HasValue());
            REQUIRE(reconciler.ObserveCallbackQuiesced(Quiesced()).HasValue());
            ExpectError(reconciler.CompleteShutdown(true), AudioErrors::HandleCapacityExhausted);
            CHECK(reconciler.Snapshot().state == AudioLifecycleState::Stopping);
            CHECK(reconciler.Snapshot().pendingOperations == 2);
            CHECK(reconciler.TerminalResults().empty());
        }

        TEST_CASE("Audio lifecycle rejects malformed duplicate stale and over-capacity records", "[unit][audio][lifecycle][validation]") {
            auto invalid = Descriptor();
            invalid.maximumPendingOperations = 0;
            ExpectError(AudioLifecycleReconciler::Create(invalid), AudioErrors::IdentityInvalid);

            auto descriptor = Descriptor();
            descriptor.maximumPendingOperations = 1;
            descriptor.maximumCallbackReferences = 1;
            auto reconciler = Active(descriptor);
            REQUIRE(reconciler.Admit({Scope(), 1}).HasValue());
            ExpectError(reconciler.Admit({Scope(Scene(8)), 2}), AudioErrors::HandleCapacityExhausted);
            ExpectError(reconciler.Admit({Scope(), 1}), AudioErrors::HandleStale);
            REQUIRE(reconciler.TrackCallbackReference({Scene(), Voice()}).HasValue());
            ExpectError(reconciler.TrackCallbackReference({Scene(), Voice()}), AudioErrors::HandleStale);
            ExpectError(reconciler.TrackCallbackReference({Scene(8), Voice(7)}), AudioErrors::HandleCapacityExhausted);
        }
    }  // namespace
}  // namespace Horo::Audio
