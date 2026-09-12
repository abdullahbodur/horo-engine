#include "Horo/Physics/CharacterWorld.h"

#include "CharacterControllerRegistry.h"

#include <new>
#include <utility>

namespace Horo::Character {
    namespace {
        /** @brief Target-private owned controller state prepared for later fixed-tick behavior. */
        struct CharacterControllerRecord final {
            CharacterControllerDescriptor descriptor;
        };

        /** @brief Validates the immutable owner tuple selected before slot allocation. */
        [[nodiscard]] Result<void> ValidateWorldDescriptor(const CharacterWorldDescriptor &descriptor) {
            if (descriptor.sceneGeneration == 0 || !descriptor.identity.IsValid() || !descriptor.physicsWorld.IsValid())
                return Result<void>::Failure(MakeError(CharacterErrors::WorldInvalid));
            return Result<void>::Success();
        }
    }  // namespace

    struct CharacterWorld::Impl final {
        Impl(const CharacterWorldDescriptor &owner, const CharacterWorldSettings &worldSettings,
             Detail::CharacterControllerRegistry<CharacterControllerRecord> &&controllerRegistry)
            : descriptor(owner), settings(worldSettings), controllers(std::move(controllerRegistry)) {}

        CharacterWorldDescriptor descriptor;
        CharacterWorldSettings settings;
        Detail::CharacterControllerRegistry<CharacterControllerRecord> controllers;
        CharacterWorldState state{CharacterWorldState::Prepared};
    };

    /** @copydoc CharacterWorld::Prepare */
    Result<std::unique_ptr<CharacterWorld>> CharacterWorld::Prepare(const CharacterWorldDescriptor &descriptor,
                                                                    const CharacterWorldSettings &settings) {
        if (const auto valid = ValidateWorldDescriptor(descriptor); valid.HasError())
            return Result<std::unique_ptr<CharacterWorld>>::Failure(valid.ErrorValue());

        auto registry =
            Detail::CharacterControllerRegistry<CharacterControllerRecord>::Create(descriptor.sceneGeneration, descriptor.identity,
                                                                                   {.maximumSlots =
                                                                                        settings.Values().capacities.maximumControllers});
        if (registry.HasError())
            return Result<std::unique_ptr<CharacterWorld>>::Failure(registry.ErrorValue());

        try {
            auto impl = std::make_unique<Impl>(descriptor, settings, std::move(registry).Value());
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
        if (impl_->state != CharacterWorldState::Prepared)
            return Result<void>::Failure(MakeError(CharacterErrors::InvalidState));
        impl_->state = CharacterWorldState::Active;
        return Result<void>::Success();
    }

    /** @copydoc CharacterWorld::CreateController */
    Result<CharacterControllerHandle> CharacterWorld::CreateController(const CharacterControllerDescriptor &descriptor) {
        if (impl_->state == CharacterWorldState::Destroyed)
            return Result<CharacterControllerHandle>::Failure(MakeError(CharacterErrors::InvalidState));
        if (const auto valid = ValidateCharacterControllerDescriptor(descriptor); valid.HasError())
            return Result<CharacterControllerHandle>::Failure(valid.ErrorValue());
        if (descriptor.sceneGeneration != impl_->descriptor.sceneGeneration || descriptor.characterWorld != impl_->descriptor.identity ||
            descriptor.physicsWorld != impl_->descriptor.physicsWorld)
            return Result<CharacterControllerHandle>::Failure(MakeError(CharacterErrors::HandleWorldMismatch));
        if (descriptor.maximumContacts > impl_->settings.Values().work.maximumContactsPerMovement)
            return Result<CharacterControllerHandle>::Failure(
                MakeError(CharacterErrors::CapacityExceeded, "Controller contact capacity exceeds the Character world work budget."));
        return impl_->controllers.Acquire(CharacterControllerRecord{descriptor});
    }

    /** @copydoc CharacterWorld::DestroyController */
    Result<void> CharacterWorld::DestroyController(const CharacterControllerHandle &handle) {
        if (impl_->state == CharacterWorldState::Destroyed)
            return Result<void>::Failure(MakeError(CharacterErrors::InvalidState));
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
        return impl_->controllers.ActiveCount();
    }

    /** @copydoc CharacterWorld::ControllerCapacity */
    std::size_t CharacterWorld::ControllerCapacity() const noexcept {
        return impl_->controllers.Capacity();
    }
}  // namespace Horo::Character
