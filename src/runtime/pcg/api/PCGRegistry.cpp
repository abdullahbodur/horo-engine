#include "Horo/PCG/PCGRegistry.h"

#include "Horo/PCG/PCGErrors.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace Horo::PCG {
    struct PCGRegistrySnapshot::State final {
        PCGRegistryInstanceId instance{};
        std::uint64_t generation{};
        PCGCapabilityProjection capabilities{};
        std::vector<PCGGraphDescriptor> graphs;
        std::vector<PCGNodeRuntimeDescriptor> runtimes;
    };

    namespace {
        constexpr std::uint8_t MaximumCapabilityValue = static_cast<std::uint8_t>(PCGCapability::RenderOutput);

        /** @brief Reports whether one closed capability enumerator is known. */
        [[nodiscard]] constexpr bool IsKnown(const PCGCapability capability) noexcept {
            return static_cast<std::uint8_t>(capability) <= MaximumCapabilityValue;
        }

        /** @brief Reports whether one host profile enumerator is known. */
        [[nodiscard]] constexpr bool IsKnown(const PCGHostProfile profile) noexcept {
            using enum PCGHostProfile;
            return profile == Interactive || profile == Headless || profile == Null;
        }

        /** @brief Reports whether one determinism enumerator is known. */
        [[nodiscard]] constexpr bool IsKnown(const PCGNodeDeterminism determinism) noexcept {
            using enum PCGNodeDeterminism;
            return determinism == PortableDeterministic || determinism == ProfileDeterministic || determinism == BestEffortPreview;
        }

        /** @brief Validates independent registry bounds and dense handle representation. */
        [[nodiscard]] bool HasValidLimits(const PCGRegistryLimits &limits) noexcept {
            constexpr auto MaximumSlots = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + 1ULL;
            return limits.maximumGraphs > 0 && limits.maximumNodeRuntimes > 0 && limits.maximumNodesPerGraph > 0 &&
                   limits.maximumGraphs <= MaximumSlots && limits.maximumNodeRuntimes <= MaximumSlots;
        }

        /** @brief Canonicalizes and validates one detached graph descriptor. */
        [[nodiscard]] Result<void> ValidateGraph(PCGGraphDescriptor &descriptor, const PCGRegistryLimits &limits) {
            if (!descriptor.generation.IsValid() || descriptor.nodes.empty() || descriptor.nodes.size() > limits.maximumNodesPerGraph)
                return Result<void>::Failure(MakeError(PCGErrors::RegistryDescriptorInvalid));
            std::ranges::sort(descriptor.nodes, {}, &PCGGraphNodeDescriptor::node);
            for (std::size_t index = 0; index < descriptor.nodes.size(); ++index) {
                if (!descriptor.nodes[index].node.IsValid() || !descriptor.nodes[index].type.IsValid() ||
                    (index > 0 && descriptor.nodes[index - 1].node == descriptor.nodes[index].node))
                    return Result<void>::Failure(MakeError(PCGErrors::RegistryDescriptorInvalid));
            }
            return Result<void>::Success();
        }

        /** @brief Validates one inert node-runtime descriptor. */
        [[nodiscard]] Result<void> ValidateRuntime(const PCGNodeRuntimeDescriptor &descriptor) {
            if (!descriptor.type.IsValid() || descriptor.contractVersion == 0 || !IsKnown(descriptor.determinism))
                return Result<void>::Failure(MakeError(PCGErrors::RegistryDescriptorInvalid));
            return Result<void>::Success();
        }

        /** @brief Applies the shared lifecycle gate before descriptor-specific mutation validation. */
        template <typename Descriptor, typename Validator>
        [[nodiscard]] Result<void> ValidateMutation(const bool closed, Descriptor &descriptor, Validator &&validator) {
            if (closed)
                return Result<void>::Failure(MakeError(PCGErrors::RegistryClosed));
            return std::forward<Validator>(validator)(descriptor);
        }

        /** @brief Applies graph-specific validation after the common mutation gate. */
        [[nodiscard]] Result<void> ValidateGraphMutation(const bool closed, PCGGraphDescriptor &descriptor,
                                                         const PCGRegistryLimits &limits) {
            return ValidateMutation(closed, descriptor, [&limits](PCGGraphDescriptor &candidate) {
                return ValidateGraph(candidate, limits);
            });
        }

        /** @brief Applies runtime-specific validation after the common mutation gate. */
        [[nodiscard]] Result<void> ValidateRuntimeMutation(const bool closed, PCGNodeRuntimeDescriptor &descriptor) {
            return ValidateMutation(closed, descriptor, ValidateRuntime);
        }

        /** @brief Applies the shared lifecycle and identity gates before removal lookup. */
        template <typename Identity> [[nodiscard]] Result<void> ValidateRemoval(const bool closed, const Identity identity) {
            if (closed)
                return Result<void>::Failure(MakeError(PCGErrors::RegistryClosed));
            if (!identity.IsValid())
                return Result<void>::Failure(MakeError(PCGErrors::IdentityInvalid));
            return Result<void>::Success();
        }

        /** @brief Finds one graph by stable identity in a canonical snapshot. */
        template <typename Range> [[nodiscard]] auto LowerBoundGraph(Range &graphs, const GraphId graph) noexcept {
            return std::ranges::lower_bound(graphs, graph, {}, [](const PCGGraphDescriptor &descriptor) {
                return descriptor.generation.graph;
            });
        }

        /** @brief Finds one runtime by semantic type in a canonical snapshot. */
        template <typename Range> [[nodiscard]] auto LowerBoundRuntime(Range &runtimes, const NodeTypeId type) noexcept {
            return std::ranges::lower_bound(runtimes, type, {}, &PCGNodeRuntimeDescriptor::type);
        }

        /** @brief Validates exact caller and descriptor capability requirements. */
        [[nodiscard]] Result<void> ValidateCapabilities(const PCGCapabilityProjection &projection, const PCGCapabilitySet caller,
                                                        const PCGCapabilitySet descriptor) {
            if (!projection.granted.ContainsAll(caller) || !projection.granted.ContainsAll(descriptor))
                return Result<void>::Failure(MakeError(PCGErrors::UnsupportedCapability));
            return Result<void>::Success();
        }

        /** @brief Reports whether an exact request or graph names an execution admission path. */
        [[nodiscard]] bool HasExecutionCapability(const PCGCapabilitySet capabilities) noexcept {
            using enum PCGCapability;
            return capabilities.Contains(Validation) || capabilities.Contains(OfflineBake) || capabilities.Contains(EditorPreview) ||
                   capabilities.Contains(RuntimeEvaluation) || capabilities.Contains(HybridEvaluation);
        }
    }  // namespace

    /** @copydoc PCGCapabilitySet::Create */
    Result<PCGCapabilitySet> PCGCapabilitySet::Create(const std::span<const PCGCapability> capabilities) {
        std::uint64_t bits{};
        for (const PCGCapability capability : capabilities) {
            if (!IsKnown(capability))
                return Result<PCGCapabilitySet>::Failure(MakeError(PCGErrors::RegistryDescriptorInvalid));
            bits |= std::uint64_t{1} << static_cast<std::uint8_t>(capability);
        }
        return Result<PCGCapabilitySet>::Success(PCGCapabilitySet{bits});
    }

    /** @copydoc PCGCapabilitySet::Contains */
    bool PCGCapabilitySet::Contains(const PCGCapability capability) const noexcept {
        return IsKnown(capability) && (bits_ & (std::uint64_t{1} << static_cast<std::uint8_t>(capability))) != 0;
    }

    /** @copydoc ProjectPCGCapabilities */
    Result<PCGCapabilityProjection> ProjectPCGCapabilities(const PCGHostProfile profile, const PCGCapabilitySet granted) {
        if (!IsKnown(profile))
            return Result<PCGCapabilityProjection>::Failure(MakeError(PCGErrors::RegistryDescriptorInvalid));

        const bool unsupported = (profile == PCGHostProfile::Headless &&
                                  (granted.Contains(PCGCapability::EditorPreview) || granted.Contains(PCGCapability::RenderOutput))) ||
                                 (profile == PCGHostProfile::Null && !granted.IsEmpty());
        if (unsupported)
            return Result<PCGCapabilityProjection>::Failure(MakeError(PCGErrors::UnsupportedCapability));
        return Result<PCGCapabilityProjection>::Success({profile, granted});
    }

    /** @copydoc PCGRegistrySnapshot::IsValid */
    bool PCGRegistrySnapshot::IsValid() const noexcept {
        return state_ != nullptr && state_->generation != 0;
    }

    /** @copydoc PCGRegistrySnapshot::Generation */
    std::uint64_t PCGRegistrySnapshot::Generation() const noexcept {
        return state_ == nullptr ? 0 : state_->generation;
    }

    /** @copydoc PCGRegistrySnapshot::RegistryInstance */
    PCGRegistryInstanceId PCGRegistrySnapshot::RegistryInstance() const noexcept {
        return state_ == nullptr ? PCGRegistryInstanceId{} : state_->instance;
    }

    /** @copydoc PCGRegistrySnapshot::Capabilities */
    const PCGCapabilityProjection &PCGRegistrySnapshot::Capabilities() const noexcept {
        static const PCGCapabilityProjection InvalidProjection{};
        return state_ == nullptr ? InvalidProjection : state_->capabilities;
    }

    /** @copydoc PCGRegistrySnapshot::Graphs */
    std::span<const PCGGraphDescriptor> PCGRegistrySnapshot::Graphs() const noexcept {
        return state_ == nullptr ? std::span<const PCGGraphDescriptor>{} : std::span<const PCGGraphDescriptor>{state_->graphs};
    }

    /** @copydoc PCGRegistrySnapshot::NodeRuntimes */
    std::span<const PCGNodeRuntimeDescriptor> PCGRegistrySnapshot::NodeRuntimes() const noexcept {
        return state_ == nullptr ? std::span<const PCGNodeRuntimeDescriptor>{}
                                 : std::span<const PCGNodeRuntimeDescriptor>{state_->runtimes};
    }

    /** @copydoc PCGRegistrySnapshot::FindGraph */
    Result<PCGGraphHandle> PCGRegistrySnapshot::FindGraph(const GraphId graph) const {
        if (!IsValid() || !graph.IsValid())
            return Result<PCGGraphHandle>::Failure(MakeError(PCGErrors::IdentityInvalid));
        const auto graphs = Graphs();
        const auto found = LowerBoundGraph(graphs, graph);
        if (found == graphs.end() || found->generation.graph != graph)
            return Result<PCGGraphHandle>::Failure(MakeError(PCGErrors::IdentityUnknown));
        const auto slot = static_cast<std::uint32_t>(std::distance(graphs.begin(), found));
        return Result<PCGGraphHandle>::Success({RegistryInstance(), Generation(), slot, found->generation});
    }

    /** @copydoc PCGRegistrySnapshot::QueryGraph */
    Result<PCGGraphHandle> PCGRegistrySnapshot::QueryGraph(const GraphGeneration generation, const PCGCapabilitySet required) const {
        if (!generation.IsValid())
            return Result<PCGGraphHandle>::Failure(MakeError(PCGErrors::IdentityInvalid));
        const auto handle = FindGraph(generation.graph);
        if (handle.HasError())
            return handle;
        const auto graph = Resolve(handle.Value());
        if (graph.HasError())
            return Result<PCGGraphHandle>::Failure(graph.ErrorValue());
        if (graph.Value()->generation.revision != generation.revision)
            return Result<PCGGraphHandle>::Failure(MakeError(PCGErrors::IdentityStale));
        if (!HasExecutionCapability(required) && !HasExecutionCapability(graph.Value()->requiredCapabilities))
            return Result<PCGGraphHandle>::Failure(MakeError(PCGErrors::UnsupportedCapability));
        if (const auto supported = ValidateCapabilities(Capabilities(), required, graph.Value()->requiredCapabilities);
            supported.HasError())
            return Result<PCGGraphHandle>::Failure(supported.ErrorValue());
        for (const PCGGraphNodeDescriptor &node : graph.Value()->nodes) {
            const auto runtime = QueryNodeRuntime(node.type, PCGCapabilitySet::Empty());
            if (runtime.HasError())
                return Result<PCGGraphHandle>::Failure(runtime.ErrorValue());
        }
        return handle;
    }

    /** @copydoc PCGRegistrySnapshot::Resolve(PCGGraphHandle) const */
    Result<const PCGGraphDescriptor *> PCGRegistrySnapshot::Resolve(const PCGGraphHandle handle) const {
        if (!IsValid() || !handle.IsValid() || handle.slot >= Graphs().size())
            return Result<const PCGGraphDescriptor *>::Failure(MakeError(PCGErrors::RegistryHandleInvalid));
        if (handle.registry != RegistryInstance() || handle.registryGeneration != Generation())
            return Result<const PCGGraphDescriptor *>::Failure(MakeError(PCGErrors::RegistryHandleStale));
        const PCGGraphDescriptor &descriptor = Graphs()[handle.slot];
        if (descriptor.generation != handle.graph)
            return Result<const PCGGraphDescriptor *>::Failure(MakeError(PCGErrors::RegistryHandleStale));
        return Result<const PCGGraphDescriptor *>::Success(&descriptor);
    }

    /** @copydoc PCGRegistrySnapshot::QueryNodeRuntime */
    Result<PCGNodeRuntimeHandle> PCGRegistrySnapshot::QueryNodeRuntime(const NodeTypeId type, const PCGCapabilitySet required) const {
        if (!IsValid() || !type.IsValid())
            return Result<PCGNodeRuntimeHandle>::Failure(MakeError(PCGErrors::IdentityInvalid));
        const auto runtimes = NodeRuntimes();
        const auto found = LowerBoundRuntime(runtimes, type);
        if (found == runtimes.end() || found->type != type)
            return Result<PCGNodeRuntimeHandle>::Failure(MakeError(PCGErrors::RuntimeUnavailable));
        if (const auto supported = ValidateCapabilities(Capabilities(), required, found->requiredCapabilities); supported.HasError())
            return Result<PCGNodeRuntimeHandle>::Failure(supported.ErrorValue());
        const auto slot = static_cast<std::uint32_t>(std::distance(runtimes.begin(), found));
        return Result<PCGNodeRuntimeHandle>::Success({RegistryInstance(), Generation(), slot, found->type, found->contractVersion});
    }

    /** @copydoc PCGRegistrySnapshot::Resolve(PCGNodeRuntimeHandle) const */
    Result<const PCGNodeRuntimeDescriptor *> PCGRegistrySnapshot::Resolve(const PCGNodeRuntimeHandle handle) const {
        if (!IsValid() || !handle.IsValid() || handle.slot >= NodeRuntimes().size())
            return Result<const PCGNodeRuntimeDescriptor *>::Failure(MakeError(PCGErrors::RegistryHandleInvalid));
        if (handle.registry != RegistryInstance() || handle.registryGeneration != Generation())
            return Result<const PCGNodeRuntimeDescriptor *>::Failure(MakeError(PCGErrors::RegistryHandleStale));
        const PCGNodeRuntimeDescriptor &descriptor = NodeRuntimes()[handle.slot];
        if (descriptor.type != handle.type || descriptor.contractVersion != handle.contractVersion)
            return Result<const PCGNodeRuntimeDescriptor *>::Failure(MakeError(PCGErrors::RegistryHandleStale));
        return Result<const PCGNodeRuntimeDescriptor *>::Success(&descriptor);
    }

    PCGRegistry::PCGRegistry(const PCGRegistryInstanceId instance, PCGCapabilityProjection capabilities, const PCGRegistryLimits limits,
                             std::shared_ptr<const PCGRegistrySnapshot::State> state) noexcept
        : instance_(instance), capabilities_(capabilities), limits_(limits), state_(std::move(state)) {}

    PCGRegistry::~PCGRegistry() {
        Close();
    }

    /** @copydoc PCGRegistry::Create */
    Result<PCGRegistry> PCGRegistry::Create(const PCGRegistryInstanceId instance, PCGCapabilityProjection capabilities,
                                            const PCGRegistryLimits limits) {
        const auto validated = ProjectPCGCapabilities(capabilities.profile, capabilities.granted);
        if (!instance.IsValid() || validated.HasError() || !HasValidLimits(limits))
            return Result<PCGRegistry>::Failure(validated.HasError() ? validated.ErrorValue()
                                                                     : MakeError(PCGErrors::RegistryDescriptorInvalid));
        auto state = std::make_shared<PCGRegistrySnapshot::State>();
        state->instance = instance;
        state->generation = 1;
        state->capabilities = capabilities;
        return Result<PCGRegistry>::Success(PCGRegistry{instance, capabilities, limits, std::move(state)});
    }

    /** @copydoc PCGRegistry::RegisterGraph */
    Result<std::uint64_t> PCGRegistry::RegisterGraph(PCGGraphDescriptor descriptor) {
        if (const auto valid = ValidateGraphMutation(closed_, descriptor, limits_); valid.HasError())
            return Result<std::uint64_t>::Failure(valid.ErrorValue());
        if (state_->graphs.size() >= limits_.maximumGraphs)
            return Result<std::uint64_t>::Failure(MakeError(PCGErrors::RegistryCapacityExceeded));
        const auto insertion = LowerBoundGraph(state_->graphs, descriptor.generation.graph);
        if (insertion != state_->graphs.end() && insertion->generation.graph == descriptor.generation.graph)
            return Result<std::uint64_t>::Failure(MakeError(PCGErrors::RegistryDuplicate));
        const auto offset = std::distance(state_->graphs.begin(), insertion);
        auto graphs = state_->graphs;
        graphs.insert(graphs.begin() + offset, std::move(descriptor));
        return Publish(std::move(graphs), state_->runtimes);
    }

    /** @copydoc PCGRegistry::ReplaceGraph */
    Result<std::uint64_t> PCGRegistry::ReplaceGraph(PCGGraphDescriptor descriptor) {
        if (const auto valid = ValidateGraphMutation(closed_, descriptor, limits_); valid.HasError())
            return Result<std::uint64_t>::Failure(valid.ErrorValue());
        auto graphs = state_->graphs;
        const auto found = LowerBoundGraph(graphs, descriptor.generation.graph);
        if (found == graphs.end() || found->generation.graph != descriptor.generation.graph)
            return Result<std::uint64_t>::Failure(MakeError(PCGErrors::IdentityUnknown));
        if (descriptor.generation.revision <= found->generation.revision)
            return Result<std::uint64_t>::Failure(MakeError(PCGErrors::IdentityStale));
        *found = std::move(descriptor);
        return Publish(std::move(graphs), state_->runtimes);
    }

    /** @copydoc PCGRegistry::UnregisterGraph */
    Result<bool> PCGRegistry::UnregisterGraph(const GraphId graph) {
        if (const auto valid = ValidateRemoval(closed_, graph); valid.HasError())
            return Result<bool>::Failure(valid.ErrorValue());
        auto graphs = state_->graphs;
        const auto found = LowerBoundGraph(graphs, graph);
        if (found == graphs.end() || found->generation.graph != graph)
            return Result<bool>::Success(false);
        graphs.erase(found);
        const auto published = Publish(std::move(graphs), state_->runtimes);
        return published.HasError() ? Result<bool>::Failure(published.ErrorValue()) : Result<bool>::Success(true);
    }

    /** @copydoc PCGRegistry::RegisterNodeRuntime */
    Result<std::uint64_t> PCGRegistry::RegisterNodeRuntime(PCGNodeRuntimeDescriptor descriptor) {
        if (const auto valid = ValidateRuntimeMutation(closed_, descriptor); valid.HasError())
            return Result<std::uint64_t>::Failure(valid.ErrorValue());
        if (state_->runtimes.size() >= limits_.maximumNodeRuntimes)
            return Result<std::uint64_t>::Failure(MakeError(PCGErrors::RegistryCapacityExceeded));
        const auto insertion = LowerBoundRuntime(state_->runtimes, descriptor.type);
        if (insertion != state_->runtimes.end() && insertion->type == descriptor.type)
            return Result<std::uint64_t>::Failure(MakeError(PCGErrors::RegistryDuplicate));
        const auto offset = std::distance(state_->runtimes.begin(), insertion);
        auto runtimes = state_->runtimes;
        runtimes.insert(runtimes.begin() + offset, descriptor);
        return Publish(state_->graphs, std::move(runtimes));
    }

    /** @copydoc PCGRegistry::ReplaceNodeRuntime */
    Result<std::uint64_t> PCGRegistry::ReplaceNodeRuntime(PCGNodeRuntimeDescriptor descriptor) {
        if (const auto valid = ValidateRuntimeMutation(closed_, descriptor); valid.HasError())
            return Result<std::uint64_t>::Failure(valid.ErrorValue());
        auto runtimes = state_->runtimes;
        const auto found = LowerBoundRuntime(runtimes, descriptor.type);
        if (found == runtimes.end() || found->type != descriptor.type)
            return Result<std::uint64_t>::Failure(MakeError(PCGErrors::RuntimeUnavailable));
        if (descriptor.contractVersion <= found->contractVersion)
            return Result<std::uint64_t>::Failure(MakeError(PCGErrors::IdentityStale));
        *found = descriptor;
        return Publish(state_->graphs, std::move(runtimes));
    }

    /** @copydoc PCGRegistry::UnregisterNodeRuntime */
    Result<bool> PCGRegistry::UnregisterNodeRuntime(const NodeTypeId type) {
        if (const auto valid = ValidateRemoval(closed_, type); valid.HasError())
            return Result<bool>::Failure(valid.ErrorValue());
        auto runtimes = state_->runtimes;
        const auto found = LowerBoundRuntime(runtimes, type);
        if (found == runtimes.end() || found->type != type)
            return Result<bool>::Success(false);
        runtimes.erase(found);
        const auto published = Publish(state_->graphs, std::move(runtimes));
        return published.HasError() ? Result<bool>::Failure(published.ErrorValue()) : Result<bool>::Success(true);
    }

    /** @copydoc PCGRegistry::Snapshot */
    Result<PCGRegistrySnapshot> PCGRegistry::Snapshot() const {
        if (closed_)
            return Result<PCGRegistrySnapshot>::Failure(MakeError(PCGErrors::RegistryClosed));
        return Result<PCGRegistrySnapshot>::Success(PCGRegistrySnapshot{state_});
    }

    /** @copydoc PCGRegistry::Close */
    void PCGRegistry::Close() noexcept {
        if (closed_)
            return;
        closed_ = true;
        state_.reset();
    }

    Result<std::uint64_t> PCGRegistry::Publish(std::vector<PCGGraphDescriptor> graphs, std::vector<PCGNodeRuntimeDescriptor> runtimes) {
        if (state_->generation == std::numeric_limits<std::uint64_t>::max())
            return Result<std::uint64_t>::Failure(MakeError(PCGErrors::RegistryGenerationExhausted));
        auto state = std::make_shared<PCGRegistrySnapshot::State>();
        state->instance = instance_;
        state->generation = state_->generation + 1;
        state->capabilities = capabilities_;
        state->graphs = std::move(graphs);
        state->runtimes = std::move(runtimes);
        state_ = std::move(state);
        return Result<std::uint64_t>::Success(state_->generation);
    }
}  // namespace Horo::PCG
