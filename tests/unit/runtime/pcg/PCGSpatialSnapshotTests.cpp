#include "Horo/PCG/PCGErrors.h"
#include "Horo/PCG/PCGSpatialSnapshot.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace Horo::PCG {
    namespace {
        template <typename Identity> Identity Id(const std::uint64_t value) {
            auto identity = Identity::Create(value);
            REQUIRE(identity.HasValue());
            return identity.Value();
        }

        template <typename T> void CheckError(const Result<T> &result, const ErrorCodeDescriptor &expected) {
            REQUIRE(result.HasError());
            const Error &actual = result.ErrorValue();
            CHECK(std::pair{actual.domain.Value(), actual.code.Value()} == std::pair{expected.domain.Value(), expected.code.Value()});
        }

        [[nodiscard]] Math::Aabb Bounds(const float extent = 10.0F) {
            return {{-extent, -extent, -extent}, {extent, extent, extent}};
        }

        [[nodiscard]] PCGSurfaceTriangle Surface(const std::uint64_t id = 4) {
            return {Id<SpatialElementId>(id), {{{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}}}, {0.0F, -1.0F, 0.0F}};
        }

        [[nodiscard]] PCGSpatialSnapshotCandidate Candidate(const std::uint64_t snapshot = 1, const std::uint64_t revision = 1) {
            PCGSpatialSnapshotCandidate candidate;
            candidate.snapshot = Id<SpatialSnapshotId>(snapshot);
            candidate.provenance = {Id<SpatialProviderId>(2), Id<SpatialSourceId>(3), Id<SpatialRevision>(revision)};
            candidate.bounds = Bounds();
            candidate.coverage = PCGSpatialCoverage::Complete;
            candidate.surfaces = {Surface()};
            candidate.volumes = {PCGBoxVolume{Id<SpatialElementId>(5), {{-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F}}},
                                 PCGSphereVolume{Id<SpatialElementId>(6), {}, 1.0F}};
            candidate.splines = {PCGSpline{Id<SpatialElementId>(7), {{{0.0F, 0.0F, 0.0F}, {}, {}}, {{1.0F, 0.0F, 0.0F}, {}, {}}}, false}};
            candidate.grids = {PCGGrid{Id<SpatialElementId>(8), {}, {1.0F, 1.0F, 1.0F}, {2, 2, 2}}};
            return candidate;
        }

        [[nodiscard]] PCGSpatialCurrentness Current(const std::uint64_t revision = 1, const std::uint64_t epoch = 1) {
            return {Id<SpatialProviderId>(2), Id<SpatialSourceId>(3), Id<SpatialRevision>(revision), epoch};
        }
    }  // namespace

    TEST_CASE("PCG spatial snapshot captures every canonical descriptor without retaining source storage", "[unit][pcg][spatial]") {
        auto source = Candidate();
        source.surfaces.insert(source.surfaces.begin(), Surface(9));
        source.surfaces.insert(source.surfaces.begin(), Surface(10));
        auto detached = source;
        auto captured = CapturePCGSpatialSnapshot(std::move(detached));
        REQUIRE(captured.HasValue());
        source.surfaces[0].vertices[0].x = 9.0F;
        source.volumes.clear();
        source.splines[0].points.clear();

        const auto snapshot = captured.Value();
        CHECK(snapshot->Id() == Id<SpatialSnapshotId>(1));
        CHECK(snapshot->Provenance().provider == Id<SpatialProviderId>(2));
        CHECK(snapshot->Coordinates().axes == PCGSpatialAxisConvention::RightHandedYUp);
        REQUIRE(snapshot->Surfaces().size() == 3);
        CHECK(snapshot->Surfaces()[0].id == Id<SpatialElementId>(4));
        CHECK(snapshot->Surfaces()[0].vertices[0].x == 0.0F);
        CHECK(snapshot->Surfaces()[1].id == Id<SpatialElementId>(9));
        CHECK(snapshot->Volumes().size() == 2);
        CHECK(snapshot->Splines()[0].points.size() == 2);
        CHECK(snapshot->Grids().size() == 1);
        CHECK(snapshot->ResidentBytes() > sizeof(PCGSpatialSnapshot));
        CHECK(snapshot->Coverage() == PCGSpatialCoverage::Complete);
    }

    TEST_CASE("PCG complete empty spatial snapshot is distinct from unavailable coverage", "[unit][pcg][spatial]") {
        auto empty = Candidate();
        empty.surfaces.clear();
        empty.volumes.clear();
        empty.splines.clear();
        empty.grids.clear();
        auto captured = CapturePCGSpatialSnapshot(std::move(empty));
        REQUIRE(captured.HasValue());
        CHECK(captured.Value()->Surfaces().empty());

        for (const auto coverage : {PCGSpatialCoverage::Partial, PCGSpatialCoverage::Missing}) {
            auto unavailable = Candidate();
            unavailable.coverage = coverage;
            CheckError(CapturePCGSpatialSnapshot(std::move(unavailable)), PCGErrors::SpatialCoverageUnavailable);
        }
    }

    TEST_CASE("PCG spatial snapshot rejects invalid identity provenance coordinates and coverage", "[unit][pcg][spatial]") {
        auto invalidSnapshot = Candidate();
        invalidSnapshot.snapshot = {};
        CheckError(CapturePCGSpatialSnapshot(std::move(invalidSnapshot)), PCGErrors::SpatialInputInvalid);
        auto invalidProvider = Candidate();
        invalidProvider.provenance.provider = {};
        CheckError(CapturePCGSpatialSnapshot(std::move(invalidProvider)), PCGErrors::SpatialInputInvalid);
        auto invalidBounds = Candidate();
        invalidBounds.bounds.maximum.x = invalidBounds.bounds.minimum.x;
        CheckError(CapturePCGSpatialSnapshot(std::move(invalidBounds)), PCGErrors::SpatialInputInvalid);
        auto invalidScale = Candidate();
        invalidScale.coordinates.metersPerUnit = std::numeric_limits<float>::quiet_NaN();
        CheckError(CapturePCGSpatialSnapshot(std::move(invalidScale)), PCGErrors::SpatialCoordinatesUnsupported);
        auto invalidEpoch = Candidate();
        invalidEpoch.coordinates.originEpoch = 0;
        CheckError(CapturePCGSpatialSnapshot(std::move(invalidEpoch)), PCGErrors::SpatialCoordinatesUnsupported);
        auto unknownAxes = Candidate();
        unknownAxes.coordinates.axes = static_cast<PCGSpatialAxisConvention>(99);
        CheckError(CapturePCGSpatialSnapshot(std::move(unknownAxes)), PCGErrors::SpatialCoordinatesUnsupported);
        auto unknownPrecision = Candidate();
        unknownPrecision.coordinates.precision = static_cast<PCGSpatialPrecision>(99);
        CheckError(CapturePCGSpatialSnapshot(std::move(unknownPrecision)), PCGErrors::SpatialCoordinatesUnsupported);
        auto invalidTransform = Candidate();
        invalidTransform.coordinates.sourceToSnapshot.scale.y = 0.0F;
        CheckError(CapturePCGSpatialSnapshot(std::move(invalidTransform)), PCGErrors::SpatialCoordinatesUnsupported);
        auto unknownCoverage = Candidate();
        unknownCoverage.coverage = static_cast<PCGSpatialCoverage>(99);
        CheckError(CapturePCGSpatialSnapshot(std::move(unknownCoverage)), PCGErrors::SpatialInputInvalid);
    }

    TEST_CASE("PCG spatial primitives reject nonfinite degenerate out-of-bounds and duplicate input", "[unit][pcg][spatial]") {
        auto nonfinite = Candidate();
        nonfinite.surfaces[0].vertices[0].x = std::numeric_limits<float>::infinity();
        CheckError(CapturePCGSpatialSnapshot(std::move(nonfinite)), PCGErrors::SpatialInputInvalid);
        auto degenerateTriangle = Candidate();
        degenerateTriangle.surfaces[0].vertices[2] = degenerateTriangle.surfaces[0].vertices[1];
        CheckError(CapturePCGSpatialSnapshot(std::move(degenerateTriangle)), PCGErrors::SpatialInputInvalid);
        auto wrongNormal = Candidate();
        wrongNormal.surfaces[0].normal = {0.0F, 1.0F, 0.0F};
        CheckError(CapturePCGSpatialSnapshot(std::move(wrongNormal)), PCGErrors::SpatialInputInvalid);
        auto angledNormal = Candidate();
        angledNormal.surfaces[0].normal = {0.0F, -0.8F, 0.6F};
        CheckError(CapturePCGSpatialSnapshot(std::move(angledNormal)), PCGErrors::SpatialInputInvalid);
        auto invalidVolume = Candidate();
        std::get<PCGSphereVolume>(invalidVolume.volumes[1]).radius = 20.0F;
        CheckError(CapturePCGSpatialSnapshot(std::move(invalidVolume)), PCGErrors::SpatialInputInvalid);
        auto invalidSpline = Candidate();
        invalidSpline.splines[0].points[1].position = {};
        CheckError(CapturePCGSpatialSnapshot(std::move(invalidSpline)), PCGErrors::SpatialInputInvalid);
        auto degenerateSplineSegment = Candidate();
        degenerateSplineSegment.splines[0].points.push_back(degenerateSplineSegment.splines[0].points.back());
        CheckError(CapturePCGSpatialSnapshot(std::move(degenerateSplineSegment)), PCGErrors::SpatialInputInvalid);
        auto invalidGrid = Candidate();
        invalidGrid.grids[0].dimensions[1] = 0;
        CheckError(CapturePCGSpatialSnapshot(std::move(invalidGrid)), PCGErrors::SpatialInputInvalid);
        auto duplicate = Candidate();
        std::get<PCGBoxVolume>(duplicate.volumes[0]).id = duplicate.surfaces[0].id;
        CheckError(CapturePCGSpatialSnapshot(std::move(duplicate)), PCGErrors::SpatialInputInvalid);
    }

    TEST_CASE("PCG spatial limits accept exact grid and element boundaries and reject one over", "[unit][pcg][spatial]") {
        auto exactGrid = Candidate();
        exactGrid.bounds = {{0.0F, 0.0F, 0.0F}, {16'383.0F, 1.0F, 1.0F}};
        exactGrid.surfaces.clear();
        exactGrid.volumes.clear();
        exactGrid.splines.clear();
        exactGrid.grids = {PCGGrid{Id<SpatialElementId>(8), {}, {1.0F, 1.0F, 1.0F}, {16'384, 1, 1}}};
        REQUIRE(CapturePCGSpatialSnapshot(std::move(exactGrid)).HasValue());
        auto oversizedGrid = Candidate();
        oversizedGrid.bounds = Bounds(20'000.0F);
        oversizedGrid.grids = {PCGGrid{Id<SpatialElementId>(8), {}, {1.0F, 1.0F, 1.0F}, {16'385, 1, 1}}};
        CheckError(CapturePCGSpatialSnapshot(std::move(oversizedGrid)), PCGErrors::SpatialCapacityExceeded);

        auto exactElements = Candidate();
        exactElements.volumes.clear();
        exactElements.splines.clear();
        exactElements.grids.clear();
        exactElements.surfaces.clear();
        exactElements.surfaces.reserve(16'384);
        for (std::uint64_t id = 1; id <= 16'384; ++id)
            exactElements.surfaces.push_back(Surface(id));
        REQUIRE(CapturePCGSpatialSnapshot(std::move(exactElements)).HasValue());
        auto oversizedElements = Candidate();
        oversizedElements.volumes.clear();
        oversizedElements.splines.clear();
        oversizedElements.grids.clear();
        oversizedElements.surfaces.clear();
        oversizedElements.surfaces.reserve(16'385);
        for (std::uint64_t id = 1; id <= 16'385; ++id)
            oversizedElements.surfaces.push_back(Surface(id));
        CheckError(CapturePCGSpatialSnapshot(std::move(oversizedElements)), PCGErrors::SpatialCapacityExceeded);
    }

    TEST_CASE("PCG currentness separates memory validity from logical revision", "[unit][pcg][spatial]") {
        const auto snapshot = CapturePCGSpatialSnapshot(Candidate()).Value();
        REQUIRE(ValidatePCGSpatialSnapshotCurrent(snapshot, Current()).HasValue());
        auto otherProvider = Current();
        otherProvider.provider = Id<SpatialProviderId>(99);
        CheckError(ValidatePCGSpatialSnapshotCurrent(snapshot, otherProvider), PCGErrors::IdentityUnknown);
        auto otherSource = Current();
        otherSource.source = Id<SpatialSourceId>(99);
        CheckError(ValidatePCGSpatialSnapshotCurrent(snapshot, otherSource), PCGErrors::IdentityUnknown);
        CheckError(ValidatePCGSpatialSnapshotCurrent(snapshot, Current(2)), PCGErrors::SpatialSnapshotStale);
        CheckError(ValidatePCGSpatialSnapshotCurrent(snapshot, Current(1, 2)), PCGErrors::SpatialSnapshotStale);
        CheckError(ValidatePCGSpatialSnapshotCurrent({}, Current()), PCGErrors::SpatialInputInvalid);
        CHECK(snapshot->Surfaces().size() == 1);
    }

    TEST_CASE("PCG spatial replacement retains old readers and enforces lineage", "[unit][pcg][spatial]") {
        auto old = CapturePCGSpatialSnapshot(Candidate()).Value();
        std::weak_ptr<const PCGSpatialSnapshot> retired = old;
        auto replacement = ReplacePCGSpatialSnapshot(old, Candidate(2, 2));
        REQUIRE(replacement.HasValue());
        CHECK(replacement.Value()->Id() == Id<SpatialSnapshotId>(2));
        CHECK(old->Id() == Id<SpatialSnapshotId>(1));
        CheckError(ReplacePCGSpatialSnapshot(old, Candidate(1, 2)), PCGErrors::SpatialReplacementInvalid);
        CheckError(ReplacePCGSpatialSnapshot(old, Candidate(2, 1)), PCGErrors::SpatialReplacementInvalid);
        auto foreign = Candidate(2, 2);
        foreign.provenance.source = Id<SpatialSourceId>(99);
        CheckError(ReplacePCGSpatialSnapshot(old, std::move(foreign)), PCGErrors::SpatialReplacementInvalid);
        CheckError(ReplacePCGSpatialSnapshot({}, Candidate(2, 2)), PCGErrors::SpatialReplacementInvalid);
        old.reset();
        CHECK(retired.expired());
    }

    TEST_CASE("PCG spatial replacement supports repeated publication and deterministic old-root retirement", "[unit][pcg][spatial]") {
        auto current = CapturePCGSpatialSnapshot(Candidate()).Value();
        std::vector<std::shared_ptr<const PCGSpatialSnapshot>> readers;
        std::vector<std::weak_ptr<const PCGSpatialSnapshot>> retired;
        for (std::uint64_t generation = 2; generation <= 64; ++generation) {
            retired.emplace_back(current);
            if (generation % 8 == 0)
                readers.push_back(current);
            auto replacement = ReplacePCGSpatialSnapshot(current, Candidate(generation, generation));
            REQUIRE(replacement.HasValue());
            current = replacement.Value();
            CHECK(current->Id() == Id<SpatialSnapshotId>(generation));
        }
        CHECK_FALSE(retired.back().expired());
        readers.clear();
        CHECK(retired.front().expired());
        CHECK(retired.back().expired());
        current.reset();
        CHECK(retired.back().expired());
    }

    TEST_CASE("PCG spatial identities and errors remain distinct stable contracts", "[unit][pcg][spatial]") {
        static_assert(!std::is_same_v<SpatialProviderId, SpatialSourceId>);
        static_assert(!std::is_same_v<SpatialSnapshotId, SpatialRevision>);
        static_assert(!std::is_convertible_v<std::uint64_t, SpatialElementId>);
        const std::array descriptors{&PCGErrors::SpatialInputInvalid,        &PCGErrors::SpatialCoordinatesUnsupported,
                                     &PCGErrors::SpatialCoverageUnavailable, &PCGErrors::SpatialCapacityExceeded,
                                     &PCGErrors::SpatialSnapshotStale,       &PCGErrors::SpatialReplacementInvalid};
        CHECK(std::ranges::all_of(descriptors, [](const ErrorCodeDescriptor *descriptor) {
            return descriptor->domain.Value() == "horo.pcg" && !descriptor->summary.empty() && !descriptor->remediationHint.empty();
        }));
        std::array<std::string_view, descriptors.size()> codes{};
        std::ranges::transform(descriptors, codes.begin(), [](const ErrorCodeDescriptor *descriptor) {
            return descriptor->code.Value();
        });
        std::ranges::sort(codes);
        CHECK(std::ranges::adjacent_find(codes) == codes.end());
    }
}  // namespace Horo::PCG
