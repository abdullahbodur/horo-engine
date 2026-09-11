#pragma once

/**
 * @file DestructionComposition.h
 * @brief Explicit backend-neutral destruction product-profile composition contracts.
 */

#include "Horo/Destruction/DestructibleDescriptor.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Horo::Destruction {
    /** @brief Version of the first portable destruction composition contract. */
    inline constexpr std::uint32_t CurrentDestructionCompositionContractVersion = 1;

    struct DestructionCompositionRevisionTag;
    struct DestructionHostCapabilityRevisionTag;
    /** @brief Non-zero revision of one complete immutable host composition. */
    using DestructionCompositionRevision = DestructionStableIdentity<DestructionCompositionRevisionTag>;
    /** @brief Non-zero revision of one host-published capability implementation/configuration. */
    using DestructionHostCapabilityRevision = DestructionStableIdentity<DestructionHostCapabilityRevisionTag>;

    /** @brief Closed product compositions; none implies another or permits profile fallback. */
    enum class DestructionProductProfile : std::uint8_t {
        Null,
        Headless,
        Editor,
        Standalone,
        Client,
        Server,
        Count
    };

    /** @brief External capability families consumed by destruction without naming a concrete backend. */
    enum class DestructionHostCapability : std::uint8_t {
        Physics,
        Vfx,
        Audio,
        Networking,
        Count
    };

    /** @brief Canonical profile policy for one capability family. */
    enum class DestructionCapabilityRequirement : std::uint8_t {
        Omitted,
        Optional,
        Required,
        Count
    };

    /** @brief Exact host-published availability; unknown or implicit discovery is not representable. */
    enum class DestructionCapabilityAvailability : std::uint8_t {
        Unavailable,
        Available,
        Count
    };

    /** @brief Observable result for a capability in one resolved composition. */
    enum class DestructionCapabilityResolutionState : std::uint8_t {
        Omitted,
        Unavailable,
        Bound,
        Count
    };

    /** @brief Lifecycle evidence checked before admitting work against an immutable composition. */
    enum class DestructionCompositionLifecycle : std::uint8_t {
        Active,
        Cancelling,
        ShuttingDown,
        Closed,
        Count
    };

    inline constexpr std::size_t DestructionHostCapabilityCount = static_cast<std::size_t>(DestructionHostCapability::Count);

    /** @brief One explicit backend-neutral capability fact supplied by the host composition root. */
    struct DestructionCapabilityFact final {
        DestructionHostCapability capability{};           /**< Closed capability family. */
        DestructionCapabilityAvailability availability{}; /**< Exact effective availability. */
        DestructionHostCapabilityRevision revision{};     /**< Non-zero only while available. */

        [[nodiscard]] constexpr auto operator<=>(const DestructionCapabilityFact &) const noexcept = default;
    };

    /** @brief Inert canonical policy for one product profile. */
    struct DestructionProductProfilePolicy final {
        DestructionProductProfile profile{};        /**< Exact profile identity. */
        bool destructionEnabled{};                  /**< False only for the explicit Null composition. */
        DestructionFeatureTier tier{};              /**< Exact feature tier, never an ordered preference. */
        DestructionReplicationIntent replication{}; /**< Canonical authority topology. */
        std::array<DestructionCapabilityRequirement, DestructionHostCapabilityCount> requirements{};

        [[nodiscard]] constexpr auto operator<=>(const DestructionProductProfilePolicy &) const noexcept = default;
    };

    /** @brief Bounded borrowed input captured and validated before composition publication. */
    struct DestructionCompositionRequest final {
        std::uint32_t contractVersion{CurrentDestructionCompositionContractVersion}; /**< Portable schema version. */
        DestructionCompositionRevision revision{};                                   /**< Exact publication generation. */
        DestructionProductProfile profile{};                                         /**< Exact product profile. */
        std::span<const DestructionCapabilityFact> capabilities{};                   /**< Exactly one fact per family. */
    };

    /** @brief One owned capability decision with no service pointer, callback, or native handle. */
    struct DestructionCapabilityResolution final {
        DestructionHostCapability capability{};             /**< Closed capability family. */
        DestructionCapabilityRequirement requirement{};     /**< Policy that produced this decision. */
        DestructionCapabilityResolutionState state{};       /**< Omitted, explicitly unavailable, or bound. */
        DestructionHostCapabilityRevision sourceRevision{}; /**< Exact evidence revision only when bound. */

        [[nodiscard]] constexpr auto operator<=>(const DestructionCapabilityResolution &) const noexcept = default;
    };

    /**
     * @brief Immutable product-profile decision produced without installation, discovery, or fallback.
     * @details The value owns only fixed-size provider-neutral data. It grants no service lifetime or mutation authority.
     */
    class DestructionComposition final {
    public:
        /**
         * @brief Resolve one exact profile against a complete immutable capability snapshot.
         * @param request Profile, revision, schema, and exactly one fact for every capability family.
         * @return Owned composition or a typed invalid/required-capability failure.
         * @post Failure publishes no partial state and performs no allocation, registration, or backend call.
         */
        [[nodiscard]] static Result<DestructionComposition> Create(const DestructionCompositionRequest &request);

        /** @brief Return the exact product policy. @return Owned immutable policy. */
        [[nodiscard]] const DestructionProductProfilePolicy &Policy() const noexcept;
        /** @brief Return the exact composition revision. @return Non-zero immutable revision. */
        [[nodiscard]] DestructionCompositionRevision Revision() const noexcept;
        /** @brief Return all decisions in capability-enum order. @return Fixed-size immutable decisions. */
        [[nodiscard]] const std::array<DestructionCapabilityResolution, DestructionHostCapabilityCount> &Capabilities() const noexcept;
        /** @brief Resolve one known family. @param capability Capability to inspect. @return Decision or CompositionInvalid. */
        [[nodiscard]] Result<DestructionCapabilityResolution> Resolve(DestructionHostCapability capability) const;

    private:
        DestructionComposition(DestructionProductProfilePolicy policy, DestructionCompositionRevision revision,
                               std::array<DestructionCapabilityResolution, DestructionHostCapabilityCount> capabilities) noexcept;

        DestructionProductProfilePolicy policy_{};
        DestructionCompositionRevision revision_{};
        std::array<DestructionCapabilityResolution, DestructionHostCapabilityCount> capabilities_{};
    };

    /**
     * @brief Return the canonical policy for one exact profile without activating any capability.
     * @param profile Exact profile; unknown values are rejected.
     * @return Inert policy or DestructionErrors::CompositionInvalid.
     */
    [[nodiscard]] Result<DestructionProductProfilePolicy> GetDestructionProductProfilePolicy(DestructionProductProfile profile);

    /**
     * @brief Revalidate immutable composition and lifecycle evidence before work admission.
     * @param composition Previously resolved composition.
     * @param currentRevision Current host-published composition revision.
     * @param lifecycle Current explicit lifecycle state.
     * @return Success, or typed stale, cancellation, shutdown, or invalid-state failure.
     */
    [[nodiscard]] Result<void> ValidateDestructionCompositionAdmission(const DestructionComposition &composition,
                                                                       DestructionCompositionRevision currentRevision,
                                                                       DestructionCompositionLifecycle lifecycle);
}  // namespace Horo::Destruction
