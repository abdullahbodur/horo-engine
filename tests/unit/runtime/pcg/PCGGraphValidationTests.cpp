#include "Horo/PCG/PCGErrors.h"
#include "Horo/PCG/PCGGraphValidation.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <initializer_list>
#include <set>
#include <string_view>
#include <utility>

namespace Horo::PCG {
    namespace {
        template <typename Identity> Identity MakeIdentity(const std::uint64_t value) {
            const auto identity = Identity::Create(value);
            REQUIRE(identity.HasValue());
            return identity.Value();
        }

        PCGCapabilitySet MakeCapabilities(const std::initializer_list<PCGCapability> capabilities) {
            const auto result = PCGCapabilitySet::Create(capabilities);
            REQUIRE(result.HasValue());
            return result.Value();
        }

        const std::array<PCGNodeTypeSupport, 3> &NodeCatalog() {
            static const std::array catalog{
                PCGNodeTypeSupport{MakeIdentity<NodeTypeId>(11), {1, 0}, {1, 0}},
                PCGNodeTypeSupport{MakeIdentity<NodeTypeId>(22), {1, 0}, {1, 0}},
                PCGNodeTypeSupport{MakeIdentity<NodeTypeId>(33), {1, 0}, {1, 0}},
            };
            return catalog;
        }

        PCGGraphPin MakePin(const std::uint64_t value, const PCGPinDirection direction) {
            return {MakeIdentity<PinId>(value), direction, PCGPinType::Scalar,
                    direction == PCGPinDirection::Input ? PCGPinCardinality::Single : PCGPinCardinality::Multiple,
                    direction == PCGPinDirection::Input ? std::optional<PCGGraphValue>{PCGGraphValue{1.0}} : std::nullopt};
        }

        PCGGraphNode MakeNode(const std::uint64_t node, const std::uint64_t type, std::vector<PCGGraphPin> pins) {
            return {MakeIdentity<NodeId>(node), MakeIdentity<NodeTypeId>(type), {1, 0}, std::move(pins), {}};
        }

        PCGGraphSourceData GraphSource(const std::uint64_t revision = 1) {
            PCGGraphSourceData source;
            source.generation = {MakeIdentity<GraphId>(501), MakeIdentity<GraphRevision>(revision)};
            source.tier = PCGOperationalTier::Baseline;
            source.mode = PCGGenerationMode::Offline;
            source.deterministicSeed = 42;
            source.nodes = {
                MakeNode(30, 33, {MakePin(301, PCGPinDirection::Input), MakePin(302, PCGPinDirection::Input)}),
                MakeNode(20, 22, {MakePin(201, PCGPinDirection::Output)}),
                MakeNode(10, 11, {MakePin(101, PCGPinDirection::Output)}),
            };
            source.edges = {
                {MakeIdentity<EdgeId>(2), MakeIdentity<NodeId>(20), MakeIdentity<PinId>(201), MakeIdentity<NodeId>(30),
                 MakeIdentity<PinId>(302)},
                {MakeIdentity<EdgeId>(1), MakeIdentity<NodeId>(10), MakeIdentity<PinId>(101), MakeIdentity<NodeId>(30),
                 MakeIdentity<PinId>(301)},
            };
            return source;
        }

        PCGGraphAsset MakeAsset(const std::uint64_t revision = 1) {
            const PCGGraphSourceContext context{.tier = PCGOperationalTier::Baseline, .supportedNodeTypes = NodeCatalog()};
            auto asset = PCGGraphAsset::Create(GraphSource(revision), context);
            REQUIRE(asset.HasValue());
            return std::move(asset).Value();
        }

        PCGGraphDescriptor GraphDescriptor(const std::uint64_t revision = 1, const PCGCapabilitySet required = PCGCapabilitySet::Empty()) {
            return {{MakeIdentity<GraphId>(501), MakeIdentity<GraphRevision>(revision)},
                    {{MakeIdentity<NodeId>(10), MakeIdentity<NodeTypeId>(11)},
                     {MakeIdentity<NodeId>(20), MakeIdentity<NodeTypeId>(22)},
                     {MakeIdentity<NodeId>(30), MakeIdentity<NodeTypeId>(33)}},
                    required};
        }

