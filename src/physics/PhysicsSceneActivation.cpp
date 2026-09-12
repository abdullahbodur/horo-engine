#include "Horo/Physics/PhysicsSceneActivation.h"

#include "Horo/Physics/CharacterWorld.h"

#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <new>
#include <thread>
#include <utility>

namespace Horo::Physics {
    namespace {
        /** @brief Owns one unpublished or active scene's dependency-ordered Physics resources. */
        class PhysicsSceneCandidate final : public Runtime::SceneActivationCandidate {
        public:
            PhysicsSceneCandidate(std::unique_ptr<PhysicsWorld> physics, std::unique_ptr<Character::CharacterWorld> character,
                                  const PhysicsSceneActivationAuthority &authority, const PhysicsSceneActivationEvidence evidence) noexcept
                : physics_(std::move(physics)), character_(std::move(character)), authority_(&authority), evidence_(evidence) {}

            [[nodiscard]] Result<void> ValidatePublication() const override {
                return authority_->IsCurrent(evidence_) ? Result<void>::Success()
                                                        : Result<void>::Failure(MakeError(PhysicsErrors::QuerySnapshotStale));
            }

            void Shutdown() noexcept override {
                character_->Shutdown();
                physics_->Shutdown();
            }

        private:
            std::unique_ptr<PhysicsWorld> physics_;
            std::unique_ptr<Character::CharacterWorld> character_;
            const PhysicsSceneActivationAuthority *authority_{};
            PhysicsSceneActivationEvidence evidence_;
        };
    }  // namespace

    /** @copydoc PhysicsSceneActivationAuthority::PhysicsSceneActivationAuthority */
    PhysicsSceneActivationAuthority::PhysicsSceneActivationAuthority() noexcept : ownerThread_(std::this_thread::get_id()) {}

    /** @copydoc PhysicsSceneActivationAuthority::Capture */
    PhysicsSceneActivationEvidence PhysicsSceneActivationAuthority::Capture() const noexcept {
        return current_;
    }

    /** @copydoc PhysicsSceneActivationAuthority::IsCurrent */
    bool PhysicsSceneActivationAuthority::IsCurrent(const PhysicsSceneActivationEvidence evidence) const noexcept {
        return std::this_thread::get_id() == ownerThread_ && evidence == current_;
    }

    Result<void> PhysicsSceneActivationAuthority::Advance(std::uint64_t &generation) const {
        if (std::this_thread::get_id() != ownerThread_)
            return Result<void>::Failure(MakeError(PhysicsErrors::ThreadAffinityViolation));
        if (generation == std::numeric_limits<std::uint64_t>::max())
            return Result<void>::Failure(MakeError(PhysicsErrors::GenerationExhausted));
        ++generation;
        return Result<void>::Success();
    }

    /** @copydoc PhysicsSceneActivationAuthority::AdvanceCollisionFilterGeneration */
    Result<void> PhysicsSceneActivationAuthority::AdvanceCollisionFilterGeneration() {
        return Advance(current_.collisionFilterGeneration);
    }

    /** @copydoc PhysicsSceneActivationAuthority::AdvanceOriginGeneration */
    Result<void> PhysicsSceneActivationAuthority::AdvanceOriginGeneration() {
        return Advance(current_.originGeneration);
    }

    /** @copydoc PhysicsSceneActivationParticipant::PhysicsSceneActivationParticipant */
    PhysicsSceneActivationParticipant::PhysicsSceneActivationParticipant(PhysicsRuntime &runtime,
                                                                         PhysicsSceneActivationAuthority &authority,
                                                                         PhysicsSceneActivationSettings settings) noexcept
        : runtime_(&runtime), authority_(&authority), settings_(std::move(settings)) {}

    /** @copydoc PhysicsSceneActivationParticipant::Prepare */
    Result<std::unique_ptr<Runtime::SceneActivationCandidate>> PhysicsSceneActivationParticipant::Prepare(
        const Runtime::RuntimeSceneDefinition &, const Runtime::RuntimeSceneView scene) {
        if (const std::array valid{runtime_->State() == PhysicsRuntimeState::Ready, scene.IsCurrent(), scene.RuntimeId().IsValid()};
            !std::ranges::all_of(valid, std::identity{})) {
            return Result<std::unique_ptr<Runtime::SceneActivationCandidate>>::Failure(MakeError(PhysicsErrors::WorldInvalid));
        }

        const PhysicsSceneActivationEvidence evidence = authority_->Capture();
        const auto identity = runtime_->IssueWorldIdentity();
        if (identity.HasError())
            return Result<std::unique_ptr<Runtime::SceneActivationCandidate>>::Failure(identity.ErrorValue());
        auto physics = runtime_->PrepareWorld(settings_.physics);
        if (physics.HasError())
            return Result<std::unique_ptr<Runtime::SceneActivationCandidate>>::Failure(physics.ErrorValue());
        auto character = Character::CharacterWorld::Prepare({scene.RuntimeId().value, identity.Value(), evidence.collisionFilterGeneration,
                                                             evidence.originGeneration},
                                                            settings_.character);
        if (character.HasError())
            return Result<std::unique_ptr<Runtime::SceneActivationCandidate>>::Failure(character.ErrorValue());
        if (const Result<void> activated = physics.Value()->Activate(identity.Value()); activated.HasError())
            return Result<std::unique_ptr<Runtime::SceneActivationCandidate>>::Failure(activated.ErrorValue());
        if (const Result<void> activated = character.Value()->Activate(); activated.HasError()) {
            physics.Value()->Shutdown();
            return Result<std::unique_ptr<Runtime::SceneActivationCandidate>>::Failure(activated.ErrorValue());
        }
        try {
            auto candidate =
                std::make_unique<PhysicsSceneCandidate>(std::move(physics).Value(), std::move(character).Value(), *authority_, evidence);
            return Result<std::unique_ptr<Runtime::SceneActivationCandidate>>::Success(std::move(candidate));
        } catch (const std::bad_alloc &) {
            return Result<std::unique_ptr<Runtime::SceneActivationCandidate>>::Failure(MakeError(PhysicsErrors::CapacityExceeded));
        }
    }
}  // namespace Horo::Physics
