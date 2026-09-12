#include "Horo/WorldStreaming/StreamingCellCandidate.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "StreamingCellCandidateTestSupport.h"
#include "WorldStreamingTestUtils.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <type_traits>
#include <utility>
#include <vector>

namespace Horo::WorldStreaming {
    namespace {
        using namespace CandidateTestSupport;
        using TestSupport::Asset;
        using TestSupport::IdentityFrom;
        using TestSupport::RequireError;
    }  // namespace

    TEST_CASE("Cell candidate resolves manifest metadata and owns canonical payload headers", "[unit][world_streaming][cell_candidate]") {
        const auto manifest = Manifest();
        auto payloads = Payloads();
        auto result = PrepareStreamingCellCandidate(manifest, Context(), Header(payloads));
        REQUIRE(result.HasValue());
        auto candidate = std::move(result).Value();

        payloads[0].version = 99;
        REQUIRE(candidate.Operation() == Operation());
        REQUIRE(candidate.Operation().fence.generation == IdentityFrom<StreamingGeneration>(1));
        REQUIRE(candidate.ChunkAsset() == Asset(4));
        REQUIRE(candidate.ManifestEntry().cell == Cell());
        REQUIRE(candidate.ManifestEntry().uncompressedSize == 48);
        REQUIRE(candidate.ManifestEntry().compressedSize == 128);
        REQUIRE(candidate.ManifestEntry().payloadCrc32 == 99);
        REQUIRE(candidate.ManifestEntry().artifactHash == Hash());
        REQUIRE(candidate.Compression() == StreamingCellCompression::None);
        REQUIRE(candidate.Payloads().size() == 2);
        REQUIRE(candidate.Payloads()[0].version == 1);
        REQUIRE(candidate.HardDependencies().size() == 2);
        REQUIRE(candidate.HardDependencies()[0] == Cell(1));
        REQUIRE(candidate.HardDependencies()[1] == Cell(2));

        static_assert(!std::is_copy_constructible_v<StreamingCellCandidate>);
        static_assert(std::is_move_constructible_v<StreamingCellCandidate>);
    }

    TEST_CASE("Cell candidate is pinned to one operation generation and manifest record",
              "[unit][world_streaming][cell_candidate][stale][replacement]") {
        const auto manifest = Manifest();
        const auto payloads = Payloads();
        auto oldResult = PrepareStreamingCellCandidate(manifest, Context(), Header(payloads));
        REQUIRE(oldResult.HasValue());
        auto oldCandidate = std::move(oldResult).Value();

        auto replacementContext = Context();
        replacementContext.operation = Operation(IdentityFrom<StreamingGeneration>(2));
        auto replacementResult = PrepareStreamingCellCandidate(manifest, replacementContext, Header(payloads));
        REQUIRE(replacementResult.HasValue());
        auto replacement = std::move(replacementResult).Value();
        REQUIRE(oldCandidate.Operation() != replacement.Operation());
        REQUIRE(oldCandidate.Operation().fence.generation == IdentityFrom<StreamingGeneration>(1));

        auto wrongHash = Header(payloads);
        wrongHash.artifactHash = Hash(8);
        RequireError(PrepareStreamingCellCandidate(manifest, Context(), wrongHash), WorldStreamingErrors::CellCandidateStale);

        auto missingContext = Context();
        missingContext.operation = Operation(IdentityFrom<StreamingGeneration>(1), Cell(3));
        auto missingHeader = Header(payloads);
        missingHeader.cell = Cell(3);
        RequireError(PrepareStreamingCellCandidate(manifest, missingContext, missingHeader),
                     WorldStreamingErrors::CellCandidateUnavailable);
    }

    TEST_CASE("Cell candidate applies mandatory count and byte ceilings transactionally",
              "[unit][world_streaming][cell_candidate][capacity]") {
        const auto manifest = Manifest();
        const auto payloads = Payloads();
        auto limited = Context();
        limited.maximumPayloads = 1;
        RequireError(PrepareStreamingCellCandidate(manifest, limited, Header(payloads)),
                     WorldStreamingErrors::CellCandidateCapacityExceeded);
        limited = Context();
        limited.maximumDependencies = 1;
        RequireError(PrepareStreamingCellCandidate(manifest, limited, Header(payloads)),
                     WorldStreamingErrors::CellCandidateCapacityExceeded);
        limited = Context();
        limited.maximumCompressedBytes = 127;
        RequireError(PrepareStreamingCellCandidate(manifest, limited, Header(payloads)),
                     WorldStreamingErrors::CellCandidateCapacityExceeded);
        limited = Context();
        limited.maximumUncompressedBytes = 47;
        RequireError(PrepareStreamingCellCandidate(manifest, limited, Header(payloads)),
                     WorldStreamingErrors::CellCandidateCapacityExceeded);
    }

