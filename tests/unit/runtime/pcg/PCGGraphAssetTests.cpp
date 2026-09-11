#include "Horo/PCG/PCGErrors.h"
#include "Horo/PCG/PCGGraphAsset.h"

#include <algorithm>
#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Horo::PCG {
    namespace {
        template <typename Identity> Identity Id(const std::uint64_t value) {
            auto id = Identity::Create(value);
            REQUIRE(id.HasValue());
            return id.Value();
        }

        const std::array<PCGNodeTypeSupport, 2> &SupportedTypes() {
            static const std::array types{PCGNodeTypeSupport{Id<NodeTypeId>(1), {1, 0}, {1, 2}},
                                          PCGNodeTypeSupport{Id<NodeTypeId>(2), {2, 0}, {2, 0}}};
            return types;
        }

        PCGGraphSourceContext Context(const PCGUnknownNodePolicy unknownPolicy = PCGUnknownNodePolicy::Reject) {
            return {.tier = PCGOperationalTier::Baseline,
                    .unknownNodePolicy = unknownPolicy,
                    .admission = PCGGraphSourceAdmissionState::Accepting,
                    .limits = {},
                    .supportedNodeTypes = SupportedTypes()};
        }

        PCGGraphPin InputPin(const std::uint64_t id, const PCGPinType type = PCGPinType::Scalar,
                             const PCGPinCardinality cardinality = PCGPinCardinality::Single,
                             std::optional<PCGGraphValue> value = PCGGraphValue{2.0}) {
            return {Id<PinId>(id), PCGPinDirection::Input, type, cardinality, std::move(value)};
        }

        PCGGraphPin OutputPin(const std::uint64_t id, const PCGPinType type = PCGPinType::Scalar) {
            return {Id<PinId>(id), PCGPinDirection::Output, type, PCGPinCardinality::Multiple, std::nullopt};
        }

        PCGGraphNode Node(const std::uint64_t id, const NodeTypeId type, const PCGNodeTypeVersion version, std::vector<PCGGraphPin> pins,
                          std::vector<std::uint8_t> payload = {}) {
            return {Id<NodeId>(id), type, version, std::move(pins), std::move(payload)};
        }

        PCGGraphSourceData ValidData(const std::uint64_t revision = 1) {
            PCGGraphSourceData data;
            data.generation = {Id<GraphId>(10), Id<GraphRevision>(revision)};
            data.tier = PCGOperationalTier::Baseline;
            data.mode = PCGGenerationMode::Offline;
            data.deterministicSeed = 0x123456789abcdef0ULL;
            data.nodes = {Node(20, Id<NodeTypeId>(2), {2, 0}, {InputPin(202)}, {4, 5}),
                          Node(10, Id<NodeTypeId>(1), {1, 1}, {OutputPin(101), InputPin(100)}, {1, 2, 3})};
            data.edges = {{Id<EdgeId>(50), Id<NodeId>(10), Id<PinId>(101), Id<NodeId>(20), Id<PinId>(202)}};
            data.exposedInputs = {{Id<ExposedInputId>(70), "world.density", Id<NodeId>(10), Id<PinId>(100), 3.5}};
            return data;
        }

        PCGGraphAsset ValidAsset(PCGGraphSourceData data = ValidData(),
                                 const PCGUnknownNodePolicy unknownPolicy = PCGUnknownNodePolicy::Reject) {
            auto asset = PCGGraphAsset::Create(std::move(data), Context(unknownPolicy));
            REQUIRE(asset.HasValue());
            return std::move(asset).Value();
        }

        template <typename T> void RequireError(const Result<T> &result, const ErrorCodeDescriptor &descriptor) {
            REQUIRE(result.HasError());
            CHECK(result.ErrorValue().domain.Value() == descriptor.domain.Value());
            CHECK(result.ErrorValue().code.Value() == descriptor.code.Value());
        }

        class UpgradeMinorVersion final : public IPCGGraphSourceMigrator {
        public:
            Result<std::vector<std::uint8_t>> Migrate(const std::span<const std::uint8_t> source, const PCGGraphSchemaVersion from,
                                                      const PCGGraphSchemaVersion target, const std::size_t maximumBytes) const override {
                REQUIRE(from == PCGGraphSchemaVersion{1, 0});
                REQUIRE(target == CurrentPCGGraphSchemaVersion);
                REQUIRE(source.size() <= maximumBytes);
                std::vector migrated(source.begin(), source.end());
                migrated[4] = static_cast<std::uint8_t>(target.major >> 8U);
                migrated[5] = static_cast<std::uint8_t>(target.major);
                migrated[6] = static_cast<std::uint8_t>(target.minor >> 8U);
                migrated[7] = static_cast<std::uint8_t>(target.minor);
                return Result<std::vector<std::uint8_t>>::Success(std::move(migrated));
            }
        };
    }  // namespace

    TEST_CASE("PCG graph source canonicalizes topology and round trips deterministically", "[unit][pcg][graph]") {
        const PCGGraphAsset asset = ValidAsset();
        REQUIRE(asset.Data().nodes.size() == 2);
        CHECK(asset.Data().nodes[0].id == Id<NodeId>(10));
        CHECK(asset.Data().nodes[1].id == Id<NodeId>(20));
        CHECK(asset.Data().nodes[0].pins[0].id == Id<PinId>(100));
        CHECK(asset.Data().nodes[0].pins[1].id == Id<PinId>(101));
        CHECK(asset.UnknownNodeCount() == 0);
        CHECK(asset.IsCookEligible());

        auto first = SerializePCGGraphAsset(asset);
        REQUIRE(first.HasValue());
        auto restored = DeserializePCGGraphAsset(first.Value(), Context());
        REQUIRE(restored.HasValue());
        CHECK(restored.Value().Data() == asset.Data());
        auto second = SerializePCGGraphAsset(restored.Value());
        REQUIRE(second.HasValue());
        CHECK(second.Value() == first.Value());

        auto differentlyOrdered = ValidData();
        std::ranges::reverse(differentlyOrdered.nodes);
        std::ranges::reverse(differentlyOrdered.nodes.back().pins);
        auto reorderedBytes = SerializePCGGraphAsset(ValidAsset(std::move(differentlyOrdered)));
        REQUIRE(reorderedBytes.HasValue());
        CHECK(reorderedBytes.Value() == first.Value());
    }

    TEST_CASE("PCG graph source normalizes signed zero and preserves typed defaults", "[unit][pcg][graph][values]") {
        auto data = ValidData();
        data.exposedInputs.front().defaultValue = -0.0;
        data.nodes.front().pins.front().defaultValue = -0.0;
        const PCGGraphAsset asset = ValidAsset(std::move(data));
        const double exposed = std::get<double>(asset.Data().exposedInputs.front().defaultValue);
        CHECK_FALSE(std::signbit(exposed));
        auto bytes = SerializePCGGraphAsset(asset);
        REQUIRE(bytes.HasValue());
        auto roundTrip = DeserializePCGGraphAsset(bytes.Value(), Context());
        REQUIRE(roundTrip.HasValue());
        CHECK(roundTrip.Value().Data() == asset.Data());
    }

    TEST_CASE("PCG graph source rejects malformed and oversized encoded bytes transactionally", "[unit][pcg][graph][decode]") {
        auto bytes = SerializePCGGraphAsset(ValidAsset());
        REQUIRE(bytes.HasValue());

        for (const std::size_t size : std::array<std::size_t, 4>{0, 4, 20, bytes.Value().size() - 1})
            RequireError(DeserializePCGGraphAsset(std::span{bytes.Value()}.first(size), Context()), PCGErrors::GraphSourceMalformed);

        auto trailing = bytes.Value();
        trailing.push_back(0);
        RequireError(DeserializePCGGraphAsset(trailing, Context()), PCGErrors::GraphSourceMalformed);

        auto oversizedCount = bytes.Value();
        oversizedCount[34] = 0;
        oversizedCount[35] = 0;
        oversizedCount[36] = 0;
        oversizedCount[37] = 33;
        RequireError(DeserializePCGGraphAsset(oversizedCount, Context()), PCGErrors::GraphSourceCapacityExceeded);

        auto context = Context();
        context.limits = GraphSourceLimitsForTier(PCGOperationalTier::Baseline).Value();
        context.limits.maximumSourceBytes = bytes.Value().size() - 1;
        RequireError(DeserializePCGGraphAsset(bytes.Value(), context), PCGErrors::GraphSourceCapacityExceeded);
        RequireError(SerializePCGGraphAsset(ValidAsset(), bytes.Value().size() - 1), PCGErrors::GraphSourceCapacityExceeded);
    }

    TEST_CASE("PCG graph schema migration is explicit bounded and revalidated", "[unit][pcg][graph][migration]") {
        CHECK(ClassifyPCGGraphSchemaCompatibility(CurrentPCGGraphSchemaVersion) == PCGGraphSchemaCompatibility::Exact);
        CHECK(ClassifyPCGGraphSchemaCompatibility({1, 0}) == PCGGraphSchemaCompatibility::MigrationRequired);
        CHECK(ClassifyPCGGraphSchemaCompatibility({1, 2}) == PCGGraphSchemaCompatibility::Unsupported);
        CHECK(ClassifyPCGGraphSchemaCompatibility({2, 0}) == PCGGraphSchemaCompatibility::Unsupported);

        auto bytes = SerializePCGGraphAsset(ValidAsset()).Value();
        bytes[6] = 0;
        bytes[7] = 0;
        RequireError(DeserializePCGGraphAsset(bytes, Context()), PCGErrors::GraphMigrationFailed);
        const UpgradeMinorVersion migrator;
        auto migrated = DeserializePCGGraphAsset(bytes, Context(), &migrator);
        REQUIRE(migrated.HasValue());
        CHECK(migrated.Value().Data().version == CurrentPCGGraphSchemaVersion);

        bytes[4] = 0;
        bytes[5] = 2;
        RequireError(DeserializePCGGraphAsset(bytes, Context(), &migrator), PCGErrors::GraphSourceVersionUnsupported);
    }

    TEST_CASE("PCG unknown nodes are rejected or preserved inert without executable fallback", "[unit][pcg][graph][unknown]") {
        auto data = ValidData();
        data.nodes.front().type = Id<NodeTypeId>(999);
        RequireError(PCGGraphAsset::Create(data, Context()), PCGErrors::GraphNodeTypeUnknown);

        auto preserved = PCGGraphAsset::Create(std::move(data), Context(PCGUnknownNodePolicy::PreserveInert));
        REQUIRE(preserved.HasValue());
        CHECK(preserved.Value().UnknownNodeCount() == 1);
        CHECK_FALSE(preserved.Value().IsCookEligible());
        auto bytes = SerializePCGGraphAsset(preserved.Value());
        REQUIRE(bytes.HasValue());
        auto roundTrip = DeserializePCGGraphAsset(bytes.Value(), Context(PCGUnknownNodePolicy::PreserveInert));
        REQUIRE(roundTrip.HasValue());
        CHECK(roundTrip.Value().Data() == preserved.Value().Data());
        CHECK(roundTrip.Value().UnknownNodeCount() == 1);
    }

    TEST_CASE("PCG graph validation rejects duplicate semantic identities and exposed bindings", "[unit][pcg][graph][duplicates]") {
        auto data = ValidData();
        data.nodes.push_back(data.nodes.front());
        RequireError(PCGGraphAsset::Create(data, Context()), PCGErrors::GraphSourceDuplicate);

        data = ValidData();
        data.nodes.front().pins.push_back(data.nodes.front().pins.front());
        RequireError(PCGGraphAsset::Create(data, Context()), PCGErrors::GraphSourceDuplicate);

        data = ValidData();
        data.nodes.back().pins.front().id = data.nodes.front().pins.front().id;
        RequireError(PCGGraphAsset::Create(data, Context()), PCGErrors::GraphSourceDuplicate);

        data = ValidData();
        data.edges.push_back(data.edges.front());
        RequireError(PCGGraphAsset::Create(data, Context()), PCGErrors::GraphSourceDuplicate);

        data = ValidData();
        auto duplicateEndpoints = data.edges.front();
        duplicateEndpoints.id = Id<EdgeId>(51);
        data.edges.push_back(duplicateEndpoints);
        RequireError(PCGGraphAsset::Create(data, Context()), PCGErrors::GraphSourceDuplicate);

        data = ValidData();
        data.exposedInputs.push_back({Id<ExposedInputId>(71), "world.density", Id<NodeId>(10), Id<PinId>(100), 2.0});
        RequireError(PCGGraphAsset::Create(data, Context()), PCGErrors::GraphSourceDuplicate);

        data = ValidData();
        data.exposedInputs.push_back({Id<ExposedInputId>(71), "world.scale", Id<NodeId>(10), Id<PinId>(100), 2.0});
        RequireError(PCGGraphAsset::Create(data, Context()), PCGErrors::GraphSourceDuplicate);
    }

    TEST_CASE("PCG graph validation rejects missing reversed mismatched and overfull edges", "[unit][pcg][graph][topology]") {
        auto data = ValidData();
        data.edges.front().sourcePin = Id<PinId>(999);
        RequireError(PCGGraphAsset::Create(data, Context()), PCGErrors::GraphTopologyInvalid);

        data = ValidData();
        std::swap(data.edges.front().sourceNode, data.edges.front().targetNode);
        std::swap(data.edges.front().sourcePin, data.edges.front().targetPin);
        RequireError(PCGGraphAsset::Create(data, Context()), PCGErrors::GraphTopologyInvalid);

        data = ValidData();
        data.nodes.front().pins.front().type = PCGPinType::Vector3;
        data.nodes.front().pins.front().defaultValue = Math::Vec3{};
        RequireError(PCGGraphAsset::Create(data, Context()), PCGErrors::GraphTopologyInvalid);

        data = ValidData();
        data.nodes.push_back(Node(30, Id<NodeTypeId>(1), {1, 0}, {OutputPin(301)}));
        data.edges.push_back({Id<EdgeId>(51), Id<NodeId>(30), Id<PinId>(301), Id<NodeId>(20), Id<PinId>(202)});
        RequireError(PCGGraphAsset::Create(data, Context()), PCGErrors::GraphTopologyInvalid);
    }

    TEST_CASE("PCG graph validation rejects cycles and invalid pin default contracts", "[unit][pcg][graph][topology]") {
        auto data = ValidData();
        data.nodes.front().pins.push_back(OutputPin(203));
        data.edges.push_back({Id<EdgeId>(51), Id<NodeId>(20), Id<PinId>(203), Id<NodeId>(10), Id<PinId>(100)});
        RequireError(PCGGraphAsset::Create(data, Context()), PCGErrors::GraphTopologyInvalid);

        data = ValidData();
        data.nodes.front().pins.front().defaultValue = Math::Vec3{};
        RequireError(PCGGraphAsset::Create(data, Context()), PCGErrors::GraphSourceMalformed);

        data = ValidData();
        data.exposedInputs.front().defaultValue = std::numeric_limits<double>::infinity();
        RequireError(PCGGraphAsset::Create(data, Context()), PCGErrors::GraphSourceMalformed);

        data = ValidData();
        data.nodes.back().pins.front().defaultValue = 1.0;
        RequireError(PCGGraphAsset::Create(data, Context()), PCGErrors::GraphSourceMalformed);
    }

    TEST_CASE("PCG graph limits reject hostile structure before publication", "[unit][pcg][graph][limits]") {
        auto context = Context();
        context.limits = GraphSourceLimitsForTier(PCGOperationalTier::Baseline).Value();
        context.limits.maximumNodes = 1;
        RequireError(PCGGraphAsset::Create(ValidData(), context), PCGErrors::GraphSourceCapacityExceeded);

        context = Context();
        context.limits = GraphSourceLimitsForTier(PCGOperationalTier::Baseline).Value();
        context.limits.maximumPinsPerNode = 1;
        RequireError(PCGGraphAsset::Create(ValidData(), context), PCGErrors::GraphSourceCapacityExceeded);

        context = Context();
        context.limits = GraphSourceLimitsForTier(PCGOperationalTier::Baseline).Value();
        context.limits.maximumTotalPins = 3;
        REQUIRE(PCGGraphAsset::Create(ValidData(), context).HasValue());
        context.limits.maximumTotalPins = 2;
        RequireError(PCGGraphAsset::Create(ValidData(), context), PCGErrors::GraphSourceCapacityExceeded);

        context = Context();
        context.limits = GraphSourceLimitsForTier(PCGOperationalTier::Baseline).Value();
        context.limits.maximumNodePayloadBytes = 3;
        REQUIRE(PCGGraphAsset::Create(ValidData(), context).HasValue());
        context.limits.maximumNodePayloadBytes = 2;
        RequireError(PCGGraphAsset::Create(ValidData(), context), PCGErrors::GraphSourceCapacityExceeded);

        context = Context();
        context.limits = GraphSourceLimitsForTier(PCGOperationalTier::Baseline).Value();
        context.limits.maximumEdges = 1;
        auto extraEdge = ValidData();
        extraEdge.edges.push_back(extraEdge.edges.front());
        RequireError(PCGGraphAsset::Create(std::move(extraEdge), context), PCGErrors::GraphSourceCapacityExceeded);

        context = Context();
        context.limits = GraphSourceLimitsForTier(PCGOperationalTier::Baseline).Value();
        context.limits.maximumExposedInputs = 1;
        auto extraInput = ValidData();
        extraInput.exposedInputs.push_back(extraInput.exposedInputs.front());
        RequireError(PCGGraphAsset::Create(std::move(extraInput), context), PCGErrors::GraphSourceCapacityExceeded);

        context = Context();
        context.limits = GraphSourceLimitsForTier(PCGOperationalTier::Baseline).Value();
        context.limits.maximumNodes = 0;
        RequireError(PCGGraphAsset::Create(ValidData(), context), PCGErrors::GraphSourceCapacityExceeded);

        auto high = GraphSourceLimitsForTier(PCGOperationalTier::High);
        REQUIRE(high.HasValue());
        CHECK(high.Value().maximumNodes == 1'024);
        CHECK(high.Value().maximumEdges == 2'048);
        RequireError(GraphSourceLimitsForTier(static_cast<PCGOperationalTier>(99)), PCGErrors::GraphSourceCapacityExceeded);
    }

    TEST_CASE("PCG graph lifecycle and replacement are explicit and preserve current state on failure", "[unit][pcg][graph][lifecycle]") {
        const PCGGraphAsset current = ValidAsset();
        auto context = Context();
        context.admission = PCGGraphSourceAdmissionState::CancellationRequested;
        RequireError(PCGGraphAsset::Create(ValidData(), context), PCGErrors::GraphLifecycleUnavailable);
        context.admission = PCGGraphSourceAdmissionState::ShuttingDown;
        RequireError(DeserializePCGGraphAsset(SerializePCGGraphAsset(current).Value(), context), PCGErrors::GraphLifecycleUnavailable);
        context.admission = PCGGraphSourceAdmissionState::Count;
        RequireError(PCGGraphAsset::Create(ValidData(), context), PCGErrors::GraphSourceMalformed);

        context = Context();
        context.unknownNodePolicy = PCGUnknownNodePolicy::Count;
        RequireError(PCGGraphAsset::Create(ValidData(), context), PCGErrors::GraphSourceMalformed);

        auto stale = ValidData(1);
        RequireError(ReplacePCGGraphAsset(current, stale, Context()), PCGErrors::GraphReplacementInvalid);
        auto foreign = ValidData(2);
        foreign.generation.graph = Id<GraphId>(11);
        RequireError(ReplacePCGGraphAsset(current, foreign, Context()), PCGErrors::GraphReplacementInvalid);
        CHECK(current.Data().generation.revision == Id<GraphRevision>(1));

        auto replacement = ReplacePCGGraphAsset(current, ValidData(2), Context());
        REQUIRE(replacement.HasValue());
        CHECK(replacement.Value().Data().generation.revision == Id<GraphRevision>(2));
        CHECK(current.Data().generation.revision == Id<GraphRevision>(1));
    }

    TEST_CASE("PCG graph rejects incompatible catalog version tier and identifiers", "[unit][pcg][graph][compatibility]") {
        auto data = ValidData();
        data.nodes.front().version = {3, 0};
        RequireError(PCGGraphAsset::Create(data, Context()), PCGErrors::GraphSourceVersionUnsupported);

        data = ValidData();
        data.tier = PCGOperationalTier::Standard;
        RequireError(PCGGraphAsset::Create(data, Context()), PCGErrors::GraphSourceMalformed);

        data = ValidData();
        data.mode = PCGGenerationMode::Runtime;
        RequireError(PCGGraphAsset::Create(data, Context()), PCGErrors::GraphSourceMalformed);

        auto standardContext = Context();
        standardContext.tier = PCGOperationalTier::Standard;
        data.tier = PCGOperationalTier::Standard;
        REQUIRE(PCGGraphAsset::Create(data, standardContext).HasValue());

        const std::array duplicateSupport{SupportedTypes()[0], SupportedTypes()[0]};
        auto invalidCatalog = Context();
        invalidCatalog.supportedNodeTypes = duplicateSupport;
        RequireError(PCGGraphAsset::Create(ValidData(), invalidCatalog), PCGErrors::GraphSourceMalformed);

        const std::array invalidSupport{PCGNodeTypeSupport{Id<NodeTypeId>(1), {2, 0}, {1, 0}}};
        invalidCatalog.supportedNodeTypes = invalidSupport;
        RequireError(PCGGraphAsset::Create(ValidData(), invalidCatalog), PCGErrors::GraphSourceMalformed);

        data = ValidData();
        data.nodes.front().type = {};
        RequireError(PCGGraphAsset::Create(data, Context()), PCGErrors::GraphSourceMalformed);
        data = ValidData();
        data.exposedInputs.front().key = "not canonical";
        RequireError(PCGGraphAsset::Create(data, Context()), PCGErrors::GraphSourceMalformed);
    }

    TEST_CASE("PCG graph errors are stable unique public descriptors", "[unit][pcg][graph][errors]") {
        const std::array descriptors{&PCGErrors::GraphSourceMalformed,          &PCGErrors::GraphSourceDuplicate,
                                     &PCGErrors::GraphSourceVersionUnsupported, &PCGErrors::GraphSourceCapacityExceeded,
                                     &PCGErrors::GraphTopologyInvalid,          &PCGErrors::GraphNodeTypeUnknown,
                                     &PCGErrors::GraphMigrationFailed,          &PCGErrors::GraphReplacementInvalid,
                                     &PCGErrors::GraphLifecycleUnavailable};
        for (std::size_t index = 0; index < descriptors.size(); ++index) {
            CHECK(descriptors[index]->domain.Value() == "horo.pcg");
            CHECK_FALSE(descriptors[index]->code.Value().empty());
            for (std::size_t prior = 0; prior < index; ++prior)
                CHECK(descriptors[index]->code.Value() != descriptors[prior]->code.Value());
        }
    }
}  // namespace Horo::PCG
