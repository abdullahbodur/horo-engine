#include "Horo/Runtime/Render/RenderGraph.h"
#include "Horo/Runtime/Render/RenderGraphErrors.h"
#include "RenderGraphValidationInternal.h"

#include <algorithm>
#include <format>
#include <functional>
#include <limits>
#include <new>
#include <queue>
#include <string>
#include <unordered_set>
#include <utility>

namespace Horo::Render {
    namespace {
        [[nodiscard]] constexpr bool Reads(const RenderGraphAccess access) noexcept {
            return access == RenderGraphAccess::Read || access == RenderGraphAccess::ReadWrite;
        }

        [[nodiscard]] constexpr bool Writes(const RenderGraphAccess access) noexcept {
            return access == RenderGraphAccess::Write || access == RenderGraphAccess::ReadWrite;
        }

        struct GraphCompileRecords {
            std::vector<RenderGraphPassRef> orderedPasses;
            std::vector<RenderGraphPassDisposition> passDispositions;
        };

        class GraphCompiler final {
        public:
            explicit GraphCompiler(const RenderGraph &graph)
                : graph_(graph), passCount_(graph.Passes().size()), successors_(passCount_), predecessors_(passCount_),
                  dataPredecessors_(passCount_), dataSuccessors_(passCount_), usagesByPass_(passCount_), indegrees_(passCount_),
                  live_(passCount_), reasons_(passCount_) {}

            [[nodiscard]] Result<GraphCompileRecords> Run() {
                if (const Result<void> structure = ValidateStructure(); structure.HasError()) {
                    return Result<GraphCompileRecords>::Failure(structure.ErrorValue());
                }
                if (const Result<void> usages = IndexUsages(); usages.HasError()) {
                    return Result<GraphCompileRecords>::Failure(usages.ErrorValue());
                }
                if (const Result<void> dependencies = BuildDependencies(); dependencies.HasError()) {
                    return Result<GraphCompileRecords>::Failure(dependencies.ErrorValue());
                }
                if (const Result<void> ordering = DetermineOrder(); ordering.HasError()) {
                    return Result<GraphCompileRecords>::Failure(ordering.ErrorValue());
                }
                if (const Result<void> resources = ValidateResourcesAndSeedLiveness(); resources.HasError()) {
                    return Result<GraphCompileRecords>::Failure(resources.ErrorValue());
                }
                PropagateLiveness();
                return Result<GraphCompileRecords>::Success(BuildRecords());
            }

        private:
            [[nodiscard]] Result<void> ValidateStructure() const {
                if (!HasValidCounts() || !ArePassesValid() || !AreResourcesValid()) {
                    return Result<void>::Failure(MakeError(RenderGraphErrors::InvalidGraph));
                }
                return Result<void>::Success();
            }

            [[nodiscard]] bool HasValidCounts() const noexcept {
                return graph_.Owner().IsValid() && graph_.Limits().IsValid() && passCount_ > 0 && passCount_ <= graph_.Limits().maxPasses &&
                       graph_.Resources().size() <= graph_.Limits().maxResources && graph_.Usages().size() <= graph_.Limits().maxUsages &&
                       graph_.Dependencies().size() <= graph_.Limits().maxDependencies &&
                       graph_.Exports().size() <= graph_.Resources().size();
            }

            [[nodiscard]] bool ArePassesValid() const noexcept {
                for (std::size_t index = 0; index < passCount_; ++index) {
                    const RenderGraphPassRef expected{graph_.Owner(), RenderPassId{static_cast<std::uint32_t>(index + 1)}};
                    if (graph_.Passes()[index].reference != expected || !Detail::IsKnown(graph_.Passes()[index].cullPolicy)) {
                        return false;
                    }
                }
                return true;
            }

            [[nodiscard]] bool AreResourcesValid() const noexcept {
                for (std::size_t index = 0; index < graph_.Resources().size(); ++index) {
                    const RenderGraphResourceId expected{graph_.Owner(), static_cast<std::uint32_t>(index + 1)};
                    if (graph_.Resources()[index].id != expected) {
                        return false;
                    }
                }
                return true;
            }

            [[nodiscard]] Result<void> IndexUsages() {
                for (const RenderGraphResourceUsage &usage : graph_.Usages()) {
                    if (!IsUsageValid(usage)) {
                        return Result<void>::Failure(MakeError(RenderGraphErrors::InvalidUsage));
                    }
                    usagesByPass_[PassIndex(usage.pass)].push_back(&usage);
                }
                return Result<void>::Success();
            }

