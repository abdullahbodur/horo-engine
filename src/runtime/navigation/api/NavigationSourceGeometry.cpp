#include "Horo/Navigation/NavigationSourceGeometry.h"

#include "Horo/Navigation/NavigationErrors.h"

#include <algorithm>
#include <limits>
#include <tuple>
#include <utility>

namespace Horo::Navigation {
    namespace {
        template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] constexpr bool IsKnownProducer(const NavigationSourceProducerKind kind) noexcept {
            return kind >= NavigationSourceProducerKind::StaticCollider && kind < NavigationSourceProducerKind::Count;
        }

        [[nodiscard]] constexpr bool IsValidLimits(const NavigationSourceGeometryLimits &limits) noexcept {
            return limits.maxContributions > 0 && limits.maxContributions <= NavigationSourceGeometryLimits::MaximumContributions &&
                   limits.maxVertices > 0 && limits.maxVertices <= NavigationSourceGeometryLimits::MaximumVertices &&
                   limits.maxTriangles > 0 && limits.maxTriangles <= NavigationSourceGeometryLimits::MaximumTriangles &&
                   limits.maxOwnedBytes > 0 && limits.maxOwnedBytes <= NavigationSourceGeometryLimits::MaximumOwnedBytes;
        }

        [[nodiscard]] constexpr auto SourceKey(const NavigationSourceProducerId producer,
                                               const NavigationSourceContributionId contribution) noexcept {
            return std::tuple{producer.Value(), contribution.Value()};
        }

        [[nodiscard]] bool HasPositiveScale(const Math::Transform &transform) noexcept {
            return transform.scale.x > 0.0F && transform.scale.y > 0.0F && transform.scale.z > 0.0F;
        }

        [[nodiscard]] bool TryAdd(const std::uint64_t left, const std::uint64_t right, std::uint64_t &sum) noexcept {
            if (left > std::numeric_limits<std::uint64_t>::max() - right)
                return false;
            sum = left + right;
            return true;
        }

        [[nodiscard]] bool TryMultiply(const std::uint64_t left, const std::uint64_t right, std::uint64_t &product) noexcept {
            if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
                return false;
            product = left * right;
            return true;
        }

        [[nodiscard]] bool IsNonDegenerate(const Math::Vec3 first, const Math::Vec3 second, const Math::Vec3 third) noexcept {
            const auto cross = Math::Cross(second - first, third - first);
            return Math::IsFinite(cross) && Math::LengthSquared(cross) > Math::DefaultEpsilon * Math::DefaultEpsilon;
        }

        struct GeometryTotals final {
            std::uint64_t vertices{};
            std::uint64_t triangles{};
            std::uint64_t ownedBytes{};
        };

        [[nodiscard]] bool AccumulateCounts(const NavigationSourceContributionInput &input, GeometryTotals &totals) noexcept {
            return TryAdd(totals.vertices, input.vertices.size(), totals.vertices) &&
                   TryAdd(totals.triangles, input.triangles.size(), totals.triangles);
        }

        [[nodiscard]] bool ComputeOwnedBytes(const std::size_t contributionCount, GeometryTotals &totals) noexcept {
            std::uint64_t contributionBytes{};
            std::uint64_t vertexBytes{};
            std::uint64_t triangleBytes{};
            std::uint64_t aggregate{};
            if (!TryMultiply(contributionCount, sizeof(NavigationSourceContribution), contributionBytes))
                return false;
            if (!TryMultiply(totals.vertices, sizeof(Math::Vec3), vertexBytes))
                return false;
            if (!TryMultiply(totals.triangles, sizeof(NavigationSourceTriangle), triangleBytes))
                return false;
            if (!TryAdd(contributionBytes, vertexBytes, aggregate))
                return false;
            return TryAdd(aggregate, triangleBytes, totals.ownedBytes);
        }

