#pragma once

/** @file PhysicsHandleRegistry.h
 * @brief Target-private bounded storage for world-scoped Physics handles and native mappings.
 */

#include "Horo/Physics/PhysicsIdentity.h"
#include "Horo/Physics/PhysicsWorldBudgets.h"

#include <cstddef>
#include <format>
#include <limits>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace Horo::Physics::Detail {
    /** @brief Testable registry limits; production uses the full non-wrapping generation range. */
    struct PhysicsHandleRegistryLimits final {
        std::uint32_t maximumSlots{};
        std::uint32_t maximumGeneration{std::numeric_limits<std::uint32_t>::max()};
    };

    /**
     * @brief Owns one typed world-local mapping without exposing its private value across the Physics target.
     *
     * Storage is allocated while the world is still a detached candidate. BindOwner assigns the host-issued
     * world generation during activation without allocation. Removed slots advance monotonically; a slot at
     * the configured generation ceiling is permanently retired instead of wrapping. Resolve returns a borrow
     * that remains valid only until removal, registry movement, or registry destruction.
     *
     * @tparam Handle One of the distinct Horo BodyHandle, ShapeHandle, or ConstraintHandle types.
     * @tparam Value Target-private native mapping or owning lease associated with one live handle.
     */
    template <typename Handle, typename Value> class PhysicsHandleRegistry final {
        static_assert(std::is_nothrow_move_constructible_v<Value>, "Physics registry values must move without throwing.");
        static_assert(std::is_nothrow_destructible_v<Value>, "Physics registry values must be safely destructible during teardown.");

    public:
        /**
         * @brief Allocates the complete bounded slot table for an unpublished world candidate.
         * @param limits Validated maximum slots and non-wrapping generation ceiling.
         * @return Unbound registry, or a stable capacity/descriptor error before publication.
         */
        [[nodiscard]] static Result<PhysicsHandleRegistry> Create(const PhysicsHandleRegistryLimits limits) {
            if (limits.maximumSlots > MaximumPhysicsResourceRecords)
                return Result<PhysicsHandleRegistry>::Failure(MakeError(PhysicsErrors::CapacityExceeded));
            if (limits.maximumGeneration == 0)
                return Result<PhysicsHandleRegistry>::Failure(
                    MakeError(PhysicsErrors::DescriptorInvalid, "Physics registry generation ceiling must be non-zero."));
            try {
                return Result<PhysicsHandleRegistry>::Success(PhysicsHandleRegistry{limits});
            } catch (const std::bad_alloc &) {
                return Result<PhysicsHandleRegistry>::Failure(
                    MakeError(PhysicsErrors::CapacityExceeded, "Unable to allocate the bounded Physics handle registry."));
            }
        }

        PhysicsHandleRegistry(const PhysicsHandleRegistry &) = delete;
        PhysicsHandleRegistry &operator=(const PhysicsHandleRegistry &) = delete;

        PhysicsHandleRegistry(PhysicsHandleRegistry &&other) noexcept
            : limits_(other.limits_), entries_(std::move(other.entries_)), owner_(std::exchange(other.owner_, {})),
              freeHead_(std::exchange(other.freeHead_, InvalidSlot)), activeCount_(std::exchange(other.activeCount_, 0)),
              exhaustedCount_(std::exchange(other.exhaustedCount_, 0)) {
            other.limits_ = {};
            other.entries_.clear();
        }

        PhysicsHandleRegistry &operator=(PhysicsHandleRegistry &&other) noexcept {
            if (this == &other)
                return *this;
            limits_ = other.limits_;
            entries_ = std::move(other.entries_);
            owner_ = std::exchange(other.owner_, {});
            freeHead_ = std::exchange(other.freeHead_, InvalidSlot);
            activeCount_ = std::exchange(other.activeCount_, 0);
            exhaustedCount_ = std::exchange(other.exhaustedCount_, 0);
            other.limits_ = {};
            other.entries_.clear();
            return *this;
        }

        /**
         * @brief Binds the registry exactly once to the published world generation.
         * @param owner Non-zero, process-unique identity supplied by aggregate scene activation.
         * @return Success, WorldInvalid for malformed identity, or InvalidState after an earlier bind.
         * @post Successful binding performs no allocation and publishes no object handle by itself.
         */
        [[nodiscard]] Result<void> BindOwner(const PhysicsWorldId owner) {
            if (!owner.IsValid())
                return Result<void>::Failure(MakeError(PhysicsErrors::WorldInvalid));
            if (owner_.IsValid())
                return Result<void>::Failure(MakeError(PhysicsErrors::InvalidState, "Physics registry owner is already bound."));
            owner_ = owner;
            return Result<void>::Success();
        }

        /**
         * @brief Installs one already-created private value and issues its exact Horo handle.
         * @param value Native mapping or lease whose move and destruction cannot throw.
         * @return New handle, CapacityExceeded while live slots fill the bound, GenerationExhausted
         * when every slot is permanently retired, or InvalidState before owner binding.
         */
        [[nodiscard]] Result<Handle> Acquire(Value value) {
            if (!owner_.IsValid())
                return Result<Handle>::Failure(MakeError(PhysicsErrors::InvalidState, "Physics registry owner is not active."));
            if (freeHead_ == InvalidSlot)
                return Result<Handle>::Failure(MakeError(FullError()));

            const std::uint32_t slotIndex = freeHead_;
            Entry &entry = entries_[slotIndex];
            freeHead_ = entry.nextFree;
            entry.nextFree = InvalidSlot;
            entry.value.emplace(std::move(value));
            ++activeCount_;
            return Result<Handle>::Success(Handle{owner_, {slotIndex, entry.generation}});
        }

        /**
         * @brief Resolves one live same-world handle to its target-private value.
         * @param handle Borrowed Horo identity.
         * @return Borrowed mapping, or a stable malformed/foreign/stale/state error before native access.
         */
        [[nodiscard]] Result<Value *> Resolve(const Handle &handle) {
            const auto resolved = ResolveIndex(handle);
            if (resolved.HasError())
                return Result<Value *>::Failure(resolved.ErrorValue());
            return Result<Value *>::Success(&*entries_[resolved.Value()].value);
        }

        /** @copydoc Resolve */
        [[nodiscard]] Result<const Value *> Resolve(const Handle &handle) const {
            const auto resolved = ResolveIndex(handle);
            if (resolved.HasError())
                return Result<const Value *>::Failure(resolved.ErrorValue());
            return Result<const Value *>::Success(&*entries_[resolved.Value()].value);
        }

        /**
         * @brief Removes the exact live generation and either recycles or permanently retires its slot.
         * @param handle Same-world handle currently resident in this registry.
         * @return Success, or a stable malformed/foreign/stale/state error without changing storage.
         */
        [[nodiscard]] Result<void> Remove(const Handle &handle) {
            const auto resolved = ResolveIndex(handle);
            if (resolved.HasError())
                return Result<void>::Failure(resolved.ErrorValue());

            Entry &entry = entries_[resolved.Value()];
            entry.value.reset();
            --activeCount_;
            if (entry.generation == limits_.maximumGeneration) {
                ++exhaustedCount_;
                return Result<void>::Success();
            }
            ++entry.generation;
            entry.nextFree = freeHead_;
            freeHead_ = resolved.Value();
            return Result<void>::Success();
        }

        /** @brief Returns whether activation bound a world generation. */
        [[nodiscard]] bool IsBound() const noexcept {
            return owner_.IsValid();
        }

        /** @brief Returns the immutable slot bound allocated during preparation. */
        [[nodiscard]] std::size_t Capacity() const noexcept {
            return entries_.size();
        }

        /** @brief Returns the number of currently resolvable values. */
        [[nodiscard]] std::size_t ActiveCount() const noexcept {
            return activeCount_;
        }

        /** @brief Returns the number of slots permanently retired at the generation ceiling. */
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

        explicit PhysicsHandleRegistry(const PhysicsHandleRegistryLimits limits) : limits_(limits), entries_(limits.maximumSlots) {
            if (entries_.empty())
                return;
            for (std::uint32_t slot = 0; slot + 1 < limits.maximumSlots; ++slot)
                entries_[slot].nextFree = slot + 1;
            freeHead_ = 0;
        }

        [[nodiscard]] const ErrorCodeDescriptor &FullError() const noexcept {
            return !entries_.empty() && exhaustedCount_ == entries_.size() ? PhysicsErrors::GenerationExhausted
                                                                           : PhysicsErrors::CapacityExceeded;
        }

        [[nodiscard]] Result<std::uint32_t> ResolveIndex(const Handle &handle) const {
            if (!owner_.IsValid())
                return Result<std::uint32_t>::Failure(MakeError(PhysicsErrors::InvalidState, "Physics registry owner is not active."));
            const auto owner = ValidatePhysicsHandleOwner(handle, owner_);
            if (owner.HasError())
                return Result<std::uint32_t>::Failure(owner.ErrorValue());
            if (handle.slot.index >= entries_.size())
                return Result<std::uint32_t>::Failure(HandleError(handle));
            const Entry &entry = entries_[handle.slot.index];
            if (!entry.value.has_value() || entry.generation != handle.slot.generation)
                return Result<std::uint32_t>::Failure(HandleError(handle));
            return Result<std::uint32_t>::Success(handle.slot.index);
        }

        [[nodiscard]] static Error HandleError(const Handle &handle) {
            return MakeError(PhysicsErrors::HandleStale, std::format("Physics handle world {}, slot {}, generation {} is not current.",
                                                                     handle.world.Value(), handle.slot.index, handle.slot.generation));
        }

        PhysicsHandleRegistryLimits limits_;
        std::vector<Entry> entries_;
        PhysicsWorldId owner_;
        std::uint32_t freeHead_{InvalidSlot};
        std::size_t activeCount_{};
        std::size_t exhaustedCount_{};
    };
}  // namespace Horo::Physics::Detail
