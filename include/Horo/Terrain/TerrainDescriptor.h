#pragma once

/**
 * @file TerrainDescriptor.h
 * @brief Revisioned Terrain bounds, provider-neutral tier limits, and shared descriptor admission.
 */

#include "Horo/Math/WorldCoordinate64.h"
#include "Horo/Terrain/TerrainIdentity.h"

#include <compare>
#include <cstdint>
#include <optional>

namespace Horo::Terrain {
    /** @brief First portable revision of the shared Terrain/Foliage descriptor contract. */
    inline constexpr std::uint32_t CurrentTerrainDescriptorContractVersion = 1;
    /** @brief Exact version of the canonical core tier-limit table. */
    inline constexpr std::uint32_t CurrentTerrainTierProfileRevision = 1;

    namespace Detail {
        struct TerrainBoundsRevisionTag;
        struct TerrainConfigurationRevisionTag;
    }  // namespace Detail

    /** @brief Non-zero revision of one immutable spatial-bounds publication. */
    using TerrainBoundsRevision = Foundation::Detail::NonZeroId64<Detail::TerrainBoundsRevisionTag, TerrainErrors::IdentityInvalid>;
    /** @brief Non-zero revision of one immutable project Terrain configuration publication. */
    using TerrainConfigurationRevision =
        Foundation::Detail::NonZeroId64<Detail::TerrainConfigurationRevisionTag, TerrainErrors::IdentityInvalid>;

    /** @brief Provider-neutral product preference; the value never grants backend capability. */
    enum class TerrainFeatureTier : std::uint8_t {
        Baseline,
        Standard,
        High,
        Ultra,
        Count
    };

    /** @brief Fixed-width set of exact tiers supported by a captured host/content plan. */
    struct TerrainFeatureTierSet final {
        std::uint8_t bits{}; /**< One bit per known TerrainFeatureTier. */

        /**
         * @brief Tests whether one known exact tier is present.
         * @param tier Tier to test without fallback or ordering semantics.
         * @return True only for a known tier whose bit is set.
         */
        [[nodiscard]] constexpr bool Contains(const TerrainFeatureTier tier) const noexcept {
            const auto index = static_cast<std::uint8_t>(tier);
            return index < static_cast<std::uint8_t>(TerrainFeatureTier::Count) &&
                   (bits & static_cast<std::uint8_t>(std::uint8_t{1} << index)) != 0;
        }

        /** @brief Checks that the set is non-empty and contains no unknown bits. @return True for a canonical set. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            constexpr auto count = static_cast<std::uint8_t>(TerrainFeatureTier::Count);
            const auto known = static_cast<std::uint8_t>((std::uint8_t{1} << count) - 1U);
            return bits != 0 && (bits & static_cast<std::uint8_t>(~known)) == 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const TerrainFeatureTierSet &) const noexcept = default;
    };

    /** @brief Returns the canonical bit for one compile-time-known Terrain tier. */
    template <TerrainFeatureTier Tier>
    inline constexpr std::uint8_t TerrainFeatureTierBit = [] {
        static_assert(Tier < TerrainFeatureTier::Count, "Tier must belong to the closed Terrain tier vocabulary");
        return static_cast<std::uint8_t>(std::uint8_t{1} << static_cast<std::uint8_t>(Tier));
    }();

