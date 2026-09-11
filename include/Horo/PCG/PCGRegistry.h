#pragma once

/**
 * @file PCGRegistry.h
 * @brief Bounded host-composed PCG graph/runtime registries and immutable capability queries.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/PCG/PCGIdentity.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace Horo::PCG {
    /** @brief Stable semantic identity of one host-composed PCG node type. */
    using NodeTypeId = PcgStableIdentity<struct NodeTypeIdentityTag>;

    /** @brief Closed host composition selected before any PCG registry publication. */
    enum class PCGHostProfile : std::uint8_t {
        Interactive,
        Headless,
        Null
    };

    /** @brief Closed execution and output capabilities projected into PCG by the host. */
    enum class PCGCapability : std::uint8_t {
        Validation,
        OfflineBake,
        EditorPreview,
        RuntimeEvaluation,
        HybridEvaluation,
        SceneOutput,
        TerrainOutput,
        FoliageOutput,
        PhysicsOutput,
        NavigationOutput,
        RenderOutput
    };

    /** @brief Compact immutable set of known PCG capabilities. */
    class PCGCapabilitySet final {
    public:
        PCGCapabilitySet() noexcept = default;

        /** @brief Creates an empty capability set. @return Empty set. */
        [[nodiscard]] static constexpr PCGCapabilitySet Empty() noexcept {
            return {};
        }

        /** @brief Validates and canonicalizes capabilities. @param capabilities Candidate capabilities.
         * @return Exact set or PCGErrors::RegistryDescriptorInvalid for an unknown value.
         */
        [[nodiscard]] static Result<PCGCapabilitySet> Create(std::span<const PCGCapability> capabilities);

        /** @brief Tests one capability. @param capability Known capability. @return True only when present. */
        [[nodiscard]] bool Contains(PCGCapability capability) const noexcept;

        /** @brief Tests whether every capability in another set is present. @param required Required set.
         * @return True when this set is a superset.
         */
        [[nodiscard]] constexpr bool ContainsAll(const PCGCapabilitySet required) const noexcept {
            return (bits_ & required.bits_) == required.bits_;
        }

        /** @brief Reports whether no capabilities are present. @return True for the empty set. */
        [[nodiscard]] constexpr bool IsEmpty() const noexcept {
            return bits_ == 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const PCGCapabilitySet &) const noexcept = default;

    private:
        explicit constexpr PCGCapabilitySet(const std::uint64_t bits) noexcept : bits_(bits) {}

        std::uint64_t bits_{};
    };

    /** @brief Validated host profile and the exact capabilities it grants without implicit additions. */
    struct PCGCapabilityProjection final {
        PCGHostProfile profile{PCGHostProfile::Null}; /**< Explicit interactive, headless, or null composition. */
        PCGCapabilitySet granted{};                   /**< Exact host grants; never inferred from runtime availability. */
    };

    /** @brief Validates that requested grants are legal for one explicit host profile.
     * @param profile Composition profile chosen by the host.
     * @param granted Exact capabilities explicitly installed by composition.
     * @return Immutable projection or a typed unsupported/invalid failure.
     */
    [[nodiscard]] Result<PCGCapabilityProjection> ProjectPCGCapabilities(PCGHostProfile profile, PCGCapabilitySet granted);

    /** @brief Determinism guarantee advertised by one inert node-runtime descriptor. */
    enum class PCGNodeDeterminism : std::uint8_t {
        PortableDeterministic,
        ProfileDeterministic,
        BestEffortPreview
    };

    /** @brief One stable authored node and its exact semantic node type. */
    struct PCGGraphNodeDescriptor final {
        NodeId node{};     /**< Stable node identity inside the graph. */
        NodeTypeId type{}; /**< Stable semantic type resolved through the runtime catalog. */
        [[nodiscard]] constexpr auto operator<=>(const PCGGraphNodeDescriptor &) const noexcept = default;
    };

    /** @brief Inert graph metadata copied by the host-owned registry. */
    struct PCGGraphDescriptor final {
        GraphGeneration generation{};              /**< Exact graph identity and durable revision. */
        std::vector<PCGGraphNodeDescriptor> nodes; /**< Complete node inventory; callbacks and pointers are forbidden. */
        PCGCapabilitySet requiredCapabilities{};   /**< Exact execution/output capabilities required for admission. */
    };

    /** @brief Inert node-runtime metadata; executable callbacks remain in later private runtime contracts. */
    struct PCGNodeRuntimeDescriptor final {
        NodeTypeId type{};               /**< Stable semantic node type. */
        std::uint32_t contractVersion{}; /**< Non-zero monotonically increasing runtime contract. */
        PCGNodeDeterminism determinism{PCGNodeDeterminism::PortableDeterministic}; /**< Strongest declared behavior. */
        PCGCapabilitySet requiredCapabilities{};                                   /**< Exact host capabilities required by this runtime. */
    };

    /** @brief Independent hard bounds for one registry composition. */
    struct PCGRegistryLimits final {
        std::size_t maximumGraphs{256};          /**< Maximum graph descriptors in one publication. */
        std::size_t maximumNodeRuntimes{1'024};  /**< Maximum semantic node runtimes in one publication. */
        std::size_t maximumNodesPerGraph{1'024}; /**< Maximum copied node descriptors in one graph. */
    };

    /** @brief Process-local graph handle valid only in its issuing immutable snapshot generation. */
    struct PCGGraphHandle final {
        std::uint64_t registryGeneration{}; /**< Exact snapshot generation. */
        std::uint32_t slot{};               /**< Dense immutable snapshot slot. */
        GraphGeneration graph{};            /**< Exact durable graph generation occupying the slot. */

        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return registryGeneration != 0 && graph.IsValid();
        }

        [[nodiscard]] constexpr auto operator<=>(const PCGGraphHandle &) const noexcept = default;
    };

    /** @brief Process-local node-runtime handle valid only in its issuing immutable snapshot generation. */
    struct PCGNodeRuntimeHandle final {
        std::uint64_t registryGeneration{}; /**< Exact snapshot generation. */
        std::uint32_t slot{};               /**< Dense immutable snapshot slot. */
        NodeTypeId type{};                  /**< Exact semantic node type occupying the slot. */
        std::uint32_t contractVersion{};    /**< Exact runtime contract occupying the slot. */

        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return registryGeneration != 0 && type.IsValid() && contractVersion != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const PCGNodeRuntimeHandle &) const noexcept = default;
    };

    /** @brief Immutable owning graph/runtime query view safe after replacement or registry shutdown. */
    class PCGRegistrySnapshot final {
    public:
        struct State;
        PCGRegistrySnapshot() = default;

        /** @brief Reports whether this snapshot was issued by a live registry. @return Snapshot validity. */
        [[nodiscard]] bool IsValid() const noexcept;
        /** @brief Returns the exact registry publication generation. @return Zero for a default snapshot. */
        [[nodiscard]] std::uint64_t Generation() const noexcept;
        /** @brief Returns the immutable host capability projection. @return Snapshot-owned projection. */
        [[nodiscard]] const PCGCapabilityProjection &Capabilities() const noexcept;
        /** @brief Returns graphs in stable graph-identity order. @return Borrowed immutable descriptors. */
        [[nodiscard]] std::span<const PCGGraphDescriptor> Graphs() const noexcept;
        /** @brief Returns runtimes in stable node-type order. @return Borrowed immutable descriptors. */
        [[nodiscard]] std::span<const PCGNodeRuntimeDescriptor> NodeRuntimes() const noexcept;

        /** @brief Finds current metadata without selecting an execution path. @param graph Stable graph identity.
         * @return Generation-safe handle or IdentityInvalid/IdentityUnknown.
         */
        [[nodiscard]] Result<PCGGraphHandle> FindGraph(GraphId graph) const;
        /** @brief Resolves an exact graph for execution without fallback. @param generation Exact graph generation.
         * @param required Exact caller-required capabilities in addition to descriptor requirements.
         * @return Generation-safe graph handle or typed stale/runtime/capability failure.
         */
        [[nodiscard]] Result<PCGGraphHandle> QueryGraph(GraphGeneration generation, PCGCapabilitySet required) const;
        /** @brief Resolves one graph handle against this exact snapshot. @param handle Issued handle.
         * @return Borrowed descriptor or typed invalid/stale failure.
         */
        [[nodiscard]] Result<const PCGGraphDescriptor *> Resolve(PCGGraphHandle handle) const;

        /** @brief Finds one exact node runtime without fallback. @param type Stable semantic node type.
         * @param required Exact caller-required capabilities.
         * @return Generation-safe runtime handle or typed unavailable/capability failure.
         */
        [[nodiscard]] Result<PCGNodeRuntimeHandle> QueryNodeRuntime(NodeTypeId type, PCGCapabilitySet required) const;
        /** @brief Resolves one runtime handle against this exact snapshot. @param handle Issued handle.
         * @return Borrowed descriptor or typed invalid/stale failure.
         */
        [[nodiscard]] Result<const PCGNodeRuntimeDescriptor *> Resolve(PCGNodeRuntimeHandle handle) const;

    private:
        friend class PCGRegistry;

        explicit PCGRegistrySnapshot(std::shared_ptr<const State> state) noexcept : state_(std::move(state)) {}

        std::shared_ptr<const State> state_;
    };

    /** @brief Host-owned bounded composition registry; mutation is restricted to its composition owner thread. */
    class PCGRegistry final {
    public:
        PCGRegistry() = delete;
        ~PCGRegistry();
        PCGRegistry(PCGRegistry &&) noexcept = default;
        PCGRegistry &operator=(PCGRegistry &&) noexcept = default;
        PCGRegistry(const PCGRegistry &) = delete;
        PCGRegistry &operator=(const PCGRegistry &) = delete;

        /** @brief Creates an empty registry without discovering services or starting work.
         * @param capabilities Validated explicit host projection.
         * @param limits Independent finite registry bounds.
         * @return Registry or RegistryDescriptorInvalid.
         */
        [[nodiscard]] static Result<PCGRegistry> Create(PCGCapabilityProjection capabilities, PCGRegistryLimits limits = {});

        /** @brief Registers one detached graph descriptor. @param descriptor Inert graph metadata.
         * @return New generation or typed validation/duplicate/capacity/lifecycle failure.
         * @pre Composition-owner thread only; no registry callback is invoked.
         */
        [[nodiscard]] Result<std::uint64_t> RegisterGraph(PCGGraphDescriptor descriptor);
        /** @brief Replaces one graph with a strictly newer revision. @param descriptor Complete detached replacement.
         * @return New generation or typed validation/unknown/stale/lifecycle failure.
         * @post Failure preserves the prior publication and issued snapshots.
         */
        [[nodiscard]] Result<std::uint64_t> ReplaceGraph(PCGGraphDescriptor descriptor);
        /** @brief Removes a graph from future snapshots. @param graph Stable graph identity.
         * @return Whether removed, or a lifecycle/generation failure.
         */
        [[nodiscard]] Result<bool> UnregisterGraph(GraphId graph);

        /** @brief Registers one inert node-runtime descriptor. @param descriptor Complete runtime metadata.
         * @return New generation or typed validation/duplicate/capacity/lifecycle failure.
         */
        [[nodiscard]] Result<std::uint64_t> RegisterNodeRuntime(PCGNodeRuntimeDescriptor descriptor);
        /** @brief Replaces one node runtime with a strictly newer contract version. @param descriptor Replacement metadata.
         * @return New generation or typed validation/unavailable/stale/lifecycle failure.
         */
        [[nodiscard]] Result<std::uint64_t> ReplaceNodeRuntime(PCGNodeRuntimeDescriptor descriptor);
        /** @brief Removes a node runtime from future snapshots. @param type Stable semantic node type.
         * @return Whether removed, or a lifecycle/generation failure.
         */
        [[nodiscard]] Result<bool> UnregisterNodeRuntime(NodeTypeId type);

        /** @brief Publishes an owning immutable query snapshot. @return Snapshot or RegistryClosed.
         * @pre Composition-owner thread only; issued snapshots may then be read concurrently.
         */
        [[nodiscard]] Result<PCGRegistrySnapshot> Snapshot() const;
        /** @brief Closes publication and releases live descriptors; issued snapshots remain valid. */
        void Close() noexcept;

        /** @brief Reports whether composition admission is closed. @return Lifecycle state. */
        [[nodiscard]] bool IsClosed() const noexcept {
            return closed_;
        }

    private:
        PCGRegistry(PCGCapabilityProjection capabilities, PCGRegistryLimits limits,
                    std::shared_ptr<const PCGRegistrySnapshot::State> state) noexcept;
        [[nodiscard]] Result<std::uint64_t> Publish(std::vector<PCGGraphDescriptor> graphs, std::vector<PCGNodeRuntimeDescriptor> runtimes);

        PCGCapabilityProjection capabilities_;
        PCGRegistryLimits limits_;
        std::shared_ptr<const PCGRegistrySnapshot::State> state_;
        bool closed_{false};
    };
}  // namespace Horo::PCG