            [[nodiscard]] Result<void> BuildDependencies() {
                std::unordered_set<std::uint64_t> edges;
                edges.reserve(graph_.Dependencies().size());
                for (const RenderGraphDependency &dependency : graph_.Dependencies()) {
                    if (!IsPassReferenceValid(dependency.before) || !IsPassReferenceValid(dependency.after) ||
                        dependency.before == dependency.after || !Detail::IsKnown(dependency.kind)) {
                        return Result<void>::Failure(MakeError(RenderGraphErrors::InvalidDependency));
                    }
                    const std::size_t before = PassIndex(dependency.before);
                    const std::size_t after = PassIndex(dependency.after);
                    if (const std::uint64_t edge =
                            (static_cast<std::uint64_t>(before) * passCount_ + after) * 3U + static_cast<std::uint8_t>(dependency.kind);
                        !edges.emplace(edge).second) {
                        return Result<void>::Failure(
                            MakeError(RenderGraphErrors::InvalidDependency,
                                      std::format("Pass {} -> pass {} repeats dependency kind {}.", dependency.before.id.value,
                                                  dependency.after.id.value, static_cast<std::uint8_t>(dependency.kind))));
                    }
                    successors_[before].push_back(after);
                    predecessors_[after].push_back(before);
                    ++indegrees_[after];
                    if (dependency.kind == RenderGraphDependencyKind::ExternalSynchronization) {
                        MarkLive(before, RenderGraphPassDispositionReason::ExternalSynchronization);
                        MarkLive(after, RenderGraphPassDispositionReason::ExternalSynchronization);
                    }
                }
                return Result<void>::Success();
            }

            [[nodiscard]] Result<void> DetermineOrder() {
                std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<>> ready;
                for (std::size_t index = 0; index < passCount_; ++index) {
                    if (indegrees_[index] == 0) {
                        ready.push(index);
                    }
                }
                topologicalOrder_.reserve(passCount_);
                while (!ready.empty()) {
                    const std::size_t pass = ready.top();
                    ready.pop();
                    topologicalOrder_.push_back(pass);
                    for (const std::size_t successor : successors_[pass]) {
                        if (--indegrees_[successor] == 0) {
                            ready.push(successor);
                        }
                    }
                }
                if (topologicalOrder_.size() != passCount_) {
                    return Result<void>::Failure(MakeError(RenderGraphErrors::DependencyCycle, DescribeCycle()));
                }
                return Result<void>::Success();
            }

            [[nodiscard]] std::string DescribeCycle() const {
                constexpr std::size_t Unseen = std::numeric_limits<std::size_t>::max();
                std::vector positions(passCount_, Unseen);
                std::vector<std::size_t> path;
                auto current = static_cast<std::size_t>(std::ranges::find_if(indegrees_, [](const std::size_t degree) {
                    return degree != 0;
                }) - indegrees_.begin());
                while (positions[current] == Unseen) {
                    positions[current] = path.size();
                    path.push_back(current);
                    current = SmallestRemainingPredecessor(current);
                }

                std::vector<std::size_t> cycle{path.begin() + static_cast<std::ptrdiff_t>(positions[current]), path.end()};
                std::ranges::sort(cycle);
                std::string message{"Dependency cycle contains pass IDs"};
                for (const std::size_t pass : cycle) {
                    message += std::format(" {}", graph_.Passes()[pass].reference.id.value);
                }
                return std::format("{}.", message);
            }

            [[nodiscard]] std::size_t SmallestRemainingPredecessor(const std::size_t pass) const noexcept {
                std::size_t selected = passCount_;
                for (const std::size_t predecessor : predecessors_[pass]) {
                    if (indegrees_[predecessor] != 0) {
                        selected = std::min(selected, predecessor);
                    }
                }
                return selected;
            }

            [[nodiscard]] Result<void> ValidateResourcesAndSeedLiveness() {
                constexpr std::size_t NoPass = std::numeric_limits<std::size_t>::max();
                std::vector lastWriter(graph_.Resources().size(), NoPass);
                for (const std::size_t pass : topologicalOrder_) {
                    if (const Result<void> reads = ValidatePassReads(pass, lastWriter); reads.HasError()) {
                        return reads;
                    }
                    RecordPassWrites(pass, lastWriter);
                }

                if (const Result<void> exports = SeedExportLiveness(lastWriter); exports.HasError()) {
                    return exports;
                }
                SeedConservativePolicyLiveness();
                return Result<void>::Success();
            }

