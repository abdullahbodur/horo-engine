#include "Horo/PCG/PCGIdentity.h"

#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <set>
#include <string_view>
#include <type_traits>

namespace Horo::PCG {
    namespace {
        template <typename Identity> Identity Id(const std::uint64_t value) {
            auto identity = Identity::Create(value);
            REQUIRE(identity.HasValue());
            return identity.Value();
        }

        ExecutionId Execution(const std::uint64_t graph = 1, const std::uint64_t revision = 2, const std::uint64_t execution = 3) {
            return {{Id<GraphId>(graph), Id<GraphRevision>(revision)}, Id<ExecutionValue>(execution)};
        }

        GeneratedOutputId Output(const ExecutionId execution = Execution()) {
            return {execution, Id<NodeId>(4), Id<PinId>(5), Id<SourceSampleId>(6), 7};
        }

        void CheckError(const Result<void> &result, const ErrorCodeDescriptor &expected) {
            REQUIRE(result.HasError());
            CHECK(result.ErrorValue().domain.Value() == expected.domain.Value());
            CHECK(result.ErrorValue().code.Value() == expected.code.Value());
        }
    }  // namespace

    TEST_CASE("PCG authored identities are distinct ordered durable values", "[unit][pcg][identity]") {
        REQUIRE(GraphId::Create(0).HasError());
        REQUIRE(NodeId::Create(0).HasError());
        REQUIRE(PinId::Create(0).HasError());
        REQUIRE(GraphRevision::Create(0).HasError());
        REQUIRE(ExecutionValue::Create(0).HasError());
        REQUIRE(SourceSampleId::Create(0).HasError());
        static_assert(!std::is_same_v<GraphId, NodeId>);
        static_assert(!std::is_same_v<NodeId, PinId>);
        static_assert(!std::is_convertible_v<std::uint64_t, GraphId>);
        static_assert(std::is_trivially_copyable_v<GraphId>);

        const std::set ordered{Id<GraphId>(9), Id<GraphId>(2), Id<GraphId>(5)};
        CHECK(ordered == std::set{Id<GraphId>(2), Id<GraphId>(5), Id<GraphId>(9)});
    }

    TEST_CASE("PCG stable identities survive save reload and deterministic recook", "[unit][pcg][identity]") {
        const auto graph = Id<GraphId>(0x0102030405060708ULL);
        const SerializedStableIdentity saved = SerializeStableIdentity(graph);
        CHECK(saved == SerializedStableIdentity{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08});
        const GraphId reloaded = DeserializeStableIdentity<GraphIdentityTag>(saved).Value();
        const GraphId recooked = DeserializeStableIdentity<GraphIdentityTag>(SerializeStableIdentity(reloaded)).Value();
        CHECK(reloaded == graph);
        CHECK(recooked == graph);
        CHECK(DeserializeStableIdentity<GraphIdentityTag>({}).ErrorValue().code.Value() ==
              PCGErrors::SerializedIdentityInvalid.code.Value());
    }

    TEST_CASE("PCG composite identities have exact canonical encodings", "[unit][pcg][identity]") {
        const GraphGeneration generation{Id<GraphId>(0x0102030405060708ULL), Id<GraphRevision>(0x1112131415161718ULL)};
        const ExecutionId execution{generation, Id<ExecutionValue>(0x2122232425262728ULL)};
        const GeneratedOutputId output{execution, Id<NodeId>(0x3132333435363738ULL), Id<PinId>(0x4142434445464748ULL),
                                       Id<SourceSampleId>(0x5152535455565758ULL), 0x61626364U};
        const auto generationBytes = SerializeGraphGeneration(generation);
        const auto executionBytes = SerializeExecutionId(execution);
        const auto outputBytes = SerializeGeneratedOutputId(output);
        CHECK(DeserializeGraphGeneration(generationBytes).Value() == generation);
        CHECK(DeserializeExecutionId(executionBytes).Value() == execution);
        CHECK(DeserializeGeneratedOutputId(outputBytes).Value() == output);
        CHECK(outputBytes[0] == 0x01);
        CHECK(outputBytes[15] == 0x18);
        CHECK(outputBytes[24] == 0x31);
        CHECK(outputBytes[48] == 0x61);
        CHECK(outputBytes[51] == 0x64);
    }