    /** @brief Absolute safety ceilings for every version-1 Terrain/Foliage descriptor. */
    struct TerrainDescriptorHardLimits final {
        static constexpr std::uint32_t SamplesPerAxis = 32'769;                          /**< Maximum height samples per axis. */
        static constexpr std::uint32_t TileInteriorQuads = 256;                          /**< Maximum quads along one tile axis. */
        static constexpr std::uint8_t LodLevels = 12;                                    /**< Maximum cooked LOD representations. */
        static constexpr std::uint8_t LayersPerTile = 16;                                /**< Maximum material layers per tile. */
        static constexpr std::uint32_t ActiveTerrainTiles = 2'048;                       /**< Maximum active logical tiles. */
        static constexpr std::uint32_t ActiveFoliageClusters = 8'192;                    /**< Maximum active foliage clusters. */
        static constexpr std::uint64_t ActiveFoliageInstances = 2'097'152;               /**< Maximum active foliage instances. */
        static constexpr std::uint64_t ResidentTerrainBytes = 2ULL * 1024 * 1024 * 1024; /**< Maximum Terrain-owned bytes. */
        static constexpr std::uint64_t ResidentFoliageBytes = 2ULL * 1024 * 1024 * 1024; /**< Maximum Foliage-owned bytes. */
        static constexpr std::uint64_t StagingBytes = 1024ULL * 1024 * 1024;             /**< Maximum candidate staging bytes. */
        static constexpr std::uint64_t RetiringBytes = 1024ULL * 1024 * 1024;            /**< Maximum old-generation bytes. */
        static constexpr std::uint64_t WorkItems = 8'388'608;                            /**< Maximum bounded admission work. */
    };

    /** @brief Complete finite descriptor ceilings captured by project configuration. */
    struct TerrainDescriptorLimits final {
        std::uint32_t maximumSamplesPerAxis{};         /**< Heightfield samples along either axis. */
        std::uint32_t maximumTileInteriorQuads{};      /**< Interior quads along one tile axis. */
        std::uint8_t maximumLodLevels{};               /**< Cooked LOD representations. */
        std::uint8_t maximumLayersPerTile{};           /**< Material layers referenced by one tile. */
        std::uint32_t maximumActiveTerrainTiles{};     /**< Simultaneously active logical terrain tiles. */
        std::uint32_t maximumActiveFoliageClusters{};  /**< Simultaneously active foliage clusters. */
        std::uint64_t maximumActiveFoliageInstances{}; /**< Simultaneously active baked and dynamic instances. */
        std::uint64_t maximumResidentTerrainBytes{};   /**< Terrain-owned steady resident bytes. */
        std::uint64_t maximumResidentFoliageBytes{};   /**< Foliage-owned steady resident bytes. */
        std::uint64_t maximumStagingBytes{};           /**< Candidate/decode/upload overlap bytes. */
        std::uint64_t maximumRetiringBytes{};          /**< Old-generation retirement overlap bytes. */
        std::uint64_t maximumWorkItems{};              /**< Complete bounded validation/preparation work. */

        [[nodiscard]] constexpr auto operator<=>(const TerrainDescriptorLimits &) const noexcept = default;
    };

    /** @brief Canonical exact ceiling table entry for one provider-neutral tier. */
    struct TerrainTierProfile final {
        TerrainFeatureTier tier{TerrainFeatureTier::Baseline};     /**< Exact tier; no fallback is implied. */
        std::uint32_t revision{CurrentTerrainTierProfileRevision}; /**< Numeric table revision. */
        TerrainDescriptorLimits limits{};                          /**< Maximum descriptor limits for the tier. */

        [[nodiscard]] constexpr auto operator<=>(const TerrainTierProfile &) const noexcept = default;
    };

    /** @brief Immutable inclusive spatial envelope associated with one non-wrapping publication revision. */
    struct TerrainRevisionedBounds final {
        TerrainBoundsRevision revision{};  /**< Exact bounds publication revision. */
        Math::WorldCoordinate64 minimum{}; /**< Inclusive canonical lower corner. */
        Math::WorldCoordinate64 maximum{}; /**< Inclusive canonical upper corner; every axis must be greater than its minimum. */

        [[nodiscard]] constexpr auto operator<=>(const TerrainRevisionedBounds &) const noexcept = default;
    };

    /** @brief Finite regular-grid shape shared by cook, package, and runtime validation. */
    struct TerrainGridShape final {
        std::uint32_t samplesX{};          /**< Complete dataset height samples along X. */
        std::uint32_t samplesZ{};          /**< Complete dataset height samples along Z. */
        std::uint32_t tileInteriorQuads{}; /**< Interior quads along one square tile axis. */
        std::uint8_t lodLevels{};          /**< Number of cooked LOD representations. */
        std::uint8_t layersPerTile{};      /**< Maximum referenced material layers per tile. */

