#include "Horo/Physics/CharacterWorld.h"

#include "CharacterControllerRegistry.h"

#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <mutex>
#include <new>
#include <thread>
#include <utility>

namespace Horo::Character {
    namespace {
        /** @brief Target-private owned controller state prepared for later fixed-tick behavior. */
        struct CharacterControllerRecord final {
            CharacterControllerDescriptor descriptor;
        };

        /** @brief Validates the immutable owner tuple selected before slot allocation. */
        std::mutex worldIdentityMutex;
        std::uint64_t nextWorldIdentity{1};

        [[nodiscard]] Result<CharacterWorldDescriptor> CompleteWorldDescriptor(const CharacterWorldPreparationDescriptor &descriptor) {
            const std::array valid{descriptor.sceneGeneration != 0, descriptor.physicsWorld.IsValid(),
                                   descriptor.collisionFilterGeneration != 0, descriptor.originGeneration != 0};
            if (!std::ranges::all_of(valid, std::identity{}))
                return Result<CharacterWorldDescriptor>::Failure(MakeError(CharacterErrors::WorldInvalid));
            const std::lock_guard identityLock{worldIdentityMutex};
            if (nextWorldIdentity == std::numeric_limits<std::uint64_t>::max())
                return Result<CharacterWorldDescriptor>::Failure(MakeError(CharacterErrors::GenerationExhausted));
            const std::uint64_t identityValue = nextWorldIdentity++;
            const auto identity = CharacterWorldId::Create(identityValue);
            if (identity.HasError())
                return Result<CharacterWorldDescriptor>::Failure(identity.ErrorValue());
            return Result<CharacterWorldDescriptor>::Success({descriptor.sceneGeneration, identity.Value(), descriptor.physicsWorld,
                                                              descriptor.collisionFilterGeneration, descriptor.originGeneration});
        }

        [[nodiscard]] Result<void> RequireOwnerThread(const std::thread::id ownerThread) {
            if (ownerThread != std::this_thread::get_id())
                return Result<void>::Failure(
                    MakeError(CharacterErrors::InvalidState, "Character world mutation requires its preparation thread."));
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> RequirePreparedMutation(const CharacterWorldState state, const std::thread::id ownerThread) {
            if (state != CharacterWorldState::Prepared)
                return Result<void>::Failure(MakeError(CharacterErrors::InvalidState));
            return RequireOwnerThread(ownerThread);
        }
    }  // namespace

    struct CharacterWorld::Impl final {
        Impl(const CharacterWorldDescriptor &owner, const CharacterWorldSettings &worldSettings,
             Detail::CharacterControllerRegistry<CharacterControllerRecord> &&controllerRegistry)
            : descriptor(owner), settings(worldSettings), controllers(std::move(controllerRegistry)) {}

        CharacterWorldDescriptor descriptor;
        CharacterWorldSettings settings;
        Detail::CharacterControllerRegistry<CharacterControllerRecord> controllers;
        std::thread::id ownerThread{std::this_thread::get_id()};
        CharacterWorldState state{CharacterWorldState::Prepared};
    };

    /** @copydoc CharacterWorld::Prepare */
    Result<std::unique_ptr<CharacterWorld>> CharacterWorld::Prepare(const CharacterWorldPreparationDescriptor &descriptor,
                                                                    const CharacterWorldSettings &settings) {
        const auto completed = CompleteWorldDescriptor(descriptor);
        if (completed.HasError())
            return Result<std::unique_ptr<CharacterWorld>>::Failure(completed.ErrorValue());
        const CharacterWorldDescriptor owner = completed.Value();

        try {
            Detail::CharacterControllerRegistry<CharacterControllerRecord> registry{owner.sceneGeneration,
                                                                                    owner.identity,
                                                                                    {.maximumSlots =
                                                                                         settings.Values().capacities.maximumControllers}};
            auto impl = std::make_unique<Impl>(owner, settings, std::move(registry));
            return Result<std::unique_ptr<CharacterWorld>>::Success(std::unique_ptr<CharacterWorld>{new CharacterWorld(std::move(impl))});
        } catch (const std::bad_alloc &) {
            return Result<std::unique_ptr<CharacterWorld>>::Failure(
                MakeError(CharacterErrors::CapacityExceeded, "Unable to allocate Character world ownership state."));
        }
    }

