#pragma once

/** @file PhysicsCookedShapeDescriptor.h
 * @brief Exact immutable cooked-shape references, separate from analytic geometry and native resources.
 */

#include "Horo/Assets/AssetId.h"
#include "Horo/Foundation/Sha256.h"

#include <optional>

namespace Horo::Physics {
    /**
     * @brief Stable asset-local subresource identity issued by authoring, not an array index or native subshape ID.
     *
     * Non-zero values are persisted with the asset and retained across reorder/recook. Construction
     * does not generate or reserve an identity. The asset schema owns uniqueness and migration.
     */
    class PhysicsShapeSubresourceId final {
    public:
        /** @brief Constructs an invalid, unspecified identity. */
        PhysicsShapeSubresourceId() = default;

        /** @brief Wraps an already assigned persistent value. @param value Stable non-zero asset-local ID.
         * @return The supplied representation; zero remains invalid and is rejected by descriptor validation. */
        [[nodiscard]] static constexpr PhysicsShapeSubresourceId FromValue(const std::uint64_t value) noexcept {
            return PhysicsShapeSubresourceId{value};
        }

        /** @brief Returns the persistent asset-local value. @return Zero only for an unspecified identity. */
        [[nodiscard]] constexpr std::uint64_t Value() const noexcept {
            return value_;
        }

        /** @brief Checks representation only. @return Whether an identity was specified. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return value_ != 0;
        }

        auto operator<=>(const PhysicsShapeSubresourceId &) const noexcept = default;

    private:
        explicit constexpr PhysicsShapeSubresourceId(const std::uint64_t value) : value_(value) {}

        std::uint64_t value_{};
    };

    /** @brief Cooked geometry vocabulary; analytic primitives use PhysicsShapeDescriptor instead. */
    enum class PhysicsCookedShapeKind : std::uint8_t {
        ConvexHull,
        TriangleMesh,
        HeightField,
        Compound
    };

    /**
     * @brief Digest of the complete canonical Physics cook target, not a renderer or generic platform label.
     * The future target encoder must bind OS/architecture/endianness, solver pin and all build/CPU
     * definitions, Physics/shape/cooker schemas, tolerance and capability profiles per ADR-085.
     * Wrapping bytes neither computes that canonical preimage nor proves platform qualification.
     */
    struct PhysicsShapeCookTargetDigest final {
        Sha256Digest digest;
        auto operator<=>(const PhysicsShapeCookTargetDigest &) const noexcept = default;
    };

    /**
     * @brief Owned exact lookup request; never a shape lease, import request or native-ready receipt.
     *
     * Asset identity and persistent subresource select semantic content. The complete Assets
     * CacheKeyV1 digest identifies the selected cook; Physics cooking must include every semantic
     * dependency and baked variant in its canonical key inputs. The payload digest binds exact
     * cooked bytes. Presence is explicit because an all-zero SHA-256 value is still a
     * representable digest, not a missing-value sentinel. No mutable path or latest-version lookup
     * is permitted. The asset owner retains the immutable snapshot during resolution.
     *
     * Requested kind is not evidence of artifact kind, mass capability or convex compound leaves.
     * Runtime must verify the full Physics envelope, target preimage, digests, offsets, counts,
     * semantic mappings and limits before constructing and publishing any native resource.
     */
    struct PhysicsCookedShapeDescriptor final {
        Assets::AssetId asset;
        PhysicsShapeSubresourceId subresource;
        PhysicsCookedShapeKind kind{PhysicsCookedShapeKind::ConvexHull};
        std::optional<Sha256Digest> cacheKeyDigest;
        std::optional<Sha256Digest> payloadDigest;
        std::optional<PhysicsShapeCookTargetDigest> target;
    };

    /**
     * @brief Checks exact-reference completeness and target equality without loading, hashing or cooking bytes.
     * @param descriptor Immutable request supplied by the owning asset/scene preparation boundary.
     * @param expectedTarget Owner-captured complete Physics target digest, not inferred from the descriptor.
     * @return Success, DescriptorInvalid for missing identity/evidence, OperationUnsupported for unknown kind,
     * or ProfileUnsupported for a target mismatch. No fallback or automatic recook occurs.
     * @pre Control/preparation use; failure diagnostics may allocate.
     * @post Success establishes reference shape only, not artifact existence, integrity or readiness.
     */
    [[nodiscard]] Result<void> ValidatePhysicsCookedShapeDescriptor(const PhysicsCookedShapeDescriptor &descriptor,
                                                                    const PhysicsShapeCookTargetDigest &expectedTarget);
}  // namespace Horo::Physics