        [[nodiscard]] constexpr auto operator<=>(const TerrainGridShape &) const noexcept = default;
    };

    /** @brief Exact counts, bytes, and work checked before allocation or candidate publication. */
    struct TerrainDescriptorFootprint final {
        std::uint32_t activeTerrainTiles{};     /**< Required logical terrain tiles. */
        std::uint32_t activeFoliageClusters{};  /**< Required logical foliage clusters. */
        std::uint64_t activeFoliageInstances{}; /**< Required baked plus dynamic foliage instances. */
        std::uint64_t residentTerrainBytes{};   /**< Complete Terrain-owned steady bytes. */
        std::uint64_t residentFoliageBytes{};   /**< Complete Foliage-owned steady bytes. */
        std::uint64_t stagingBytes{};           /**< Peak candidate/decode/upload bytes. */
        std::uint64_t retiringBytes{};          /**< Peak old-generation overlap bytes. */
        std::uint64_t workItems{};              /**< Complete deterministic preparation work. */

        [[nodiscard]] constexpr auto operator<=>(const TerrainDescriptorFootprint &) const noexcept = default;
    };

    /** @brief Mutable construction data copied into one immutable configuration snapshot. */
    struct TerrainConfigurationSnapshotData final {
        std::uint32_t contractVersion{CurrentTerrainDescriptorContractVersion}; /**< Portable schema version. */
        std::uint32_t tierProfileRevision{CurrentTerrainTierProfileRevision};   /**< Exact numeric table version. */
        TerrainConfigurationRevision configuration{};                           /**< Project configuration publication. */
        TerrainCapabilityRevision capability{};                                 /**< Captured effective capability plan. */
        TerrainFeatureTier tier{TerrainFeatureTier::Baseline};                  /**< Exact selected tier. */
        TerrainDescriptorLimits limits{};                                       /**< Finite project limits no wider than tier. */

        [[nodiscard]] constexpr auto operator<=>(const TerrainConfigurationSnapshotData &) const noexcept = default;
    };

    /**
     * @brief Immutable fixed-size Terrain/Foliage configuration snapshot.
     *
     * The snapshot owns no registry, backend handle, callback, reservation, worker, or mutable ambient state.
     */
    class TerrainConfigurationSnapshot final {
    public:
        /**
         * @brief Validates and copies one exact tier configuration before work.
         * @param data Candidate revision, tier, and finite limits.
         * @return Immutable snapshot or a typed invalid revision, tier, or limit-profile failure.
         */
        [[nodiscard]] static Result<TerrainConfigurationSnapshot> Create(const TerrainConfigurationSnapshotData &data);

        /** @brief Returns the exact captured configuration. @return Borrowed immutable fixed-size data. */
        [[nodiscard]] const TerrainConfigurationSnapshotData &Data() const noexcept;

    private:
        explicit TerrainConfigurationSnapshot(const TerrainConfigurationSnapshotData &data) noexcept;

        TerrainConfigurationSnapshotData data_;
    };

    /** @brief Mutable construction data copied into one immutable combined Terrain/Foliage descriptor. */
    struct TerrainDatasetDescriptorData final {
        std::uint32_t contractVersion{CurrentTerrainDescriptorContractVersion}; /**< Portable schema version. */
        TerrainDatasetId dataset{};                                             /**< Stable authored/cooked dataset identity. */
        TerrainContentRevision content{};                                       /**< Exact immutable semantic content. */
        TerrainRevisionedBounds bounds{};                                       /**< Exact spatial envelope publication. */
        TerrainGridShape grid{};                                                /**< Complete bounded regular-grid shape. */
        TerrainDescriptorFootprint footprint{};                                 /**< Exact peak counts, bytes, and work. */

        [[nodiscard]] constexpr auto operator<=>(const TerrainDatasetDescriptorData &) const noexcept = default;
    };

