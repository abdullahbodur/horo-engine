#include "Horo/PCG/PCGErrors.h"
#include "Horo/PCG/PCGRegistry.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <initializer_list>
#include <set>
#include <string_view>
#include <utility>

namespace Horo::PCG {
    namespace {
        template <typename Identity> Identity Id(const std::uint64_t value) {
            auto identity = Identity::Create(value);
            REQUIRE(identity.HasValue());
            return std::move(identity).Value();
        }

        PCGCapabilitySet Capabilities(std::initializer_list<PCGCapability> values) {
            const auto result = PCGCapabilitySet::Create(values);
            REQUIRE(result.HasValue());
            return result.Value();
        }

        PCGCapabilityProjection Projection(const PCGHostProfile profile, std::initializer_list<PCGCapability> values) {
            auto projection = ProjectPCGCapabilities(profile, Capabilities(values));
            REQUIRE(projection.HasValue());
            return projection.Value();
        }

        PCGGraphDescriptor Graph(const std::uint64_t graphValue, const std::uint64_t revision, const std::uint64_t nodeValue = 1,
                                 const std::uint64_t typeValue = 11, const PCGCapabilitySet required = PCGCapabilitySet::Empty()) {
            return {{Id<GraphId>(graphValue), Id<GraphRevision>(revision)}, {{Id<NodeId>(nodeValue), Id<NodeTypeId>(typeValue)}}, required};
        }

        PCGNodeRuntimeDescriptor Runtime(const std::uint64_t typeValue, const std::uint32_t version = 1,
                                         const PCGCapabilitySet required = PCGCapabilitySet::Empty(),
                                         const PCGNodeDeterminism determinism = PCGNodeDeterminism::PortableDeterministic) {
            return {Id<NodeTypeId>(typeValue), version, determinism, required};
        }

        void CheckError(const auto &result, const ErrorCodeDescriptor &descriptor) {
            REQUIRE(result.HasError());
            CHECK(result.ErrorValue().domain.Value() == "horo.pcg");
            CHECK(result.ErrorValue().code.Value() == descriptor.code.Value());
        }
    }  // namespace

    TEST_CASE("PCG capability projection preserves exact interactive and headless grants", "[unit][pcg][registry]") {
        using enum PCGCapability;
        const auto interactive = Projection(PCGHostProfile::Interactive, {Validation, EditorPreview, RenderOutput});
        CHECK(interactive.granted.Contains(Validation));
        CHECK(interactive.granted.Contains(EditorPreview));
        CHECK(interactive.granted.Contains(RenderOutput));
        CHECK_FALSE(interactive.granted.Contains(RuntimeEvaluation));

        const auto headless = Projection(PCGHostProfile::Headless, {Validation, RuntimeEvaluation, SceneOutput, NavigationOutput});
        CHECK(headless.granted.Contains(RuntimeEvaluation));
        CHECK_FALSE(headless.granted.Contains(EditorPreview));
        CheckError(ProjectPCGCapabilities(PCGHostProfile::Headless, Capabilities({EditorPreview})), PCGErrors::UnsupportedCapability);
        CheckError(ProjectPCGCapabilities(PCGHostProfile::Headless, Capabilities({RenderOutput})), PCGErrors::UnsupportedCapability);
        CheckError(ProjectPCGCapabilities(static_cast<PCGHostProfile>(255), PCGCapabilitySet::Empty()),
                   PCGErrors::RegistryDescriptorInvalid);
        const std::array unknown{static_cast<PCGCapability>(255)};
        CheckError(PCGCapabilitySet::Create(unknown), PCGErrors::RegistryDescriptorInvalid);
    }

