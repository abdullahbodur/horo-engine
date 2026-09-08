#include "SaveCaptureSnapshotTestUtils.h"

#include <catch2/catch_test_macros.hpp>
#include <memory>

namespace Horo::Runtime {
    namespace {
        using namespace CaptureTestSupport;

        TEST_CASE("Empty optional capture publishes omission without payload records", "[unit][save][capture]") {
            auto destructionCount = std::make_shared<int>();
            CanonicalStateParticipantRegistry registry;
            const SaveRecordId record = Test::Id<SaveRecordId>(26);
            Register(registry, Descriptor("project.capture.edge_optional", {record}, false), destructionCount);
            const SaveParticipantRegistrySnapshot participants = registry.Snapshot().Value();
            auto builder = RuntimeSaveCaptureBuilder::Create(Provenance(participants, 94), participants).Value();

            const RuntimeSaveSnapshot snapshot = builder.Seal().Value();
            REQUIRE(snapshot.Records().empty());
            REQUIRE(snapshot.Participants().size() == 1);
            REQUIRE(snapshot.Participants().front().disposition == CanonicalCaptureDisposition::Omitted);
        }
    }  // namespace
}  // namespace Horo::Runtime
