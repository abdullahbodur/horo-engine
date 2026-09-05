#pragma once

/** @file PhysicsWorldDescriptor.h
 * @brief Inert SI world preparation policy and bounded scene-plan capacity.
 */

#include "Horo/Math/SceneMath.h"

#include <cstdint>

namespace Horo::Physics {
    /** @brief Maximum bodies in one CanonicalV1 scene candidate. */
    inline constexpr std::uint32_t MaximumPhysicsBodies = 262'144;
    /** @brief Maximum collider slots in one CanonicalV1 scene candidate. */
    inline constexpr std::uint32_t MaximumPhysicsColliderSlots = 1'048'576;
    /** @brief Maximum constraints in one CanonicalV1 scene candidate. */
    inline constexpr std::uint32_t MaximumPhysicsConstraints = 262'144;
    /** @brief Maximum aggregate plan and binding-table bytes in one CanonicalV1 candidate. */
    inline constexpr std::uint64_t MaximumPhysicsPlanBytes = 512ULL * 1024 * 1024;
    /** @brief CanonicalV1 admitted gravity magnitude in m/s². */
    inline constexpr double MaximumPhysicsGravityMagnitude = 20.0;
    /** @brief Named numeric/solver policy; unknown profiles require explicit future qualification. */
    enum class PhysicsToleranceProfileId : std::uint8_t {
        CanonicalV1
    };

    /** @brief ADR-087 scene preparation bounds, independent of native scratch/shape-payload budgets. */
    struct PhysicsWorldCapacity final {
        std::uint32_t maximumBodies{MaximumPhysicsBodies};
        std::uint32_t maximumColliderSlots{MaximumPhysicsColliderSlots};
        std::uint32_t maximumConstraints{MaximumPhysicsConstraints};
        std::uint64_t maximumPlanBytes{MaximumPhysicsPlanBytes}; /**< Aggregate plan plus binding-table bytes. */
    };

    /**
     * @brief Owned preparation policy; construction performs no allocation, registration or solver work.
     *
     * Gravity is in m/s² in Horo's right-handed +Y-up space. Runtime supplies the fixed delta in
     * seconds; it is never measured from wall/render time. Capacity may be lowered, never silently
     * raised or clamped. Zero object capacities admit an empty world; the plan-byte budget is positive.
     * World identity is assigned only by successful aggregate scene publication, not by this descriptor.
     */
    struct PhysicsWorldDescriptor final {
        std::uint32_t contractVersion{1};
        PhysicsToleranceProfileId profile{PhysicsToleranceProfileId::CanonicalV1};
        Math::Vec3 gravity{0.0F, -9.81F, 0.0F};
        double fixedDeltaSeconds{1.0 / 60.0};
        PhysicsWorldCapacity capacity;
    };

    /**
     * @brief Validates representation and CanonicalV1 preparation limits without changing the descriptor.
     * @param descriptor Retained host policy with finite gravity of magnitude at most 20 m/s² and positive finite fixed delta
     * within the normal fp32 magnitude range; validation does not round the requested double value.
     * @return Success or a stable Physics descriptor/profile/capacity error.
     * @pre Control/preparation use; error construction may allocate.
     * @post Success does not qualify a fixed rate, allocate a world, admit assets/filter data, or promise
     * determinism, native availability, memory sufficiency or safe publication. Those are separate gates.
     */
    [[nodiscard]] Result<void> ValidatePhysicsWorldDescriptor(const PhysicsWorldDescriptor &descriptor);
}  // namespace Horo::Physics
