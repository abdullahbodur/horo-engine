#pragma once

/**
 * @file TerrainIdentity.h
 * @brief Stable authored terrain identities and generation-safe runtime instance handles.
 */

#include "Horo/Foundation/Handles.h"
#include "Horo/Foundation/Result.h"
#include "Horo/Foundation/StrongId.h"
#include "Horo/Terrain/TerrainErrors.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace Horo::Terrain {
    /** @brief Canonical fixed-width representation shared by stable Terrain identity domains. */
    using SerializedTerrainIdentity = std::array<std::uint8_t, 16>;

    /** @brief Strong persistent 128-bit identity whose tag prevents cross-domain substitution. */
    template <typename Tag> class TerrainStableIdentity final {
    public:
        /** @brief Constructs the reserved all-zero identity. */
        constexpr TerrainStableIdentity() = default;

        [[nodiscard]] constexpr auto operator<=>(const TerrainStableIdentity &) const noexcept = default;

        /**
         * @brief Validates exact persistent identity bytes.
         * @param bytes Canonical 128-bit value; all zeroes are reserved.
         * @return Typed identity or TerrainErrors::IdentityInvalid.
         */
        [[nodiscard]] static Result<TerrainStableIdentity> Create(const SerializedTerrainIdentity &bytes) {
            TerrainStableIdentity candidate{bytes};
            if (candidate.IsValid())
                return Result<TerrainStableIdentity>::Success(candidate);
            return Result<TerrainStableIdentity>::Failure(MakeError(TerrainErrors::IdentityInvalid));
        }

        /** @brief Checks representation without resolving an owner. @return True unless every byte is zero. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            for (const std::uint8_t byte : bytes_) {
                if (byte != 0)
                    return true;
            }
            return false;
        }

        /** @brief Returns exact canonical bytes. @return Borrowed bytes owned by this value. */
        [[nodiscard]] constexpr const SerializedTerrainIdentity &Bytes() const noexcept {
            return bytes_;
        }

    private:
        explicit constexpr TerrainStableIdentity(const SerializedTerrainIdentity &bytes) noexcept : bytes_(bytes) {}

        SerializedTerrainIdentity bytes_{};
    };

    struct TerrainProjectIdentityTag;
    struct TerrainDatasetIdentityTag;
    struct FoliageTypeIdentityTag;
    struct FoliageClusterIdentityTag;
    struct FoliageInstanceIdentityTag;

    /** @brief Stable project scope supplied from canonical project metadata, never a display name or path. */
    using TerrainProjectId = TerrainStableIdentity<TerrainProjectIdentityTag>;
    /** @brief Deterministic stable identity of one authored/cooked terrain dataset. */
    using TerrainDatasetId = TerrainStableIdentity<TerrainDatasetIdentityTag>;
    /** @brief Deterministic stable identity of one project-owned foliage definition. */
    using FoliageTypeId = TerrainStableIdentity<FoliageTypeIdentityTag>;
    /** @brief Deterministic stable identity of one cooked foliage cluster. */
    using FoliageClusterId = TerrainStableIdentity<FoliageClusterIdentityTag>;
    /** @brief Deterministic stable identity of one baked foliage placement in a compatible base. */
    using FoliageInstanceId = TerrainStableIdentity<FoliageInstanceIdentityTag>;

    /** @brief Canonical signed terrain-tile address in world tile space. */
    struct TerrainTileCoordinate final {
        std::int32_t x{};   /**< Floor-quantized world tile X; negative coordinates remain distinct. */
        std::int32_t z{};   /**< Floor-quantized world tile Z; negative coordinates remain distinct. */
        std::uint8_t lod{}; /**< Cooked LOD representation at this address. */

        [[nodiscard]] constexpr auto operator<=>(const TerrainTileCoordinate &) const noexcept = default;
    };

    /** @brief Stable tile identity binding an exact dataset to an explicit world-space tile address. */
    struct TerrainTileId final {
        TerrainDatasetId dataset{};   /**< Dataset that owns manifest membership. */
        TerrainTileCoordinate tile{}; /**< Exact signed tile and LOD address. */

        /** @brief Checks representation; manifest membership is validated separately. @return Whether the dataset is valid. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return dataset.IsValid();
        }

        [[nodiscard]] constexpr auto operator<=>(const TerrainTileId &) const noexcept = default;
    };

    /** @brief Canonical dataset-plus-coordinate encoding of a terrain tile identity. */
    using SerializedTerrainTileId = std::array<std::uint8_t, 25>;

    /** @brief Maximum canonical authored key accepted by one deterministic derivation operation. */
    inline constexpr std::size_t MaximumTerrainIdentityKeyBytes = 256;
    /** @brief Maximum total identities inspected by one catalog validation operation. */
    inline constexpr std::size_t MaximumTerrainIdentityCatalogEntries = 4'096;

    /**
     * @brief Derives a stable dataset identity from a project and canonical authored key.
     * @param project Stable project metadata identity.
     * @param canonicalKey Non-empty canonical semantic key, never a display name or path.
     * @return SHA-256-domain-separated 128-bit identity or TerrainErrors::DerivationInvalid.
     */
    [[nodiscard]] Result<TerrainDatasetId> DeriveTerrainDatasetId(TerrainProjectId project, std::span<const std::byte> canonicalKey);

    /**
     * @brief Derives a stable foliage definition identity from a project and canonical authored key.
     * @param project Stable project metadata identity.
     * @param canonicalKey Non-empty canonical definition key, never a display name or native handle.
     * @return SHA-256-domain-separated 128-bit identity or TerrainErrors::DerivationInvalid.
     */
    [[nodiscard]] Result<FoliageTypeId> DeriveFoliageTypeId(TerrainProjectId project, std::span<const std::byte> canonicalKey);

    /**
     * @brief Derives a stable cluster identity from its exact tile and canonical semantic key.
     * @param tile Stable dataset and signed tile address.
     * @param canonicalClusterKey Non-empty deterministic cluster provenance bytes, never an array position.
     * @return Stable cluster identity or TerrainErrors::DerivationInvalid.
     */
    [[nodiscard]] Result<FoliageClusterId> DeriveFoliageClusterId(const TerrainTileId &tile,
                                                                  std::span<const std::byte> canonicalClusterKey);

    /**
     * @brief Derives a stable baked instance identity from its tile, foliage type, and placement key.
     * @param tile Stable tile that owns the baked placement.
     * @param foliageType Stable project-owned foliage definition.
     * @param canonicalPlacementKey Non-empty deterministic placement provenance/signature bytes.
     * @return Stable baked instance identity or TerrainErrors::DerivationInvalid.
     */
    [[nodiscard]] Result<FoliageInstanceId> DeriveFoliageInstanceId(const TerrainTileId &tile, FoliageTypeId foliageType,
                                                                    std::span<const std::byte> canonicalPlacementKey);

    /** @brief Encodes one stable identity as exact canonical bytes. @param identity Identity to encode. @return Exact bytes. */
    template <typename Tag>
    [[nodiscard]] constexpr SerializedTerrainIdentity SerializeTerrainIdentity(const TerrainStableIdentity<Tag> identity) noexcept {
        return identity.Bytes();
    }

    /**
     * @brief Decodes canonical bytes into one requested stable identity domain.
     * @param bytes Exact persistent bytes.
     * @return Typed identity or TerrainErrors::SerializedIdentityInvalid.
     */
    template <typename Tag>
    [[nodiscard]] Result<TerrainStableIdentity<Tag>> DeserializeTerrainIdentity(const SerializedTerrainIdentity &bytes) {
        auto identity = TerrainStableIdentity<Tag>::Create(bytes);
        if (identity.HasValue())
            return identity;
        return Result<TerrainStableIdentity<Tag>>::Failure(MakeError(TerrainErrors::SerializedIdentityInvalid));
    }

    /** @brief Encodes dataset and tile coordinate in canonical network byte order. @param tile Tile identity. @return Exact bytes. */
    [[nodiscard]] SerializedTerrainTileId SerializeTerrainTileId(const TerrainTileId &tile) noexcept;
    /**
     * @brief Decodes and validates a canonical terrain tile identity.
     * @param bytes Exact dataset-plus-coordinate bytes.
     * @return Typed tile or TerrainErrors::SerializedIdentityInvalid.
     */
    [[nodiscard]] Result<TerrainTileId> DeserializeTerrainTileId(const SerializedTerrainTileId &bytes);

    namespace Detail {
        struct TerrainContentRevisionTag;
        struct TerrainResidencyRevisionTag;
        struct TerrainMutationRevisionTag;
        struct TerrainCapabilityRevisionTag;
    }  // namespace Detail

    /** @brief Immutable semantic dataset-content revision. */
    using TerrainContentRevision = Foundation::Detail::NonZeroId64<Detail::TerrainContentRevisionTag, TerrainErrors::IdentityInvalid>;
    /** @brief Published tile/cluster availability revision. */
    using TerrainResidencyRevision = Foundation::Detail::NonZeroId64<Detail::TerrainResidencyRevisionTag, TerrainErrors::IdentityInvalid>;
    /** @brief Committed runtime overlay revision. */
    using TerrainMutationRevision = Foundation::Detail::NonZeroId64<Detail::TerrainMutationRevisionTag, TerrainErrors::IdentityInvalid>;
    /** @brief Immutable effective capability-plan revision. */
    using TerrainCapabilityRevision = Foundation::Detail::NonZeroId64<Detail::TerrainCapabilityRevisionTag, TerrainErrors::IdentityInvalid>;

    /**
     * @brief Advances a valid non-wrapping Terrain revision.
     * @param current Current non-zero revision.
     * @return Next revision, TerrainErrors::IdentityInvalid, or TerrainErrors::GenerationExhausted.
     */
    template <typename Revision> [[nodiscard]] Result<Revision> AdvanceTerrainRevision(const Revision current) {
        if (!current.IsValid())
            return Result<Revision>::Failure(MakeError(TerrainErrors::IdentityInvalid));
        if (current.Value() == std::numeric_limits<std::uint64_t>::max())
            return Result<Revision>::Failure(MakeError(TerrainErrors::GenerationExhausted));
        return Revision::Create(current.Value() + 1U);
    }

    /** @brief Exact revisions required for one immutable consumer snapshot. */
    struct TerrainSnapshotRevision final {
        TerrainContentRevision content{};       /**< Immutable semantic content revision. */
        TerrainResidencyRevision residency{};   /**< Published tile/cluster membership revision. */
        TerrainMutationRevision mutation{};     /**< Runtime overlay revision. */
        TerrainCapabilityRevision capability{}; /**< Effective typed capability-plan revision. */

        /** @brief Checks that every independent revision is present. @return True when all revisions are non-zero. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return content.IsValid() && residency.IsValid() && mutation.IsValid() && capability.IsValid();
        }

        [[nodiscard]] constexpr auto operator<=>(const TerrainSnapshotRevision &) const noexcept = default;
    };

    struct TerrainRuntimeSlotTag;
    struct RuntimeFoliageInstanceSlotTag;

    /** @brief Process-local live dataset incarnation; never serialize this value. */
    struct TerrainRuntimeHandle final {
        TerrainDatasetId dataset{};                 /**< Stable dataset realized by this runtime. */
        Horo::Handle<TerrainRuntimeSlotTag> slot{}; /**< Runtime registry slot and non-zero generation. */

        /** @brief Checks representation, not current registry residency. @return Whether every dimension is usable. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return dataset.IsValid() && slot.IsValid() && slot.generation != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const TerrainRuntimeHandle &) const noexcept = default;
    };

    /** @brief Process-local foliage overlay identity owned by one exact terrain runtime incarnation. */
    struct RuntimeFoliageInstanceHandle final {
        TerrainRuntimeHandle terrain{};                     /**< Exact terrain incarnation that owns the instance. */
        Horo::Handle<RuntimeFoliageInstanceSlotTag> slot{}; /**< Instance slot and non-zero generation. */

        /** @brief Checks representation, not live registry membership. @return Whether owner and slot are usable. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return terrain.IsValid() && slot.IsValid() && slot.generation != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const RuntimeFoliageInstanceHandle &) const noexcept = default;
    };

    /** @brief Runtime admission state relevant to identity access. */
    enum class TerrainRuntimeLifecycle : std::uint8_t {
        Active,    /**< Owner admits bounded identity access. */
        Cancelled, /**< Candidate work is fenced and cannot publish. */
        Closing,   /**< Retirement is in progress and handles remain non-admissible. */
        Closed,    /**< Terminal owner state after retirement. */
    };

    /**
     * @brief Validates an instance against the exact active runtime and slot generation.
     * @param submitted Consumer-supplied instance handle.
     * @param current Exact owner record currently stored in the registry.
     * @param lifecycle Current runtime admission state.
     * @return Success or a typed invalid, unknown, stale-generation, or lifecycle failure.
     */
    [[nodiscard]] Result<void> ValidateRuntimeFoliageInstance(const RuntimeFoliageInstanceHandle &submitted,
                                                              const RuntimeFoliageInstanceHandle &current,
                                                              TerrainRuntimeLifecycle lifecycle);

    /**
     * @brief Advances one runtime foliage slot generation without wrap or owner changes.
     * @param current Current valid handle.
     * @return Replacement handle, IdentityInvalid, or GenerationExhausted.
     */
    [[nodiscard]] Result<RuntimeFoliageInstanceHandle> AdvanceRuntimeFoliageInstanceGeneration(const RuntimeFoliageInstanceHandle &current);

    /** @brief Borrowed stable identity domains validated together before activation or cook publication. */
    struct TerrainIdentityCatalog final {
        TerrainProjectId project{};                          /**< Exact stable project scope. */
        std::span<const TerrainDatasetId> datasets{};        /**< Dataset identity domain. */
        std::span<const TerrainTileId> tiles{};              /**< Dataset-scoped tile identity domain. */
        std::span<const FoliageTypeId> foliageTypes{};       /**< Foliage definition identity domain. */
        std::span<const FoliageClusterId> clusters{};        /**< Cooked cluster identity domain. */
        std::span<const FoliageInstanceId> bakedInstances{}; /**< Baked placement identity domain. */
    };

    /**
     * @brief Rejects invalid, duplicate, foreign-dataset, or over-capacity catalog identities transactionally.
     * @param catalog Borrowed identity domains; no input is retained or mutated.
     * @return Success or typed invalid, conflict, or capacity failure.
     */
    [[nodiscard]] Result<void> ValidateTerrainIdentityCatalog(const TerrainIdentityCatalog &catalog);
}  // namespace Horo::Terrain
