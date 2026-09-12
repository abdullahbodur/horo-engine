#pragma once

/** @file CharacterControllerRegistry.h
 * @brief Target-private bounded storage for scene-scoped Character controller records.
 */

#include "Horo/Physics/CharacterControllerContracts.h"
#include "Horo/Physics/CharacterWorldSettings.h"

#include <cstddef>
#include <format>
#include <limits>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace Horo::Character::Detail {
    /** @brief Testable registry limits; production uses the full non-wrapping generation range. */
    struct CharacterControllerRegistryLimits final {
        std::uint32_t maximumSlots{};
        std::uint32_t maximumGeneration{std::numeric_limits<std::uint32_t>::max()};
    };

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
        CharacterControllerRegistry(CharacterControllerRegistry &&) noexcept = default;
        CharacterControllerRegistry &operator=(CharacterControllerRegistry &&) noexcept = default;

        /** @brief Installs one record and returns its exact scene/world/slot generation. */
        [[nodiscard]] Result<CharacterControllerHandle> Acquire(Value value) {
            if (freeHead_ == InvalidSlot)
                return Result<CharacterControllerHandle>::Failure(MakeError(FullError()));

            const std::uint32_t slotIndex = freeHead_;
            Entry &entry = entries_[slotIndex];
            freeHead_ = entry.nextFree;
            entry.nextFree = InvalidSlot;
            entry.value.emplace(std::move(value));
            ++activeCount_;
            return Result<CharacterControllerHandle>::Success({sceneGeneration_, world_, {slotIndex, entry.generation}});
        }

        /** @brief Resolves one exact live record to a borrow bounded by registry mutation or destruction. */
        [[nodiscard]] Result<const Value *> Resolve(const CharacterControllerHandle &handle) const {
            const auto index = ResolveIndex(handle);
            if (index.HasError())
                return Result<const Value *>::Failure(index.ErrorValue());
            return Result<const Value *>::Success(&*entries_[index.Value()].value);
        }

        /** @brief Removes one exact live generation and recycles or permanently retires its slot. */
        [[nodiscard]] Result<void> Remove(const CharacterControllerHandle &handle) {
            const auto index = ResolveIndex(handle);
            if (index.HasError())
                return Result<void>::Failure(index.ErrorValue());

            Entry &entry = entries_[index.Value()];
            entry.value.reset();
            --activeCount_;
            if (entry.generation == limits_.maximumGeneration) {
                ++exhaustedCount_;
                return Result<void>::Success();
            }
            ++entry.generation;
            entry.nextFree = freeHead_;
            freeHead_ = index.Value();
            return Result<void>::Success();
        }

        /** @brief Destroys every resident record without allocating; the registry is terminal afterward. */
        void Drain() noexcept {
            for (Entry &entry : entries_) {
                entry.value.reset();
                entry.nextFree = InvalidSlot;
            }
            freeHead_ = InvalidSlot;
            activeCount_ = 0;
        }

        [[nodiscard]] std::size_t Capacity() const noexcept {
            return entries_.size();
        }

        [[nodiscard]] std::size_t ActiveCount() const noexcept {
            return activeCount_;
        }

        [[nodiscard]] std::size_t ExhaustedCount() const noexcept {
            return exhaustedCount_;
        }

    private:
        static constexpr std::uint32_t InvalidSlot = std::numeric_limits<std::uint32_t>::max();

        struct Entry final {
            std::optional<Value> value;
            std::uint32_t generation{1};
            std::uint32_t nextFree{InvalidSlot};
        };

        CharacterControllerRegistry(const std::uint64_t sceneGeneration, const CharacterWorldId world,
                                    const CharacterControllerRegistryLimits limits)
            : sceneGeneration_(sceneGeneration), world_(world), limits_(limits), entries_(limits.maximumSlots) {
            for (std::uint32_t slot = 0; slot + 1 < limits.maximumSlots; ++slot)
                entries_[slot].nextFree = slot + 1;
            freeHead_ = 0;
        }

        [[nodiscard]] const ErrorCodeDescriptor &FullError() const noexcept {
            return exhaustedCount_ == entries_.size() ? CharacterErrors::GenerationExhausted : CharacterErrors::CapacityExceeded;
        }

        [[nodiscard]] Result<std::uint32_t> ResolveIndex(const CharacterControllerHandle &handle) const {
            if (const auto owner = ValidateCharacterControllerHandleOwner(handle, sceneGeneration_, world_); owner.HasError())
                return Result<std::uint32_t>::Failure(owner.ErrorValue());
            if (handle.slot.index >= entries_.size())
                return Result<std::uint32_t>::Failure(HandleError(handle));
            const Entry &entry = entries_[handle.slot.index];
            if (!entry.value.has_value() || entry.generation != handle.slot.generation)
                return Result<std::uint32_t>::Failure(HandleError(handle));
            return Result<std::uint32_t>::Success(handle.slot.index);
        }

        [[nodiscard]] static Error HandleError(const CharacterControllerHandle &handle) {
            return MakeError(CharacterErrors::HandleStale,
                             std::format("Character handle scene {}, world {}, slot {}, generation {} is not current.",
                                         handle.sceneGeneration, handle.world.Value(), handle.slot.index, handle.slot.generation));
        }

        std::uint64_t sceneGeneration_{};
        CharacterWorldId world_;
        CharacterControllerRegistryLimits limits_;
        std::vector<Entry> entries_;
        std::uint32_t freeHead_{InvalidSlot};
        std::size_t activeCount_{};
        std::size_t exhaustedCount_{};
    };
}  // namespace Horo::Character::Detail
