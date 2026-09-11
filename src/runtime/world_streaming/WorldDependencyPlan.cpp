#include "Horo/WorldStreaming/WorldDependencyPlan.h"

#include "Horo/WorldStreaming/WorldStreamingErrors.h"

#include <algorithm>
#include <numeric>
#include <optional>
#include <tuple>
#include <utility>

namespace Horo::WorldStreaming {
    namespace {
        [[nodiscard]] bool IsKnown(const WorldDependencyKind kind) noexcept {
            switch (kind) {
                case WorldDependencyKind::Hard:
                case WorldDependencyKind::Soft:
                    return true;
            }
            return false;
        }

        [[nodiscard]] bool EdgeLess(const WorldDependencyCandidate &left, const WorldDependencyCandidate &right) noexcept {
            return std::tuple{left.kind, left.source.address, left.target.address} <
                   std::tuple{right.kind, right.source.address, right.target.address};
        }

        [[nodiscard]] bool SameEdge(const WorldDependencyCandidate &left, const WorldDependencyCandidate &right) noexcept {
            return left.kind == right.kind && left.source.address == right.source.address && left.target.address == right.target.address;
        }

        [[nodiscard]] std::optional<std::size_t> FindObject(const std::span<const WorldSpatialAssignmentEntry> objects,
                                                            const WorldAuthoringObjectAddress address) noexcept {
            const auto found = std::ranges::lower_bound(objects, address, {}, &WorldSpatialAssignmentEntry::address);
            if (found == objects.end() || found->address != address)
                return std::nullopt;
            return static_cast<std::size_t>(found - objects.begin());
        }

        class DisjointSets final {
        public:
            explicit DisjointSets(const std::size_t size) : parents_(size) {
                std::iota(parents_.begin(), parents_.end(), 0);
            }

            [[nodiscard]] std::size_t Root(std::size_t value) noexcept {
                while (parents_[value] != value) {
                    parents_[value] = parents_[parents_[value]];
                    value = parents_[value];
                }
                return value;
            }

            void Unite(const std::size_t left, const std::size_t right) noexcept {
                const auto leftRoot = Root(left);
                const auto rightRoot = Root(right);
                if (leftRoot == rightRoot)
                    return;
                const auto first = std::min(leftRoot, rightRoot);
                const auto second = std::max(leftRoot, rightRoot);
                parents_[second] = first;
            }

        private:
            std::vector<std::size_t> parents_;
        };

        struct PlanStorage final {
            struct SoftReferenceResolution final {
                std::size_t source{};
                std::optional<std::size_t> target{};
            };

            std::vector<WorldDependencyBundleEntry> bundles;
            std::vector<WorldDependencyEndpoint> members;
            std::vector<WorldDependencyCandidate> softReferences;
            std::vector<SoftReferenceResolution> softResolutions;
        };

        struct HardGraphState final {
            DisjointSets &sets;
            std::vector<std::uint32_t> &counts;
            std::vector<bool> &members;
        };

        [[nodiscard]] Result<void> ValidateEndpointRevision(const WorldDependencyEndpoint &endpoint,
                                                            const WorldSpatialAssignmentEntry &object) {
            if (endpoint.revision != object.revision)
                return Result<void>::Failure(MakeError(WorldStreamingErrors::DependencyPlanRevisionStale));
            return Result<void>::Success();
        }

        [[nodiscard]] bool IsValidEdge(const WorldDependencyCandidate &edge) noexcept {
            return IsKnown(edge.kind) && edge.source.IsValid() && edge.target.IsValid() && edge.source.address != edge.target.address;
        }

