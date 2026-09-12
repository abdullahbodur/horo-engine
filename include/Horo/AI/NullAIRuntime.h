#pragma once

/**
 * @file NullAIRuntime.h
 * @brief Explicit unavailable gameplay-AI runtime for headless compositions without AI capability.
 */

#include "Horo/AI/AITaskLifecycle.h"

namespace Horo::AI {
    /**
     * @brief Deliberate absence of gameplay-AI execution capability.
     * @details The null runtime is inert: it owns no agents, starts no tasks, and never fabricates a successful decision.
     */
    class NullAiRuntime final {
    public:
        /** @brief Reports deliberate capability absence. @return Always false. */
        [[nodiscard]] constexpr bool IsAvailable() const noexcept {
            return false;
        }

        /**
         * @brief Rejects task admission without mutating the supplied lifecycle.
         * @param lifecycle Borrowed task lifecycle that remains Idle.
         * @param context Borrowed operation context retained by neither runtime nor lifecycle.
         * @return AIErrors::RuntimeUnavailable.
         */
        [[nodiscard]] Result<void> StartTask(AiTaskLifecycle &lifecycle, const AiTaskOperationContext &context) const;
    };
}  // namespace Horo::AI
