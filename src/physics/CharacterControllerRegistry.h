#pragma once

/** @file CharacterControllerRegistry.h
 * @brief Target-private bounded storage for scene-scoped Character controller records.
 */

#include "GenerationalSlotStorage.h"
#include "Horo/Physics/CharacterControllerContracts.h"
#include "Horo/Physics/CharacterWorldSettings.h"

#include <cstddef>
#include <format>
#include <new>
#include <type_traits>
#include <utility>

namespace Horo::Character::Detail {
    /** @brief Testable registry limits; production uses the full non-wrapping generation range. */
    using CharacterControllerRegistryLimits = Physics::Detail::GenerationalSlotStorageLimits;

    /** @brief Owns one bounded mapping from Character handles to target-private controller records. */
    template <typename Value> class CharacterControllerRegistry final {
        static_assert(std::is_nothrow_move_constructible_v<Value>, "Character registry values must move without throwing.");
        static_assert(std::is_nothrow_destructible_v<Value>, "Character registry values must destruct without throwing.");

    public:
        /** @brief Allocates the complete slot table for one unpublished world candidate. */
        [[nodiscard]] static Result<CharacterControllerRegistry> Create(const std::uint64_t sceneGeneration, const CharacterWorldId world,
                                                                        const CharacterControllerRegistryLimits limits) {
            if (sceneGeneration == 0 || !world.IsValid())
                return Result<CharacterControllerRegistry>::Failure(MakeError(CharacterErrors::WorldInvalid));
            if (limits.maximumSlots == 0)
                return Result<CharacterControllerRegistry>::Failure(MakeError(CharacterErrors::DescriptorInvalid));
            if (limits.maximumSlots > CharacterWorldSettingLimits::MaximumControllers)
                return Result<CharacterControllerRegistry>::Failure(MakeError(CharacterErrors::CapacityExceeded));
            if (limits.maximumGeneration == 0)
                return Result<CharacterControllerRegistry>::Failure(
                    MakeError(CharacterErrors::DescriptorInvalid, "Character slot generation ceiling must be non-zero."));
            try {
                return Result<CharacterControllerRegistry>::Success(CharacterControllerRegistry{sceneGeneration, world, limits});
            } catch (const std::bad_alloc &) {
                return Result<CharacterControllerRegistry>::Failure(
                    MakeError(CharacterErrors::CapacityExceeded, "Unable to allocate the bounded Character controller registry."));
            }
        }

        CharacterControllerRegistry(const CharacterControllerRegistry &) = delete;
        CharacterControllerRegistry &operator=(const CharacterControllerRegistry &) = delete;

        CharacterControllerRegistry(CharacterControllerRegistry &&other) noexcept
            : sceneGeneration_(std::exchange(other.sceneGeneration_, 0)), world_(std::exchange(other.world_, {})),
              storage_(std::move(other.storage_)) {}

        CharacterControllerRegistry &operator=(CharacterControllerRegistry &&other) noexcept {
            if (this == &other)
                return *this;
            sceneGeneration_ = std::exchange(other.sceneGeneration_, 0);
            world_ = std::exchange(other.world_, {});
            storage_ = std::move(other.storage_);
            return *this;
        }

        /** @brief Installs one record and returns its exact scene/world/slot generation. */
        [[nodiscard]] Result<CharacterControllerHandle> Acquire(Value value) {
            const std::optional<Physics::Detail::GenerationalSlot> slot = storage_.Acquire(std::move(value));
            if (!slot)
                return Result<CharacterControllerHandle>::Failure(MakeError(FullError()));
            return Result<CharacterControllerHandle>::Success({sceneGeneration_, world_, {slot->index, slot->generation}});
        }

        /** @brief Resolves one exact live record to a borrow bounded by registry mutation or destruction. */
        [[nodiscard]] Result<const Value *> Resolve(const CharacterControllerHandle &handle) const {
            return storage_.ResolveOwned(handle, [this](const auto &candidate) {
                return ValidateCharacterControllerHandleOwner(candidate, sceneGeneration_, world_);
            }, HandleError);
        }

        /** @brief Removes one exact live generation and recycles or permanently retires its slot. */
        [[nodiscard]] Result<void> Remove(const CharacterControllerHandle &handle) {
            return storage_.RemoveOwned(handle, [this](const auto &candidate) {
                return ValidateCharacterControllerHandleOwner(candidate, sceneGeneration_, world_);
            }, HandleError);
        }

        /** @brief Destroys every resident record without allocating; the registry is terminal afterward. */
        void Drain() noexcept {
            storage_.Drain();
        }

        [[nodiscard]] std::size_t Capacity() const noexcept {
            return storage_.Capacity();
        }

        [[nodiscard]] std::size_t ActiveCount() const noexcept {
            return storage_.ActiveCount();
        }

    private:
        CharacterControllerRegistry(const std::uint64_t sceneGeneration, const CharacterWorldId world,
                                    const CharacterControllerRegistryLimits limits)
            : sceneGeneration_(sceneGeneration), world_(world), storage_(limits) {}

        [[nodiscard]] const ErrorCodeDescriptor &FullError() const noexcept {
            return storage_.AllSlotsExhausted() ? CharacterErrors::GenerationExhausted : CharacterErrors::CapacityExceeded;
        }

        [[nodiscard]] static Error HandleError(const CharacterControllerHandle &handle) {
            return MakeError(CharacterErrors::HandleStale,
                             std::format("Character handle scene {}, world {}, slot {}, generation {} is not current.",
                                         handle.sceneGeneration, handle.world.Value(), handle.slot.index, handle.slot.generation));
        }

        std::uint64_t sceneGeneration_{};
        CharacterWorldId world_;
        Physics::Detail::GenerationalSlotStorage<Value> storage_;
    };
}  // namespace Horo::Character::Detail
