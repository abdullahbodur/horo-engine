#include "Horo/Terrain/TerrainIdentity.h"

#include "Horo/Foundation/Sha256.h"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <string_view>
#include <type_traits>

namespace Horo::Terrain {
    namespace {
        constexpr std::size_t MaximumDerivationPreimageBytes = 384;

        /** @brief Appends bytes to a bounded identity-derivation preimage. */
        [[nodiscard]] bool Append(std::array<std::byte, MaximumDerivationPreimageBytes> &output, std::size_t &size,
                                  const std::span<const std::byte> bytes) noexcept {
            if (bytes.size() > output.size() - size)
                return false;
            std::ranges::copy(bytes, output.begin() + static_cast<std::ptrdiff_t>(size));
            size += bytes.size();
            return true;
        }

        /** @brief Creates the first 128 bits of a domain-separated SHA-256 identity. */
        template <typename Identity, std::size_t PieceCount>
        [[nodiscard]] Result<Identity> Derive(const std::string_view domain,
                                              const std::array<std::span<const std::byte>, PieceCount> &pieces) {
            std::array<std::byte, MaximumDerivationPreimageBytes> preimage{};
            std::size_t size{};
            if (const auto domainBytes = std::as_bytes(std::span{domain.data(), domain.size()}); !Append(preimage, size, domainBytes))
                return Result<Identity>::Failure(MakeError(TerrainErrors::DerivationInvalid));
            if (constexpr std::byte separator{}; !Append(preimage, size, std::span{&separator, 1}))
                return Result<Identity>::Failure(MakeError(TerrainErrors::DerivationInvalid));
            for (const auto piece : pieces) {
                if (!Append(preimage, size, piece))
                    return Result<Identity>::Failure(MakeError(TerrainErrors::DerivationInvalid));
            }

            const Sha256Digest digest = ComputeSha256(std::span{preimage.data(), size});
            SerializedTerrainIdentity identityBytes{};
            std::ranges::copy_n(digest.bytes.begin(), identityBytes.size(), identityBytes.begin());
            auto identity = Identity::Create(identityBytes);
            if (identity.HasError())
                return Result<Identity>::Failure(MakeError(TerrainErrors::DerivationInvalid));
            return identity;
        }

        template <typename Identity> [[nodiscard]] bool ContainsInvalid(const std::span<const Identity> identities) noexcept {
            return std::ranges::any_of(identities, [](const Identity &identity) {
                return !identity.IsValid();
            });
        }