    /** @copydoc CharacterWorld::CharacterWorld */
    CharacterWorld::CharacterWorld(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

    /** @copydoc CharacterWorld::~CharacterWorld */
    CharacterWorld::~CharacterWorld() {
        Shutdown();
    }

    /** @copydoc CharacterWorld::Activate */
    Result<void> CharacterWorld::Activate() {
        if (const auto ready = RequirePreparedMutation(impl_->state, impl_->ownerThread); ready.HasError())
            return ready;
        impl_->state = CharacterWorldState::Active;
        return Result<void>::Success();
    }

    /** @copydoc CharacterWorld::CreateController */
    Result<CharacterControllerHandle> CharacterWorld::CreateController(const CharacterControllerDescriptor &descriptor) {
        if (const auto ready = RequirePreparedMutation(impl_->state, impl_->ownerThread); ready.HasError())
            return Result<CharacterControllerHandle>::Failure(
                MakeError(CharacterErrors::InvalidState, "Controller creation requires prepared owner-thread mutation."));
        if (const auto valid = ValidateCharacterControllerDescriptor(descriptor); valid.HasError())
            return Result<CharacterControllerHandle>::Failure(valid.ErrorValue());
        const std::array ownerMatches{descriptor.sceneGeneration == impl_->descriptor.sceneGeneration,
                                      descriptor.characterWorld == impl_->descriptor.identity,
                                      descriptor.physicsWorld == impl_->descriptor.physicsWorld};
        if (!std::ranges::all_of(ownerMatches, std::identity{}))
            return Result<CharacterControllerHandle>::Failure(MakeError(CharacterErrors::HandleWorldMismatch));
        if (descriptor.maximumContacts > impl_->settings.Values().work.maximumContactsPerMovement)
            return Result<CharacterControllerHandle>::Failure(
                MakeError(CharacterErrors::CapacityExceeded, "Controller contact capacity exceeds the Character world work budget."));
        return impl_->controllers.Acquire(CharacterControllerRecord{descriptor});
    }

    /** @copydoc CharacterWorld::DestroyController */
    Result<void> CharacterWorld::DestroyController(const CharacterControllerHandle &handle) {
        if (const auto ready = RequirePreparedMutation(impl_->state, impl_->ownerThread); ready.HasError())
            return Result<void>::Failure(
                MakeError(CharacterErrors::InvalidState, "Controller destruction requires prepared owner-thread mutation."));
        return impl_->controllers.Remove(handle);
    }

    /** @copydoc CharacterWorld::ControllerDescriptor */
    Result<CharacterControllerDescriptor> CharacterWorld::ControllerDescriptor(const CharacterControllerHandle &handle) const {
        if (impl_->state == CharacterWorldState::Destroyed)
            return Result<CharacterControllerDescriptor>::Failure(MakeError(CharacterErrors::InvalidState));
        const auto record = impl_->controllers.Resolve(handle);
        if (record.HasError())
            return Result<CharacterControllerDescriptor>::Failure(record.ErrorValue());
        return Result<CharacterControllerDescriptor>::Success(record.Value()->descriptor);
    }

    /** @copydoc CharacterWorld::Shutdown */
    void CharacterWorld::Shutdown() noexcept {
        if (impl_->state == CharacterWorldState::Destroyed)
            return;
        impl_->controllers.Drain();
        impl_->state = CharacterWorldState::Destroyed;
    }

    /** @copydoc CharacterWorld::State */
    CharacterWorldState CharacterWorld::State() const noexcept {
        return impl_->state;
    }

    /** @copydoc CharacterWorld::Descriptor */
    const CharacterWorldDescriptor &CharacterWorld::Descriptor() const noexcept {
        return impl_->descriptor;
    }

    /** @copydoc CharacterWorld::Settings */
    const CharacterWorldSettings &CharacterWorld::Settings() const noexcept {
        return impl_->settings;
    }

    /** @copydoc CharacterWorld::ActiveControllerCount */
    std::size_t CharacterWorld::ActiveControllerCount() const noexcept {
        return impl_->controllers.Statistics().active;
    }

    /** @copydoc CharacterWorld::ControllerCapacity */
    std::size_t CharacterWorld::ControllerCapacity() const noexcept {
        return impl_->controllers.Statistics().capacity;
    }
}  // namespace Horo::Character
