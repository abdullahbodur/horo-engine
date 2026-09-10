#pragma once

/**
 * @file SkeletalMeshSkinning.h
 * @brief Immutable typed skeletal-mesh skinning and skeleton-binding contract.
 */

#include "Horo/Animation/SkeletonAsset.h"
#include "Horo/Math/SceneMath.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace Horo::Animation {
    /** @brief Exact portable skeletal-mesh skinning contract version. */
    struct SkeletalMeshSkinningContractVersion final {
        std::uint16_t major{};
        std::uint16_t minor{};
        std::uint16_t patch{};

        [[nodiscard]] constexpr auto operator<=>(const SkeletalMeshSkinningContractVersion &) const noexcept = default;
    };

    /** @brief Skeletal-mesh skinning version implemented by this API slice. */
    inline constexpr SkeletalMeshSkinningContractVersion CurrentSkeletalMeshSkinningContractVersion{1, 0, 0};

    /** @brief Compile-time safety ceilings for one portable skeletal-mesh skinning asset. */
    struct SkeletalMeshSkinningHardLimits final {
        static constexpr std::uint32_t Lods = 16;                  /**< Absolute LOD ceiling. */
        static constexpr std::uint32_t VerticesPerLod = 4'194'304; /**< Absolute vertex ceiling per LOD. */
        static constexpr std::uint32_t SectionsPerLod = 1'024;     /**< Absolute section ceiling per LOD. */
        static constexpr std::uint32_t BindingJoints = 1'024;      /**< Absolute mesh-to-skeleton remap ceiling. */
        static constexpr std::uint32_t PaletteJoints = 256;        /**< Absolute section-palette ceiling. */
        static constexpr std::uint32_t InfluencesPerVertex = 8;    /**< Absolute per-vertex influence ceiling. */
    };

    /** @brief Finite policy captured before a skinning validation transaction begins. */
    struct SkeletalMeshSkinningLimits final {
        std::uint32_t maximumLods{SkeletalMeshSkinningHardLimits::Lods};
        std::uint32_t maximumVerticesPerLod{SkeletalMeshSkinningHardLimits::VerticesPerLod};
        std::uint32_t maximumSectionsPerLod{SkeletalMeshSkinningHardLimits::SectionsPerLod};
        std::uint32_t maximumBindingJoints{SkeletalMeshSkinningHardLimits::BindingJoints};
        std::uint32_t maximumPaletteJoints{SkeletalMeshSkinningHardLimits::PaletteJoints};
        std::uint32_t maximumInfluencesPerVertex{SkeletalMeshSkinningHardLimits::InfluencesPerVertex};

        [[nodiscard]] constexpr auto operator<=>(const SkeletalMeshSkinningLimits &) const noexcept = default;
    };

    /** @brief One mesh-local joint mapped to one exact stable joint in the bound skeleton. */
    struct SkeletonJointRemap final {
        SkinningJointId meshJoint{}; /**< Stable source identity used by vertices and palettes. */
        JointId skeletonJoint{};     /**< Stable target identity in the immutable skeleton. */

        [[nodiscard]] constexpr auto operator<=>(const SkeletonJointRemap &) const noexcept = default;
    };

    /** @brief Exact immutable skeleton publication required by one skeletal-mesh asset. */
    struct SkeletonBinding final {
        SkeletonId skeleton{};                                  /**< Persistent target skeleton identity. */
        SkeletonAssetContractVersion skeletonContractVersion{}; /**< Exact portable skeleton schema. */
        SkeletonAssetGeneration skeletonGeneration{};           /**< Exact immutable publication generation. */
        std::vector<SkeletonJointRemap> jointRemap{};           /**< Mesh-local to stable skeleton joint mapping. */

        [[nodiscard]] bool operator==(const SkeletonBinding &) const noexcept = default;
    };

    /** @brief One positive mesh-local joint contribution to a vertex. */
    struct SkinningInfluence final {
        SkinningJointId joint{}; /**< Mesh-local joint resolved through SkeletonBinding. */
        float weight{};          /**< Finite positive weight; normalized during validation. */

        [[nodiscard]] bool operator==(const SkinningInfluence &) const noexcept = default;
    };

    /** @brief Bounded influence set for one vertex; canonicalized by weight then stable joint identity. */
    struct SkinnedVertex final {
        std::vector<SkinningInfluence> influences{};

        [[nodiscard]] bool operator==(const SkinnedVertex &) const noexcept = default;
    };

    /** @brief Contiguous vertex range and exact bounded joint palette for one draw section. */
    struct SkeletalMeshSection final {
        SkeletalMeshSectionId id{};             /**< Stable section identity. */
        std::uint32_t firstVertex{};            /**< First vertex in this LOD. */
        std::uint32_t vertexCount{};            /**< Non-zero contiguous vertex count. */
        std::vector<SkinningJointId> palette{}; /**< Canonical mesh-local joints available to the section. */

        [[nodiscard]] bool operator==(const SkeletalMeshSection &) const noexcept = default;
    };

    /** @brief One bounded skinning LOD with conservative local-space bounds. */
    struct SkeletalMeshSkinningLod final {
        std::uint16_t level{};                       /**< Contiguous zero-based LOD level. */
        Math::Aabb localBounds{};                    /**< Finite conservative bounds for every admitted pose. */
        std::vector<SkinnedVertex> vertices{};       /**< Vertex-order influence data. */
        std::vector<SkeletalMeshSection> sections{}; /**< Complete non-overlapping vertex partition. */

        [[nodiscard]] bool operator==(const SkeletalMeshSkinningLod &other) const noexcept {
            return level == other.level && localBounds.minimum == other.localBounds.minimum &&
                   localBounds.maximum == other.localBounds.maximum && vertices == other.vertices && sections == other.sections;
        }
    };

    /** @brief Mutable candidate copied into an immutable skinning snapshot after complete validation. */
    struct SkeletalMeshSkinningData final {
        SkeletalMeshSkinningContractVersion contractVersion{CurrentSkeletalMeshSkinningContractVersion};
        SkeletalMeshId mesh{};                       /**< Persistent Assets-owned skeletal-mesh identity. */
        SkeletonBinding binding{};                   /**< Exact generation-safe skeleton binding. */
        std::vector<SkeletalMeshSkinningLod> lods{}; /**< Candidate LOD data; canonicalized during creation. */

        [[nodiscard]] bool operator==(const SkeletalMeshSkinningData &) const noexcept = default;
    };

    /** @brief Owner-state snapshot used to fail closed before skinning validation allocates work storage. */
    enum class SkeletalMeshSkinningAdmissionState : std::uint8_t {
        Accepting,
        CancellationRequested,
        ShuttingDown,
        Count
    };

    /** @brief Immutable operation inputs captured by the asset owner before validation. */
    struct SkeletalMeshSkinningBuildContext final {
        SkeletalMeshSkinningAdmissionState admission{SkeletalMeshSkinningAdmissionState::Accepting}; /**< Owner state. */
        std::optional<SkeletalMeshId> replacing{};           /**< Stable current mesh identity for an atomic reload. */
        SkeletonAssetGeneration currentSkeletonGeneration{}; /**< Current target skeleton publication. */
        SkeletalMeshSkinningLimits limits{};                 /**< Finite limits fixed for the complete operation. */
    };

    /** @brief Immutable validated skinning data with deterministic LOD, section, palette, and influence order. */
    class SkeletalMeshSkinningAsset final {
    public:
        /**
         * @brief Validates and takes ownership of complete skeletal-mesh skinning data transactionally.
         * @param candidate Candidate mesh identity, skeleton binding, remap, LODs, sections, palettes, and influences.
         * @param skeleton Immutable skeleton snapshot against which target joints and compatibility are checked.
         * @param context Captured admission, reload, skeleton-generation, and finite-limit policy.
         * @return Immutable skinning data or a stable version, lifecycle, binding, layout, influence, identity, or limit failure.
         * @pre Load, cook, or owner control boundary; never invoke from frame-hot skinning or render extraction.
         * @post Success owns canonical portable data; failure publishes no partial binding or replacement.
         */
        [[nodiscard]] static Result<SkeletalMeshSkinningAsset> Create(SkeletalMeshSkinningData candidate, const SkeletonAsset &skeleton,
                                                                      const SkeletalMeshSkinningBuildContext &context);

        /** @brief Returns the immutable canonical source data. @return Borrowed data owned by this snapshot. */
        [[nodiscard]] const SkeletalMeshSkinningData &Data() const noexcept;

        /**
         * @brief Returns the canonical palette of a section in one LOD.
         * @param lodLevel Contiguous canonical LOD level.
         * @param section Stable section identity within that LOD.
         * @return Borrowed palette, or an empty view when either identity is absent.
         */
        [[nodiscard]] std::span<const SkinningJointId> Palette(std::uint16_t lodLevel, SkeletalMeshSectionId section) const noexcept;

    private:
        explicit SkeletalMeshSkinningAsset(SkeletalMeshSkinningData data) noexcept : data_(std::move(data)) {}

        SkeletalMeshSkinningData data_;
    };
}  // namespace Horo::Animation