            [[nodiscard]] Result<void> ValidatePassReads(const std::size_t pass, const std::vector<std::size_t> &lastWriter) {
                constexpr std::size_t NoPass = std::numeric_limits<std::size_t>::max();
                for (const RenderGraphResourceUsage *usageRecord : usagesByPass_[pass]) {
                    const RenderGraphResourceUsage &usage = *usageRecord;
                    if (!Reads(usage.access)) {
                        continue;
                    }
                    const std::size_t resource = ResourceIndex(usage.resource);
                    if (lastWriter[resource] == NoPass &&
                        graph_.Resources()[resource].resourceClass == RenderGraphResourceClass::Transient) {
                        return Result<void>::Failure(MakeError(RenderGraphErrors::ReadBeforeWrite,
                                                               std::format("Pass {} reads transient resource {} before an ordered write.",
                                                                           usage.pass.id.value, usage.resource.value)));
                    }
                    RecordDataPredecessor(pass, lastWriter[resource]);
                }
                return Result<void>::Success();
            }

            void RecordPassWrites(const std::size_t pass, std::vector<std::size_t> &lastWriter) {
                for (const RenderGraphResourceUsage *usageRecord : usagesByPass_[pass]) {
                    const RenderGraphResourceUsage &usage = *usageRecord;
                    if (!Writes(usage.access)) {
                        continue;
                    }
                    const std::size_t resource = ResourceIndex(usage.resource);
                    lastWriter[resource] = pass;
                    if (graph_.Resources()[resource].resourceClass != RenderGraphResourceClass::Transient) {
                        MarkLive(pass, RenderGraphPassDispositionReason::ImportedResourceMutation);
                    }
                }
            }

            [[nodiscard]] Result<void> SeedExportLiveness(const std::vector<std::size_t> &lastWriter) {
                constexpr std::size_t NoPass = std::numeric_limits<std::size_t>::max();
                for (const RenderGraphResourceExport &exported : graph_.Exports()) {
                    if (!IsResourceReferenceValid(exported.resource)) {
                        return Result<void>::Failure(MakeError(RenderGraphErrors::InvalidExport));
                    }
                    const std::size_t resource = ResourceIndex(exported.resource);
                    if (lastWriter[resource] != NoPass) {
                        MarkLive(lastWriter[resource], RenderGraphPassDispositionReason::ExportedOutput);
                    } else if (graph_.Resources()[resource].resourceClass == RenderGraphResourceClass::Transient) {
                        return Result<void>::Failure(
                            MakeError(RenderGraphErrors::ReadBeforeWrite, "An exported transient resource has no ordered writer."));
                    }
                }
                return Result<void>::Success();
            }

            void SeedConservativePolicyLiveness() noexcept {
                for (std::size_t pass = 0; pass < passCount_; ++pass) {
                    if (graph_.Passes()[pass].cullPolicy == RenderGraphPassCullPolicy::ConservativeKeep) {
                        MarkLive(pass, RenderGraphPassDispositionReason::ConservativePolicy);
                    }
                }
            }

            void RecordDataPredecessor(const std::size_t pass, const std::size_t predecessor) {
                if (predecessor == std::numeric_limits<std::size_t>::max() || predecessor == pass) {
                    return;
                }
                dataPredecessors_[pass].push_back(predecessor);
                dataSuccessors_[predecessor].push_back(pass);
            }

            void PropagateLiveness() {
                for (auto position = topologicalOrder_.rbegin(); position != topologicalOrder_.rend(); ++position) {
                    const std::size_t pass = *position;
                    if (live_[pass] == 0) {
                        continue;
                    }
                    for (const std::size_t predecessor : dataPredecessors_[pass]) {
                        MarkLive(predecessor, RenderGraphPassDispositionReason::RequiredResourceProducer);
                    }
                    for (const std::size_t predecessor : predecessors_[pass]) {
                        MarkLive(predecessor, RenderGraphPassDispositionReason::RequiredDependency);
                    }
                }
            }

            void MarkLive(const std::size_t pass, const RenderGraphPassDispositionReason reason) noexcept {
                if (live_[pass] == 0) {
                    live_[pass] = 1;
                    reasons_[pass] = reason;
                }
            }

            [[nodiscard]] GraphCompileRecords BuildRecords() const {
                GraphCompileRecords records;
                records.orderedPasses.reserve(passCount_);
                records.passDispositions.reserve(passCount_);
                for (const std::size_t pass : topologicalOrder_) {
                    if (live_[pass] != 0) {
                        records.orderedPasses.push_back(graph_.Passes()[pass].reference);
                    }
                }
                for (std::size_t pass = 0; pass < passCount_; ++pass) {
                    const bool retained = live_[pass] != 0;
                    records.passDispositions.emplace_back(graph_.Passes()[pass].reference,
                                                          retained ? RenderGraphPassDispositionKind::Retained
                                                                   : RenderGraphPassDispositionKind::Culled,
                                                          retained ? reasons_[pass] : CullReason(pass));
                }
                return records;
            }

