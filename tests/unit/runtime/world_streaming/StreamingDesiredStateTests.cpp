#include "Horo/WorldStreaming/StreamingDesiredState.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingTestUtils.h"

#include <catch2/catch_test_macros.hpp>

namespace Horo::WorldStreaming {
    namespace {
        using TestSupport::Context;
        using TestSupport::Descriptor;
        using TestSupport::IdentityFrom;
        using TestSupport::RequireError;

        TEST_CASE("Streaming desired residency and retention remain independent typed axes", "[unit][world_streaming][desired_state]") {
            const auto unloaded =
                StreamingSourceDesiredState::Create(Descriptor(), StreamingDesiredResidency::Unloaded, StreamingRetention::Releasable)
                    .Value();
            const auto loadedPinned =
                StreamingSourceDesiredState::Create(Descriptor(), StreamingDesiredResidency::Loaded, StreamingRetention::Pinned).Value();
            const auto activated =
                StreamingSourceDesiredState::Create(Descriptor(), StreamingDesiredResidency::Activated, StreamingRetention::Releasable)
                    .Value();

            REQUIRE_FALSE(unloaded.RequiresLoading());
            REQUIRE_FALSE(unloaded.RequiresActivation());
            REQUIRE(loadedPinned.RequiresLoading());
            REQUIRE_FALSE(loadedPinned.RequiresActivation());
            REQUIRE(loadedPinned.IsPinned());
            REQUIRE(loadedPinned.Residency() == StreamingDesiredResidency::Loaded);
            REQUIRE(loadedPinned.Retention() == StreamingRetention::Pinned);
            REQUIRE(activated.RequiresLoading());
            REQUIRE(activated.RequiresActivation());
            REQUIRE_FALSE(activated.IsPinned());
        }

        TEST_CASE("Streaming desired states reject malformed and unsupported values", "[unit][world_streaming][desired_state]") {
            RequireError(StreamingSourceDesiredState::Create(Descriptor(), StreamingDesiredResidency::Unloaded, StreamingRetention::Pinned),
                         WorldStreamingErrors::SourceDesiredStateInvalid);
            RequireError(StreamingSourceDesiredState::Create(Descriptor(), static_cast<StreamingDesiredResidency>(255),
                                                             StreamingRetention::Releasable),
                         WorldStreamingErrors::SourceDesiredStateUnsupported);
            RequireError(StreamingSourceDesiredState::Create(Descriptor(), StreamingDesiredResidency::Loaded,
                                                             static_cast<StreamingRetention>(255)),
                         WorldStreamingErrors::SourceDesiredStateUnsupported);

            auto malformed = Descriptor();
            malformed.id = {};
            RequireError(StreamingSourceDesiredState::Create(malformed, StreamingDesiredResidency::Loaded, StreamingRetention::Releasable),
                         WorldStreamingErrors::SourceDescriptorInvalid);

            auto unsupported = Descriptor();
            unsupported.intent = static_cast<StreamingSourceIntent>(255);
            RequireError(StreamingSourceDesiredState::Create(unsupported, StreamingDesiredResidency::Loaded,
                                                             StreamingRetention::Releasable),
                         WorldStreamingErrors::SourceIntentUnsupported);
        }

        TEST_CASE("Streaming desired-state admission preserves replacement and capacity rules", "[unit][world_streaming][desired_state]") {
            const auto desired =
                StreamingSourceDesiredState::Create(Descriptor(), StreamingDesiredResidency::Activated, StreamingRetention::Pinned).Value();
            const auto context = Context();
            REQUIRE(ValidateStreamingSourceDesiredStateAdmission(desired, context).Value() == StreamingSourceAdmissionKind::Insert);
            REQUIRE(context.activeSourceCount == 2);
            REQUIRE_FALSE(context.currentRevision.has_value());

            auto full = Context();
            full.activeSourceCount = full.sourceCapacity;
            RequireError(ValidateStreamingSourceDesiredStateAdmission(desired, full), WorldStreamingErrors::SourceCapacityExceeded);

            full.currentRevision = IdentityFrom<StreamingSourceRevision>(1);
            RequireError(ValidateStreamingSourceDesiredStateAdmission(desired, full), WorldStreamingErrors::SourceRevisionStale);

            const auto replacement = StreamingSourceDesiredState::Create(Descriptor(IdentityFrom<StreamingSourceRevision>(2)),
                                                                         StreamingDesiredResidency::Loaded, StreamingRetention::Releasable)
                                         .Value();
            REQUIRE(ValidateStreamingSourceDesiredStateAdmission(replacement, full).Value() == StreamingSourceAdmissionKind::Replace);
            REQUIRE(full.activeSourceCount == full.sourceCapacity);
        }

        TEST_CASE("Streaming desired-state admission rejects stale and unavailable owners without mutation",
                  "[unit][world_streaming][desired_state][lifecycle]") {
            const auto desired =
                StreamingSourceDesiredState::Create(Descriptor(), StreamingDesiredResidency::Loaded, StreamingRetention::Pinned).Value();

            auto stale = Context();
            ++stale.expectedOwner.generation;
            RequireError(ValidateStreamingSourceDesiredStateAdmission(desired, stale), WorldStreamingErrors::SourceOwnerStale);

            for (const auto state : {StreamingSourceOwnerState::Cancelling, StreamingSourceOwnerState::Closed}) {
                auto unavailable = Context();
                unavailable.ownerState = state;
                RequireError(ValidateStreamingSourceDesiredStateAdmission(desired, unavailable),
                             WorldStreamingErrors::SourceLifecycleUnavailable);
                REQUIRE(unavailable.activeSourceCount == 2);
                REQUIRE_FALSE(unavailable.currentRevision.has_value());
            }
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
