#pragma once

/**
 * @file WorldObjectOwnership.h
 * @brief Inert persistent, spatial-cell, and runtime-spawned ownership policy.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Foundation/StrongId.h"
#include "Horo/WorldStreaming/WorldSpatialObjectDescriptor.h"
#include "Horo/WorldStreaming/WorldStreamingRuntimeComposition.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Horo::WorldStreaming {
    namespace Detail {
        struct RuntimeSpawnedObjectIdTag;
        struct WorldObjectOwnershipRevisionTag;
        struct WorldObjectRuntimeOwnerIdTag;
        struct WorldObjectRuntimeOwnerGenerationTag;
    }  // namespace Detail

    /** @brief Stable runtime-spawned object identity; persistence promotion is owned by the save contract. */
    using RuntimeSpawnedObjectId =
        Foundation::Detail::NonZeroId64<Detail::RuntimeSpawnedObjectIdTag, WorldStreamingErrors::IdentityInvalid>;
    /** @brief Monotonic revision of one object's ownership publication. */
    using WorldObjectOwnershipRevision =
        Foundation::Detail::NonZeroId64<Detail::WorldObjectOwnershipRevisionTag, WorldStreamingErrors::IdentityInvalid>;
    /** @brief Stable identity of an explicit non-spatial runtime owner. */
    using WorldObjectRuntimeOwnerId =
        Foundation::Detail::NonZeroId64<Detail::WorldObjectRuntimeOwnerIdTag, WorldStreamingErrors::IdentityInvalid>;
    /** @brief Generation fence for one non-spatial runtime-owner lifetime. */
    using WorldObjectRuntimeOwnerGeneration =
        Foundation::Detail::NonZeroId64<Detail::WorldObjectRuntimeOwnerGenerationTag, WorldStreamingErrors::IdentityInvalid>;

    /** @brief Provenance and authored-placement class whose ownership policy must remain unambiguous. */
    enum class WorldObjectOwnershipClass : std::uint8_t {
        AuthoredAlwaysPresent,
        AuthoredSpatial,
        RuntimeSpawned,
    };

    /** @brief Explicit lifetime authority currently responsible for one object. */
    enum class WorldObjectOwnerKind : std::uint8_t {
        World,
        Cell,
        Runtime,
    };

    /** @brief Required disposition when the exact owning cell generation retires. */
    enum class WorldObjectCellExitPolicy : std::uint8_t {
        NotApplicable,
        Retire,
        RequireHandoff,
    };

    /** @brief Complete owner binding with no pointer, entity handle, or implicit global fallback. */
    struct WorldObjectOwnerBinding final {
        StreamingRuntimeOwnerToken world{};                    /**< Exact mounted world authority. */
        WorldObjectOwnerKind kind{};                           /**< World, cell, or explicit runtime authority. */
        StreamingFence cell{};                                 /**< Exact cell attempt; valid only for Cell. */
        WorldObjectRuntimeOwnerId runtimeOwner{};              /**< Non-spatial runtime authority; valid only for Runtime. */
        WorldObjectRuntimeOwnerGeneration runtimeGeneration{}; /**< Exact runtime-owner lifetime; valid only for Runtime. */

        /** @brief Checks the exclusive owner representation and generation fences. @return True when structurally usable. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] constexpr auto operator<=>(const WorldObjectOwnerBinding &) const noexcept = default;
    };

    /** @brief Immutable ownership fact for one authored or runtime-spawned object. */
    struct WorldObjectOwnershipDescriptor final {
        WorldObjectOwnershipClass objectClass{};    /**< Authored placement or runtime-spawned provenance. */
        WorldAuthoringObjectAddress authored{};     /**< Durable authored identity; used only by authored classes. */
        RuntimeSpawnedObjectId runtimeSpawned{};    /**< Stable runtime identity; used only by RuntimeSpawned. */
        WorldObjectOwnershipRevision revision{};    /**< Exact immutable ownership revision. */
        WorldObjectOwnerBinding owner{};            /**< Explicit current lifetime authority. */
        WorldObjectCellExitPolicy cellExitPolicy{}; /**< Explicit cell-retirement behavior. */

        /** @brief Checks identity, owner, and class-policy coherence without resolving live state. @return True when valid. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] constexpr auto operator<=>(const WorldObjectOwnershipDescriptor &) const noexcept = default;
    };

    /** @brief Admission lifecycle for the bounded ownership authority. */
    enum class WorldObjectOwnershipOwnerState : std::uint8_t {
        Active,
        Cancelling,
        Closed,
    };

    /** @brief Immutable insert, replacement, or ownership-handoff publication request. */
    struct WorldObjectOwnershipRequest final {
        WorldObjectOwnershipDescriptor candidate{};                     /**< Complete proposed ownership fact. */
        std::optional<WorldObjectOwnershipRevision> expectedRevision{}; /**< Required current revision for existing identities. */
    };

    /** @brief Immutable bounded owner snapshot consumed by pure admission validation. */
    struct WorldObjectOwnershipAdmissionContext final {
        StreamingRuntimeOwnerToken expectedWorld{};              /**< Exact active mounted-world authority. */
        std::optional<WorldObjectOwnershipDescriptor> current{}; /**< Current fact for the candidate identity, if any. */
        std::size_t objectCount{};                               /**< Distinct ownership records currently charged. */
        std::size_t objectCapacity{};                            /**< Maximum admitted ownership records. */
        WorldObjectOwnershipOwnerState state{WorldObjectOwnershipOwnerState::Closed}; /**< Current lifecycle gate. */
    };

    /** @brief Mutation kind authorized by successful pure ownership admission. */
    enum class WorldObjectOwnershipAdmissionKind : std::uint8_t {
        Insert,
        Replace,
        Handoff,
    };

    /**
     * @brief Validates one inert object-ownership descriptor.
     * @param descriptor Complete descriptor value.
     * @return Success or a typed invalid or unsupported-policy error.
     */
    [[nodiscard]] Result<void> ValidateWorldObjectOwnershipDescriptor(const WorldObjectOwnershipDescriptor &descriptor);

    /**
     * @brief Validates bounded insert, replacement, or runtime-spawned handoff without mutating owner state.
     * @param request Complete candidate and optional compare-and-swap revision.
     * @param context Exact world lifetime, current fact, capacity, and lifecycle snapshot.
     * @return Authorized mutation kind or a typed invalid, stale, conflict, capacity, or lifecycle error.
     * @post Failure does not modify the supplied immutable context.
     */
    [[nodiscard]] Result<WorldObjectOwnershipAdmissionKind> ValidateWorldObjectOwnershipAdmission(
        const WorldObjectOwnershipRequest &request, const WorldObjectOwnershipAdmissionContext &context);

    /** @brief Advances an ownership revision without wrapping. @param current Current valid revision. @return Successor or
     * GenerationExhausted. */
    [[nodiscard]] Result<WorldObjectOwnershipRevision> NextWorldObjectOwnershipRevision(WorldObjectOwnershipRevision current);
}  // namespace Horo::WorldStreaming