        [[nodiscard]] Result<GeometryTotals> Measure(const std::span<const NavigationSourceContributionInput *const> ordered,
                                                     const NavigationSourceGeometryLimits &limits) {
            GeometryTotals totals{};
            for (const auto *input : ordered) {
                if (!AccumulateCounts(*input, totals))
                    return Failure<GeometryTotals>(NavigationErrors::SourceGeometryCapacityExceeded);
            }
            if (totals.vertices > limits.maxVertices || totals.triangles > limits.maxTriangles)
                return Failure<GeometryTotals>(NavigationErrors::SourceGeometryCapacityExceeded);
            if (!ComputeOwnedBytes(ordered.size(), totals) || totals.ownedBytes > limits.maxOwnedBytes)
                return Failure<GeometryTotals>(NavigationErrors::SourceGeometryCapacityExceeded);
            return Result<GeometryTotals>::Success(totals);
        }

        [[nodiscard]] Result<void> ValidateInputMetadata(const NavigationSourceContributionInput &input) {
            if (!IsKnownProducer(input.kind))
                return Failure<void>(NavigationErrors::SourceGeometryUnsupported);
            if (!input.producer.IsValid() || !input.contribution.IsValid() || !input.revision.IsValid() || input.vertices.empty() ||
                input.triangles.empty() || !HasPositiveScale(input.localToCanonicalMeters) ||
                input.localToCanonicalMeters.TryToMatrix().HasError())
                return Failure<void>(NavigationErrors::SourceGeometryInvalid);
            return Result<void>::Success();
        }

        [[nodiscard]] bool HasValidIndices(const NavigationSourceTriangleInput &triangle, const std::size_t vertexCount) noexcept {
            return triangle.vertexIndices[0] < vertexCount && triangle.vertexIndices[1] < vertexCount &&
                   triangle.vertexIndices[2] < vertexCount && triangle.vertexIndices[0] != triangle.vertexIndices[1] &&
                   triangle.vertexIndices[1] != triangle.vertexIndices[2] && triangle.vertexIndices[0] != triangle.vertexIndices[2];
        }