    TEST_CASE("PCG null composition advertises no evaluation or output success", "[unit][pcg][registry][headless]") {
        using enum PCGCapability;
        const auto nullProjection = ProjectPCGCapabilities(PCGHostProfile::Null, PCGCapabilitySet::Empty());
        REQUIRE(nullProjection.HasValue());
        CheckError(ProjectPCGCapabilities(PCGHostProfile::Null, Capabilities({Validation})), PCGErrors::UnsupportedCapability);
        auto registry = PCGRegistry::Create(nullProjection.Value()).Value();
        REQUIRE(registry.RegisterNodeRuntime(Runtime(11)).HasValue());
        REQUIRE(registry.RegisterGraph(Graph(1, 1)).HasValue());
        const auto snapshot = registry.Snapshot().Value();
        CheckError(snapshot.QueryGraph({Id<GraphId>(1), Id<GraphRevision>(1)}, Capabilities({RuntimeEvaluation})),
                   PCGErrors::UnsupportedCapability);
        CheckError(snapshot.QueryGraph({Id<GraphId>(1), Id<GraphRevision>(1)}, PCGCapabilitySet::Empty()),
                   PCGErrors::UnsupportedCapability);
        CheckError(snapshot.QueryNodeRuntime(Id<NodeTypeId>(11), Capabilities({SceneOutput})), PCGErrors::UnsupportedCapability);
        CHECK(snapshot.FindGraph(Id<GraphId>(1)).HasValue());
    }

    TEST_CASE("PCG registry publishes canonical immutable graph and runtime queries", "[unit][pcg][registry]") {
        using enum PCGCapability;
        auto registry =
            PCGRegistry::Create(Projection(PCGHostProfile::Interactive, {Validation, OfflineBake, RuntimeEvaluation, SceneOutput})).Value();
        REQUIRE(registry.RegisterNodeRuntime(Runtime(22, 1, Capabilities({RuntimeEvaluation}))).HasValue());
        REQUIRE(registry.RegisterNodeRuntime(Runtime(11)).HasValue());
        REQUIRE(registry.RegisterGraph(Graph(20, 4, 2, 22, Capabilities({SceneOutput}))).HasValue());
        REQUIRE(registry.RegisterGraph(Graph(10, 3, 1, 11)).HasValue());

        const auto snapshot = registry.Snapshot().Value();
        REQUIRE(snapshot.IsValid());
        CHECK(snapshot.Graphs().size() == 2);
        CHECK(snapshot.Graphs()[0].generation.graph == Id<GraphId>(10));
        CHECK(snapshot.NodeRuntimes()[0].type == Id<NodeTypeId>(11));
        const auto graph = snapshot.QueryGraph({Id<GraphId>(20), Id<GraphRevision>(4)}, Capabilities({RuntimeEvaluation}));
        REQUIRE(graph.HasValue());
        CHECK(snapshot.Resolve(graph.Value()).Value()->generation.graph == Id<GraphId>(20));
        const auto runtime = snapshot.QueryNodeRuntime(Id<NodeTypeId>(22), Capabilities({RuntimeEvaluation}));
        REQUIRE(runtime.HasValue());
        CHECK(snapshot.Resolve(runtime.Value()).Value()->contractVersion == 1);
    }

    TEST_CASE("PCG query rejects missing capability runtime and stale graph without fallback", "[unit][pcg][registry]") {
        using enum PCGCapability;
        auto registry = PCGRegistry::Create(Projection(PCGHostProfile::Headless, {Validation, RuntimeEvaluation, SceneOutput})).Value();
        REQUIRE(registry.RegisterGraph(Graph(1, 5, 7, 70, Capabilities({SceneOutput}))).HasValue());
        auto snapshot = registry.Snapshot().Value();
        CheckError(snapshot.QueryGraph({Id<GraphId>(1), Id<GraphRevision>(5)}, Capabilities({RuntimeEvaluation})),
                   PCGErrors::RuntimeUnavailable);

        REQUIRE(registry.RegisterNodeRuntime(Runtime(70, 1, Capabilities({RuntimeEvaluation}))).HasValue());
        snapshot = registry.Snapshot().Value();
        CheckError(snapshot.QueryGraph({Id<GraphId>(1), Id<GraphRevision>(4)}, Capabilities({RuntimeEvaluation})),
                   PCGErrors::IdentityStale);
        CheckError(snapshot.QueryGraph({Id<GraphId>(1), Id<GraphRevision>(5)}, Capabilities({TerrainOutput})),
                   PCGErrors::UnsupportedCapability);
        CheckError(snapshot.QueryNodeRuntime(Id<NodeTypeId>(71), PCGCapabilitySet::Empty()), PCGErrors::RuntimeUnavailable);
        CheckError(snapshot.FindGraph(Id<GraphId>(2)), PCGErrors::IdentityUnknown);
    }

