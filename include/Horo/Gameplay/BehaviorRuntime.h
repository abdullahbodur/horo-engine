#pragma once

/**
 * @file BehaviorRuntime.h
 * @brief Deterministic scene-scoped behavior lifecycle, input, event, and deferred mutation runner.
 */

#include "Horo/Gameplay/BehaviorRegistry.h"
#include "Horo/Runtime/Scene/RuntimeScene.h"

#include <memory>

namespace Horo::Gameplay {
    /** @brief Explicit per-scene bounds for behavior instances and deferred custom events. */
    struct BehaviorRuntimeLimits {
        std::size_t maximumInstances{16'384};
        std::size_t maximumQueuedEvents{4'096};
    };

    /** @brief Owns every behavior instance attached to one active runtime scene. */
    class BehaviorRuntime final {
    public:
        /**
         * @brief Constructs and activates all known scene behavior instances.
         * @param scene Runtime scene that outlives this runner.
         * @param registry Frozen registry that outlives this runner and its module factories.
         * @param limits Explicit admission budgets.
         * @return Active runner or a typed validation/factory error without partial lifetime leakage.
         */
        [[nodiscard]] static Result<std::unique_ptr<BehaviorRuntime>> Create(Runtime::RuntimeScene &scene, const BehaviorRegistry &registry,
                                                                             BehaviorRuntimeLimits limits = {});
        ~BehaviorRuntime();
        BehaviorRuntime(const BehaviorRuntime &) = delete;
        BehaviorRuntime &operator=(const BehaviorRuntime &) = delete;

        /** @brief Delivers queued events/input, runs one deterministic tick, and commits mutations. */
        [[nodiscard]] Result<void> FixedUpdate(std::span<const GameplayInputAction> input, FixedDeltaTime delta);
        /** @brief Runs presentation-only callbacks without committing simulation mutation. */
        void PresentationUpdate(FrameDeltaTime delta);
        /** @brief Enables or disables one attachment with exact lifecycle transitions. */
        [[nodiscard]] Result<void> SetEnabled(BehaviorInstanceId instance, bool enabled);
        /** @brief Runs disable/destroy and releases every module-owned instance exactly once. */
        void Shutdown() noexcept;
        /** @brief Reports the number of constructed scene-scoped instances. */
        [[nodiscard]] std::size_t InstanceCount() const noexcept;

    private:
        struct Impl;
        explicit BehaviorRuntime(std::unique_ptr<Impl> impl) noexcept;
        std::unique_ptr<Impl> impl_;
    };
}  // namespace Horo::Gameplay