        [[nodiscard]] Result<void> AppendCanonicalVertices(const NavigationSourceContributionInput &input,
                                                           std::vector<Math::Vec3> &vertices) {
            const auto matrix = input.localToCanonicalMeters.TryToMatrix().Value();
            for (const auto vertex : input.vertices) {
                auto transformed = Math::TryTransformPoint(matrix, vertex);
                if (transformed.HasError())
                    return Failure<void>(NavigationErrors::SourceGeometryInvalid);
                vertices.push_back(std::move(transformed).Value());
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> AppendTriangle(const NavigationSourceContributionInput &input,
                                                  const NavigationSourceTriangleInput &triangle, const std::uint32_t sourceTriangleIndex,
                                                  const std::uint32_t firstVertex, const std::vector<Math::Vec3> &vertices,
                                                  std::vector<NavigationSourceTriangle> &triangles) {
            if (!triangle.area.IsValid() || !HasValidIndices(triangle, input.vertices.size()))
                return Failure<void>(NavigationErrors::SourceGeometryInvalid);
            const auto first = firstVertex + triangle.vertexIndices[0];
            const auto second = firstVertex + triangle.vertexIndices[1];
            const auto third = firstVertex + triangle.vertexIndices[2];
            if (!IsNonDegenerate(vertices[first], vertices[second], vertices[third]))
                return Failure<void>(NavigationErrors::SourceGeometryInvalid);
            triangles.push_back({
                .vertexIndices = {first, second, third},
                .area = triangle.area,
                .materialSlot = triangle.materialSlot,
                .provenance =
                    {
                        .kind = input.kind,
                        .producer = input.producer,
                        .contribution = input.contribution,
                        .revision = input.revision,
                        .contentDigest = input.contentDigest,
                        .sourceTriangleIndex = sourceTriangleIndex,
                    },
            });
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> AppendContribution(const NavigationSourceContributionInput &input, std::vector<Math::Vec3> &vertices,
                                                      std::vector<NavigationSourceTriangle> &triangles,
                                                      std::vector<NavigationSourceContribution> &contributions) {
            const auto firstVertex = static_cast<std::uint32_t>(vertices.size());
            const auto firstTriangle = static_cast<std::uint32_t>(triangles.size());
            if (AppendCanonicalVertices(input, vertices).HasError())
                return Failure<void>(NavigationErrors::SourceGeometryInvalid);
            for (std::size_t index = 0; index < input.triangles.size(); ++index) {
                if (AppendTriangle(input, input.triangles[index], static_cast<std::uint32_t>(index), firstVertex, vertices, triangles)
                        .HasError())
                    return Failure<void>(NavigationErrors::SourceGeometryInvalid);
            }

            contributions.push_back({
                .kind = input.kind,
                .producer = input.producer,
                .contribution = input.contribution,
                .revision = input.revision,
                .contentDigest = input.contentDigest,
                .localToCanonicalMeters = input.localToCanonicalMeters,
                .firstVertex = firstVertex,
                .vertexCount = static_cast<std::uint32_t>(input.vertices.size()),
                .firstTriangle = firstTriangle,
                .triangleCount = static_cast<std::uint32_t>(input.triangles.size()),
            });
            return Result<void>::Success();
        }

        [[nodiscard]] bool IsValidObservation(const NavigationSourceObservation &observation) noexcept {
            return observation.producer.IsValid() && observation.contribution.IsValid() && observation.revision.IsValid();
        }

        [[nodiscard]] bool Matches(const NavigationSourceContribution &captured, const NavigationSourceObservation &current) noexcept {
            return current.kind == captured.kind && current.producer == captured.producer &&
                   current.contribution == captured.contribution && current.revision == captured.revision &&
                   current.contentDigest == captured.contentDigest;
        }

        [[nodiscard]] Result<void> ValidateCreateRequest(const NavigationSourceSnapshotRevision revision,
                                                         const std::span<const NavigationSourceContributionInput> inputs,
                                                         const NavigationSourceGeometryLimits &limits) {
            if (!revision.IsValid() || !IsValidLimits(limits) || inputs.empty())
                return Failure<void>(NavigationErrors::SourceGeometryInvalid);
            if (inputs.size() > limits.maxContributions)
                return Failure<void>(NavigationErrors::SourceGeometryCapacityExceeded);
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc NavigationSourceGeometrySnapshot::Create */
    Result<NavigationSourceGeometrySnapshot> NavigationSourceGeometrySnapshot::Create(
        const NavigationSourceSnapshotRevision revision, const std::span<const NavigationSourceContributionInput> inputs,
        const NavigationSourceGeometryLimits &limits) {
        if (const auto requestValidation = ValidateCreateRequest(revision, inputs, limits); requestValidation.HasError())
            return Result<NavigationSourceGeometrySnapshot>::Failure(requestValidation.ErrorValue());

        std::vector<const NavigationSourceContributionInput *> ordered;
        ordered.reserve(inputs.size());
        for (const auto &input : inputs) {
            if (const auto validation = ValidateInputMetadata(input); validation.HasError())
                return Result<NavigationSourceGeometrySnapshot>::Failure(validation.ErrorValue());
            ordered.push_back(&input);
        }
        std::ranges::sort(ordered, [](const auto *left, const auto *right) {
            return SourceKey(left->producer, left->contribution) < SourceKey(right->producer, right->contribution);
        });
        if (std::ranges::adjacent_find(ordered, [](const auto *left, const auto *right) {
            return SourceKey(left->producer, left->contribution) == SourceKey(right->producer, right->contribution);
        }) != ordered.end())
            return Failure<NavigationSourceGeometrySnapshot>(NavigationErrors::DescriptorConflict);

        auto totals = Measure(ordered, limits);
        if (totals.HasError())
            return Failure<NavigationSourceGeometrySnapshot>(NavigationErrors::SourceGeometryCapacityExceeded);
        std::vector<NavigationSourceContribution> contributions;
        std::vector<Math::Vec3> vertices;
        std::vector<NavigationSourceTriangle> triangles;
        contributions.reserve(ordered.size());
        vertices.reserve(static_cast<std::size_t>(totals.Value().vertices));
        triangles.reserve(static_cast<std::size_t>(totals.Value().triangles));
        for (const auto *input : ordered) {
            if (const auto appended = AppendContribution(*input, vertices, triangles, contributions); appended.HasError())
                return Failure<NavigationSourceGeometrySnapshot>(NavigationErrors::SourceGeometryInvalid);
        }
        return Result<NavigationSourceGeometrySnapshot>::Success(
            NavigationSourceGeometrySnapshot{revision, limits, std::move(contributions), std::move(vertices), std::move(triangles)});
    }

    /** @copydoc NavigationSourceGeometrySnapshot::Revision */
    NavigationSourceSnapshotRevision NavigationSourceGeometrySnapshot::Revision() const noexcept {
        return revision_;
    }

    /** @copydoc NavigationSourceGeometrySnapshot::Limits */
    NavigationSourceGeometryLimits NavigationSourceGeometrySnapshot::Limits() const noexcept {
        return limits_;
    }

    /** @copydoc NavigationSourceGeometrySnapshot::Contributions */
    std::span<const NavigationSourceContribution> NavigationSourceGeometrySnapshot::Contributions() const noexcept {
        return contributions_;
    }

    /** @copydoc NavigationSourceGeometrySnapshot::Vertices */
    std::span<const Math::Vec3> NavigationSourceGeometrySnapshot::Vertices() const noexcept {
        return vertices_;
    }

    /** @copydoc NavigationSourceGeometrySnapshot::Triangles */
    std::span<const NavigationSourceTriangle> NavigationSourceGeometrySnapshot::Triangles() const noexcept {
        return triangles_;
    }

    /** @copydoc NavigationSourceGeometrySnapshot::ValidateCurrent */
    Result<void> NavigationSourceGeometrySnapshot::ValidateCurrent(
        const NavigationSourceSnapshotRevision expectedSnapshotRevision,
        const std::span<const NavigationSourceObservation> currentSources) const {
        if (!expectedSnapshotRevision.IsValid())
            return Failure<void>(NavigationErrors::SourceGeometryInvalid);
        if (expectedSnapshotRevision != revision_ || currentSources.size() != contributions_.size())
            return Failure<void>(NavigationErrors::SourceGeometryStale);

        std::vector<NavigationSourceObservation> ordered(currentSources.begin(), currentSources.end());
        if (std::ranges::any_of(ordered, [](const auto &source) {
            return !IsKnownProducer(source.kind);
        }))
            return Failure<void>(NavigationErrors::SourceGeometryUnsupported);
        if (std::ranges::any_of(ordered, [](const auto &source) {
            return !IsValidObservation(source);
        }))
            return Failure<void>(NavigationErrors::SourceGeometryInvalid);
        std::ranges::sort(ordered, [](const auto &left, const auto &right) {
            return SourceKey(left.producer, left.contribution) < SourceKey(right.producer, right.contribution);
        });
        for (std::size_t index = 0; index < ordered.size(); ++index) {
            if (!Matches(contributions_[index], ordered[index]))
                return Failure<void>(NavigationErrors::SourceGeometryStale);
        }
        return Result<void>::Success();
    }

    NavigationSourceGeometrySnapshot::NavigationSourceGeometrySnapshot(const NavigationSourceSnapshotRevision revision,
                                                                       const NavigationSourceGeometryLimits &limits,
                                                                       std::vector<NavigationSourceContribution> contributions,
                                                                       std::vector<Math::Vec3> vertices,
                                                                       std::vector<NavigationSourceTriangle> triangles) noexcept
        : revision_(revision), limits_(limits), contributions_(std::move(contributions)), vertices_(std::move(vertices)),
          triangles_(std::move(triangles)) {}
}  // namespace Horo::Navigation