    /** @brief Immutable dataset facts shared by Terrain and Foliage admission paths. */
    class TerrainDatasetDescriptor final {
    public:
        /**
         * @brief Validates and copies dataset facts against one immutable configuration.
         * @param data Candidate identity, revisions, bounds, dimensions, and footprint.
         * @param configuration Captured exact tier limits.
         * @return Immutable descriptor or a typed malformed/limit failure.
         * @post Failure performs no allocation, registration, activation, or partial publication.
         */
        [[nodiscard]] static Result<TerrainDatasetDescriptor> Create(const TerrainDatasetDescriptorData &data,
                                                                     const TerrainConfigurationSnapshot &configuration);

        /** @brief Returns the exact captured dataset facts. @return Borrowed immutable fixed-size data. */
        [[nodiscard]] const TerrainDatasetDescriptorData &Data() const noexcept;

    private:
        explicit TerrainDatasetDescriptor(const TerrainDatasetDescriptorData &data) noexcept;

        TerrainDatasetDescriptorData data_;
    };

    /** @brief Whether admission installs a first dataset generation or replaces an exact current generation. */
    enum class TerrainDescriptorAdmissionKind : std::uint8_t {
        Insert,
        Replace,
        Count
    };

    /** @brief Exact current owner state captured at a side-effect-free admission boundary. */
    struct TerrainDescriptorAdmissionContext final {
        TerrainRuntimeLifecycle lifecycle{TerrainRuntimeLifecycle::Active}; /**< Current owner admission lifecycle. */
        std::optional<TerrainDatasetId> currentDataset{};                   /**< Published owner, absent for first insert. */
        std::optional<TerrainContentRevision> currentContent{};             /**< Published content, absent for first insert. */
        std::optional<TerrainRevisionedBounds> currentBounds{};             /**< Published bounds, absent for first insert. */
        TerrainConfigurationRevision configuration{};                       /**< Current project configuration revision. */
        TerrainCapabilityRevision capability{};                             /**< Current effective capability revision. */
        TerrainFeatureTierSet supportedTiers{};                             /**< Exact tiers present in content/host plan. */
    };

    /** @brief Pure admission request carrying an explicit replacement expectation. */
    struct TerrainDescriptorAdmissionRequest final {
        TerrainDescriptorAdmissionKind kind{TerrainDescriptorAdmissionKind::Insert}; /**< Explicit mutation intent. */
        std::optional<TerrainContentRevision> expectedCurrentContent{};              /**< Required only for replacement. */
    };

    /**
     * @brief Returns the canonical version-1 profile for one exact provider-neutral tier.
     * @param tier Exact requested tier.
     * @return Profile or TerrainErrors::TierInvalid; no alternative tier is selected.
     */
    [[nodiscard]] Result<TerrainTierProfile> GetTerrainTierProfile(TerrainFeatureTier tier);

    /**
     * @brief Resolves an exact requested tier without ordering, downgrade, or silent fallback.
     * @param requested Exact product-selected tier.
     * @param supported Exact tiers supported by captured content and host capabilities.
     * @return The requested tier or a typed invalid/unsupported failure.
     */
    [[nodiscard]] Result<TerrainFeatureTier> ResolveTerrainFeatureTier(TerrainFeatureTier requested, TerrainFeatureTierSet supported);

    /**
     * @brief Validates lifecycle, revisions, exact-tier support, and insert/replacement fencing before work.
     * @param descriptor Immutable candidate dataset facts.
     * @param configuration Immutable configuration used to validate the candidate.
     * @param request Explicit insert or replacement intent.
     * @param context Exact current owner state captured at admission.
     * @return Success or a typed lifecycle, stale-revision, unsupported-tier, or replacement failure.
     * @post Success grants no ownership and performs no allocation, queue mutation, callback, or publication.
     */
    [[nodiscard]] Result<void> ValidateTerrainDescriptorAdmission(const TerrainDatasetDescriptor &descriptor,
                                                                  const TerrainConfigurationSnapshot &configuration,
                                                                  const TerrainDescriptorAdmissionRequest &request,
                                                                  const TerrainDescriptorAdmissionContext &context);
}  // namespace Horo::Terrain
