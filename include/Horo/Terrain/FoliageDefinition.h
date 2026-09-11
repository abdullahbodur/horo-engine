#pragma once

/**
 * @file FoliageDefinition.h
 * @brief Stable provider-neutral foliage type, placement, culling, wind, and collision definitions.
 */

#include "Horo/Terrain/TerrainDescriptor.h"
#include "Horo/Terrain/TerrainIdentity.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Horo::Terrain {
    /** @brief Current stable cook/runtime foliage definition schema. */
    inline constexpr std::uint16_t CurrentFoliageDefinitionContractVersion = 1;
    /** @brief Current deterministic placement algorithm and quantization contract. */
    inline constexpr std::uint16_t CurrentFoliagePlacementAlgorithmVersion = 1;
    /** @brief Maximum mesh LOD assets retained by one fixed-size definition. */
    inline constexpr std::size_t MaximumFoliageMeshLods = 4;

    namespace Detail {
        struct FoliageDefinitionRevisionTag;
        struct FoliageMeshAssetIdentityTag;
        struct FoliageMaterialAssetIdentityTag;
        struct FoliageImpostorAssetIdentityTag;
    }  // namespace Detail

    /** @brief Immutable semantic revision of one foliage type definition. */
    using FoliageDefinitionRevision = Foundation::Detail::NonZeroId64<Detail::FoliageDefinitionRevisionTag, TerrainErrors::IdentityInvalid>;
    /** @brief Stable foliage mesh artifact reference, distinct from material and impostor identities. */
    using FoliageMeshAssetId = TerrainStableIdentity<Detail::FoliageMeshAssetIdentityTag>;
    /** @brief Stable foliage material artifact reference, distinct from mesh and impostor identities. */
    using FoliageMaterialAssetId = TerrainStableIdentity<Detail::FoliageMaterialAssetIdentityTag>;
    /** @brief Stable foliage impostor artifact reference, distinct from mesh and material identities. */
    using FoliageImpostorAssetId = TerrainStableIdentity<Detail::FoliageImpostorAssetIdentityTag>;

    /** @brief Versioned deterministic placement recipe. */
    enum class FoliagePlacementAlgorithm : std::uint8_t {
        StratifiedJitterV1, /**< Canonically ordered strata with deterministic fixed-point jitter. */
        Count,              /**< Sentinel outside the valid serialized vocabulary. */
    };

    /** @brief Authored orientation policy evaluated by Terrain Cook. */
    enum class FoliageSurfaceAlignment : std::uint8_t {
        Upright,       /**< Retain canonical world-up orientation. */
        SurfaceNormal, /**< Align the instance up axis to the quantized terrain normal. */
        Count,         /**< Sentinel outside the valid serialized vocabulary. */
    };

    /** @brief Provider-neutral visibility recipe required by cooked content. */
    enum class FoliageCullingRecipe : std::uint8_t {
        CpuDirect,   /**< Bounded CPU visibility/LOD planning with direct or instanced batches. */
        GpuIndirect, /**< Explicit optional renderer-owned compute and indirect recipe. */
        Count,       /**< Sentinel outside the valid serialized vocabulary. */
    };

    /** @brief Authored vertex-deformation semantics; no per-instance CPU simulation state is implied. */
    enum class FoliageWindModel : std::uint8_t {
        None,                 /**< Definition has no wind deformation. */
        VertexBend,           /**< Primary/secondary branch bend only. */
        VertexBendAndFlutter, /**< Branch bend plus leaf flutter. */
        Count,                /**< Sentinel outside the valid serialized vocabulary. */
    };

    /** @brief Neutral optional collision primitive cooked for the Physics consumer. */
    enum class FoliageCollisionShape : std::uint8_t {
        None,     /**< Visual-only foliage with no collision consumer requirement. */
        Cylinder, /**< Upright simplified cylinder. */
        Capsule,  /**< Upright simplified capsule. */
        Count,    /**< Sentinel outside the valid serialized vocabulary. */
    };

    /** @brief Explicit capability bits consumed while validating one definition. */
    enum class FoliageDefinitionCapability : std::uint8_t {
        CpuCulling,
        GpuIndirectCulling,
        Impostors,
        VertexWind,
        Collision,
        NavigationBlocking,
        Count,
    };

    /** @brief Fixed capability set; unknown bits are invalid rather than ignored. */
    struct FoliageDefinitionCapabilitySet final {
        std::uint16_t bits{}; /**< Bit positions correspond exactly to FoliageDefinitionCapability. */

        /** @brief Checks that no unknown capability bit is set. @return True for a closed-vocabulary set. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return bits < (std::uint16_t{1} << static_cast<std::uint8_t>(FoliageDefinitionCapability::Count));
        }

        /** @brief Tests one known capability. @param capability Capability to query. @return True only when present. */
        [[nodiscard]] constexpr bool Contains(const FoliageDefinitionCapability capability) const noexcept {
            if (capability >= FoliageDefinitionCapability::Count)
                return false;
            return (bits & (std::uint16_t{1} << static_cast<std::uint8_t>(capability))) != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const FoliageDefinitionCapabilitySet &) const noexcept = default;
    };

    /** @brief Compile-time bit for constructing a closed FoliageDefinitionCapabilitySet. */
    template <FoliageDefinitionCapability Capability>
    inline constexpr std::uint16_t FoliageDefinitionCapabilityBit = [] {
        static_assert(Capability < FoliageDefinitionCapability::Count);
        return std::uint16_t{1} << static_cast<std::uint8_t>(Capability);
    }();

    /** @brief Fixed-size stable mesh/material/impostor asset references. */
    struct FoliageVisualAssets final {
        std::array<FoliageMeshAssetId, MaximumFoliageMeshLods> meshLods{}; /**< Near-to-far mesh LOD artifacts. */
        std::uint8_t meshLodCount{};                                       /**< Number of populated leading mesh entries. */
        FoliageMaterialAssetId material{};                                 /**< Cooked material-semantic artifact. */
        std::optional<FoliageImpostorAssetId> impostor{};                  /**< Optional cooked impostor artifact. */

        [[nodiscard]] constexpr auto operator<=>(const FoliageVisualAssets &) const noexcept = default;
    };

    /** @brief Deterministic fixed-point placement constraints shared by cook and runtime validation. */
    struct FoliagePlacementDefinition final {
        std::uint16_t algorithmVersion{CurrentFoliagePlacementAlgorithmVersion};            /**< Exact placement behavior version. */
        FoliagePlacementAlgorithm algorithm{FoliagePlacementAlgorithm::StratifiedJitterV1}; /**< Exact algorithm. */
        FoliageSurfaceAlignment alignment{FoliageSurfaceAlignment::Upright};                /**< Authored orientation policy. */
        std::uint64_t seed{};                                                               /**< Stable authored seed; zero is valid. */
        std::uint32_t densityPerSquareKilometer{};                                          /**< Fixed-point placement density. */
        std::int64_t minimumAltitudeMillimeters{};                                          /**< Inclusive canonical altitude floor. */
        std::int64_t maximumAltitudeMillimeters{};                                          /**< Inclusive canonical altitude ceiling. */
        std::uint32_t minimumSlopeMilliDegrees{};                                           /**< Inclusive quantized slope floor. */
        std::uint32_t maximumSlopeMilliDegrees{};                                           /**< Inclusive quantized slope ceiling. */
        std::uint32_t minimumSeparationMillimeters{}; /**< Minimum accepted inter-instance distance. */
        std::uint32_t coordinateQuantumMillimeters{}; /**< Fixed coordinate/tie-break quantization. */

        [[nodiscard]] constexpr auto operator<=>(const FoliagePlacementDefinition &) const noexcept = default;
    };

    /** @brief Fixed, monotonically ordered per-view visibility and LOD thresholds. */
    struct FoliageCullingDefinition final {
        FoliageCullingRecipe recipe{FoliageCullingRecipe::CpuDirect};                   /**< Exact required visibility recipe. */
        std::array<std::uint32_t, MaximumFoliageMeshLods> meshLodDistanceMillimeters{}; /**< Leading LOD thresholds. */
        std::uint32_t impostorStartDistanceMillimeters{};                               /**< Required only with an impostor asset. */
        std::uint32_t cullDistanceMillimeters{};      /**< Exclusive visibility distance after all LODs. */
        std::uint32_t crossFadeDistanceMillimeters{}; /**< Bounded transition width; zero disables blending. */

        [[nodiscard]] constexpr auto operator<=>(const FoliageCullingDefinition &) const noexcept = default;
    };

    /** @brief Fixed-point wind parameters with no backend or scene-global state. */
    struct FoliageWindDefinition final {
        FoliageWindModel model{FoliageWindModel::None}; /**< Required deformation model. */
        std::uint16_t primaryStrengthPermille{};        /**< Primary bend weight in [0,1000]. */
        std::uint16_t secondaryStrengthPermille{};      /**< Secondary bend weight in [0,1000]. */
        std::uint32_t primaryFrequencyMilliHertz{};     /**< Positive primary oscillation frequency when enabled. */
        std::uint32_t secondaryFrequencyMilliHertz{};   /**< Positive secondary oscillation frequency when enabled. */
        std::uint32_t gustProbabilityPerMillion{};      /**< Quantized probability in [0,1,000,000]. */
        std::uint16_t gustStrengthPermille{};           /**< Additional gust weight in [0,1000]. */
        std::uint16_t branchFlexibilityPermille{};      /**< Branch flexibility in [0,1000]. */
        std::uint16_t leafFlutterPermille{};            /**< Leaf flutter in [0,1000], only for flutter model. */

        [[nodiscard]] constexpr auto operator<=>(const FoliageWindDefinition &) const noexcept = default;
    };

    /** @brief Provider-neutral optional collision and navigation-blocking semantics. */
    struct FoliageCollisionDefinition final {
        FoliageCollisionShape shape{FoliageCollisionShape::None}; /**< Exact primitive or visual-only None. */
        std::uint32_t radiusMillimeters{};                        /**< Positive radius when collision is enabled. */
        std::uint32_t heightMillimeters{};                        /**< Positive total height when collision is enabled. */
        bool blocksProjectiles{};                                 /**< Authored projectile response requirement. */
        bool blocksNavigation{};                                  /**< Authored conservative navigation requirement. */

        [[nodiscard]] constexpr auto operator<=>(const FoliageCollisionDefinition &) const noexcept = default;
    };

    /** @brief Complete stable inert foliage type definition. */
    struct FoliageTypeDefinitionData final {
        std::uint16_t contractVersion{CurrentFoliageDefinitionContractVersion}; /**< Exact schema version. */
        FoliageTypeId type{};                                                   /**< Stable project-owned type identity. */
        FoliageDefinitionRevision revision{};                                   /**< Immutable semantic revision. */
        FoliageVisualAssets assets{};                                           /**< Stable visual asset references. */
        FoliagePlacementDefinition placement{};                                 /**< Deterministic placement rules. */
        FoliageCullingDefinition culling{};                                     /**< Exact visibility recipe. */
        FoliageWindDefinition wind{};                                           /**< Optional deformation semantics. */
        FoliageCollisionDefinition collision{};                                 /**< Optional collision semantics. */
        std::uint32_t minimumScalePermille{1'000};                              /**< Inclusive authored scale floor. */
        std::uint32_t maximumScalePermille{1'000};                              /**< Inclusive authored scale ceiling. */
        std::uint32_t maximumInstances{};                                       /**< Exact per-type finite ceiling. */

        [[nodiscard]] constexpr auto operator<=>(const FoliageTypeDefinitionData &) const noexcept = default;
    };

    /** @brief Validated immutable foliage definition suitable for cook/runtime transfer. */
    class FoliageTypeDefinition final {
    public:
        /**
         * @brief Validates a definition against exact project limits and available capabilities.
         * @param data Complete candidate definition.
         * @param configuration Immutable exact-tier project configuration.
         * @param capabilities Explicit provider-neutral capability set.
         * @return Immutable definition or a typed definition, feature, or limit failure.
         */
        [[nodiscard]] static Result<FoliageTypeDefinition> Create(const FoliageTypeDefinitionData &data,
                                                                  const TerrainConfigurationSnapshot &configuration,
                                                                  FoliageDefinitionCapabilitySet capabilities);

        /** @brief Returns stable definition bytes-as-values. @return Borrowed immutable definition data. */
        [[nodiscard]] const FoliageTypeDefinitionData &Data() const noexcept;

        [[nodiscard]] constexpr auto operator<=>(const FoliageTypeDefinition &) const noexcept = default;

    private:
        explicit FoliageTypeDefinition(const FoliageTypeDefinitionData &data) noexcept;
        FoliageTypeDefinitionData data_;
    };

    /** @brief Mutation intent for bounded foliage definition admission. */
    enum class FoliageDefinitionAdmissionKind : std::uint8_t {
        Insert,
        Replace,
        Count,
    };

    /** @brief Insert or exact compare-and-swap replacement request. */
    struct FoliageDefinitionAdmissionRequest final {
        FoliageDefinitionAdmissionKind kind{FoliageDefinitionAdmissionKind::Insert}; /**< Requested mutation. */
        std::optional<FoliageDefinitionRevision> expectedCurrentRevision{};          /**< Required only for replacement. */
    };

    /** @brief Immutable owner snapshot used by pure definition admission. */
    struct FoliageDefinitionAdmissionContext final {
        TerrainRuntimeLifecycle lifecycle{TerrainRuntimeLifecycle::Closed}; /**< Exact admission lifecycle. */
        TerrainConfigurationRevision configuration{};                       /**< Current project configuration. */
        TerrainCapabilityRevision capability{};                             /**< Current capability-plan revision. */
        FoliageDefinitionCapabilitySet capabilities{};                      /**< Current explicit feature set. */
        std::optional<FoliageTypeId> currentType{};                         /**< Current stable definition identity. */
        std::optional<FoliageDefinitionRevision> currentRevision{};         /**< Current immutable definition revision. */
    };

    /**
     * @brief Validates exact lifecycle/configuration/capability and insert/replacement fencing without publication.
     * @param candidate Validated immutable candidate.
     * @param configuration Configuration used to validate the candidate.
     * @param request Explicit insert or replacement intent.
     * @param context Current owner snapshot; no input is retained or mutated.
     * @return Success or a typed stale, lifecycle, unsupported-feature, or replacement failure.
     */
    [[nodiscard]] Result<void> ValidateFoliageDefinitionAdmission(const FoliageTypeDefinition &candidate,
                                                                  const TerrainConfigurationSnapshot &configuration,
                                                                  const FoliageDefinitionAdmissionRequest &request,
                                                                  const FoliageDefinitionAdmissionContext &context);
}  // namespace Horo::Terrain
