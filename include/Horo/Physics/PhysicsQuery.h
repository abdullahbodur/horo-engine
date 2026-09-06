#pragma once

/**
 * @file PhysicsQuery.h
 * @brief Backend-neutral Physics query descriptors, hits, bounds and deterministic ordering.
 */

#include "Horo/Assets/AssetId.h"
#include "Horo/Physics/PhysicsCookedShapeDescriptor.h"
#include "Horo/Physics/PhysicsFilterIdentity.h"
#include "Horo/Physics/PhysicsIdentity.h"
#include "Horo/Physics/PhysicsPose.h"

#include <optional>
#include <variant>

namespace Horo::Physics {
    inline constexpr std::uint32_t MaximumPhysicsQueryHits = 1024;
    inline constexpr double PhysicsQueryUnitVectorSquaredNormTolerance = 1.0e-6;

    /** @brief Whether trigger colliders participate in a query. */
    enum class PhysicsQueryTriggerPolicy : std::uint8_t {
        Exclude,
        Include,
    };

    /** @brief Result collection semantics applied after deterministic ordering. */
    enum class PhysicsQueryCollection : std::uint8_t {
        Closest,
        All,
        ThroughFirstBlock,
    };

    /** @brief Public ordering vocabulary; native broadphase traversal order is never observable. */
    enum class PhysicsQueryOrdering : std::uint8_t {
        ClosestFirst,
    };

    /** @brief Query-only response resolved from the active profile/channel schema. */
    enum class PhysicsQueryResponse : std::uint8_t {
        Overlap,
        Block,
    };

    /**
     * @brief Bounded typed selectors intersected with the selected query channel.
     *
     * Optional selectors name at most one required layer/profile and one excluded body. This
     * allocation-free baseline is deliberately not an unbounded callback or borrowed ID array.
     */
    struct PhysicsQueryFilter final {
        PhysicsQueryChannelId channel;
        PhysicsQueryTriggerPolicy triggers{PhysicsQueryTriggerPolicy::Exclude};
        std::optional<CollisionLayerId> requiredLayer;
        std::optional<CollisionProfileId> requiredProfile;
        std::optional<BodyHandle> excludedBody;
    };

    /** @brief Finite ray in the world's current origin frame; direction must be unit length. */
    struct PhysicsRayQuery final {
        Math::Vec3 origin;
        Math::Vec3 direction{0, 0, -1};
        float maximumDistanceMeters{1};
    };

    /** @brief Resident shape swept from one scale-free pose along a finite unit direction. */
    struct PhysicsSweepQuery final {
        ShapeHandle shape;
        PhysicsPose pose;
        Math::Vec3 direction{0, 0, -1};
        float maximumDistanceMeters{1};
    };

    /** @brief Resident shape overlap at one scale-free pose. */
    struct PhysicsOverlapQuery final {
        ShapeHandle shape;
        PhysicsPose pose;
    };

    /** @brief Point test in the world's current origin frame. */
    struct PhysicsPointQuery final {
        Math::Vec3 point;
    };

    using PhysicsQueryGeometry = std::variant<PhysicsRayQuery, PhysicsSweepQuery, PhysicsOverlapQuery, PhysicsPointQuery>;

    /**
     * @brief Owned inert query request targeting one exact world and scene generation.
     *
     * Shape/body handles are non-owning identities. The receiving world validates their live slot
     * generations and retains any native/schema leases only for execution; this value owns none.
     */
    struct PhysicsQueryDescriptor final {
        PhysicsWorldId world;
        std::uint64_t sceneGeneration{};
        PhysicsQueryGeometry geometry;
        PhysicsQueryFilter filter;
        PhysicsQueryCollection collection{PhysicsQueryCollection::Closest};
        PhysicsQueryOrdering ordering{PhysicsQueryOrdering::ClosestFirst};
        std::uint32_t maximumHitCount{1};
    };

    /** @brief Stable physical-material evidence copied into a hit. */
    struct PhysicsQueryMaterial final {
        Assets::AssetId asset;
        std::uint64_t assetGeneration{};
        PhysicsMaterialSlotId slot;
    };

    /**
     * @brief One solver-neutral query hit with copied stable identity and generation evidence.
     *
     * No field owns a world, schema, shape, material, project document or native solver object.
     * A result buffer owner controls hit lifetime. A stale handle remains stale and cannot alias a
     * replacement because body/shape slot generations are retained.
     */
    struct PhysicsQueryHit final {
        BodyHandle body;
        ShapeHandle shape;
        std::optional<PhysicsShapeSubresourceId> subshape;
        std::optional<PhysicsQueryMaterial> material;
        CollisionLayerId layer;
        CollisionProfileId profile;
        PhysicsQueryChannelId channel;
        std::uint64_t filterSchemaGeneration{};
        PhysicsQueryResponse response{PhysicsQueryResponse::Block};
        Math::Vec3 position;
        std::optional<Math::Vec3> normal;
        float distanceMeters{};
    };

    /** @brief Metadata for caller-owned bounded hit storage. */
    struct PhysicsQueryResult final {
        std::uint32_t hitCount{};
        bool truncated{};
        std::uint64_t filterSchemaGeneration{};
        std::uint64_t broadphaseSnapshotGeneration{};
    };

    /**
     * @brief Validates inert request shape, bounds, typed selectors and captured owner generations.
     * @param descriptor Request to validate without accessing native state.
     * @param expectedWorld Exact receiving world generation.
     * @param expectedSceneGeneration Active scene generation observed by that world.
     * @return Success or a stable malformed, foreign-world, stale-generation, unsupported or capacity error.
     */
    [[nodiscard]] Result<void> ValidatePhysicsQueryDescriptor(const PhysicsQueryDescriptor &descriptor, PhysicsWorldId expectedWorld,
                                                              std::uint64_t expectedSceneGeneration);

    /**
     * @brief Validates copied public hit evidence against its admitted descriptor.
     * @param hit Hit emitted into caller-owned storage.
     * @param descriptor Descriptor that admitted the query.
     * @return Success or a stable malformed, foreign-world or stale-generation error.
     */
    [[nodiscard]] Result<void> ValidatePhysicsQueryHit(const PhysicsQueryHit &hit, const PhysicsQueryDescriptor &descriptor);

    /**
     * @brief Validates bounded result metadata returned by a query provider.
     * @param result Provider result metadata for caller-owned hit storage.
     * @param descriptor Descriptor whose result contract applies.
     * @return Success, CapacityExceeded for an over-limit result, or QuerySnapshotStale for missing schema evidence.
     */
    [[nodiscard]] Result<void> ValidatePhysicsQueryResult(const PhysicsQueryResult &result, const PhysicsQueryDescriptor &descriptor);

    /**
     * @brief Compares validated hits by the public deterministic closest-first contract.
     * @param left First validated hit.
     * @param right Second validated hit.
     * @return True when left precedes right by distance, response, object, subshape, contact and material evidence.
     * @pre Both hits passed ValidatePhysicsQueryHit for the same descriptor; invalid floating values are unsupported.
     */
    [[nodiscard]] bool PhysicsQueryHitLess(const PhysicsQueryHit &left, const PhysicsQueryHit &right) noexcept;
}  // namespace Horo::Physics
