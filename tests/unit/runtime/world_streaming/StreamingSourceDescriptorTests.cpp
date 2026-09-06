#include "Horo/WorldStreaming/StreamingSourceDescriptor.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingTestUtils.h"

#include <catch2/catch_test_macros.hpp>
#include <limits>

namespace Horo::WorldStreaming {
    namespace {
        using TestSupport::Context;
        using TestSupport::Descriptor;
        using TestSupport::IdentityFrom;
        using TestSupport::Owner;
        using TestSupport::RequireError;
        using TestSupport::World;

        TEST_CASE("Streaming source descriptors are inert valid value contracts", "[unit][world_streaming][source]") {
            const auto descriptor = Descriptor();
            REQUIRE(descriptor.IsValid());
            REQUIRE(ValidateStreamingSourceDescriptor(descriptor).HasValue());
            REQUIRE(ValidateStreamingSourceAdmission(descriptor, Context()).Value() == StreamingSourceAdmissionKind::Insert);

            REQUIRE(StreamingSourcePriority::Create(0.0F).HasValue());
            REQUIRE(StreamingSourcePriority::Create(std::numeric_limits<float>::max()).HasValue());
            REQUIRE(StreamingSourcePriority::Create(-1.0F).HasError());
            REQUIRE(StreamingSourcePriority::Create(std::numeric_limits<float>::infinity()).HasError());
            REQUIRE(StreamingSourcePriority::Create(std::numeric_limits<float>::quiet_NaN()).HasError());
        }

        TEST_CASE("Streaming source descriptors distinguish malformed and unsupported input", "[unit][world_streaming][source]") {
            auto descriptor = Descriptor();
            descriptor.id = {};
            RequireError(ValidateStreamingSourceDescriptor(descriptor), WorldStreamingErrors::SourceDescriptorInvalid);

            descriptor = Descriptor();
            descriptor.owner.slot = StreamingSourceOwnerToken::InvalidSlot;
            RequireError(ValidateStreamingSourceDescriptor(descriptor), WorldStreamingErrors::SourceDescriptorInvalid);

            descriptor = Descriptor();
            descriptor.intent = static_cast<StreamingSourceIntent>(255);
            RequireError(ValidateStreamingSourceDescriptor(descriptor), WorldStreamingErrors::SourceIntentUnsupported);

            auto malformedContext = Context();
            malformedContext.activeSourceCount = malformedContext.sourceCapacity + 1;
            RequireError(ValidateStreamingSourceAdmission(Descriptor(), malformedContext), WorldStreamingErrors::SourceDescriptorInvalid);

            malformedContext = Context();
            malformedContext.expectedOwner = {};
            RequireError(ValidateStreamingSourceAdmission(Descriptor(), malformedContext), WorldStreamingErrors::SourceDescriptorInvalid);

            malformedContext = Context();
            malformedContext.currentRevision = StreamingSourceRevision{};
            RequireError(ValidateStreamingSourceAdmission(Descriptor(), malformedContext), WorldStreamingErrors::SourceDescriptorInvalid);

            malformedContext = Context();
            malformedContext.ownerState = static_cast<StreamingSourceOwnerState>(255);
            RequireError(ValidateStreamingSourceAdmission(Descriptor(), malformedContext), WorldStreamingErrors::SourceDescriptorInvalid);
        }

        TEST_CASE("Streaming source owner tokens fence replacement and reused slots", "[unit][world_streaming][source][lifecycle]") {
            const auto descriptor = Descriptor();
            REQUIRE(descriptor.owner.IsValid());
            REQUIRE_FALSE(StreamingSourceOwnerToken{}.IsValid());

            auto staleGeneration = Context();
            staleGeneration.expectedOwner = Owner(2);
            RequireError(ValidateStreamingSourceAdmission(descriptor, staleGeneration), WorldStreamingErrors::SourceOwnerStale);

            auto staleEpoch = Context();
            staleEpoch.expectedOwner = Owner(1, IdentityFrom<PartitionEpoch>(2));
            RequireError(ValidateStreamingSourceAdmission(descriptor, staleEpoch), WorldStreamingErrors::SourceOwnerStale);

            auto otherPartition = Context();
            otherPartition.expectedOwner.partition = World(2);
            RequireError(ValidateStreamingSourceAdmission(descriptor, otherPartition), WorldStreamingErrors::SourceOwnerStale);
        }

        TEST_CASE("Streaming source admission distinguishes capacity from stale replacement", "[unit][world_streaming][source]") {
            auto full = Context();
            full.activeSourceCount = full.sourceCapacity;
            RequireError(ValidateStreamingSourceAdmission(Descriptor(), full), WorldStreamingErrors::SourceCapacityExceeded);

            full.currentRevision = IdentityFrom<StreamingSourceRevision>(4);
            RequireError(ValidateStreamingSourceAdmission(Descriptor(IdentityFrom<StreamingSourceRevision>(4)), full),
                         WorldStreamingErrors::SourceRevisionStale);
            RequireError(ValidateStreamingSourceAdmission(Descriptor(IdentityFrom<StreamingSourceRevision>(3)), full),
                         WorldStreamingErrors::SourceRevisionStale);
            REQUIRE(ValidateStreamingSourceAdmission(Descriptor(IdentityFrom<StreamingSourceRevision>(5)), full).Value() ==
                    StreamingSourceAdmissionKind::Replace);
            REQUIRE(full.activeSourceCount == full.sourceCapacity);
        }

        TEST_CASE("Cancelling and closed source owners reject admission without changing snapshots",
                  "[unit][world_streaming][source][lifecycle]") {
            auto context = Context();
            context.ownerState = StreamingSourceOwnerState::Cancelling;
            RequireError(ValidateStreamingSourceAdmission(Descriptor(), context), WorldStreamingErrors::SourceLifecycleUnavailable);
            REQUIRE(context.activeSourceCount == 2);
            REQUIRE_FALSE(context.currentRevision.has_value());

            context.ownerState = StreamingSourceOwnerState::Closed;
            RequireError(ValidateStreamingSourceAdmission(Descriptor(), context), WorldStreamingErrors::SourceLifecycleUnavailable);
            REQUIRE(context.activeSourceCount == 2);
            REQUIRE_FALSE(context.currentRevision.has_value());
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
