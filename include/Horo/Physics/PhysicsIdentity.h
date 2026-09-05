#pragma once

/** @file PhysicsIdentity.h
 * @brief World-scoped Horo physics identities, independent of scene and native solver storage.
 */

#include "Horo/Foundation/Handles.h"
#include "Horo/Foundation/Result.h"
#include "Horo/Physics/PhysicsErrors.h"

namespace Horo::Physics {
    /**
     * @brief Process-local identity of one published physics-world generation; never serialize it.
     *
     * The host assigns a non-zero value that is never reused during the process lifetime, including
     * after scene replacement. Construction validates representation only: it does not allocate a
     * world, reserve an identity, register a solver or prove that a world is active. The scene binding
     * owner separately retains the associated SceneRuntimeId without a reverse Physics dependency.
     */
    class PhysicsWorldId final {
    public:
        /** @brief Constructs an invalid, unbound identity. */
        PhysicsWorldId() = default;

        /** @brief Validates a host-issued world generation. @param value Non-zero process-local identity.
         * @return Typed identity or PhysicsErrors::WorldInvalid; no native or ambient side effects.
         */
        [[nodiscard]] static Result<PhysicsWorldId> Create(std::uint64_t value);

        /** @brief Returns the host-issued value. @return Zero only for the invalid default identity. */
        [[nodiscard]] constexpr std::uint64_t Value() const noexcept {
            return value_;
        }

        /** @brief Checks representation, not world liveness. @return Whether the value is non-zero. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return value_ != 0;
        }

        auto operator<=>(const PhysicsWorldId &) const noexcept = default;

    private:
        explicit constexpr PhysicsWorldId(const std::uint64_t value) : value_(value) {}

        std::uint64_t value_{};
    };

    /**
     * @brief Non-owning, world-scoped object identity; only the owning typed registry can resolve it.
     *
     * Uses Foundation's zero-based index with InvalidIndex sentinel, plus a non-zero slot generation.
     * Registry retirement must never wrap generations; scene/world replacement invalidates all old
     * handles even when indices and slot generations match. A handle neither owns a resource nor
     * extends its lease. Native pointers, solver IDs and serialized object identity are not handles.
     */
    template <typename Tag> struct PhysicsHandle final {
        PhysicsWorldId world;
        Horo::Handle<Tag> slot;

        /** @brief Checks world/index/generation shape, not residency. @return True for a well-formed handle. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return world.IsValid() && slot.IsValid() && slot.generation != 0;
        }

        constexpr auto operator<=>(const PhysicsHandle &) const noexcept = default;
    };

    struct PhysicsBodyTag;
    struct PhysicsShapeTag;
    struct PhysicsConstraintTag;

    /** @brief Non-owning identity of one body in one exact world generation. */
    using BodyHandle = PhysicsHandle<PhysicsBodyTag>;
    /** @brief Non-owning world-local shape identity; shared immutable shape leases remain private. */
    using ShapeHandle = PhysicsHandle<PhysicsShapeTag>;
    /** @brief Non-owning identity of one constraint in one exact world generation. */
    using ConstraintHandle = PhysicsHandle<PhysicsConstraintTag>;

    /**
     * @brief Rejects malformed or foreign handles before a registry lookup, without accessing native state.
     * @param handle Borrowed identity submitted to the owning Physics boundary.
     * @param expectedWorld Exact world generation receiving the operation.
     * @return Success for a well-formed same-world handle, otherwise a stable Physics error.
     * @pre Control/owner-thread validation, not a solver callback. Error construction may allocate.
     * @post Success is not a liveness or slot-generation check; the registry must still validate
     * occupancy and the exact current slot generation before resolving any resource.
     */
    template <typename Tag>
    [[nodiscard]] Result<void> ValidatePhysicsHandleOwner(const PhysicsHandle<Tag> &handle, const PhysicsWorldId expectedWorld) {
        if (!expectedWorld.IsValid())
            return Result<void>::Failure(MakeError(PhysicsErrors::WorldInvalid));
        if (!handle.IsValid())
            return Result<void>::Failure(MakeError(PhysicsErrors::HandleMalformed));
        if (handle.world != expectedWorld)
            return Result<void>::Failure(MakeError(PhysicsErrors::HandleWorldMismatch));
        return Result<void>::Success();
    }
}  // namespace Horo::Physics
