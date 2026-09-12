#include "Horo/Navigation/NavigationBakeInput.h"

#include "Horo/Navigation/NavigationErrors.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iterator>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace Horo::Navigation {
    namespace {
        template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor, std::string message = {}) {
            return Result<T>::Failure(MakeError(descriptor, std::move(message)));
        }

        [[nodiscard]] constexpr bool IsValid(const NavigationBakeInputRevisions &revisions) noexcept {
            return revisions.requestGeneration.IsValid() && revisions.definition.IsValid() && revisions.scene.IsValid() &&
                   revisions.areaRegistry.IsValid() && revisions.projectProfile.IsValid() && revisions.coordinates.IsValid() &&
                   revisions.geometry.IsValid();
        }

        [[nodiscard]] constexpr bool IsValid(const NavigationBakeInputLimits &limits) noexcept {
            return limits.maxProfiles > 0 && limits.maxProfiles <= NavigationBakeInputLimits::MaximumProfiles && limits.maxAreas > 0 &&
                   limits.maxAreas <= NavigationBakeInputLimits::MaximumAreas && limits.maxSurfaceBindings > 0 &&
                   limits.maxSurfaceBindings <= NavigationBakeInputLimits::MaximumSurfaceBindings && limits.maxModifiers > 0 &&
                   limits.maxModifiers <= NavigationBakeInputLimits::MaximumModifiers && limits.maxTileTriangles > 0 &&
                   limits.maxTileTriangles <= NavigationBakeInputLimits::MaximumTileTriangles && limits.maxWorkUnits > 0 &&
                   limits.maxWorkUnits <= NavigationBakeInputLimits::MaximumWorkUnits && limits.maxOwnedBytes > 0 &&
                   limits.maxOwnedBytes <= NavigationBakeInputLimits::MaximumOwnedBytes;
        }

        [[nodiscard]] constexpr bool IsKnown(const NavigationBakeModifierMode mode) noexcept {
            return mode >= NavigationBakeModifierMode::AssignArea && mode < NavigationBakeModifierMode::Count;
        }

        [[nodiscard]] constexpr bool IsKnown(const NavigationBakePublicationState state) noexcept {
            return state >= NavigationBakePublicationState::Ready && state < NavigationBakePublicationState::Count;
        }

        [[nodiscard]] bool HasPositiveScale(const Math::Transform &transform) noexcept {
            return transform.scale.x > 0.0F && transform.scale.y > 0.0F && transform.scale.z > 0.0F;
        }

        [[nodiscard]] bool IsNonDegenerate(const Math::Aabb &bounds) noexcept {
            if (!bounds.IsValid())
                return false;
            const auto dimensions = bounds.maximum - bounds.minimum;
            return dimensions.x > Math::DefaultEpsilon && dimensions.y > Math::DefaultEpsilon && dimensions.z > Math::DefaultEpsilon;
        }

        [[nodiscard]] bool TryAdd(const std::uint64_t left, const std::uint64_t right, std::uint64_t &result) noexcept {
            if (left > std::numeric_limits<std::uint64_t>::max() - right)
                return false;
            result = left + right;
            return true;
        }

        [[nodiscard]] bool TryMultiply(const std::uint64_t left, const std::uint64_t right, std::uint64_t &result) noexcept {
            if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
                return false;
            result = left * right;
            return true;
        }

        [[nodiscard]] bool AddStorage(const std::uint64_t count, const std::uint64_t elementSize, std::uint64_t &ownedBytes) noexcept {
            std::uint64_t bytes{};
            return TryMultiply(count, elementSize, bytes) && TryAdd(ownedBytes, bytes, ownedBytes);
        }

        [[nodiscard]] std::string SurfaceContext(const NavigationBakeSurfaceInput &surface, const std::string_view reason) {
            return "Navigation surface " + std::to_string(surface.surface.Value()) + ", profile " +
                   std::to_string(surface.profile.Value()) + ", producer " + std::to_string(surface.producer.Value()) + ", contribution " +
                   std::to_string(surface.contribution.Value()) + ": " + std::string(reason);
        }

        [[nodiscard]] std::string ModifierContext(const NavigationBakeModifierInput &modifier, const std::string_view reason) {
            return "Navigation modifier " + std::to_string(modifier.id.Value()) + ", surface " + std::to_string(modifier.surface.Value()) +
                   ", profile " + std::to_string(modifier.profile.Value()) + ": " + std::string(reason);
        }

        [[nodiscard]] constexpr auto SurfaceBindingKey(const NavigationBakeSurfaceInput &surface) noexcept {
            return std::tuple{surface.profile.Value(), surface.surface.Value(), surface.producer.Value(), surface.contribution.Value()};
        }

        [[nodiscard]] constexpr auto PartitionKey(const NavigationBakeSurfaceInput &surface) noexcept {
            return std::tuple{surface.profile.Value(), surface.surface.Value()};
        }

        [[nodiscard]] constexpr auto PartitionKey(const NavigationTileBuildPartition &partition) noexcept {
            return std::tuple{partition.profile.Value(), partition.surface.Value()};
        }

        [[nodiscard]] constexpr auto ModifierKey(const NavigationBakeModifierInput &modifier) noexcept {
            return std::tuple{modifier.profile.Value(), modifier.surface.Value(), modifier.id.Value()};
        }

        [[nodiscard]] const NavigationSourceContribution *FindContribution(const NavigationSourceGeometrySnapshot &geometry,
                                                                           const NavigationBakeSurfaceInput &surface) noexcept {
            const auto contributions = geometry.Contributions();
            const auto target = std::tuple{surface.producer.Value(), surface.contribution.Value()};
            const auto found =
                std::lower_bound(contributions.begin(), contributions.end(), target, [](const auto &candidate, const auto key) {
                return std::tuple{candidate.producer.Value(), candidate.contribution.Value()} < key;
            });
            if (found == contributions.end() || found->producer != surface.producer || found->contribution != surface.contribution)
                return nullptr;
            return &*found;
        }

        [[nodiscard]] const NavigationResolvedBakeProfile *FindProfile(const std::vector<NavigationResolvedBakeProfile> &profiles,
                                                                       const NavigationAgentProfileId id) noexcept {
            const auto found = std::lower_bound(profiles.begin(), profiles.end(), id.Value(), [](const auto &candidate, const auto value) {
                return candidate.id.Value() < value;
            });
            return found != profiles.end() && found->id == id ? &*found : nullptr;
        }

        [[nodiscard]] const NavigationTileBuildPartition *FindPartition(const std::vector<NavigationTileBuildPartition> &partitions,
                                                                        const NavigationAgentProfileId profile,
                                                                        const SurfaceId surface) noexcept {
            const auto target = std::tuple{profile.Value(), surface.Value()};
            const auto found = std::lower_bound(partitions.begin(), partitions.end(), target, [](const auto &candidate, const auto key) {
                return PartitionKey(candidate) < key;
            });
            return found != partitions.end() && found->profile == profile && found->surface == surface ? &*found : nullptr;
        }

        [[nodiscard]] Result<NavigationAreaDescriptor> ResolveArea(const NavigationAreaRegistry &registry, const NavigationAreaId id,
                                                                   std::string context) {
            auto resolved = registry.ResolveArea(id);
            if (resolved.HasError())
                return Result<NavigationAreaDescriptor>::Failure(
                    WrapError(NavigationErrors::BakeInputReferenceMissing, resolved.ErrorValue(), std::move(context)));
            return Result<NavigationAreaDescriptor>::Success(resolved.Value());
        }

        [[nodiscard]] bool RememberArea(std::vector<NavigationResolvedBakeArea> &areas, const NavigationAreaDescriptor &area,
                                        const std::size_t maximumAreas) {
            const auto found = std::lower_bound(areas.begin(), areas.end(), area.id.Value(), [](const auto &candidate, const auto value) {
                return candidate.id.Value() < value;
            });
            if (found != areas.end() && found->id == area.id)
                return true;
            if (areas.size() >= maximumAreas)
                return false;
            areas.insert(found, {.id = area.id, .source = area.source, .traversalCost = area.traversalCost, .flags = area.flags});
            return true;
        }

        class FingerprintBuilder final {
        public:
            FingerprintBuilder() noexcept {
                static constexpr std::array<std::byte, 28> Domain{
                    std::byte{0x68}, std::byte{0x6f}, std::byte{0x72}, std::byte{0x6f}, std::byte{0x2e}, std::byte{0x6e}, std::byte{0x61},
                    std::byte{0x76}, std::byte{0x2e}, std::byte{0x62}, std::byte{0x61}, std::byte{0x6b}, std::byte{0x65}, std::byte{0x2e},
                    std::byte{0x69}, std::byte{0x6e}, std::byte{0x70}, std::byte{0x75}, std::byte{0x74}, std::byte{0x2e}, std::byte{0x76},
                    std::byte{0x31}, std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},    std::byte{0},    std::byte{1},
                };
                digest_ = ComputeSha256(std::span<const std::byte>{Domain});
            }

            void AddU64(const std::uint64_t value) noexcept {
                std::array<std::byte, 8> bytes{};
                for (std::size_t index = 0; index < bytes.size(); ++index)
                    bytes[index] = static_cast<std::byte>((value >> ((bytes.size() - 1 - index) * 8)) & 0xFFU);
                Add(bytes);
            }

            void AddU32(const std::uint32_t value) noexcept {
                AddU64(value);
            }

            void AddFloat(const float value) noexcept {
                AddU32(std::bit_cast<std::uint32_t>(value == 0.0F ? 0.0F : value));
            }

            void AddDigest(const Sha256Digest &value) noexcept {
                std::array<std::byte, 32> bytes{};
                for (std::size_t index = 0; index < bytes.size(); ++index)
                    bytes[index] = static_cast<std::byte>(value.bytes[index]);
                Add(bytes);
            }

            [[nodiscard]] const Sha256Digest &Finish() const noexcept {
                return digest_;
            }

        private:
            template <std::size_t Size> void Add(const std::array<std::byte, Size> &value) noexcept {
                static_assert(Size <= 32);
                std::array<std::byte, 64> input{};
                for (std::size_t index = 0; index < digest_.bytes.size(); ++index)
                    input[index] = static_cast<std::byte>(digest_.bytes[index]);
                std::copy(value.begin(), value.end(), input.begin() + static_cast<std::ptrdiff_t>(digest_.bytes.size()));
                digest_ = ComputeSha256(std::span{input}.first(digest_.bytes.size() + value.size()));
            }

            Sha256Digest digest_{};
        };

        void Add(FingerprintBuilder &builder, const NavigationBakeInputRevisions &revisions) noexcept {
            builder.AddU64(revisions.definition.Value());
            builder.AddU64(revisions.scene.Value());
            builder.AddU64(revisions.areaRegistry.Value());
            builder.AddU64(revisions.projectProfile.Value());
            builder.AddU64(revisions.coordinates.Value());
            builder.AddU64(revisions.geometry.Value());
        }

        void Add(FingerprintBuilder &builder, const Math::Vec3 value) noexcept {
            builder.AddFloat(value.x);
            builder.AddFloat(value.y);
            builder.AddFloat(value.z);
        }

        [[nodiscard]] Sha256Digest Fingerprint(const NavigationBakeInputRevisions &revisions,
                                               const std::vector<NavigationResolvedBakeProfile> &profiles,
                                               const std::vector<NavigationResolvedBakeArea> &areas,
                                               const std::vector<NavigationTileBuildPartition> &partitions,
                                               const std::vector<NavigationTileBuildTriangle> &triangles,
                                               const std::vector<NavigationTileBuildModifier> &modifiers) noexcept {
            FingerprintBuilder builder;
            Add(builder, revisions);
            builder.AddU64(profiles.size());
            for (const auto &profile : profiles) {
                builder.AddU64(profile.id.Value());
                builder.AddFloat(profile.buildGeometry.radiusMeters);
                builder.AddFloat(profile.buildGeometry.heightMeters);
                builder.AddFloat(profile.buildGeometry.maxSlopeDegrees);
                builder.AddFloat(profile.buildGeometry.stepHeightMeters);
                builder.AddFloat(profile.buildGeometry.cellSizeMeters);
                builder.AddFloat(profile.buildGeometry.cellHeightMeters);
                builder.AddFloat(profile.buildGeometry.minimumRegionSizeMeters);
            }
            builder.AddU64(areas.size());
            for (const auto &area : areas) {
                builder.AddU64(area.id.Value());
                builder.AddU32(static_cast<std::uint32_t>(area.source.kind));
                builder.AddU64(area.source.id.Value());
                builder.AddFloat(area.traversalCost);
                builder.AddU64(area.flags.bits);
            }
            builder.AddU64(partitions.size());
            for (const auto &partition : partitions) {
                builder.AddU64(partition.profile.Value());
                builder.AddU64(partition.surface.Value());
                builder.AddU64(partition.filter.Value());
                builder.AddU32(partition.firstTriangle);
                builder.AddU32(partition.triangleCount);
                builder.AddU32(partition.firstModifier);
                builder.AddU32(partition.modifierCount);
            }
            builder.AddU64(triangles.size());
            for (const auto &triangle : triangles) {
                for (const auto vertex : triangle.vertices)
                    Add(builder, vertex);
                builder.AddU64(triangle.area.Value());
                builder.AddFloat(triangle.traversalCost);
                builder.AddU32(triangle.materialSlot.value);
                builder.AddU32(static_cast<std::uint32_t>(triangle.provenance.kind));
                builder.AddU64(triangle.provenance.producer.Value());
                builder.AddU64(triangle.provenance.contribution.Value());
                builder.AddU64(triangle.provenance.revision.Value());
                builder.AddDigest(triangle.provenance.contentDigest);
                builder.AddU32(triangle.provenance.sourceTriangleIndex);
            }
            builder.AddU64(modifiers.size());
            for (const auto &modifier : modifiers) {
                builder.AddU64(modifier.profile.Value());
                builder.AddU64(modifier.surface.Value());
                builder.AddU64(modifier.id.Value());
                builder.AddU32(static_cast<std::uint32_t>(modifier.mode));
                builder.AddU64(modifier.area.Value());
                Add(builder, modifier.canonicalBounds.minimum);
                Add(builder, modifier.canonicalBounds.maximum);
            }
            return builder.Finish();
        }

        [[nodiscard]] Result<void> ValidateCapacity(
            const NavigationBakeInputLimits &limits, const NavigationSourceGeometrySnapshot &geometry,
            const std::vector<NavigationResolvedBakeProfile> &profiles, const std::vector<NavigationResolvedBakeArea> &areas,
            const std::vector<NavigationTileBuildPartition> &partitions, const std::vector<NavigationTileBuildTriangle> &triangles,
            const std::vector<NavigationTileBuildModifier> &modifiers, const std::uint64_t workUnits) {
            if (triangles.size() > limits.maxTileTriangles || workUnits > limits.maxWorkUnits)
                return Failure<void>(NavigationErrors::BakeInputCapacityExceeded);

            std::uint64_t ownedBytes{};
            if (!AddStorage(geometry.Contributions().size(), sizeof(NavigationSourceContribution), ownedBytes) ||
                !AddStorage(geometry.Vertices().size(), sizeof(Math::Vec3), ownedBytes) ||
                !AddStorage(geometry.Triangles().size(), sizeof(NavigationSourceTriangle), ownedBytes) ||
                !AddStorage(profiles.capacity(), sizeof(NavigationResolvedBakeProfile), ownedBytes) ||
                !AddStorage(areas.capacity(), sizeof(NavigationResolvedBakeArea), ownedBytes) ||
                !AddStorage(partitions.capacity(), sizeof(NavigationTileBuildPartition), ownedBytes) ||
                !AddStorage(triangles.capacity(), sizeof(NavigationTileBuildTriangle), ownedBytes) ||
                !AddStorage(modifiers.capacity(), sizeof(NavigationTileBuildModifier), ownedBytes) || ownedBytes > limits.maxOwnedBytes)
                return Failure<void>(NavigationErrors::BakeInputCapacityExceeded);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<std::uint64_t> ResolveTriangleCapacity(const NavigationBakeInputLimits &limits,
                                                                    const NavigationAreaRegistry &areaRegistry,
                                                                    const NavigationSourceGeometrySnapshot &geometry,
                                                                    const std::size_t profileCount, const std::size_t surfaceCount,
                                                                    const std::size_t modifierCount) {
            std::uint64_t fixedBytes{};
            const auto reservedAreaCount = std::min<std::size_t>(areaRegistry.Areas().size(), limits.maxAreas);
            if (!AddStorage(geometry.Contributions().size(), sizeof(NavigationSourceContribution), fixedBytes) ||
                !AddStorage(geometry.Vertices().size(), sizeof(Math::Vec3), fixedBytes) ||
                !AddStorage(geometry.Triangles().size(), sizeof(NavigationSourceTriangle), fixedBytes) ||
                !AddStorage(profileCount, sizeof(NavigationResolvedBakeProfile), fixedBytes) ||
                !AddStorage(reservedAreaCount, sizeof(NavigationResolvedBakeArea), fixedBytes) ||
                !AddStorage(surfaceCount, sizeof(NavigationTileBuildPartition), fixedBytes) ||
                !AddStorage(modifierCount, sizeof(NavigationTileBuildModifier), fixedBytes) || fixedBytes >= limits.maxOwnedBytes)
                return Failure<std::uint64_t>(NavigationErrors::BakeInputCapacityExceeded);
            const auto available = (limits.maxOwnedBytes - fixedBytes) / sizeof(NavigationTileBuildTriangle);
            const auto capacity =
                std::min({available, limits.maxTileTriangles, static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())});
            if (capacity == 0)
                return Failure<std::uint64_t>(NavigationErrors::BakeInputCapacityExceeded);
            return Result<std::uint64_t>::Success(capacity);
        }

        struct CanonicalBakeStorage final {
            std::vector<NavigationResolvedBakeProfile> profiles;
            std::vector<NavigationResolvedBakeArea> areas;
            std::vector<NavigationTileBuildPartition> partitions;
            std::vector<NavigationTileBuildTriangle> triangles;
            std::vector<NavigationTileBuildModifier> modifiers;
            std::uint64_t workUnits{};
        };

        [[nodiscard]] Result<std::vector<NavigationResolvedBakeProfile>> ResolveProfiles(
            const std::span<const NavigationAgentProfileDescriptor> profiles) {
            std::vector<const NavigationAgentProfileDescriptor *> ordered;
            ordered.reserve(profiles.size());
            for (const auto &profile : profiles) {
                if (const auto validation = ValidateNavigationAgentProfile(profile); validation.HasError())
                    return Result<std::vector<NavigationResolvedBakeProfile>>::Failure(validation.ErrorValue());
                ordered.push_back(&profile);
            }
            std::ranges::sort(ordered, {}, [](const auto *profile) {
                return profile->id.Value();
            });
            if (std::ranges::adjacent_find(ordered, [](const auto *left, const auto *right) {
                return left->id == right->id;
            }) != ordered.end())
                return Failure<std::vector<NavigationResolvedBakeProfile>>(NavigationErrors::DescriptorConflict);

            std::vector<NavigationResolvedBakeProfile> resolved;
            resolved.reserve(ordered.size());
            for (const auto *profile : ordered)
                resolved.push_back({.id = profile->id, .buildGeometry = profile->buildGeometry});
            return Result<std::vector<NavigationResolvedBakeProfile>>::Success(std::move(resolved));
        }

        [[nodiscard]] Result<std::vector<const NavigationBakeSurfaceInput *>> ResolveSurfaces(
            const std::span<const NavigationBakeSurfaceInput> surfaces, const std::vector<NavigationResolvedBakeProfile> &profiles,
            const NavigationSourceGeometrySnapshot &geometry) {
            std::vector<const NavigationBakeSurfaceInput *> ordered;
            ordered.reserve(surfaces.size());
            for (const auto &surface : surfaces) {
                if (!surface.surface.IsValid() || !surface.profile.IsValid() || !surface.filter.IsValid() || !surface.producer.IsValid() ||
                    !surface.contribution.IsValid())
                    return Failure<std::vector<const NavigationBakeSurfaceInput *>>(NavigationErrors::BakeInputInvalid,
                                                                                    SurfaceContext(surface,
                                                                                                   "a stable identity is invalid"));
                if (FindProfile(profiles, surface.profile) == nullptr)
                    return Failure<std::vector<const NavigationBakeSurfaceInput *>>(NavigationErrors::BakeInputReferenceMissing,
                                                                                    SurfaceContext(surface,
                                                                                                   "the referenced profile is missing"));
                if (FindContribution(geometry, surface) == nullptr)
                    return Failure<
                        std::vector<const NavigationBakeSurfaceInput *>>(NavigationErrors::BakeInputReferenceMissing,
                                                                         SurfaceContext(surface,
                                                                                        "the referenced source contribution is missing"));
                ordered.push_back(&surface);
            }
            std::ranges::sort(ordered, [](const auto *left, const auto *right) {
                return SurfaceBindingKey(*left) < SurfaceBindingKey(*right);
            });
            if (std::ranges::adjacent_find(ordered, [](const auto *left, const auto *right) {
                return SurfaceBindingKey(*left) == SurfaceBindingKey(*right);
            }) != ordered.end())
                return Failure<std::vector<const NavigationBakeSurfaceInput *>>(NavigationErrors::DescriptorConflict);
            return Result<std::vector<const NavigationBakeSurfaceInput *>>::Success(std::move(ordered));
        }

        [[nodiscard]] std::uint64_t PotentialTriangleCount(const std::span<const NavigationBakeSurfaceInput *const> surfaces,
                                                           const NavigationSourceGeometrySnapshot &geometry,
                                                           const std::uint64_t capacity) noexcept {
            std::uint64_t total{};
            for (const auto *surface : surfaces) {
                const auto count = FindContribution(geometry, *surface)->triangleCount;
                if (!TryAdd(total, count, total) || total > capacity)
                    return capacity;
            }
            return total;
        }

        [[nodiscard]] Result<void> AppendCanonicalTriangle(const NavigationAreaRegistry &areaRegistry,
                                                           const NavigationSourceGeometrySnapshot &geometry,
                                                           const NavigationBakeSurfaceInput &surface,
                                                           const NavigationSourceTriangle &sourceTriangle,
                                                           const NavigationBakeInputLimits &limits, const std::uint64_t triangleCapacity,
                                                           CanonicalBakeStorage &storage) {
            if (!TryAdd(storage.workUnits, 1, storage.workUnits) || storage.workUnits > limits.maxWorkUnits)
                return Failure<void>(NavigationErrors::BakeInputCapacityExceeded);
            auto area = ResolveArea(areaRegistry, sourceTriangle.area, SurfaceContext(surface, "a triangle area is missing"));
            if (area.HasError())
                return Result<void>::Failure(area.ErrorValue());
            if (!RememberArea(storage.areas, area.Value(), limits.maxAreas))
                return Failure<void>(NavigationErrors::BakeInputCapacityExceeded);
            auto traversal = areaRegistry.ResolveTraversal(surface.filter, sourceTriangle.area);
            if (traversal.HasError())
                return Result<void>::Failure(WrapError(NavigationErrors::BakeInputReferenceMissing, traversal.ErrorValue(),
                                                       SurfaceContext(surface, "the filter or one of its area references is missing")));
            if (!traversal.Value().traversable)
                return Result<void>::Success();
            if (storage.triangles.size() >= triangleCapacity)
                return Failure<void>(NavigationErrors::BakeInputCapacityExceeded);

            const auto &vertices = geometry.Vertices();
            storage.triangles.push_back({.vertices = {vertices[sourceTriangle.vertexIndices[0]], vertices[sourceTriangle.vertexIndices[1]],
                                                      vertices[sourceTriangle.vertexIndices[2]]},
                                         .area = sourceTriangle.area,
                                         .traversalCost = traversal.Value().traversalCost,
                                         .materialSlot = sourceTriangle.materialSlot,
                                         .provenance = sourceTriangle.provenance});
            ++storage.partitions.back().triangleCount;
            if (!TryAdd(storage.workUnits, 1, storage.workUnits) || storage.workUnits > limits.maxWorkUnits)
                return Failure<void>(NavigationErrors::BakeInputCapacityExceeded);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> GatherCanonicalTriangles(const NavigationAreaRegistry &areaRegistry,
                                                            const NavigationSourceGeometrySnapshot &geometry,
                                                            const std::span<const NavigationBakeSurfaceInput *const> surfaces,
                                                            const NavigationBakeInputLimits &limits, const std::uint64_t triangleCapacity,
                                                            CanonicalBakeStorage &storage) {
            for (const auto *surface : surfaces) {
                const auto *source = FindContribution(geometry, *surface);
                if (storage.partitions.empty() || PartitionKey(storage.partitions.back()) != PartitionKey(*surface)) {
                    storage.partitions.push_back({.surface = surface->surface,
                                                  .profile = surface->profile,
                                                  .filter = surface->filter,
                                                  .firstTriangle = static_cast<std::uint32_t>(storage.triangles.size())});
                } else if (storage.partitions.back().filter != surface->filter) {
                    return Failure<void>(NavigationErrors::DescriptorConflict,
                                         SurfaceContext(*surface, "one surface/profile pair has conflicting filters"));
                }

                const auto sourceTriangles = geometry.Triangles().subspan(source->firstTriangle, source->triangleCount);
                for (const auto &sourceTriangle : sourceTriangles) {
                    if (auto appended =
                            AppendCanonicalTriangle(areaRegistry, geometry, *surface, sourceTriangle, limits, triangleCapacity, storage);
                        appended.HasError())
                        return appended;
                }
            }
            if (std::ranges::any_of(storage.partitions, [](const auto &partition) {
                return partition.triangleCount == 0;
            }))
                return Failure<void>(NavigationErrors::BakeInputInvalid,
                                     "A canonical surface/profile partition contains no traversable triangles.");
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ResolveModifiers(const std::span<const NavigationBakeModifierInput> modifiers,
                                                    const NavigationAreaRegistry &areaRegistry, const NavigationBakeInputLimits &limits,
                                                    CanonicalBakeStorage &storage) {
            std::vector<const NavigationBakeModifierInput *> ordered;
            ordered.reserve(modifiers.size());
            for (const auto &modifier : modifiers) {
                if (!modifier.id.IsValid() || !modifier.surface.IsValid() || !modifier.profile.IsValid() || !modifier.area.IsValid() ||
                    !IsKnown(modifier.mode) || !IsNonDegenerate(modifier.localBounds) ||
                    !HasPositiveScale(modifier.localToCanonicalMeters) || modifier.localToCanonicalMeters.TryToMatrix().HasError())
                    return Failure<void>(NavigationErrors::BakeInputInvalid,
                                         ModifierContext(modifier, "identity, bounds, mode, or transform is invalid"));
                if (FindProfile(storage.profiles, modifier.profile) == nullptr ||
                    FindPartition(storage.partitions, modifier.profile, modifier.surface) == nullptr)
                    return Failure<void>(NavigationErrors::BakeInputReferenceMissing,
                                         ModifierContext(modifier, "the referenced profile or surface is missing"));
                ordered.push_back(&modifier);
            }
            std::ranges::sort(ordered, [](const auto *left, const auto *right) {
                return ModifierKey(*left) < ModifierKey(*right);
            });
            if (std::ranges::adjacent_find(ordered, [](const auto *left, const auto *right) {
                return ModifierKey(*left) == ModifierKey(*right);
            }) != ordered.end())
                return Failure<void>(NavigationErrors::DescriptorConflict);

            for (const auto *modifier : ordered) {
                auto area = ResolveArea(areaRegistry, modifier->area, ModifierContext(*modifier, "the assigned area is missing"));
                if (area.HasError())
                    return Result<void>::Failure(area.ErrorValue());
                if (!RememberArea(storage.areas, area.Value(), limits.maxAreas))
                    return Failure<void>(NavigationErrors::BakeInputCapacityExceeded);
                auto bounds = Math::TransformAabb(modifier->localBounds, modifier->localToCanonicalMeters.TryToMatrix().Value());
                if (bounds.HasError() || !IsNonDegenerate(bounds.Value()))
                    return Failure<void>(NavigationErrors::BakeInputInvalid,
                                         ModifierContext(*modifier, "canonical bounds are non-finite or degenerate"));
                storage.modifiers.push_back({.id = modifier->id,
                                             .surface = modifier->surface,
                                             .profile = modifier->profile,
                                             .mode = modifier->mode,
                                             .area = modifier->area,
                                             .canonicalBounds = bounds.Value()});
                if (!TryAdd(storage.workUnits, 8, storage.workUnits) || storage.workUnits > limits.maxWorkUnits)
                    return Failure<void>(NavigationErrors::BakeInputCapacityExceeded);
            }
            return Result<void>::Success();
        }

        void BindModifierRanges(CanonicalBakeStorage &storage) noexcept {
            for (auto &partition : storage.partitions) {
                const auto first = std::lower_bound(storage.modifiers.begin(), storage.modifiers.end(), PartitionKey(partition),
                                                    [](const auto &modifier, const auto key) {
                    return std::tuple{modifier.profile.Value(), modifier.surface.Value()} < key;
                });
                const auto last =
                    std::upper_bound(first, storage.modifiers.end(), PartitionKey(partition), [](const auto key, const auto &modifier) {
                    return key < std::tuple{modifier.profile.Value(), modifier.surface.Value()};
                });
                partition.firstModifier = static_cast<std::uint32_t>(std::distance(storage.modifiers.begin(), first));
                partition.modifierCount = static_cast<std::uint32_t>(std::distance(first, last));
            }
        }
    }  // namespace

    /** @copydoc NavigationBakeInputSnapshot::Create */
    Result<NavigationBakeInputSnapshot> NavigationBakeInputSnapshot::Create(
        const NavigationBakeInputRevisions &revisions, const NavigationAreaRegistry &areas,
        const std::span<const NavigationAgentProfileDescriptor> profiles, const std::span<const NavigationBakeSurfaceInput> surfaces,
        const std::span<const NavigationBakeModifierInput> modifiers, NavigationSourceGeometrySnapshot &&geometry,
        const NavigationBakeInputLimits &limits) {
        try {
            if (!IsValid(revisions) || !IsValid(limits) || revisions.geometry != geometry.Revision() || profiles.empty() ||
                surfaces.empty())
                return Failure<NavigationBakeInputSnapshot>(NavigationErrors::BakeInputInvalid);
            if (profiles.size() > limits.maxProfiles || surfaces.size() > limits.maxSurfaceBindings ||
                modifiers.size() > limits.maxModifiers)
                return Failure<NavigationBakeInputSnapshot>(NavigationErrors::BakeInputCapacityExceeded);

            auto resolvedProfiles = ResolveProfiles(profiles);
            if (resolvedProfiles.HasError())
                return Result<NavigationBakeInputSnapshot>::Failure(resolvedProfiles.ErrorValue());
            auto orderedSurfaces = ResolveSurfaces(surfaces, resolvedProfiles.Value(), geometry);
            if (orderedSurfaces.HasError())
                return Result<NavigationBakeInputSnapshot>::Failure(orderedSurfaces.ErrorValue());
            auto triangleCapacity = ResolveTriangleCapacity(limits, areas, geometry, resolvedProfiles.Value().size(),
                                                            orderedSurfaces.Value().size(), modifiers.size());
            if (triangleCapacity.HasError())
                return Result<NavigationBakeInputSnapshot>::Failure(triangleCapacity.ErrorValue());

            CanonicalBakeStorage storage;
            storage.profiles = std::move(resolvedProfiles).Value();
            storage.areas.reserve(std::min<std::size_t>(areas.Areas().size(), limits.maxAreas));
            storage.partitions.reserve(orderedSurfaces.Value().size());
            storage.triangles.reserve(
                static_cast<std::size_t>(PotentialTriangleCount(orderedSurfaces.Value(), geometry, triangleCapacity.Value())));
            storage.modifiers.reserve(modifiers.size());
            if (auto gathered =
                    GatherCanonicalTriangles(areas, geometry, orderedSurfaces.Value(), limits, triangleCapacity.Value(), storage);
                gathered.HasError())
                return Result<NavigationBakeInputSnapshot>::Failure(gathered.ErrorValue());
            if (auto resolved = ResolveModifiers(modifiers, areas, limits, storage); resolved.HasError())
                return Result<NavigationBakeInputSnapshot>::Failure(resolved.ErrorValue());
            BindModifierRanges(storage);

            if (const auto capacity = ValidateCapacity(limits, geometry, storage.profiles, storage.areas, storage.partitions,
                                                       storage.triangles, storage.modifiers, storage.workUnits);
                capacity.HasError())
                return Result<NavigationBakeInputSnapshot>::Failure(capacity.ErrorValue());
            const auto fingerprint =
                Fingerprint(revisions, storage.profiles, storage.areas, storage.partitions, storage.triangles, storage.modifiers);
            return Result<NavigationBakeInputSnapshot>::Success(
                NavigationBakeInputSnapshot{revisions, limits, fingerprint, std::move(storage.profiles), std::move(storage.areas),
                                            std::move(storage.partitions), std::move(storage.triangles), std::move(storage.modifiers),
                                            std::move(geometry)});
        } catch (const std::bad_alloc &) {
            return Failure<NavigationBakeInputSnapshot>(NavigationErrors::BakeInputCapacityExceeded);
        }
    }

    /** @copydoc NavigationBakeInputSnapshot::Revisions */
    const NavigationBakeInputRevisions &NavigationBakeInputSnapshot::Revisions() const noexcept {
        return revisions_;
    }

    /** @copydoc NavigationBakeInputSnapshot::Limits */
    const NavigationBakeInputLimits &NavigationBakeInputSnapshot::Limits() const noexcept {
        return limits_;
    }

    /** @copydoc NavigationBakeInputSnapshot::Fingerprint */
    const Sha256Digest &NavigationBakeInputSnapshot::Fingerprint() const noexcept {
        return fingerprint_;
    }

    /** @copydoc NavigationBakeInputSnapshot::Profiles */
    std::span<const NavigationResolvedBakeProfile> NavigationBakeInputSnapshot::Profiles() const noexcept {
        return profiles_;
    }

    /** @copydoc NavigationBakeInputSnapshot::Areas */
    std::span<const NavigationResolvedBakeArea> NavigationBakeInputSnapshot::Areas() const noexcept {
        return areas_;
    }

    /** @copydoc NavigationBakeInputSnapshot::Partitions */
    std::span<const NavigationTileBuildPartition> NavigationBakeInputSnapshot::Partitions() const noexcept {
        return partitions_;
    }

    /** @copydoc NavigationBakeInputSnapshot::Triangles */
    std::span<const NavigationTileBuildTriangle> NavigationBakeInputSnapshot::Triangles() const noexcept {
        return triangles_;
    }

    /** @copydoc NavigationBakeInputSnapshot::Modifiers */
    std::span<const NavigationTileBuildModifier> NavigationBakeInputSnapshot::Modifiers() const noexcept {
        return modifiers_;
    }

    /** @copydoc NavigationBakeInputSnapshot::ValidatePublication */
    Result<void> NavigationBakeInputSnapshot::ValidatePublication(const NavigationBakeRequestGeneration expectedRequestGeneration,
                                                                  const NavigationBakeInputRevisions &currentRevisions,
                                                                  const std::span<const NavigationSourceObservation> currentSources,
                                                                  const NavigationBakePublicationState state) const {
        if (!expectedRequestGeneration.IsValid() || !IsValid(currentRevisions) || !IsKnown(state))
            return Failure<void>(NavigationErrors::BakeInputInvalid);
        switch (state) {
            case NavigationBakePublicationState::Ready:
                break;
            case NavigationBakePublicationState::Cancelled:
                return Failure<void>(NavigationErrors::BakeInputCancelled);
            case NavigationBakePublicationState::Failed:
                return Failure<void>(NavigationErrors::BakeInputFailed);
            case NavigationBakePublicationState::Superseded:
                return Failure<void>(NavigationErrors::BakeInputStale);
            case NavigationBakePublicationState::ShuttingDown:
                return Failure<void>(NavigationErrors::BakeInputShuttingDown);
            case NavigationBakePublicationState::Count:
                return Failure<void>(NavigationErrors::BakeInputInvalid);
        }
        if (expectedRequestGeneration != revisions_.requestGeneration || currentRevisions != revisions_)
            return Failure<void>(NavigationErrors::BakeInputStale);
        auto sourceValidation = geometry_.ValidateCurrent(revisions_.geometry, currentSources);
        if (sourceValidation.HasError())
            return Result<void>::Failure(WrapError(NavigationErrors::BakeInputStale, sourceValidation.ErrorValue(),
                                                   "Navigation bake input source provenance changed before publication."));
        return Result<void>::Success();
    }

    NavigationBakeInputSnapshot::NavigationBakeInputSnapshot(NavigationBakeInputRevisions revisions, NavigationBakeInputLimits limits,
                                                             Sha256Digest fingerprint, std::vector<NavigationResolvedBakeProfile> profiles,
                                                             std::vector<NavigationResolvedBakeArea> areas,
                                                             std::vector<NavigationTileBuildPartition> partitions,
                                                             std::vector<NavigationTileBuildTriangle> triangles,
                                                             std::vector<NavigationTileBuildModifier> modifiers,
                                                             NavigationSourceGeometrySnapshot geometry) noexcept
        : revisions_(revisions), limits_(limits), fingerprint_(fingerprint), profiles_(std::move(profiles)), areas_(std::move(areas)),
          partitions_(std::move(partitions)), triangles_(std::move(triangles)), modifiers_(std::move(modifiers)),
          geometry_(std::move(geometry)) {}
}  // namespace Horo::Navigation
