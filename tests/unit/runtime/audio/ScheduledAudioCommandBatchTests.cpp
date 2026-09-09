#include "Horo/Audio/AudioCommandBuffer.h"
#include "Horo/Audio/ScheduledAudioCommandBatch.h"

#include <catch2/catch_test_macros.hpp>

namespace Horo::Audio {
    namespace {
        AudioRuntimeId Owner() {
            return AudioRuntimeId::Create(17).Value();
        }

        AudioSceneContextHandle Scene() {
            return {.owner = Owner(), .slot = 1, .generation = 2};
        }

        AudioCommand Start(const std::uint32_t slot) {
            return {.scope = {.owner = Owner(), .epoch = 5, .scene = Scene()},
                    .payload = AudioStartVoiceCommand{.voice = {.owner = Owner(), .slot = slot, .generation = 1}}};
        }

        AudioMemoryHandle Storage() {
            return {.owner = Owner(), .pool = AudioMemoryPoolId::Create(23).Value(), .slot = 1, .generation = 4};
        }

        AudioCommandBuffer Buffer() {
            auto result = AudioCommandBuffer::Create({.owner = Owner(),
                                                      .storageIdentity = AudioMemoryPoolId::Create(24).Value(),
                                                      .epoch = 5,
                                                      .slots = 4,
                                                      .criticalSlots = 1,
                                                      .budgetBytes = 4096});
            REQUIRE(result.HasValue());
            return std::move(result).Value();
        }

        ScheduledAudioCommandBatch Batch() {
            ScheduledAudioCommandBatch batch{.target = {.kind = AudioCommandTargetKind::ExactSampleFrame,
                                                        .sampleFrame = 144'000,
                                                        .clockGeneration = 3,
                                                        .discontinuityRevision = 7},
                                             .commandCount = 2};
            batch.commands[0] = Start(2);
            batch.commands[1] = Start(3);
            return batch;
        }

