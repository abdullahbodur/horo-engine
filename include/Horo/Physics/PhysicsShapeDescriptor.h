#pragma once

/** @file PhysicsShapeDescriptor.h
 * @brief Owned analytic shape descriptors in normalized Horo SI local space.
 */

#include "Horo/Math/SceneMath.h"

#include <variant>

namespace Horo::Physics {
    /** @brief Box half dimensions in meters along the local X/Y/Z axes; each must be positive and finite. */
    struct PhysicsBoxShape final {
        Math::Vec3 halfExtentsMeters{0.5F, 0.5F, 0.5F};
    };

    /** @brief Sphere radius in meters, positive and finite. */
    struct PhysicsSphereShape final {
        float radiusMeters{0.5F};
    };

    /** @brief +Y capsule with positive finite radius and cylindrical half-height, excluding hemispherical caps. */
    struct PhysicsCapsuleShape final {
        float radiusMeters{0.5F};
        float cylindricalHalfHeightMeters{0.5F};
    };

    /** @brief Oriented plane defined by dot(unit normal, local point) = signed distance in meters; static use only. */
    struct PhysicsStaticPlaneShape final {
        Math::Vec3 normal{0, 1, 0};
        float signedDistanceMeters{};
    };

    /** @brief Maximum squared-norm error for an analytic plane normal; not a collision or identity epsilon. */
    inline constexpr double PhysicsPlaneNormalSquaredNormTolerance = 1.0e-6;

    /**
     * @brief Inert analytic geometry request, not a resident shape or a solver-support assertion.
     *
     * Dimensions already include admitted authored scale. There is no runtime scale multiplier, source
     * path, renderer handle or native type. Local placement belongs to the body/collider binding.
     * Convex hull, mesh, height-field and compound activation require the separate exact cooked-artifact
     * contract; this analytic vocabulary cannot represent or silently substitute those shapes.
     * The world owns resulting shapes; only its registry may issue a ShapeHandle after publication.
     */
    using PhysicsShapeDescriptor = std::variant<PhysicsBoxShape, PhysicsSphereShape, PhysicsCapsuleShape, PhysicsStaticPlaneShape>;

    /**
     * @brief Validates analytic dimensions and plane representation without normalization or native work.
     * @param descriptor Immutable normalized-meter geometry.
     * @return Success or PhysicsErrors::DescriptorInvalid with the invalid dimension/normal reason.
     * @pre Preparation/control use; failure diagnostics may allocate.
     * @post Success is structural only: motion compatibility, profile size bounds, material/filter policy,
     * native feature availability and world capacity must still be admitted before shape creation.
     */
    [[nodiscard]] Result<void> ValidatePhysicsShapeDescriptor(const PhysicsShapeDescriptor &descriptor);
}  // namespace Horo::Physics