    TEST_CASE("Cell candidate rejects incompatible versions and required unknown forward payloads",
              "[unit][world_streaming][cell_candidate][unsupported]") {
        const auto manifest = Manifest();
        auto payloads = Payloads();
        auto incompatible = Header(payloads);
        incompatible.majorVersion = 2;
        RequireError(PrepareStreamingCellCandidate(manifest, Context(), incompatible), WorldStreamingErrors::CellCandidateUnsupported);

        payloads[1].provider = StreamingCellProvider::FirstCustom;
        payloads[1].requirement = StreamingCellPayloadRequirement::Required;
        auto forward = Header(payloads);
        forward.minorVersion = 1;
        RequireError(PrepareStreamingCellCandidate(manifest, Context(), forward), WorldStreamingErrors::CellCandidateUnsupported);

        payloads[1].requirement = StreamingCellPayloadRequirement::Optional;
        REQUIRE(PrepareStreamingCellCandidate(manifest, Context(), Header(payloads)).HasValue());
    }

    TEST_CASE("Cell candidate validates required core payload and exact table layout", "[unit][world_streaming][cell_candidate][failure]") {
        const auto manifest = Manifest();
        auto payloads = Payloads();
        payloads[0].requirement = StreamingCellPayloadRequirement::Optional;
        RequireError(PrepareStreamingCellCandidate(manifest, Context(), Header(payloads)), WorldStreamingErrors::CellCandidateInvalid);

        payloads = Payloads();
        payloads[1].provider = payloads[0].provider;
        RequireError(PrepareStreamingCellCandidate(manifest, Context(), Header(payloads)), WorldStreamingErrors::CellCandidateInvalid);

        payloads = Payloads();
        payloads[1].offset = 200;
        RequireError(PrepareStreamingCellCandidate(manifest, Context(), Header(payloads)), WorldStreamingErrors::CellCandidateInvalid);

        payloads = Payloads();
        payloads[1].uncompressedSize = 31;
        RequireError(PrepareStreamingCellCandidate(manifest, Context(), Header(payloads)), WorldStreamingErrors::CellCandidateInvalid);

        payloads = Payloads();
        payloads[1].provider = static_cast<StreamingCellProvider>(8);
        RequireError(PrepareStreamingCellCandidate(manifest, Context(), Header(payloads)), WorldStreamingErrors::CellCandidateInvalid);
    }

    TEST_CASE("Cell candidate honors cancellation shutdown and operation phase evidence",
              "[unit][world_streaming][cell_candidate][lifecycle]") {
        const auto manifest = Manifest();
        const auto payloads = Payloads();
        auto context = Context();
        context.lifecycle = StreamingCellCandidateLifecycle::Cancelling;
        RequireError(PrepareStreamingCellCandidate(manifest, context, Header(payloads)),
                     WorldStreamingErrors::CellCandidateLifecycleUnavailable);
        context.lifecycle = StreamingCellCandidateLifecycle::Closed;
        RequireError(PrepareStreamingCellCandidate(manifest, context, Header(payloads)),
                     WorldStreamingErrors::CellCandidateLifecycleUnavailable);

        context = Context();
        context.operationKind = StreamingCellOperationKind::Activate;
        RequireError(PrepareStreamingCellCandidate(manifest, context, Header(payloads)), WorldStreamingErrors::CellCandidateUnsupported);
        context = Context();
        context.operationState = StreamingCellOperationState::Terminal;
        RequireError(PrepareStreamingCellCandidate(manifest, context, Header(payloads)), WorldStreamingErrors::CellCandidateUnsupported);

        context = Context();
        context.operation = {};
        RequireError(PrepareStreamingCellCandidate(manifest, context, Header(payloads)), WorldStreamingErrors::CellCandidateInvalid);

        context = Context();
        context.lifecycle = static_cast<StreamingCellCandidateLifecycle>(255);
        RequireError(PrepareStreamingCellCandidate(manifest, context, Header(payloads)), WorldStreamingErrors::CellCandidateInvalid);
    }
}  // namespace Horo::WorldStreaming