        TEST_CASE("Scheduled audio batch keeps equal-time commands in one stable callback record", "[unit][audio][commands][batch]") {
            const auto batch = Batch();
            AudioCommand prepared;
            REQUIRE(MakeScheduledAudioBatchCommand(batch, Storage(), prepared) == ScheduledAudioCommandBatchStatus::Ok);
            const auto &reference = std::get<AudioScheduledBatchCommand>(prepared.payload);
            REQUIRE(reference.target.sampleFrame == 144'000);
            REQUIRE(reference.commandCount == 2);
            REQUIRE_FALSE(reference.containsCriticalCommand);

            auto buffer = Buffer();
            REQUIRE(buffer.TryPublish({.sequence = 1, .command = prepared}) == AudioCommandPublishStatus::Published);
            AudioCommandRecord output;
            REQUIRE(buffer.TryConsume(output));
            const auto &published = std::get<AudioScheduledBatchCommand>(output.command.payload);
            REQUIRE(published.storage == Storage());
            REQUIRE(published.target.sampleFrame == batch.target.sampleFrame);
            REQUIRE(published.commandCount == batch.commandCount);
        }

        TEST_CASE("Scheduled audio batch supports explicit next-buffer targeting", "[unit][audio][commands][batch]") {
            auto batch = Batch();
            batch.target = {};
            AudioCommand prepared;
            REQUIRE(MakeScheduledAudioBatchCommand(batch, Storage(), prepared) == ScheduledAudioCommandBatchStatus::Ok);
            REQUIRE(std::get<AudioScheduledBatchCommand>(prepared.payload).target.kind == AudioCommandTargetKind::NextBufferBoundary);
        }

        TEST_CASE("Scheduled audio batch rejects partial ambiguous and stale-shaped input", "[unit][audio][commands][batch]") {
            auto batch = Batch();
            AudioCommand prepared = Start(9);
            SECTION("empty") {
                batch.commandCount = 0;
            }
            SECTION("over capacity") {
                batch.commandCount = MaximumScheduledAudioCommands + 1;
            }
            SECTION("ambiguous buffer target") {
                batch.target = {.kind = AudioCommandTargetKind::NextBufferBoundary, .sampleFrame = 1};
            }
            SECTION("missing clock generation") {
                batch.target.clockGeneration = 0;
            }
            SECTION("mixed scope") {
                batch.commands[1].scope.epoch = 6;
            }
            const auto before = prepared;
            REQUIRE(MakeScheduledAudioBatchCommand(batch, Storage(), prepared) != ScheduledAudioCommandBatchStatus::Ok);
            REQUIRE(prepared.scope == before.scope);
            REQUIRE(std::holds_alternative<AudioStartVoiceCommand>(prepared.payload));
        }

        TEST_CASE("Scheduled audio batch rejects nesting and reserves critical capacity as one unit", "[unit][audio][commands][batch]") {
            auto batch = Batch();
            batch.commands[1].payload = AudioStopVoiceCommand{.voice = {.owner = Owner(), .slot = 3, .generation = 1}};
            AudioCommand prepared;
            REQUIRE(MakeScheduledAudioBatchCommand(batch, Storage(), prepared) == ScheduledAudioCommandBatchStatus::Ok);
            REQUIRE(ClassifyAudioCommand(prepared) == AudioCommandClass::Critical);

            batch.commands[1] = prepared;
            REQUIRE(NormalizeScheduledAudioCommandBatch(batch, batch) == ScheduledAudioCommandBatchStatus::NestedBatch);

            auto otherStorage = Storage();
            otherStorage.owner = AudioRuntimeId::Create(18).Value();
            REQUIRE(MakeScheduledAudioBatchCommand(Batch(), otherStorage, prepared) == ScheduledAudioCommandBatchStatus::InvalidStorage);
        }

        TEST_CASE("Scheduled audio batch remains all-or-none through ordinary and critical saturation", "[unit][audio][commands][batch]") {
            auto buffer = Buffer();
            REQUIRE(buffer.TryPublish({.sequence = 1, .command = Start(2)}) == AudioCommandPublishStatus::Published);
            REQUIRE(buffer.TryPublish({.sequence = 2, .command = Start(3)}) == AudioCommandPublishStatus::Published);
            REQUIRE(buffer.TryPublish({.sequence = 3, .command = Start(4)}) == AudioCommandPublishStatus::Published);

            AudioCommand ordinaryBatch;
            REQUIRE(MakeScheduledAudioBatchCommand(Batch(), Storage(), ordinaryBatch) == ScheduledAudioCommandBatchStatus::Ok);
            REQUIRE(buffer.TryPublish({.sequence = 4, .command = ordinaryBatch}) == AudioCommandPublishStatus::OrdinaryFull);

            auto critical = Batch();
            critical.commands[1].payload = AudioStopVoiceCommand{.voice = {.owner = Owner(), .slot = 3, .generation = 1}};
            AudioCommand criticalBatch;
            REQUIRE(MakeScheduledAudioBatchCommand(critical, Storage(), criticalBatch) == ScheduledAudioCommandBatchStatus::Ok);
            REQUIRE(buffer.TryPublish({.sequence = 4, .command = criticalBatch}) == AudioCommandPublishStatus::Published);
            REQUIRE(buffer.TryPublish({.sequence = 5, .command = criticalBatch}) == AudioCommandPublishStatus::CriticalRetry);

            AudioCommandRecord output;
            REQUIRE(buffer.TryConsume(output));
            REQUIRE(output.sequence == 1);
            REQUIRE(buffer.TryPublish({.sequence = 5, .command = criticalBatch}) == AudioCommandPublishStatus::Published);
            for (std::uint64_t sequence = 2; sequence <= 5; ++sequence) {
                REQUIRE(buffer.TryConsume(output));
                REQUIRE(output.sequence == sequence);
            }
        }
    }  // namespace
}  // namespace Horo::Audio