    TEST_CASE("PCG replacement preserves old snapshots and rejects stale handles", "[unit][pcg][registry][lifecycle]") {
        auto registry = PCGRegistry::Create(Projection(PCGHostProfile::Interactive, {PCGCapability::Validation})).Value();
        REQUIRE(registry.RegisterNodeRuntime(Runtime(11)).HasValue());
        REQUIRE(registry.RegisterGraph(Graph(1, 1)).HasValue());
        const auto oldSnapshot = registry.Snapshot().Value();
        const auto oldGraphHandle = oldSnapshot.FindGraph(Id<GraphId>(1)).Value();
        const auto oldRuntimeHandle = oldSnapshot.QueryNodeRuntime(Id<NodeTypeId>(11), PCGCapabilitySet::Empty()).Value();

        REQUIRE(registry.ReplaceNodeRuntime(Runtime(11, 2)).HasValue());
        REQUIRE(registry.ReplaceGraph(Graph(1, 2)).HasValue());
        const auto current = registry.Snapshot().Value();
        CHECK(oldSnapshot.Resolve(oldGraphHandle).Value()->generation.revision == Id<GraphRevision>(1));
        CHECK(oldSnapshot.Resolve(oldRuntimeHandle).Value()->contractVersion == 1);
        CheckError(current.Resolve(oldGraphHandle), PCGErrors::RegistryHandleStale);
        CheckError(current.Resolve(oldRuntimeHandle), PCGErrors::RegistryHandleStale);
        CHECK(current.QueryGraph({Id<GraphId>(1), Id<GraphRevision>(2)}, Capabilities({PCGCapability::Validation})).HasValue());
        CheckError(registry.ReplaceGraph(Graph(1, 2)), PCGErrors::IdentityStale);
        CheckError(registry.ReplaceNodeRuntime(Runtime(11, 2)), PCGErrors::RegistryHandleStale);
    }

    TEST_CASE("PCG registration validates ordering and independent hard bounds", "[unit][pcg][registry][boundary]") {
        const PCGRegistryLimits limits{2, 2, 2};
        auto registry = PCGRegistry::Create(Projection(PCGHostProfile::Interactive, {}), limits).Value();
        REQUIRE(registry.RegisterNodeRuntime(Runtime(1)).HasValue());
        REQUIRE(registry.RegisterNodeRuntime(Runtime(2)).HasValue());
        CheckError(registry.RegisterNodeRuntime(Runtime(3)), PCGErrors::RegistryCapacityExceeded);

        auto graph = Graph(1, 1, 2, 1);
        graph.nodes.push_back({Id<NodeId>(1), Id<NodeTypeId>(2)});
        REQUIRE(registry.RegisterGraph(std::move(graph)).HasValue());
        REQUIRE(registry.RegisterGraph(Graph(2, 1, 1, 1)).HasValue());
        CheckError(registry.RegisterGraph(Graph(3, 1)), PCGErrors::RegistryCapacityExceeded);

        CheckError(PCGRegistry::Create(Projection(PCGHostProfile::Interactive, {}), {0, 1, 1}), PCGErrors::RegistryDescriptorInvalid);
        auto invalidGraph = Graph(9, 1);
        invalidGraph.nodes.push_back(invalidGraph.nodes.front());
        auto graphRegistry = PCGRegistry::Create(Projection(PCGHostProfile::Interactive, {}), {1, 1, 2}).Value();
        CheckError(graphRegistry.RegisterGraph(std::move(invalidGraph)), PCGErrors::RegistryDescriptorInvalid);
        auto runtimeRegistry = PCGRegistry::Create(Projection(PCGHostProfile::Interactive, {})).Value();
        CheckError(runtimeRegistry.RegisterNodeRuntime(Runtime(1, 0)), PCGErrors::RegistryDescriptorInvalid);
    }

    TEST_CASE("PCG duplicates fail without mutating the published generation", "[unit][pcg][registry][failure]") {
        auto registry = PCGRegistry::Create(Projection(PCGHostProfile::Interactive, {})).Value();
        REQUIRE(registry.RegisterNodeRuntime(Runtime(11)).HasValue());
        REQUIRE(registry.RegisterGraph(Graph(1, 1)).HasValue());
        const auto before = registry.Snapshot().Value();
        CheckError(registry.RegisterNodeRuntime(Runtime(11)), PCGErrors::RegistryDuplicate);
        CheckError(registry.RegisterGraph(Graph(1, 2)), PCGErrors::RegistryDuplicate);
        const auto after = registry.Snapshot().Value();
        CHECK(after.Generation() == before.Generation());
        CHECK(after.Graphs()[0].generation.revision == Id<GraphRevision>(1));
        CHECK(after.NodeRuntimes()[0].contractVersion == 1);
    }

