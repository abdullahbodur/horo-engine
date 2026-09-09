#pragma once

/** @file ScheduledAudioCommandBatch.h
 * @brief Fixed-capacity atomic audio command batch preparation.
 */

#include "Horo/Audio/AudioCommands.h"

#include <array>

namespace Horo::Audio {
    /** @brief Owned fixed-capacity commands sharing exactly one callback application boundary. */
    struct ScheduledAudioCommandBatch final {
        AudioCommandTarget target;
        std::uint32_t commandCount{};
        std::array<AudioCommand, MaximumScheduledAudioCommands> commands;
    };

    /** @brief Preparation result; rejection never changes the output command. */
    enum class ScheduledAudioCommandBatchStatus : std::uint8_t {
        Ok,
        InvalidCount,
        InvalidTarget,
        InvalidCommand,
        MixedScope,
        NestedBatch,
        InvalidStorage
    };

    /**
     * @brief Validate and normalize a complete batch into caller-owned fixed storage.
     * @param input Commands in declared stable order with one buffer or exact-sample target.
     * @param normalized Output assigned only on success; aliasing input is allowed.
     * @return Typed structural result. All commands must share one exact runtime, epoch and scene scope.
     * @note Exact-sample targets require nonzero clock/discontinuity generations. Buffer targets require all timing fields zero.
     */
    [[nodiscard]] ScheduledAudioCommandBatchStatus NormalizeScheduledAudioCommandBatch(const ScheduledAudioCommandBatch &input,
                                                                                       ScheduledAudioCommandBatch &normalized) noexcept;

    /**
     * @brief Build the single command record that publishes a normalized retained batch atomically.
     * @param batch Valid complete batch stored at storage by its control owner through callback acknowledgement.
     * @param storage Live CommandStorage handle whose owner matches the batch scope.
     * @param command Output assigned only on success and suitable for ordinary staging publication.
     * @return Ok or a typed batch/storage failure. The callback applies every child in array order at target.
     */
    [[nodiscard]] ScheduledAudioCommandBatchStatus MakeScheduledAudioBatchCommand(const ScheduledAudioCommandBatch &batch,
                                                                                  const AudioMemoryHandle &storage,
                                                                                  AudioCommand &command) noexcept;
}  // namespace Horo::Audio