            [[nodiscard]] RenderGraphPassDispositionReason CullReason(const std::size_t pass) const noexcept {
                if (const auto isCulled = [this](const std::size_t successor) {
                    return live_[successor] == 0;
                }; std::ranges::any_of(successors_[pass], isCulled) || std::ranges::any_of(dataSuccessors_[pass], isCulled)) {
                    return RenderGraphPassDispositionReason::OnlyRequiredByCulledPasses;
                }
                return RenderGraphPassDispositionReason::UnusedTransientOutputs;
            }

            [[nodiscard]] bool IsPassReferenceValid(const RenderGraphPassRef reference) const noexcept {
                return reference.owner == graph_.Owner() && reference.id.value > 0 && reference.id.value <= passCount_;
            }

            [[nodiscard]] bool IsResourceReferenceValid(const RenderGraphResourceId resource) const noexcept {
                return resource.owner == graph_.Owner() && resource.value > 0 && resource.value <= graph_.Resources().size();
            }

            [[nodiscard]] bool IsUsageValid(const RenderGraphResourceUsage &usage) const noexcept {
                return IsPassReferenceValid(usage.pass) && IsResourceReferenceValid(usage.resource) && Detail::IsKnown(usage.access) &&
                       Detail::IsKnown(usage.kind);
            }

            [[nodiscard]] static std::size_t PassIndex(const RenderGraphPassRef pass) noexcept {
                return pass.id.value - 1;
            }

            [[nodiscard]] static std::size_t ResourceIndex(const RenderGraphResourceId resource) noexcept {
                return resource.value - 1;
            }

            const RenderGraph &graph_;
            std::size_t passCount_;
            std::vector<std::vector<std::size_t>> successors_;
            std::vector<std::vector<std::size_t>> predecessors_;
            std::vector<std::vector<std::size_t>> dataPredecessors_;
            std::vector<std::vector<std::size_t>> dataSuccessors_;
            std::vector<std::vector<const RenderGraphResourceUsage *>> usagesByPass_;
            std::vector<std::size_t> indegrees_;
            std::vector<std::size_t> topologicalOrder_;
            std::vector<std::uint8_t> live_;
            std::vector<RenderGraphPassDispositionReason> reasons_;
        };
    }  // namespace

    /** @copydoc RenderGraphSchedule::RenderGraphSchedule */
    RenderGraphSchedule::RenderGraphSchedule(const RenderGraphOwnerId owner, std::vector<RenderGraphPassRef> orderedPasses,
                                             std::vector<RenderGraphPassDisposition> passDispositions) noexcept
        : owner_(owner), orderedPasses_(std::move(orderedPasses)), passDispositions_(std::move(passDispositions)) {}

    /** @copydoc RenderGraphSchedule::RenderGraphSchedule(RenderGraphSchedule &&) */
    RenderGraphSchedule::RenderGraphSchedule(RenderGraphSchedule &&other) noexcept
        : owner_(std::exchange(other.owner_, {})), orderedPasses_(std::exchange(other.orderedPasses_, {})),
          passDispositions_(std::exchange(other.passDispositions_, {})) {}

    /** @copydoc RenderGraphSchedule::operator= */
    RenderGraphSchedule &RenderGraphSchedule::operator=(RenderGraphSchedule &&other) noexcept {
        if (this != &other) {
            owner_ = std::exchange(other.owner_, {});
            orderedPasses_ = std::exchange(other.orderedPasses_, {});
            passDispositions_ = std::exchange(other.passDispositions_, {});
        }
        return *this;
    }

    /** @copydoc RenderGraphSchedule::Owner */
    RenderGraphOwnerId RenderGraphSchedule::Owner() const noexcept {
        return owner_;
    }

    /** @copydoc RenderGraphSchedule::OrderedPasses */
    std::span<const RenderGraphPassRef> RenderGraphSchedule::OrderedPasses() const noexcept {
        return orderedPasses_;
    }

    /** @copydoc RenderGraphSchedule::PassDispositions */
    std::span<const RenderGraphPassDisposition> RenderGraphSchedule::PassDispositions() const noexcept {
        return passDispositions_;
    }

    /** @copydoc CompileRenderGraph */
    Result<RenderGraphSchedule> CompileRenderGraph(const RenderGraph &graph) {
        try {
            auto compiled = GraphCompiler{graph}.Run();
            if (compiled.HasError()) {
                return Result<RenderGraphSchedule>::Failure(compiled.ErrorValue());
            }
            GraphCompileRecords records = std::move(compiled).Value();
            return Result<RenderGraphSchedule>::Success(
                RenderGraphSchedule{graph.Owner(), std::move(records.orderedPasses), std::move(records.passDispositions)});
        } catch (const std::bad_alloc &) {
            return Result<RenderGraphSchedule>::Failure(
                MakeError(RenderGraphErrors::AllocationFailed, "Render graph compilation storage allocation failed."));
        }
    }
}  // namespace Horo::Render