    TEST_CASE("PCG decoders reject reserved composite dimensions", "[unit][pcg][identity]") {
        REQUIRE(DeserializeGraphGeneration({}).HasError());
        REQUIRE(DeserializeExecutionId({}).HasError());
        REQUIRE(DeserializeGeneratedOutputId({}).HasError());
        auto executionBytes = SerializeExecutionId(Execution());
        for (std::size_t index = 16; index < 24; ++index)
            executionBytes[index] = 0;
        REQUIRE(DeserializeExecutionId(executionBytes).HasError());
        auto outputBytes = SerializeGeneratedOutputId(Output());
        for (std::size_t index = 40; index < 48; ++index)
            outputBytes[index] = 0;
        REQUIRE(DeserializeGeneratedOutputId(outputBytes).HasError());
    }

    TEST_CASE("PCG output decoder rejects every reserved typed component", "[unit][pcg][identity]") {
        constexpr std::array<std::size_t, 6> ComponentOffsets{0, 8, 16, 24, 32, 40};
        for (const std::size_t offset : ComponentOffsets) {
            auto bytes = SerializeGeneratedOutputId(Output());
            for (std::size_t index = offset; index < offset + 8; ++index)
                bytes[index] = 0;
            INFO("component offset " << offset);
            REQUIRE(DeserializeGeneratedOutputId(bytes).HasError());
        }
    }

    TEST_CASE("PCG execution access rejects replacement graph generations and executions", "[unit][pcg][identity]") {
        const ExecutionId current = Execution();
        REQUIRE(ValidateExecutionAccess(current, current).HasValue());
        CheckError(ValidateExecutionAccess({}, current), PCGErrors::IdentityInvalid);
        CheckError(ValidateExecutionAccess(Execution(8, 2, 3), current), PCGErrors::IdentityUnknown);
        CheckError(ValidateExecutionAccess(Execution(1, 8, 3), current), PCGErrors::IdentityStale);
        CheckError(ValidateExecutionAccess(Execution(1, 2, 8), current), PCGErrors::IdentityStale);
    }

    TEST_CASE("PCG generated output never resolves into replacement generation", "[unit][pcg][identity]") {
        const ExecutionId current = Execution();
        REQUIRE(ValidateGeneratedOutputAccess(Output(current), current).HasValue());
        CheckError(ValidateGeneratedOutputAccess({}, current), PCGErrors::IdentityInvalid);
        CheckError(ValidateGeneratedOutputAccess(Output(Execution(9, 2, 3)), current), PCGErrors::IdentityUnknown);
        CheckError(ValidateGeneratedOutputAccess(Output(Execution(1, 9, 3)), current), PCGErrors::IdentityStale);
        CheckError(ValidateGeneratedOutputAccess(Output(Execution(1, 2, 9)), current), PCGErrors::IdentityStale);
    }

    TEST_CASE("PCG output equality includes deterministic provenance and ordinal", "[unit][pcg][identity]") {
        const GeneratedOutputId original = Output();
        auto changedNode = original;
        changedNode.node = Id<NodeId>(40);
        auto changedPin = original;
        changedPin.pin = Id<PinId>(50);
        auto changedSample = original;
        changedSample.sample = Id<SourceSampleId>(60);
        auto changedOrdinal = original;
        changedOrdinal.ordinal = 8;
        CHECK(original != changedNode);
        CHECK(original != changedPin);
        CHECK(original != changedSample);
        CHECK(original != changedOrdinal);
        CHECK(original == DeserializeGeneratedOutputId(SerializeGeneratedOutputId(original)).Value());
        static_assert(std::is_trivially_copyable_v<GeneratedOutputId>);
    }

    TEST_CASE("PCG revision advancement has explicit boundary behavior", "[unit][pcg][identity]") {
        REQUIRE(AdvanceGraphRevision({}).ErrorValue().code.Value() == PCGErrors::IdentityInvalid.code.Value());
        CHECK(AdvanceGraphRevision(Id<GraphRevision>(41)).Value() == Id<GraphRevision>(42));
        CHECK(AdvanceGraphRevision(Id<GraphRevision>(std::numeric_limits<std::uint64_t>::max())).ErrorValue().code.Value() ==
              PCGErrors::RevisionExhausted.code.Value());
    }

    TEST_CASE("PCG identity errors are stable unique public descriptors", "[unit][pcg][errors]") {
        const std::array descriptors{&PCGErrors::IdentityInvalid, &PCGErrors::IdentityUnknown, &PCGErrors::IdentityStale,
                                     &PCGErrors::RevisionExhausted, &PCGErrors::SerializedIdentityInvalid};
        std::set<std::string_view> codes;
        for (const ErrorCodeDescriptor *descriptor : descriptors) {
            CHECK(descriptor->domain.Value() == "horo.pcg");
            CHECK(codes.insert(descriptor->code.Value()).second);
            CHECK_FALSE(descriptor->summary.empty());
            CHECK_FALSE(descriptor->remediationHint.empty());
        }
    }
}  // namespace Horo::PCG
