#pragma once

/**
 * @file WorldLayerOwnershipModel.h
 * @brief Inert world-layer classification and control-ownership contract.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Foundation/StrongId.h"
#include "Horo/WorldStreaming/WorldStreamingRuntimeComposition.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Horo::WorldStreaming {
    namespace Detail {
        struct WorldLayerRevisionTag;
        struct WorldLayerControlOwnerIdTag;
        struct WorldLayerControlOwnerGenerationTag;
        struct WorldLayerControlHandoffIdTag;
        struct WorldLayerControlHandoffGenerationTag;
    }  // namespace Detail

    /** @brief Monotonic revision of one stable world-layer publication. */
    using WorldLayerRevision = Foundation::Detail::NonZeroId64<Detail::WorldLayerRevisionTag, WorldStreamingErrors::IdentityInvalid>;
    /** @brief Stable identity of an editor, gameplay, or replication layer-control authority. */
    using WorldLayerControlOwnerId =
        Foundation::Detail::NonZeroId64<Detail::WorldLayerControlOwnerIdTag, WorldStreamingErrors::IdentityInvalid>;
    /** @brief Generation fence for one non-streaming layer-control authority lifetime. */
    using WorldLayerControlOwnerGeneration =
        Foundation::Detail::NonZeroId64<Detail::WorldLayerControlOwnerGenerationTag, WorldStreamingErrors::IdentityInvalid>;
    /** @brief Stable identity of one explicit layer-control handoff authorization. */
    using WorldLayerControlHandoffId =
        Foundation::Detail::NonZeroId64<Detail::WorldLayerControlHandoffIdTag, WorldStreamingErrors::IdentityInvalid>;
    /** @brief Generation fence preventing replay of a superseded handoff authorization. */
    using WorldLayerControlHandoffGeneration =
        Foundation::Detail::NonZeroId64<Detail::WorldLayerControlHandoffGenerationTag, WorldStreamingErrors::IdentityInvalid>;

    /** @brief Whether layer content is addressed through partition cells. */
    enum class WorldLayerPlacement : std::uint8_t {
        Spatial,
        NonSpatial,
    };

    /** @brief Policy that determines how a layer enters and leaves residency. */
    enum class WorldLayerResidencyPolicy : std::uint8_t {
        Persistent,
        Streamed,
        RuntimeControlled,
    };

    /** @brief Product audience in which authored layer content may exist. */
    enum class WorldLayerAudience : std::uint8_t {
        Runtime,
        EditorOnly,
    };

    /** @brief Explicit authority allowed to publish control state for one layer. */
    enum class WorldLayerControlOwnerKind : std::uint8_t {
        WorldStreaming,
        EditorDocument,
        GameplayScript,
        NetworkReplication,
    };

    /** @brief Exact owner binding with no service pointer or ambient registry lookup. */
    struct WorldLayerControlOwner final {
        StreamingRuntimeOwnerToken world{};            /**< Exact mounted-world lifetime. */
        WorldLayerControlOwnerKind kind{};             /**< Authority role for layer control. */
        WorldLayerControlOwnerId authority{};          /**< Stable authority identity; unused by WorldStreaming. */
        WorldLayerControlOwnerGeneration generation{}; /**< Authority lifetime; unused by WorldStreaming. */

        /** @brief Checks exclusive owner representation and lifetime fencing. @return True when structurally usable. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] constexpr auto operator<=>(const WorldLayerControlOwner &) const noexcept = default;
    };

    /** @brief Exact current-owner authorization for one target authority lifetime and ownership revision. */
    struct WorldLayerControlHandoffReceipt final {
        WorldLayerControlHandoffId id{};                 /**< Stable authorization identity. */
        WorldLayerControlHandoffGeneration generation{}; /**< Exact non-replayable authorization generation. */
        WorldLayerControlOwner currentOwner{};           /**< Exact authority granting the handoff. */
        WorldLayerControlOwner targetOwner{};            /**< Exact target authority lifetime being admitted. */
        WorldLayerRevision expectedRevision{};           /**< Exact ownership publication authorized to hand off. */

        /** @brief Checks receipt identity and exact source/target owner coherence. @return True when structurally usable. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] constexpr auto operator<=>(const WorldLayerControlHandoffReceipt &) const noexcept = default;
    };

    /** @brief Immutable classification and control-ownership fact for one stable layer identity. */
    struct WorldLayerOwnershipDescriptor final {
        StreamingLayerId layer{};              /**< Stable world.index layer identity. */
        WorldLayerRevision revision{};         /**< Exact immutable publication revision. */
        WorldLayerPlacement placement{};       /**< Spatial-cell or non-spatial content. */
        WorldLayerResidencyPolicy residency{}; /**< Persistent, streamed, or runtime-controlled lifetime. */
        WorldLayerAudience audience{};         /**< Runtime-visible or editor-only authored content. */
        WorldLayerControlOwner owner{};        /**< Exact control authority and lifetime. */

        /** @brief Checks identity and classification/owner coherence without resolving live state. @return True when valid. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] constexpr auto operator<=>(const WorldLayerOwnershipDescriptor &) const noexcept = default;
    };

    /** @brief Lifecycle gate for the bounded layer-ownership authority. */
    enum class WorldLayerOwnershipAuthorityState : std::uint8_t {
        Active,
        Cancelling,
        Closed,
    };

    /** @brief Immutable insert, replacement, or runtime-control handoff request. */
    struct WorldLayerOwnershipRequest final {
        WorldLayerOwnershipDescriptor candidate{};                /**< Complete proposed ownership fact. */
        std::optional<WorldLayerRevision> expectedRevision{};     /**< Required current revision for replacement. */
        std::optional<WorldLayerControlHandoffReceipt> handoff{}; /**< Exact authorization presented for an owner change. */
    };

    /** @brief Immutable bounded owner snapshot consumed by pure admission validation. */
    struct WorldLayerOwnershipAdmissionContext final {
        StreamingRuntimeOwnerToken expectedWorld{};                         /**< Exact active mounted-world authority. */
        std::optional<WorldLayerOwnershipDescriptor> current{};             /**< Current fact for the candidate identity, if any. */
        std::optional<WorldLayerControlHandoffReceipt> authorizedHandoff{}; /**< Current authorization, if handoff is allowed. */
        std::optional<WorldLayerControlOwner> validatedHandoffTarget{};     /**< Fresh target lifetime proven by the composition owner. */
        std::size_t layerCount{};                                           /**< Distinct layer facts currently charged. */
        std::size_t layerCapacity{};                                        /**< Maximum admitted layer facts. */
        WorldLayerOwnershipAuthorityState state{WorldLayerOwnershipAuthorityState::Closed}; /**< Current lifecycle gate. */
    };

    /** @brief Mutation kind authorized by successful pure layer admission. */
    enum class WorldLayerOwnershipAdmissionKind : std::uint8_t {
        Insert,
        Replace,
        Handoff,
    };

    /**
     * @brief Validates one inert world-layer classification and owner fact.
     * @param descriptor Complete descriptor value.
     * @return Success or a typed invalid or unsupported-policy error.
     */
    [[nodiscard]] Result<void> ValidateWorldLayerOwnershipDescriptor(const WorldLayerOwnershipDescriptor &descriptor);

    /**
     * @brief Validates bounded insertion, replacement, or runtime-control handoff without mutation.
     * @param request Complete candidate, optional compare-and-swap revision, and exact handoff receipt when ownership changes.
     * @param context Exact world lifetime, current fact, current handoff authorization/target evidence, capacity, and lifecycle snapshot.
     * @return Authorized mutation kind or a typed invalid, stale, conflict, capacity, or lifecycle error.
     * @post Failure does not modify the supplied immutable context.
     */
    [[nodiscard]] Result<WorldLayerOwnershipAdmissionKind> ValidateWorldLayerOwnershipAdmission(
        const WorldLayerOwnershipRequest &request, const WorldLayerOwnershipAdmissionContext &context);

    /** @brief Advances a layer revision without wrapping. @param current Current valid revision. @return Successor or
     * GenerationExhausted. */
    [[nodiscard]] Result<WorldLayerRevision> NextWorldLayerRevision(WorldLayerRevision current);
}  // namespace Horo::WorldStreaming
