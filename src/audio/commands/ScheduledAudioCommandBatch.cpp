#include "Horo/Audio/ScheduledAudioCommandBatch.h"

#include <algorithm>
#include <array>
#include <ranges>

namespace Horo::Audio {
    namespace {
        /** @brief Enforce one explicit target representation without accepting ambiguous unused fields. */
        bool ValidTarget(const AudioCommandTarget &target) noexcept {
            using enum AudioCommandTargetKind;
            if (target.kind == NextBufferBoundary) {
                return target.sampleFrame == 0 && target.clockGeneration == 0 && target.discontinuityRevision == 0;
            }
            return target.kind == ExactSampleFrame && target.clockGeneration != 0 && target.discontinuityRevision != 0;
        }

        /** @brief Check the process-local retained-storage identity without resolving or borrowing it. */
        bool ValidStorage(const AudioMemoryHandle &storage, const AudioRuntimeId owner) noexcept {
            return storage.owner == owner && storage.owner.IsValid() && storage.pool.IsValid() && storage.slot != 0 &&
                   storage.generation != 0;
        }
    }  // namespace

    /** @copydoc NormalizeScheduledAudioCommandBatch */
    ScheduledAudioCommandBatchStatus NormalizeScheduledAudioCommandBatch(const ScheduledAudioCommandBatch &input,
                                                                         ScheduledAudioCommandBatch &normalized) noexcept {
        using enum ScheduledAudioCommandBatchStatus;
        if (input.commandCount == 0 || input.commandCount > MaximumScheduledAudioCommands) {
            return InvalidCount;
        }
        if (!ValidTarget(input.target)) {
            return InvalidTarget;
        }

        ScheduledAudioCommandBatch candidate{.target = input.target, .commandCount = input.commandCount};
        for (std::uint32_t index = 0; index < input.commandCount; ++index) {
            if (std::holds_alternative<AudioScheduledBatchCommand>(input.commands[index].payload)) {
                return NestedBatch;
            }
            if (NormalizeAudioCommand(input.commands[index], candidate.commands[index]) != AudioCommandStatus::Ok) {
                return InvalidCommand;
            }
            if (index != 0 && candidate.commands[index].scope != candidate.commands[0].scope) {
                return MixedScope;
            }
        }
        normalized = candidate;
        return Ok;
    }

    /** @copydoc MakeScheduledAudioBatchCommand */
    ScheduledAudioCommandBatchStatus MakeScheduledAudioBatchCommand(const ScheduledAudioCommandBatch &batch,
                                                                    const AudioMemoryHandle storage, AudioCommand &command) noexcept {
        using enum ScheduledAudioCommandBatchStatus;
        ScheduledAudioCommandBatch normalized;
        if (const auto status = NormalizeScheduledAudioCommandBatch(batch, normalized); status != Ok) {
            return status;
        }
        const auto scope = normalized.commands[0].scope;
        if (!ValidStorage(storage, scope.owner)) {
            return InvalidStorage;
        }
        const auto children = std::span(normalized.commands).first(normalized.commandCount);
        const bool containsCritical = std::ranges::any_of(children, [](const AudioCommand &child) {
            return ClassifyAudioCommand(child) == AudioCommandClass::Critical;
        });
        command = {.scope = scope,
                   .payload = AudioScheduledBatchCommand{.storage = storage,
                                                         .target = normalized.target,
                                                         .commandCount = normalized.commandCount,
                                                         .containsCriticalCommand = containsCritical}};
        return Ok;
    }
}  // namespace Horo::Audio