        [[nodiscard]] Result<void> ProcessSoftEdge(const WorldDependencyCandidate &edge,
                                                   const std::span<const WorldSpatialAssignmentEntry> objects, const std::size_t source,
                                                   const std::optional<std::size_t> target, const WorldDependencyPlanLimits limits,
                                                   PlanStorage &storage) {
            if (target.has_value()) {
                if (const auto revision = ValidateEndpointRevision(edge.target, objects[*target]); revision.HasError())
                    return revision;
            }
            if (storage.softReferences.size() >= limits.maximumSoftReferences)
                return Result<void>::Failure(MakeError(WorldStreamingErrors::DependencyPlanCapacityExceeded));
            storage.softReferences.emplace_back(edge);
            storage.softResolutions.emplace_back(source, target);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ProcessHardEdge(const WorldDependencyCandidate &edge,
                                                   const std::span<const WorldSpatialAssignmentEntry> objects, const std::size_t source,
                                                   const std::optional<std::size_t> target, const WorldDependencyPlanLimits limits,
                                                   const HardGraphState state) {
            if (!target.has_value())
                return Result<void>::Failure(MakeError(WorldStreamingErrors::DependencyPlanHardTargetMissing));
            if (const auto revision = ValidateEndpointRevision(edge.target, objects[*target]); revision.HasError())
                return revision;
            if (state.counts[source] >= limits.maximumHardDependenciesPerObject)
                return Result<void>::Failure(MakeError(WorldStreamingErrors::DependencyPlanCapacityExceeded));
            ++state.counts[source];
            state.members[source] = true;
            state.members[*target] = true;
            state.sets.Unite(source, *target);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ProcessEdge(const WorldDependencyCandidate &edge,
                                               const std::span<const WorldSpatialAssignmentEntry> objects,
                                               const WorldDependencyPlanLimits limits, DisjointSets &sets,
                                               std::vector<std::uint32_t> &hardCounts, std::vector<bool> &hardMembers,
                                               PlanStorage &storage) {
            if (!IsValidEdge(edge))
                return Result<void>::Failure(MakeError(WorldStreamingErrors::DependencyPlanInvalid));
            const auto source = FindObject(objects, edge.source.address);
            if (!source.has_value())
                return Result<void>::Failure(MakeError(WorldStreamingErrors::DependencyPlanInvalid));
            if (const auto revision = ValidateEndpointRevision(edge.source, objects[*source]); revision.HasError())
                return revision;

            const auto target = FindObject(objects, edge.target.address);
            if (edge.kind == WorldDependencyKind::Soft)
                return ProcessSoftEdge(edge, objects, *source, target, limits, storage);
            return ProcessHardEdge(edge, objects, *source, target, limits, {sets, hardCounts, hardMembers});
        }

        [[nodiscard]] Result<void> BuildBundles(const std::span<const WorldSpatialAssignmentEntry> objects,
                                                const WorldDependencyPlanLimits limits, DisjointSets &sets,
                                                const std::vector<bool> &hardMembers, PlanStorage &storage) {
            std::vector<std::vector<WorldDependencyEndpoint>> components(objects.size());
            for (std::size_t index = 0; index < objects.size(); ++index) {
                if (!hardMembers[index])
                    continue;
                const auto &object = objects[index];
                components[sets.Root(index)].emplace_back(object.address, object.revision);
            }
            for (const auto &component : components) {
                if (component.empty())
                    continue;
                if (component.size() > limits.maximumBundleMembers)
                    return Result<void>::Failure(MakeError(WorldStreamingErrors::DependencyPlanCapacityExceeded));
                const auto offset = static_cast<std::uint32_t>(storage.members.size());
                storage.members.insert(storage.members.end(), component.begin(), component.end());
                storage.bundles.emplace_back(offset, static_cast<std::uint32_t>(component.size()));
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateSoftReferencePolicy(DisjointSets &sets,
                                                               const std::span<const PlanStorage::SoftReferenceResolution> resolutions) {
            for (const auto &resolution : resolutions) {
                if (!resolution.target.has_value())
                    continue;
                if (sets.Root(resolution.source) == sets.Root(*resolution.target))
                    return Result<void>::Failure(MakeError(WorldStreamingErrors::DependencyPlanAmbiguous));
            }
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc WorldDependencyPlan::WorldDependencyPlan */
    WorldDependencyPlan::WorldDependencyPlan(const WorldPartitionId partition, std::vector<WorldDependencyBundleEntry> bundles,
                                             std::vector<WorldDependencyEndpoint> members,
                                             std::vector<WorldDependencyCandidate> softReferences) noexcept
        : partition_(partition), bundles_(std::move(bundles)), members_(std::move(members)), softReferences_(std::move(softReferences)) {}

    /** @copydoc WorldDependencyPlan::Create */
    Result<WorldDependencyPlan> WorldDependencyPlan::Create(const WorldSpatialAssignment &assignments,
                                                            const std::span<const WorldDependencyCandidate> dependencies,
                                                            const WorldDependencyPlanLimits limits) {
        if (limits.maximumEdges == 0 || limits.maximumHardDependenciesPerObject == 0 || limits.maximumBundleMembers == 0 ||
            limits.maximumSoftReferences == 0)
            return Result<WorldDependencyPlan>::Failure(MakeError(WorldStreamingErrors::DependencyPlanInvalid));
        if (dependencies.size() > limits.maximumEdges)
            return Result<WorldDependencyPlan>::Failure(MakeError(WorldStreamingErrors::DependencyPlanCapacityExceeded));

        std::vector<WorldDependencyCandidate> ordered{dependencies.begin(), dependencies.end()};
        std::ranges::sort(ordered, EdgeLess);
        if (std::ranges::adjacent_find(ordered, SameEdge) != ordered.end())
            return Result<WorldDependencyPlan>::Failure(MakeError(WorldStreamingErrors::DependencyPlanInvalid));

        const auto objects = assignments.Objects();
        DisjointSets sets{objects.size()};
        std::vector<std::uint32_t> hardCounts(objects.size());
        std::vector<bool> hardMembers(objects.size());
        PlanStorage storage;
        for (const auto &edge : ordered) {
            if (const auto processed = ProcessEdge(edge, objects, limits, sets, hardCounts, hardMembers, storage); processed.HasError())
                return Result<WorldDependencyPlan>::Failure(processed.ErrorValue());
        }
        if (const auto policy = ValidateSoftReferencePolicy(sets, storage.softResolutions); policy.HasError())
            return Result<WorldDependencyPlan>::Failure(policy.ErrorValue());
        if (const auto bundles = BuildBundles(objects, limits, sets, hardMembers, storage); bundles.HasError())
            return Result<WorldDependencyPlan>::Failure(bundles.ErrorValue());
        return Result<WorldDependencyPlan>::Success(WorldDependencyPlan{assignments.Partition(), std::move(storage.bundles),
                                                                        std::move(storage.members), std::move(storage.softReferences)});
    }

    /** @copydoc WorldDependencyPlan::MembersForBundle */
    std::span<const WorldDependencyEndpoint> WorldDependencyPlan::MembersForBundle(const std::size_t bundleIndex) const noexcept {
        if (bundleIndex >= bundles_.size())
            return {};
        const auto &bundle = bundles_[bundleIndex];
        return std::span{members_}.subspan(bundle.memberOffset, bundle.memberCount);
    }
}  // namespace Horo::WorldStreaming