    TEST_CASE("PCG unregister and shutdown preserve issued snapshots", "[unit][pcg][registry][lifecycle]") {
        auto registry = PCGRegistry::Create(Projection(PCGHostProfile::Interactive, {})).Value();
        REQUIRE(registry.RegisterNodeRuntime(Runtime(11)).HasValue());
        REQUIRE(registry.RegisterGraph(Graph(1, 1)).HasValue());
        const auto pinned = registry.Snapshot().Value();
        CHECK_FALSE(registry.UnregisterGraph(Id<GraphId>(99)).Value());
        CHECK_FALSE(registry.UnregisterNodeRuntime(Id<NodeTypeId>(99)).Value());
        CHECK(registry.UnregisterGraph(Id<GraphId>(1)).Value());
        CHECK(registry.UnregisterNodeRuntime(Id<NodeTypeId>(11)).Value());
        CHECK(pinned.Graphs().size() == 1);
        CHECK(pinned.NodeRuntimes().size() == 1);

        registry.Close();
        registry.Close();
        CHECK(registry.IsClosed());
        CheckError(registry.Snapshot(), PCGErrors::RegistryClosed);
        CheckError(registry.RegisterGraph(Graph(2, 1)), PCGErrors::RegistryClosed);
        CheckError(registry.RegisterNodeRuntime(Runtime(12)), PCGErrors::RegistryClosed);
        CheckError(registry.UnregisterGraph(Id<GraphId>(2)), PCGErrors::RegistryClosed);
        CHECK(pinned.FindGraph(Id<GraphId>(1)).HasValue());
    }

    TEST_CASE("PCG registry handles reject malformed slots and wrong snapshot generations", "[unit][pcg][registry]") {
        auto registry = PCGRegistry::Create(Projection(PCGHostProfile::Interactive, {})).Value();
        REQUIRE(registry.RegisterNodeRuntime(Runtime(11)).HasValue());
        REQUIRE(registry.RegisterGraph(Graph(1, 1)).HasValue());
        const auto snapshot = registry.Snapshot().Value();
        CheckError(snapshot.Resolve(PCGGraphHandle{}), PCGErrors::RegistryHandleInvalid);
        CheckError(snapshot.Resolve(PCGNodeRuntimeHandle{}), PCGErrors::RegistryHandleInvalid);
        auto graphHandle = snapshot.FindGraph(Id<GraphId>(1)).Value();
        graphHandle.slot = 99;
        CheckError(snapshot.Resolve(graphHandle), PCGErrors::RegistryHandleInvalid);
        auto runtimeHandle = snapshot.QueryNodeRuntime(Id<NodeTypeId>(11), PCGCapabilitySet::Empty()).Value();
        runtimeHandle.contractVersion = 2;
        CheckError(snapshot.Resolve(runtimeHandle), PCGErrors::RegistryHandleStale);
        CheckError(PCGRegistrySnapshot{}.FindGraph(Id<GraphId>(1)), PCGErrors::IdentityInvalid);
    }

    TEST_CASE("PCG registry errors remain stable unique public descriptors", "[unit][pcg][registry][errors]") {
        const std::array descriptors{&PCGErrors::RegistryDescriptorInvalid,   &PCGErrors::RegistryDuplicate,
                                     &PCGErrors::RegistryCapacityExceeded,    &PCGErrors::RegistryClosed,
                                     &PCGErrors::RegistryGenerationExhausted, &PCGErrors::RegistryHandleInvalid,
                                     &PCGErrors::RegistryHandleStale,         &PCGErrors::UnsupportedCapability,
                                     &PCGErrors::RuntimeUnavailable};
        std::set<std::string_view> codes;
        for (const auto *descriptor : descriptors) {
            CHECK(descriptor->domain.Value() == "horo.pcg");
            CHECK(codes.insert(descriptor->code.Value()).second);
            CHECK_FALSE(descriptor->summary.empty());
            CHECK_FALSE(descriptor->remediationHint.empty());
        }
    }
}  // namespace Horo::PCG
