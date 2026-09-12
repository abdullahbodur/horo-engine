#pragma once

#include "Horo/WorldStreaming/StreamingCellCandidate.h"
#include "WorldStreamingTestUtils.h"

#include <array>
#include <utility>

namespace Horo::WorldStreaming::CandidateTestSupport {
    inline StreamingCellId Cell(const std::int32_t x = 0) {
        return {x, 0, 0, 0, TestSupport::Layer()};
    }

    inline Sha256Digest Hash(const std::uint8_t value = 7) {
        Sha256Digest hash{};
        hash.bytes.front() = value;
        return hash;
    }

    inline StreamingCellOperationHandle Operation(const StreamingGeneration generation = TestSupport::IdentityFrom<StreamingGeneration>(1),
                                                  const StreamingCellId cell = Cell()) {
        return {.operation = TestSupport::IdentityFrom<StreamingCellOperationId>(9),
                .fence = {.partition = TestSupport::World(),
                          .epoch = TestSupport::IdentityFrom<PartitionEpoch>(1),
                          .cell = cell,
                          .generation = generation}};
    }

    inline CookedWorldIndexManifest Manifest(const std::span<const StreamingCellId> dependencies) {
        const auto grid = WorldCellQuantizationPolicy::Create({}, 100, {-1, 3, -1, 1, -1, 1}, 1).Value();
        const std::array layers{
            WorldLayerDescriptor{TestSupport::Layer(), "base", WorldLayerOwnership::WorldStreaming, WorldLayerFlags::Persistent, 1.0F}};
        const std::array cells{WorldPartitionCellDescriptor{Cell(), {TestSupport::Asset(4)}},
                               WorldPartitionCellDescriptor{Cell(1), {TestSupport::Asset(5)}},
                               WorldPartitionCellDescriptor{Cell(2), {TestSupport::Asset(6)}}};
        auto descriptor = WorldPartitionDescriptor::Create({}, TestSupport::World(),
                                                           {Math::WorldCoordinate64::FromMillimeters(-100, -100, -100),
                                                            Math::WorldCoordinate64::FromMillimeters(399, 199, 199)},
                                                           grid, layers, cells, {2, 4, 16})
                              .Value();
        const std::array cooked{CookedWorldCellManifestCandidate{Cell(), 48, 128, 99, Hash(), dependencies},
                                CookedWorldCellManifestCandidate{Cell(1), 1, 1, 1, Hash(8), {}},
                                CookedWorldCellManifestCandidate{Cell(2), 1, 1, 2, Hash(9), {}}};
        return std::move(CookedWorldIndexManifest::Create(std::move(descriptor), cooked, {4, 4, 4, 256, 256})).Value();
    }

    inline CookedWorldIndexManifest Manifest() {
        const std::array dependencies{Cell(1), Cell(2)};
        return Manifest(dependencies);
    }

    inline StreamingCellCandidateContext Context() {
        return {.operation = Operation(),
                .operationKind = StreamingCellOperationKind::Load,
                .operationState = StreamingCellOperationState::Preparing,
                .maximumPayloads = 4,
                .maximumDependencies = 4,
                .maximumCompressedBytes = 256,
                .maximumUncompressedBytes = 256,
                .lifecycle = StreamingCellCandidateLifecycle::Active};
    }

    inline std::array<StreamingCellPayloadHeader, 2> Payloads() {
        return {{{StreamingCellProvider::CoreEcs, StreamingCellPayloadRequirement::Required, 1, 176, 16, 16, 11},
                 {StreamingCellProvider::Terrain, StreamingCellPayloadRequirement::Optional, 3, 192, 32, 32, 22}}};
    }

    inline StreamingCellHeaderView Header(const std::span<const StreamingCellPayloadHeader> payloads) {
        return {.majorVersion = StreamingCellHeaderView::CurrentMajorVersion,
                .minorVersion = StreamingCellHeaderView::CurrentMinorVersion,
                .cell = Cell(),
                .compression = StreamingCellCompression::None,
                .compressedSize = 128,
                .uncompressedSize = 48,
                .payloadCrc32 = 99,
                .artifactHash = Hash(),
                .payloads = payloads};
    }

    inline StreamingCellCandidate Candidate(const CookedWorldIndexManifest &manifest) {
        const auto payloads = Payloads();
        return std::move(PrepareStreamingCellCandidate(manifest, Context(), Header(payloads))).Value();
    }
}  // namespace Horo::WorldStreaming::CandidateTestSupport
