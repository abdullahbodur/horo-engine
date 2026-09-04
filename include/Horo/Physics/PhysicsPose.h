#pragma once

/** @file PhysicsPose.h
 * @brief Scale-free Horo Physics pose values and explicit orientation validation.
 */

#include "Horo/Math/SceneMath.h"

namespace Horo::Physics {
    /** @brief Maximum absolute squared-norm error for a Physics rotation quaternion; not a geometry/identity epsilon. */
    inline constexpr double PhysicsRotationSquaredNormTolerance = 1.0e-6;

    /**
     * @brief Owned translation in meters and rotation in the recipient's explicitly declared local frame.
     *
     * A body pose is in the current Physics origin frame; a shape/constraint frame is relative to
     * its declared body or parent. The enclosing descriptor/operation owns that frame and generation.
     * There is no scale: authored scale must already be resolved through shape/cook policy.
     * This value owns no world, object, origin lease, native transform or borrowed memory.
     */
    struct PhysicsPose final {
        Math::Vec3 translation;
        Math::Quaternion rotation;
        bool operator==(const PhysicsPose &) const noexcept = default;
    };

    /**
     * @brief Rejects non-finite coordinates and non-unit quaternions without normalizing or changing the input.
     * @param pose Owned pose in the caller-declared coordinate frame.
     * @return Success or PhysicsErrors::DescriptorInvalid with a numeric failure reason.
     * @pre Preparation/control use; diagnostic construction may allocate.
     * @post Success proves only finite translation and the named squared-norm tolerance. World-origin
     * identity, local-cluster/shape bounds, motion policy and frame compatibility remain caller checks.
     * Quaternion signs are preserved; this function is not a serialization canonicalizer.
     */
    [[nodiscard]] Result<void> ValidatePhysicsPose(const PhysicsPose &pose);
}  // namespace Horo::Physics
