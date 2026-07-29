#pragma once

/**
 * @file SceneFileWatchService.h
 * @brief Bounded background canonical-scene fingerprint inspection.
 */

#include "Horo/Foundation/JobSystem.h"
#include "editor/document/SceneDocumentPersistence.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace Horo::Editor {
    /** @brief Owner-thread update produced by one background canonical-scene inspection. */
    struct SceneFileWatchUpdate {
        std::uint64_t generation{};
        std::optional<SceneFileFingerprint> fingerprint;
        std::optional<Error> error;
    };

    /** @brief Owns at most one non-blocking canonical-scene inspection job. */
    class SceneFileWatchService {
    public:
        /**
         * @brief Creates a scene watcher over an injected scheduler.
         * @param jobs Scheduler that outlives this service.
         */
        explicit SceneFileWatchService(JobSystem &jobs);
        /** @brief Cancels and joins an accepted inspection before releasing state. */
        ~SceneFileWatchService();
        SceneFileWatchService(const SceneFileWatchService &) = delete;
        SceneFileWatchService &operator=(const SceneFileWatchService &) = delete;

        /**
         * @brief Schedules one bounded fingerprint inspection.
         * @param absoluteProjectRoot Absolute root containing the canonical scene.
         * @param absoluteScenePath Absolute project-contained scene path.
         * @return Monotonic generation or a typed admission/lifecycle error.
         */
        [[nodiscard]] Result<std::uint64_t> Request(const std::filesystem::path &absoluteProjectRoot,
                                                    const std::filesystem::path &absoluteScenePath);

        /**
         * @brief Drains completed current-generation updates without waiting.
         * @return Zero or one owner-thread update.
         */
        [[nodiscard]] std::vector<SceneFileWatchUpdate> DrainUpdates();
        /** @brief Returns true while an accepted inspection has not reached a terminal state. */
        [[nodiscard]] bool HasPendingInspection() const noexcept;
        /**
         * @brief Cancels accepted work and invalidates queued updates while keeping the service reusable.
         *
         * Use this after an explicit owner-thread save or reload establishes a new canonical baseline.
         */
        void Reset() noexcept;
        /** @brief Cancels and joins accepted work; idempotent. */
        void Shutdown() noexcept;

    private:
        struct State;
        std::unique_ptr<State> state_;
    };
}  // namespace Horo::Editor
