#pragma once

/** @file PhysicsShapeDescriptor.h
 * @brief Owned analytic shape descriptors in normalized Horo SI local space.
 */

#include "Horo/Physics/PhysicsPose.h"

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

    /** @brief Typed authored scale resolved into analytic geometry before runtime shape admission. */
    struct PhysicsShapeScale final {
        Math::Vec3 factors{1, 1, 1};
        bool operator==(const PhysicsShapeScale &) const noexcept = default;
    };

    /**
     * @brief Owned authoring request for one analytic primitive and its body-local placement.
     *
     * Scale is transient semantic input. Resolution folds an admitted scale into geometry and
     * never retains a runtime/native scale multiplier. This value owns no world, body, shape,
     * asset, native solver object or borrowed memory.
     */
    struct PhysicsPrimitiveShapeRequest final {
        PhysicsShapeDescriptor geometry;
        PhysicsPose localPose;
        PhysicsShapeScale scale;
    };

    /**
     * @brief Owned validated analytic primitive with scale-free body-local placement.
     *
     * This is structural preparation output, not a resident shape, lease or proof of motion-mode,
     * material, filter, capability, profile-size or world-capacity admission.
     */
    struct ResolvedPhysicsPrimitiveShape final {
        PhysicsShapeDescriptor geometry;
        PhysicsPose localPose;
    };

    /**
     * @brief Validates analytic dimensions and plane representation without normalization or native work.
     * @param descriptor Immutable normalized-meter geometry.
     * @return Success or PhysicsErrors::DescriptorInvalid with the invalid dimension/normal reason.
     * @pre Preparation/control use; failure diagnostics may allocate.
     * @post Success is structural only: motion compatibility, profile size bounds, material/filter policy,
     * native feature availability and world capacity must still be admitted before shape creation.
     */
    [[nodiscard]] Result<void> ValidatePhysicsShapeDescriptor(const PhysicsShapeDescriptor &descriptor);

    /**
     * @brief Validates and folds authored analytic scale into an owned scale-free primitive.
     * @param request Immutable primitive geometry, body-local pose and typed authored scale.
     * @return The resolved primitive; PhysicsErrors::DescriptorInvalid for non-finite, non-positive,
     * degenerate or overflowing numeric input; or PhysicsErrors::OperationUnsupported when a
     * non-uniform affine scale cannot preserve the requested sphere or capsule geometry.
     * @pre Preparation/control use; failure diagnostics may allocate.
     * @post The request is unchanged on success and failure. Boxes admit positive non-uniform scale.
     * Spheres and capsules require uniform scale. Planes admit positive non-uniform scale through
     * inverse-transpose equation transformation. The result stores no scale multiplier.
     */
    [[nodiscard]] Result<ResolvedPhysicsPrimitiveShape> ResolvePhysicsPrimitiveShape(const PhysicsPrimitiveShapeRequest &request);
}  // namespace Horo::Physics