        template <typename Identity> [[nodiscard]] bool ContainsDuplicate(const std::span<const Identity> identities) noexcept {
            for (std::size_t index = 0; index < identities.size(); ++index) {
                for (std::size_t candidate = index + 1; candidate < identities.size(); ++candidate) {
                    if (identities[index] == identities[candidate])
                        return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool CatalogSizeFits(const TerrainIdentityCatalog &catalog) noexcept {
            std::size_t total{};
            for (const std::size_t count : {catalog.datasets.size(), catalog.tiles.size(), catalog.foliageTypes.size(),
                                            catalog.clusters.size(), catalog.bakedInstances.size()}) {
                if (count > MaximumTerrainIdentityCatalogEntries - total)
                    return false;
                total += count;
            }
            return true;
        }

        [[nodiscard]] bool ContainsDataset(const std::span<const TerrainDatasetId> datasets, const TerrainDatasetId dataset) noexcept {
            return std::ranges::find(datasets, dataset) != datasets.end();
        }

        /** @brief Checks every typed catalog domain for reserved identity representations. */
        [[nodiscard]] bool CatalogContainsInvalidIdentity(const TerrainIdentityCatalog &catalog) noexcept {
            return ContainsInvalid(catalog.datasets) || ContainsInvalid(catalog.tiles) || ContainsInvalid(catalog.foliageTypes) ||
                   ContainsInvalid(catalog.clusters) || ContainsInvalid(catalog.bakedInstances);
        }

        /** @brief Checks every typed catalog domain for duplicate identities. */
        [[nodiscard]] bool CatalogContainsDuplicateIdentity(const TerrainIdentityCatalog &catalog) noexcept {
            return ContainsDuplicate(catalog.datasets) || ContainsDuplicate(catalog.tiles) || ContainsDuplicate(catalog.foliageTypes) ||
                   ContainsDuplicate(catalog.clusters) || ContainsDuplicate(catalog.bakedInstances);
        }

        /** @brief Checks that each tile belongs to a dataset declared by the same catalog. */
        [[nodiscard]] bool CatalogContainsForeignTile(const TerrainIdentityCatalog &catalog) noexcept {
            return std::ranges::any_of(catalog.tiles, [&catalog](const TerrainTileId &tile) {
                return !ContainsDataset(catalog.datasets, tile.dataset);
            });
        }

        template <typename Integer>
        void StoreNetworkOrder(const Integer value, const std::span<std::uint8_t, sizeof(Integer)> output) noexcept {
            using Unsigned = std::make_unsigned_t<Integer>;
            const auto bits = static_cast<Unsigned>(value);
            for (std::size_t index = 0; index < output.size(); ++index) {
                const auto shift = static_cast<unsigned>((output.size() - index - 1U) * 8U);
                output[index] = static_cast<std::uint8_t>(bits >> shift);
            }
        }

        template <typename Integer>
        [[nodiscard]] Integer LoadNetworkOrder(const std::span<const std::uint8_t, sizeof(Integer)> input) noexcept {
            using Unsigned = std::make_unsigned_t<Integer>;
            Unsigned bits{};
            for (const std::uint8_t byte : input)
                bits = static_cast<Unsigned>((bits << 8U) | byte);
            return std::bit_cast<Integer>(bits);
        }
    }  // namespace

    /** @copydoc DeriveTerrainDatasetId */
    Result<TerrainDatasetId> DeriveTerrainDatasetId(const TerrainProjectId project, const std::span<const std::byte> canonicalKey) {
        if (!project.IsValid() || canonicalKey.empty() || canonicalKey.size() > MaximumTerrainIdentityKeyBytes)
            return Result<TerrainDatasetId>::Failure(MakeError(TerrainErrors::DerivationInvalid));
        return Derive<TerrainDatasetId>("horo.terrain.dataset.v1",
                                        std::array<std::span<const std::byte>, 2>{std::as_bytes(std::span{project.Bytes()}), canonicalKey});
    }

    /** @copydoc DeriveFoliageTypeId */
    Result<FoliageTypeId> DeriveFoliageTypeId(const TerrainProjectId project, const std::span<const std::byte> canonicalKey) {
        if (!project.IsValid() || canonicalKey.empty() || canonicalKey.size() > MaximumTerrainIdentityKeyBytes)
            return Result<FoliageTypeId>::Failure(MakeError(TerrainErrors::DerivationInvalid));
        return Derive<FoliageTypeId>("horo.terrain.foliage_type.v1",
                                     std::array<std::span<const std::byte>, 2>{std::as_bytes(std::span{project.Bytes()}), canonicalKey});
    }

    /** @copydoc DeriveFoliageClusterId */
    Result<FoliageClusterId> DeriveFoliageClusterId(const TerrainTileId &tile, const std::span<const std::byte> canonicalClusterKey) {
        if (!tile.IsValid() || canonicalClusterKey.empty() || canonicalClusterKey.size() > MaximumTerrainIdentityKeyBytes)
            return Result<FoliageClusterId>::Failure(MakeError(TerrainErrors::DerivationInvalid));
        const SerializedTerrainTileId tileBytes = SerializeTerrainTileId(tile);
        return Derive<FoliageClusterId>("horo.terrain.foliage_cluster.v1",
                                        std::array<std::span<const std::byte>, 2>{std::as_bytes(std::span{tileBytes}),
                                                                                  canonicalClusterKey});
    }

    /** @copydoc DeriveFoliageInstanceId */
    Result<FoliageInstanceId> DeriveFoliageInstanceId(const TerrainTileId &tile, const FoliageTypeId foliageType,
                                                      const std::span<const std::byte> canonicalPlacementKey) {
        if (!tile.IsValid() || !foliageType.IsValid() || canonicalPlacementKey.empty() ||
            canonicalPlacementKey.size() > MaximumTerrainIdentityKeyBytes)
            return Result<FoliageInstanceId>::Failure(MakeError(TerrainErrors::DerivationInvalid));
        const SerializedTerrainTileId tileBytes = SerializeTerrainTileId(tile);
        return Derive<FoliageInstanceId>("horo.terrain.foliage_instance.v1",
                                         std::array<std::span<const std::byte>, 3>{std::as_bytes(std::span{tileBytes}),
                                                                                   std::as_bytes(std::span{foliageType.Bytes()}),
                                                                                   canonicalPlacementKey});
    }

    /** @copydoc SerializeTerrainTileId */
    SerializedTerrainTileId SerializeTerrainTileId(const TerrainTileId &tile) noexcept {
        SerializedTerrainTileId bytes{};
        std::ranges::copy(tile.dataset.Bytes(), bytes.begin());
        StoreNetworkOrder(tile.tile.x, std::span<std::uint8_t, sizeof(tile.tile.x)>{bytes.data() + 16, sizeof(tile.tile.x)});
        StoreNetworkOrder(tile.tile.z, std::span<std::uint8_t, sizeof(tile.tile.z)>{bytes.data() + 20, sizeof(tile.tile.z)});
        bytes[24] = tile.tile.lod;
        return bytes;
    }

    /** @copydoc DeserializeTerrainTileId */
    Result<TerrainTileId> DeserializeTerrainTileId(const SerializedTerrainTileId &bytes) {
        SerializedTerrainIdentity datasetBytes{};
        std::ranges::copy_n(bytes.begin(), datasetBytes.size(), datasetBytes.begin());
        auto dataset = TerrainDatasetId::Create(datasetBytes);
        if (dataset.HasError())
            return Result<TerrainTileId>::Failure(MakeError(TerrainErrors::SerializedIdentityInvalid));
        return Result<TerrainTileId>::Success({
            .dataset = dataset.Value(),
            .tile = {.x = LoadNetworkOrder<std::int32_t>(std::span<const std::uint8_t, 4>{bytes.data() + 16, 4}),
                     .z = LoadNetworkOrder<std::int32_t>(std::span<const std::uint8_t, 4>{bytes.data() + 20, 4}),
                     .lod = bytes[24]},
        });
    }

    /** @copydoc ValidateRuntimeFoliageInstance */
    Result<void> ValidateRuntimeFoliageInstance(const RuntimeFoliageInstanceHandle &submitted, const RuntimeFoliageInstanceHandle &current,
                                                const TerrainRuntimeLifecycle lifecycle) {
        if (!submitted.IsValid() || !current.IsValid())
            return Result<void>::Failure(MakeError(TerrainErrors::IdentityInvalid));
        if (lifecycle != TerrainRuntimeLifecycle::Active)
            return Result<void>::Failure(MakeError(TerrainErrors::LifecycleUnavailable));
        if (submitted.terrain.dataset != current.terrain.dataset || submitted.terrain.slot.index != current.terrain.slot.index ||
            submitted.slot.index != current.slot.index)
            return Result<void>::Failure(MakeError(TerrainErrors::IdentityUnknown));
        if (submitted.terrain.slot.generation != current.terrain.slot.generation || submitted.slot.generation != current.slot.generation)
            return Result<void>::Failure(MakeError(TerrainErrors::GenerationStale));
        return Result<void>::Success();
    }

    /** @copydoc AdvanceRuntimeFoliageInstanceGeneration */
    Result<RuntimeFoliageInstanceHandle> AdvanceRuntimeFoliageInstanceGeneration(const RuntimeFoliageInstanceHandle &current) {
        if (!current.IsValid())
            return Result<RuntimeFoliageInstanceHandle>::Failure(MakeError(TerrainErrors::IdentityInvalid));
        if (current.slot.generation == std::numeric_limits<std::uint32_t>::max())
            return Result<RuntimeFoliageInstanceHandle>::Failure(MakeError(TerrainErrors::GenerationExhausted));
        auto replacement = current;
        ++replacement.slot.generation;
        return Result<RuntimeFoliageInstanceHandle>::Success(replacement);
    }

    /** @copydoc ValidateTerrainIdentityCatalog */
    Result<void> ValidateTerrainIdentityCatalog(const TerrainIdentityCatalog &catalog) {
        if (!catalog.project.IsValid())
            return Result<void>::Failure(MakeError(TerrainErrors::IdentityInvalid));
        if (!CatalogSizeFits(catalog))
            return Result<void>::Failure(MakeError(TerrainErrors::CapacityExceeded));
        if (CatalogContainsInvalidIdentity(catalog))
            return Result<void>::Failure(MakeError(TerrainErrors::IdentityInvalid));
        if (CatalogContainsDuplicateIdentity(catalog))
            return Result<void>::Failure(MakeError(TerrainErrors::IdentityConflict));
        if (CatalogContainsForeignTile(catalog))
            return Result<void>::Failure(MakeError(TerrainErrors::IdentityUnknown));
        return Result<void>::Success();
    }
}  // namespace Horo::Terrain
