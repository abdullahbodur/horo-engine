#pragma once

/**
 * @file WorldPartitionCapabilityProfile.h
 * @brief Typed world-partition capability and project-settings profile contracts.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Foundation/StrongId.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"

#include <compare>
#include <cstdint>

namespace Horo::WorldStreaming {
    namespace Detail {
        struct WorldPartitionCapabilityIdTag;
        struct WorldPartitionCapabilityRevisionTag;
        struct WorldPartitionSettingsIdTag;
        struct WorldPartitionSettingsRevisionTag;
    }  // namespace Detail

    /** @brief Stable identity of one host/product world-partition capability authority. */
    using WorldPartitionCapabilityId =
        Foundation::Detail::NonZeroId64<Detail::WorldPartitionCapabilityIdTag, WorldStreamingErrors::IdentityInvalid>;
    /** @brief Non-zero revision of one immutable host capability snapshot. */
    using WorldPartitionCapabilityRevision =
        Foundation::Detail::NonZeroId64<Detail::WorldPartitionCapabilityRevisionTag, WorldStreamingErrors::IdentityInvalid>;
    /** @brief Stable identity of one project-owned world-partition settings authority. */
    using WorldPartitionSettingsId =
        Foundation::Detail::NonZeroId64<Detail::WorldPartitionSettingsIdTag, WorldStreamingErrors::IdentityInvalid>;
    /** @brief Non-zero revision of one validated project-settings publication. */
    using WorldPartitionSettingsRevision =
        Foundation::Detail::NonZeroId64<Detail::WorldPartitionSettingsRevisionTag, WorldStreamingErrors::IdentityInvalid>;

    /** @brief Closed project profiles with explicit package policy and no fallback ordering. */
    enum class WorldPartitionProjectProfile : std::uint8_t {
        Editor,
        Standalone,
        Client,
        Server,
        Count,
    };

    /** @brief Canonical persisted coordinate representation requested by a project. */
    enum class WorldPartitionPrecision : std::uint8_t {
        SignedMillimeter64,
        Count,
    };

    /** @brief Exact cooked cell packaging selected by project settings. */
    enum class WorldPartitionPackageMode : std::uint8_t {
        StandaloneCellFile,
        ArchiveChunk,
        Count,
    };

    /** @brief Bitset of exact package modes supported by one host/product artifact. */
    enum class WorldPartitionPackageCapabilities : std::uint8_t {
        None = 0,
        StandaloneCellFile = 1U << 0U,
        ArchiveChunk = 1U << 1U,
    };

    /** @brief Combines declared package capabilities. */
    [[nodiscard]] constexpr WorldPartitionPackageCapabilities operator|(const WorldPartitionPackageCapabilities left,
                                                                        const WorldPartitionPackageCapabilities right) noexcept {
        return static_cast<WorldPartitionPackageCapabilities>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
    }

    /** @brief Explicit settings-admission lifecycle state. */
    enum class WorldPartitionSettingsLifecycle : std::uint8_t {
        Active,
        Cancelling,
        Closed,
        Count,
    };

    /** @brief Immutable host/product ceilings used to validate project settings. */
    struct WorldPartitionCapabilitySnapshot final {
        static constexpr std::int64_t ImplementationMaximumCellSizeMillimeters = std::int64_t{1} << 31U; /**< Overflow-safe edge. */
        static constexpr std::uint32_t ImplementationMaximumLayers = 65'534;                     /**< Excludes reserved layer identities. */
        static constexpr std::uint32_t ImplementationMaximumCells = 1'048'576;                   /**< Per-publication hard ceiling. */
        static constexpr std::uint32_t ImplementationMaximumLayerNameBytes = 4U * 1024U * 1024U; /**< Aggregate UTF-8 ceiling. */
        static constexpr std::uint32_t ImplementationMaximumQueryResults = 65'536;               /**< Per-query hard ceiling. */

        WorldPartitionCapabilityId capability{};      /**< Stable host/product capability authority. */
        WorldPartitionCapabilityRevision revision{};  /**< Exact host capability publication. */
        std::int64_t minimumCellSizeMillimeters{};    /**< Smallest supported level-zero cell edge. */
        std::int64_t maximumCellSizeMillimeters{};    /**< Largest supported level-zero cell edge. */
        std::uint8_t maximumLodLevels{};              /**< Maximum supported LOD count. */
        std::uint32_t maximumLayers{};                /**< Maximum descriptor layer count. */
        std::uint32_t maximumCells{};                 /**< Maximum descriptor cell count. */
        std::uint32_t maximumLayerNameBytes{};        /**< Maximum aggregate layer-name bytes. */
        std::uint32_t maximumQueryResults{};          /**< Maximum handles emitted by one spatial query. */
        WorldPartitionPrecision precision{};          /**< Exact supported persisted precision. */
        WorldPartitionPackageCapabilities packages{}; /**< Supported package representations. */

        [[nodiscard]] constexpr auto operator<=>(const WorldPartitionCapabilitySnapshot &) const noexcept = default;
    };

    /** @brief Inert canonical package policy for one project profile. */
    struct WorldPartitionProjectProfilePolicy final {
        WorldPartitionProjectProfile profile{};       /**< Exact profile identity. */
        WorldPartitionPackageCapabilities packages{}; /**< Modes the profile permits. */

        [[nodiscard]] constexpr auto operator<=>(const WorldPartitionProjectProfilePolicy &) const noexcept = default;
    };

    /** @brief Borrowed creation input captured into one immutable settings value. */
    struct WorldPartitionProjectSettingsRequest final {
        static constexpr std::uint32_t CurrentContractVersion = 1;

        std::uint32_t contractVersion{CurrentContractVersion}; /**< Exact portable contract version. */
        WorldPartitionSettingsId settings{};                   /**< Stable project-settings authority. */
        WorldPartitionSettingsRevision revision{};             /**< Exact project-settings publication. */
        WorldPartitionProjectProfile profile{};                /**< Explicit product profile. */
        std::int64_t baseCellSizeMillimeters{};                /**< Requested level-zero cell edge. */
        std::uint8_t lodLevels{};                              /**< Requested LOD count. */
        std::uint32_t maximumLayers{};                         /**< Requested descriptor layer ceiling. */
        std::uint32_t maximumCells{};                          /**< Requested descriptor cell ceiling. */
        std::uint32_t maximumLayerNameBytes{};                 /**< Requested aggregate layer-name bytes. */
        std::uint32_t maximumQueryResults{};                   /**< Requested spatial-query output ceiling. */
        WorldPartitionPrecision precision{};                   /**< Requested persisted precision. */
        WorldPartitionPackageMode packageMode{};               /**< Exact package representation; never a preference. */
    };

    /** @brief Owned immutable project settings resolved against one exact capability revision. */
    class WorldPartitionProjectSettings final {
    public:
        /**
         * @brief Validates and captures one project profile against explicit host capabilities.
         * @param request Complete project-owned settings request.
         * @param capabilities Complete immutable host/product capability snapshot.
         * @return Owned settings or a typed invalid, unsupported, or capacity failure.
         * @post Failure publishes no partial state and performs no registration or activation.
         */
        [[nodiscard]] static Result<WorldPartitionProjectSettings> Create(const WorldPartitionProjectSettingsRequest &request,
                                                                          const WorldPartitionCapabilitySnapshot &capabilities);

        /** @brief Returns the exact settings revision. @return Non-zero immutable revision. */
        [[nodiscard]] WorldPartitionSettingsRevision Revision() const noexcept;
        /** @brief Returns the stable project-settings identity. @return Non-zero immutable identity. */
        [[nodiscard]] WorldPartitionSettingsId Settings() const noexcept;
        /** @brief Returns the stable capability authority identity. @return Non-zero immutable identity. */
        [[nodiscard]] WorldPartitionCapabilityId Capability() const noexcept;
        /** @brief Returns the capability revision captured at validation. @return Exact immutable capability revision. */
        [[nodiscard]] WorldPartitionCapabilityRevision CapabilityRevision() const noexcept;
        /** @brief Returns the exact project profile. @return Closed profile identity. */
        [[nodiscard]] WorldPartitionProjectProfile Profile() const noexcept;
        /** @brief Returns the requested level-zero cell edge. @return Positive canonical millimeters. */
        [[nodiscard]] std::int64_t BaseCellSizeMillimeters() const noexcept;
        /** @brief Returns the requested LOD count. @return Value in [1, 32]. */
        [[nodiscard]] std::uint8_t LodLevels() const noexcept;
        /** @brief Returns the requested layer ceiling. @return Positive bounded count. */
        [[nodiscard]] std::uint32_t MaximumLayers() const noexcept;
        /** @brief Returns the requested cell ceiling. @return Positive bounded count. */
        [[nodiscard]] std::uint32_t MaximumCells() const noexcept;
        /** @brief Returns the requested aggregate layer-name ceiling. @return Positive bounded byte count. */
        [[nodiscard]] std::uint32_t MaximumLayerNameBytes() const noexcept;
        /** @brief Returns the requested query ceiling. @return Positive count no larger than MaximumCells(). */
        [[nodiscard]] std::uint32_t MaximumQueryResults() const noexcept;
        /** @brief Returns the exact persisted precision. @return Supported precision identity. */
        [[nodiscard]] WorldPartitionPrecision Precision() const noexcept;
        /** @brief Returns the exact selected package mode. @return Supported non-fallback mode. */
        [[nodiscard]] WorldPartitionPackageMode PackageMode() const noexcept;

    private:
        WorldPartitionProjectSettings(const WorldPartitionProjectSettingsRequest &request, WorldPartitionCapabilityId capability,
                                      WorldPartitionCapabilityRevision capabilityRevision) noexcept;

        WorldPartitionSettingsId settings_{};
        WorldPartitionSettingsRevision revision_{};
        WorldPartitionCapabilityId capability_{};
        WorldPartitionCapabilityRevision capabilityRevision_{};
        WorldPartitionProjectProfile profile_{};
        std::int64_t baseCellSizeMillimeters_{};
        std::uint8_t lodLevels_{};
        std::uint32_t maximumLayers_{};
        std::uint32_t maximumCells_{};
        std::uint32_t maximumLayerNameBytes_{};
        std::uint32_t maximumQueryResults_{};
        WorldPartitionPrecision precision_{};
        WorldPartitionPackageMode packageMode_{};
    };

    /**
     * @brief Returns the inert canonical package policy for one exact profile.
     * @param profile Exact project profile.
     * @return Fixed-size policy or WorldStreamingErrors::PartitionSettingsInvalid.
     */
    [[nodiscard]] Result<WorldPartitionProjectProfilePolicy> GetWorldPartitionProjectProfilePolicy(WorldPartitionProjectProfile profile);

    /**
     * @brief Revalidates revision and lifecycle evidence before settings-dependent work admission.
     * @param settings Previously validated immutable settings.
     * @param currentSettings Current project-settings authority identity.
     * @param currentSettingsRevision Current project-settings publication.
     * @param currentCapability Current host capability authority identity.
     * @param currentCapabilityRevision Current host capability publication.
     * @param lifecycle Current explicit lifecycle state.
     * @return Success, or a typed stale, cancellation, shutdown, or invalid-state failure.
     */
    [[nodiscard]] Result<void> ValidateWorldPartitionSettingsAdmission(const WorldPartitionProjectSettings &settings,
                                                                       WorldPartitionSettingsId currentSettings,
                                                                       WorldPartitionSettingsRevision currentSettingsRevision,
                                                                       WorldPartitionCapabilityId currentCapability,
                                                                       WorldPartitionCapabilityRevision currentCapabilityRevision,
                                                                       WorldPartitionSettingsLifecycle lifecycle);
}  // namespace Horo::WorldStreaming