        PCGRegistry MakeRegistry(const std::initializer_list<std::uint64_t> runtimeTypes = {11, 22, 33}, const std::uint64_t revision = 1,
                                 const PCGCapabilitySet graphCapabilities = PCGCapabilitySet::Empty()) {
            const auto projection = ProjectPCGCapabilities(PCGHostProfile::Interactive,
                                                           MakeCapabilities({PCGCapability::Validation, PCGCapability::OfflineBake}));
            REQUIRE(projection.HasValue());
            auto registry = PCGRegistry::Create(MakeIdentity<PCGRegistryInstanceId>(7'001), projection.Value()).Value();
            for (const std::uint64_t type : runtimeTypes) {
                const PCGNodeRuntimeDescriptor descriptor{MakeIdentity<NodeTypeId>(type), 1, PCGNodeDeterminism::PortableDeterministic,
                                                          PCGCapabilitySet::Empty()};
                REQUIRE(registry.RegisterNodeRuntime(descriptor).HasValue());
            }
            REQUIRE(registry.RegisterGraph(GraphDescriptor(revision, graphCapabilities)).HasValue());
            return registry;
        }

        void CheckFailure(const Result<PCGValidatedGraph> &result, const ErrorCodeDescriptor &outer,
                          const ErrorCodeDescriptor *cause = nullptr) {
            REQUIRE(result.HasError());
            CHECK(result.ErrorValue().code.Value() == outer.code.Value());
            if (cause != nullptr) {
                REQUIRE(result.ErrorValue().cause.Get() != nullptr);
                CHECK(result.ErrorValue().cause.Get()->code.Value() == cause->code.Value());
            }
        }
    }  // namespace

    TEST_CASE("PCG graph validation emits stable dependency-first runtime handles", "[unit][pcg][validation]") {
        const PCGGraphAsset graph = MakeAsset();
        auto registry = MakeRegistry({33, 11, 22}, 1, MakeCapabilities({PCGCapability::OfflineBake}));
        const auto snapshot = registry.Snapshot().Value();

        const auto first = ValidatePCGGraph(graph, snapshot, MakeCapabilities({PCGCapability::OfflineBake}));
        const auto second = ValidatePCGGraph(graph, snapshot, MakeCapabilities({PCGCapability::OfflineBake}));
        REQUIRE(first.HasValue());
        REQUIRE(second.HasValue());
        CHECK(first.Value().Generation() == graph.Data().generation);
        CHECK(first.Value().RegistryGeneration() == snapshot.Generation());
        CHECK(first.Value().RegistryGraph().graph == graph.Data().generation);
        CHECK(snapshot.Resolve(first.Value().RegistryGraph()).Value()->generation == graph.Data().generation);
        REQUIRE(first.Value().Nodes().size() == 3);
        CHECK(first.Value().Nodes()[0].node == MakeIdentity<NodeId>(10));
        CHECK(first.Value().Nodes()[1].node == MakeIdentity<NodeId>(20));
        CHECK(first.Value().Nodes()[2].node == MakeIdentity<NodeId>(30));
        CHECK(first.Value().Nodes()[0].runtime.IsValid());
        CHECK(first.Value().Nodes()[0].runtime == second.Value().Nodes()[0].runtime);
    }

    TEST_CASE("PCG graph validation reports every bounded missing runtime deterministically", "[unit][pcg][validation][failure]") {
        const PCGGraphAsset graph = MakeAsset();
        auto registry = MakeRegistry({11});
        const auto result = ValidatePCGGraph(graph, registry.Snapshot().Value(), PCGCapabilitySet::Empty());
        CheckFailure(result, PCGErrors::GraphValidationFailed, &PCGErrors::RuntimeUnavailable);
        REQUIRE(result.ErrorValue().diagnostics.size() == 2);
        CHECK(result.ErrorValue().diagnostics[0].location.source == "pcg://graph/501/revision/1/node/20");
        CHECK(result.ErrorValue().diagnostics[1].location.source == "pcg://graph/501/revision/1/node/30");

        const PCGGraphValidationLimits oneDiagnostic{.maximumDiagnostics = 1};
        const auto bounded = ValidatePCGGraph(graph, registry.Snapshot().Value(), PCGCapabilitySet::Empty(), oneDiagnostic);
        CheckFailure(bounded, PCGErrors::GraphValidationCapacityExceeded, &PCGErrors::RuntimeUnavailable);
        REQUIRE(bounded.ErrorValue().diagnostics.size() == 1);
        CHECK(bounded.ErrorValue().diagnostics.front().location.source == "pcg://graph/501/revision/1/node/20");
    }

    TEST_CASE("PCG graph validation rejects stale descriptors and unavailable capabilities", "[unit][pcg][validation][stale]") {
        const PCGGraphAsset graph = MakeAsset();
        auto staleRegistry = MakeRegistry({11, 22, 33}, 2);
        CheckFailure(ValidatePCGGraph(graph, staleRegistry.Snapshot().Value(), PCGCapabilitySet::Empty()), PCGErrors::GraphValidationFailed,
                     &PCGErrors::IdentityStale);

        auto registry = MakeRegistry();
        CheckFailure(ValidatePCGGraph(graph, registry.Snapshot().Value(), MakeCapabilities({PCGCapability::SceneOutput})),
                     PCGErrors::GraphValidationFailed, &PCGErrors::UnsupportedCapability);

        const auto nullProjection = ProjectPCGCapabilities(PCGHostProfile::Null, PCGCapabilitySet::Empty());
        REQUIRE(nullProjection.HasValue());
        auto nullRegistry = PCGRegistry::Create(MakeIdentity<PCGRegistryInstanceId>(7'002), nullProjection.Value()).Value();
        REQUIRE(nullRegistry.RegisterGraph(GraphDescriptor()).HasValue());
        CheckFailure(ValidatePCGGraph(graph, nullRegistry.Snapshot().Value(), PCGCapabilitySet::Empty()), PCGErrors::GraphValidationFailed,
                     &PCGErrors::UnsupportedCapability);

        auto mismatched = MakeRegistry();
        auto replacement = GraphDescriptor(2);
        replacement.nodes[1].type = MakeIdentity<NodeTypeId>(11);
        REQUIRE(mismatched.ReplaceGraph(std::move(replacement)).HasValue());
        CheckFailure(ValidatePCGGraph(MakeAsset(2), mismatched.Snapshot().Value(), PCGCapabilitySet::Empty()),
                     PCGErrors::GraphValidationFailed, &PCGErrors::RegistryDescriptorInvalid);
    }

    TEST_CASE("PCG graph validation preserves snapshot lifetime across replacement and shutdown", "[unit][pcg][validation][lifecycle]") {
        const PCGGraphAsset firstGraph = MakeAsset();
        auto registry = MakeRegistry();
        const auto retained = registry.Snapshot().Value();
        REQUIRE(registry.ReplaceGraph(GraphDescriptor(2)).HasValue());
        REQUIRE(
            registry
                .ReplaceNodeRuntime({MakeIdentity<NodeTypeId>(11), 2, PCGNodeDeterminism::PortableDeterministic, PCGCapabilitySet::Empty()})
                .HasValue());
        registry.Close();

        CHECK(ValidatePCGGraph(firstGraph, retained, PCGCapabilitySet::Empty()).HasValue());
        const auto current = registry.Snapshot();
        REQUIRE(current.HasError());
        CHECK(current.ErrorValue().code.Value() == PCGErrors::RegistryClosed.code.Value());
    }

    TEST_CASE("PCG graph validation enforces finite work and lifecycle admission", "[unit][pcg][validation][boundary]") {
        const PCGGraphAsset graph = MakeAsset();
        auto registry = MakeRegistry();
        const auto snapshot = registry.Snapshot().Value();
        CHECK(ValidatePCGGraph(graph, snapshot, PCGCapabilitySet::Empty(), {.maximumNodes = 3, .maximumEdges = 2}).HasValue());
        CheckFailure(ValidatePCGGraph(graph, snapshot, PCGCapabilitySet::Empty(), {.maximumNodes = 2}),
                     PCGErrors::GraphValidationCapacityExceeded);
        CheckFailure(ValidatePCGGraph(graph, snapshot, PCGCapabilitySet::Empty(), {.maximumNodes = 0}),
                     PCGErrors::GraphValidationCapacityExceeded);
        CheckFailure(ValidatePCGGraph(graph, snapshot, PCGCapabilitySet::Empty(),
                                      {.maximumNodes = PCGGraphValidationLimits::HardMaximumNodes + 1}),
                     PCGErrors::GraphValidationCapacityExceeded);
        CheckFailure(ValidatePCGGraph(graph, snapshot, PCGCapabilitySet::Empty(),
                                      {.maximumDiagnostics = PCGGraphValidationLimits::HardMaximumDiagnostics + 1}),
                     PCGErrors::GraphValidationCapacityExceeded);
        CheckFailure(ValidatePCGGraph(graph, snapshot, PCGCapabilitySet::Empty(), {}, PCGGraphValidationAdmission::CancellationRequested),
                     PCGErrors::GraphLifecycleUnavailable);
        CheckFailure(ValidatePCGGraph(graph, snapshot, PCGCapabilitySet::Empty(), {}, PCGGraphValidationAdmission::ShuttingDown),
                     PCGErrors::GraphLifecycleUnavailable);
        CheckFailure(ValidatePCGGraph(graph, snapshot, PCGCapabilitySet::Empty(), {}, static_cast<PCGGraphValidationAdmission>(255)),
                     PCGErrors::GraphValidationFailed);
    }

    TEST_CASE("PCG graph validation errors remain stable unique public descriptors", "[unit][pcg][validation][errors]") {
        const std::array descriptors{&PCGErrors::GraphValidationFailed, &PCGErrors::GraphValidationCapacityExceeded};
        std::set<std::string_view> codes;
        for (const ErrorCodeDescriptor *descriptor : descriptors) {
            CHECK(descriptor->domain.Value() == "horo.pcg");
            CHECK_FALSE(descriptor->summary.empty());
            CHECK_FALSE(descriptor->remediationHint.empty());
            CHECK(codes.insert(descriptor->code.Value()).second);
        }
    }
}  // namespace Horo::PCG
