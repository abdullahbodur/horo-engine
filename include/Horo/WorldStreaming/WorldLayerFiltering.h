#pragma once

/**
 * @file WorldLayerFiltering.h
 * @brief Deterministic editor, runtime, and platform filtering of stable world layers.
 */

#include "Horo/WorldStreaming/WorldLayerOwnershipModel.h"
#include "Horo/WorldStreaming/WorldPartitionDescriptor.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Horo::WorldStreaming {
    namespace Detail {
        struct WorldLayerFilterPolicyIdTag;
        struct WorldLayerFilterPolicyRevisionTag;
    }  // namespace Detail

    /** @brief Stable identity of one authored layer-filter policy. */
    using WorldLayerFilterPolicyId =
        Foundation::Detail::NonZeroId64<Detail::WorldLayerFilterPolicyIdTag, WorldStreamingErrors::IdentityInvalid>;
    /** @brief Monotonic immutable revision of one filter policy. */
    using WorldLayerFilterPolicyRevision =
        Foundation::Detail::NonZeroId64<Detail::WorldLayerFilterPolicyRevisionTag, WorldStreamingErrors::IdentityInvalid>;

    /** @brief Product composition for which stable authored layer existence is resolved. */
    enum class WorldLayerExecutionTarget : std::uint8_t {
        Editor,
        ClientRuntime,
        DedicatedServerRuntime,
    };

    /** @brief Explicit platform decision for layers marked optional by the authored source. */
    enum class WorldLayerOptionalPolicy : std::uint8_t {
        Include,
        Exclude,
    };

    /** @brief Immutable target policy captured for one filtering pass. */
    struct WorldLayerFilterPolicy final {
        WorldLayerFilterPolicyId id{};             /**< Stable policy identity. */
        WorldLayerFilterPolicyRevision revision{}; /**< Exact immutable policy revision. */
        WorldLayerExecutionTarget target{};        /**< Editor, client, or dedicated-server composition. */
        WorldLayerOptionalPolicy optional{};       /**< Explicit optional-layer support decision. */

        /** @brief Checks identities and closed enum values. @return True when structurally usable. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] constexpr auto operator<=>(const WorldLayerFilterPolicy &) const noexcept = default;
    };

    /** @brief Stable source layer and its version-one manifest filtering flags. */
    struct WorldLayerFilterCandidate final {
        WorldLayerOwnershipDescriptor ownership{}; /**< Stable identity, audience, residency, and control owner. */
        WorldLayerFlags flags{};                   /**< Persistent, optional, server-only, and client-only source flags. */
    };

    /** @brief Observable reason a stable source layer is included or excluded. */
    enum class WorldLayerFilterDisposition : std::uint8_t {
        Unresolved,
        Included,
        ExcludedEditorOnly,
        ExcludedServerOnly,
        ExcludedClientOnly,
        ExcludedOptional,
    };

    /** @brief One deterministic target decision bound to an exact mounted world, source identity, and revision. */
    struct WorldLayerFilterDecision final {
        StreamingRuntimeOwnerToken world{};        /**< Exact mounted-world lifetime containing the source layer. */
        StreamingLayerId layer{};                  /**< Unchanged stable source identity. */
        WorldLayerRevision ownershipRevision{};    /**< Exact source classification revision. */
        WorldLayerFilterDisposition disposition{}; /**< Inclusion result and typed reason. */

        /** @brief Reports whether the source layer exists in the selected target. @return True only for Included. */
        [[nodiscard]] constexpr bool IsIncluded() const noexcept {
            return disposition == WorldLayerFilterDisposition::Included;
        }

        [[nodiscard]] constexpr auto operator<=>(const WorldLayerFilterDecision &) const noexcept = default;
    };

    /** @brief Lifecycle gate for pure filtering admission. */
    enum class WorldLayerFilterAuthorityState : std::uint8_t {
        Active,
        Cancelling,
        Closed,
    };

    /** @brief Exact owner, policy fence, limits, and lifecycle captured by one filtering pass. */
    struct WorldLayerFilterContext final {
        StreamingRuntimeOwnerToken expectedWorld{};                                            /**< Exact mounted-world authority. */
        WorldLayerFilterPolicyId expectedPolicy{};                                             /**< Required stable policy identity. */
        WorldLayerFilterPolicyRevision expectedPolicyRevision{};                               /**< Required immutable policy revision. */
        std::size_t maximumCandidates{};                                                       /**< Positive input and decision ceiling. */
        WorldLayerFilterAuthorityState authorityState{WorldLayerFilterAuthorityState::Closed}; /**< Current lifecycle gate. */
    };

    /** @brief Summary of one complete filtering pass bound to its exact mounted-world lifetime. */
    struct WorldLayerFilterResult final {
        StreamingRuntimeOwnerToken world{};        /**< Exact mounted-world lifetime used by every decision. */
        WorldLayerFilterPolicyId policy{};         /**< Exact policy used. */
        WorldLayerFilterPolicyRevision revision{}; /**< Exact policy revision used. */
        std::size_t decisionCount{};               /**< Number of decisions written. */
        std::size_t includedCount{};               /**< Number of Included decisions. */
    };

    /**
     * @brief Resolves target inclusion for a canonical stable-layer snapshot without changing source identity.
     * @param policy Exact editor/runtime/platform policy.
     * @param candidates Strictly ascending unique stable-layer candidates.
     * @param context Exact world, policy fence, capacity, and lifecycle snapshot.
     * @param decisions Caller-owned output with room for every candidate decision.
     * @return Complete summary or a typed invalid, unsupported, stale, conflict, capacity, or lifecycle error.
     * @post Failure leaves @p decisions unchanged; success writes exactly candidates.size() rows in source order.
     */
    [[nodiscard]] Result<WorldLayerFilterResult> FilterWorldLayers(const WorldLayerFilterPolicy &policy,
                                                                   std::span<const WorldLayerFilterCandidate> candidates,
                                                                   const WorldLayerFilterContext &context,
                                                                   std::span<WorldLayerFilterDecision> decisions);

    /**
     * @brief Validates an exact-successor replacement of one immutable filter policy.
     * @param current Current policy publication.
     * @param replacement Complete proposed successor with the same stable identity.
     * @param authorityState Current filtering-authority lifecycle gate.
     * @return Success or a typed invalid, unsupported, conflict, stale, lifecycle, or exhaustion error.
     */
    [[nodiscard]] Result<void> ValidateWorldLayerFilterPolicyReplacement(const WorldLayerFilterPolicy &current,
                                                                         const WorldLayerFilterPolicy &replacement,
                                                                         WorldLayerFilterAuthorityState authorityState);

    /** @brief Advances a filter-policy revision without wrapping. @param current Current valid revision. @return Successor or
     * GenerationExhausted. */
    [[nodiscard]] Result<WorldLayerFilterPolicyRevision> NextWorldLayerFilterPolicyRevision(WorldLayerFilterPolicyRevision current);
}  // namespace Horo::WorldStreaming
