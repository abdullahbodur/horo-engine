#pragma once

/** @file PhysicsSceneActivation.h
 * @brief Physics-owned aggregate scene activation participant and Character-world lifetime.
 */

#include "Horo/Physics/CharacterWorldSettings.h"
#include "Horo/Physics/PhysicsWorld.h"
#include "Horo/Runtime/Scene/RuntimeScene.h"

#include <cstdint>

namespace Horo::Physics {
    /** @brief Immutable policy pinned by one Physics scene-activation participant. */
    struct PhysicsSceneActivationSettings final {
        PhysicsWorldSettings physics;
        Character::CharacterWorldSettings character;
        std::uint64_t collisionFilterGeneration{};
        std::uint64_t originGeneration{};
    };

    /** @brief Prepares and retires paired Physics and Character worlds through RuntimeScene's aggregate boundary. */
    class PhysicsSceneActivationParticipant final : public Runtime::SceneActivationParticipant {
    public:
        /** @brief Binds one process Physics owner and immutable activation policy. */
        PhysicsSceneActivationParticipant(PhysicsRuntime &runtime, PhysicsSceneActivationSettings settings) noexcept;

        /** @copydoc Runtime::SceneActivationParticipant::Prepare */
        [[nodiscard]] Result<std::unique_ptr<Runtime::SceneActivationCandidate>> Prepare(const Runtime::RuntimeSceneDefinition &definition,
                                                                                         Runtime::RuntimeSceneView scene) override;

    private:
        PhysicsRuntime *runtime_{};
        PhysicsSceneActivationSettings settings_;
        std::uint64_t nextWorldIdentity_{1};
    };
}  // namespace Horo::Physics
