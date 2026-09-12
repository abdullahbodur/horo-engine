#include "Horo/Physics/PhysicsSceneActivation.h"

#include "Horo/Physics/CharacterWorld.h"

#include <limits>
#include <new>
#include <utility>

namespace Horo::Physics {
    namespace {
        /** @brief Owns one unpublished or active scene's dependency-ordered Physics resources. */
        class PhysicsSceneCandidate final : public Runtime::SceneActivationCandidate {
        public:
            PhysicsSceneCandidate(std::unique_ptr<PhysicsWorld> physics, std::unique_ptr<Character::CharacterWorld> character,
                                  const PhysicsWorldId identity) noexcept
                : physics_(std::move(physics)), character_(std::move(character)), identity_(identity) {}

            [[nodiscard]] Result<void> Activate() override {
                if (const Result<void> activated = physics_->Activate(identity_); activated.HasError())
                    return activated;
                if (const Result<void> activated = character_->Activate(); activated.HasError()) {
                    Shutdown();
                    return activated;
                }
                return Result<void>::Success();
            }

            void Shutdown() noexcept override {
                character_->Shutdown();
                physics_->Shutdown();
            }

        private:
            std::unique_ptr<PhysicsWorld> physics_;
            std::unique_ptr<Character::CharacterWorld> character_;
            PhysicsWorldId identity_;
        };
    }  // namespace

    /** @copydoc PhysicsSceneActivationParticipant::PhysicsSceneActivationParticipant */
    PhysicsSceneActivationParticipant::PhysicsSceneActivationParticipant(PhysicsRuntime &runtime,
                                                                         PhysicsSceneActivationSettings settings) noexcept
        : runtime_(&runtime), settings_(std::move(settings)) {}

    /** @copydoc PhysicsSceneActivationParticipant::Prepare */
    Result<std::unique_ptr<Runtime::SceneActivationCandidate>> PhysicsSceneActivationParticipant::Prepare(
        const Runtime::RuntimeSceneDefinition &, const Runtime::RuntimeSceneView scene) {
        if (!runtime_ || runtime_->State() != PhysicsRuntimeState::Ready || !scene.IsCurrent() || !scene.RuntimeId().IsValid() ||
            settings_.collisionFilterGeneration == 0 || settings_.originGeneration == 0)
            return Result<std::unique_ptr<Runtime::SceneActivationCandidate>>::Failure(MakeError(PhysicsErrors::WorldInvalid));
        if (nextWorldIdentity_ == 0 || nextWorldIdentity_ == std::numeric_limits<std::uint64_t>::max())
            return Result<std::unique_ptr<Runtime::SceneActivationCandidate>>::Failure(MakeError(PhysicsErrors::GenerationExhausted));

        const auto identity = PhysicsWorldId::Create(nextWorldIdentity_++);
        if (identity.HasError())
            return Result<std::unique_ptr<Runtime::SceneActivationCandidate>>::Failure(identity.ErrorValue());
        auto physics = runtime_->PrepareWorld(settings_.physics);
        if (physics.HasError())
            return Result<std::unique_ptr<Runtime::SceneActivationCandidate>>::Failure(physics.ErrorValue());
        auto character = Character::CharacterWorld::Prepare({scene.RuntimeId().value, identity.Value(), settings_.collisionFilterGeneration,
                                                             settings_.originGeneration},
                                                            settings_.character);
        if (character.HasError())
            return Result<std::unique_ptr<Runtime::SceneActivationCandidate>>::Failure(character.ErrorValue());
        try {
            return Result<std::unique_ptr<Runtime::SceneActivationCandidate>>::Success(
                std::make_unique<PhysicsSceneCandidate>(std::move(physics).Value(), std::move(character).Value(), identity.Value()));
        } catch (const std::bad_alloc &) {
            return Result<std::unique_ptr<Runtime::SceneActivationCandidate>>::Failure(MakeError(PhysicsErrors::CapacityExceeded));
        }
    }
}  // namespace Horo::Physics
