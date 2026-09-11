#include "Horo/WorldStreaming/WorldSpatialObjectDescriptor.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingTestUtils.h"

#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <type_traits>

namespace Horo::WorldStreaming {
    namespace {
        using TestSupport::Asset;
        using TestSupport::IdentityFrom;
        using TestSupport::RequireError;

        [[nodiscard]] WorldSpatialObjectDescriptor Descriptor(const std::uint64_t object = 7, const std::uint64_t revision = 1) {
            return {
                .version = {},
                .address = {Asset(3), object},
                .revision = IdentityFrom<WorldAuthoringRevision>(revision),
                .sourceAsset = Asset(9),
                .bounds = {Math::WorldCoordinate64::FromMillimeters(-100, 20, 30), Math::WorldCoordinate64::FromMillimeters(400, 500, 600)},
                .placement = WorldSpatialObjectPlacementClass::Spatial,
            };
        }

        [[nodiscard]] WorldSpatialObjectAdmissionContext Context() {
            return {.currentDescriptor = std::nullopt,
                    .objectCount = 2,
                    .objectCapacity = 4,
                    .ownerState = WorldSpatialObjectOwnerState::Active};
        }

        TEST_CASE("Spatial object descriptor is inert durable authored metadata", "[unit][world_streaming][spatial_object]") {
            const auto descriptor = Descriptor();
            REQUIRE(descriptor.IsValid());
            REQUIRE(ValidateWorldSpatialObjectDescriptor(descriptor).HasValue());
            auto alwaysPresent = descriptor;
            alwaysPresent.placement = WorldSpatialObjectPlacementClass::AlwaysPresent;
            REQUIRE(ValidateWorldSpatialObjectDescriptor(alwaysPresent).HasValue());
            REQUIRE(ValidateWorldSpatialObjectAdmission({descriptor, std::nullopt}, Context()).Value() ==
                    WorldSpatialObjectAdmissionKind::Insert);
            CHECK(descriptor.address.page == Asset(3));
            CHECK(descriptor.sourceAsset == Asset(9));
            CHECK(descriptor.bounds.minimum.Millimeters()[0] == -100);
            static_assert(std::is_trivially_copyable_v<WorldSpatialObjectDescriptor>);
            static_assert(!std::is_pointer_v<decltype(WorldSpatialObjectDescriptor::sourceAsset)>);
        }

        TEST_CASE("Spatial object descriptor distinguishes malformed and unsupported values", "[unit][world_streaming][spatial_object]") {
            auto descriptor = Descriptor();
            descriptor.address = {};
            RequireError(ValidateWorldSpatialObjectDescriptor(descriptor), WorldStreamingErrors::SpatialObjectDescriptorInvalid);

            descriptor = Descriptor();
            descriptor.address.object = 0;
            RequireError(ValidateWorldSpatialObjectDescriptor(descriptor), WorldStreamingErrors::SpatialObjectDescriptorInvalid);

            descriptor = Descriptor();
            descriptor.revision = {};
            RequireError(ValidateWorldSpatialObjectDescriptor(descriptor), WorldStreamingErrors::SpatialObjectDescriptorInvalid);

            descriptor = Descriptor();
            descriptor.sourceAsset = {};
            RequireError(ValidateWorldSpatialObjectDescriptor(descriptor), WorldStreamingErrors::SpatialObjectDescriptorInvalid);

            descriptor = Descriptor();
            descriptor.bounds.minimum = Math::WorldCoordinate64::FromMillimeters(401, 20, 30);
            RequireError(ValidateWorldSpatialObjectDescriptor(descriptor), WorldStreamingErrors::SpatialObjectDescriptorInvalid);

            descriptor = Descriptor();
            descriptor.version.minor = 1;
            RequireError(ValidateWorldSpatialObjectDescriptor(descriptor), WorldStreamingErrors::SpatialObjectVersionUnsupported);

            descriptor = Descriptor();
            descriptor.version.major = 2;
            RequireError(ValidateWorldSpatialObjectDescriptor(descriptor), WorldStreamingErrors::SpatialObjectVersionUnsupported);

            descriptor = Descriptor();
            descriptor.placement = static_cast<WorldSpatialObjectPlacementClass>(255);
            RequireError(ValidateWorldSpatialObjectDescriptor(descriptor), WorldStreamingErrors::SpatialObjectPlacementUnsupported);
        }

        TEST_CASE("Spatial object replacement requires exact identity and revision successor",
                  "[unit][world_streaming][spatial_object][replacement]") {
            auto context = Context();
            context.currentDescriptor = Descriptor(7, 5);
            context.objectCount = context.objectCapacity;
            const WorldSpatialObjectRequest replacement{.candidate = Descriptor(7, 6),
                                                        .expectedRevision = IdentityFrom<WorldAuthoringRevision>(5)};

            REQUIRE(ValidateWorldSpatialObjectAdmission(replacement, context).Value() == WorldSpatialObjectAdmissionKind::Replace);
            CHECK(context.currentDescriptor->revision == IdentityFrom<WorldAuthoringRevision>(5));
            CHECK(context.objectCount == context.objectCapacity);

            auto stale = replacement;
            stale.expectedRevision = IdentityFrom<WorldAuthoringRevision>(4);
            RequireError(ValidateWorldSpatialObjectAdmission(stale, context), WorldStreamingErrors::SpatialObjectRevisionStale);

            stale = replacement;
            stale.candidate.revision = IdentityFrom<WorldAuthoringRevision>(7);
            RequireError(ValidateWorldSpatialObjectAdmission(stale, context), WorldStreamingErrors::SpatialObjectRevisionStale);

            auto conflict = replacement;
            conflict.candidate.address.object = 8;
            RequireError(ValidateWorldSpatialObjectAdmission(conflict, context), WorldStreamingErrors::SpatialObjectIdentityConflict);
        }

        TEST_CASE("Spatial object admission rejects capacity and malformed owner snapshots transactionally",
                  "[unit][world_streaming][spatial_object]") {
            auto context = Context();
            context.objectCount = context.objectCapacity;
            RequireError(ValidateWorldSpatialObjectAdmission({Descriptor(), std::nullopt}, context),
                         WorldStreamingErrors::SpatialObjectCapacityExceeded);

            context.objectCount = context.objectCapacity + 1;
            RequireError(ValidateWorldSpatialObjectAdmission({Descriptor(), std::nullopt}, context),
                         WorldStreamingErrors::SpatialObjectDescriptorInvalid);

            context = Context();
            context.objectCapacity = 0;
            RequireError(ValidateWorldSpatialObjectAdmission({Descriptor(), std::nullopt}, context),
                         WorldStreamingErrors::SpatialObjectDescriptorInvalid);

            context = Context();
            auto request = WorldSpatialObjectRequest{Descriptor(), WorldAuthoringRevision{}};
            RequireError(ValidateWorldSpatialObjectAdmission(request, context), WorldStreamingErrors::SpatialObjectDescriptorInvalid);
            CHECK(context.objectCount == 2);
            CHECK_FALSE(context.currentDescriptor.has_value());

            context = Context();
            request = WorldSpatialObjectRequest{Descriptor(), IdentityFrom<WorldAuthoringRevision>(1)};
            RequireError(ValidateWorldSpatialObjectAdmission(request, context), WorldStreamingErrors::SpatialObjectRevisionStale);

            context = Context();
            context.ownerState = static_cast<WorldSpatialObjectOwnerState>(255);
            RequireError(ValidateWorldSpatialObjectAdmission({Descriptor(), std::nullopt}, context),
                         WorldStreamingErrors::SpatialObjectDescriptorInvalid);

            context = Context();
            context.currentDescriptor = Descriptor();
            context.currentDescriptor->sourceAsset = {};
            RequireError(ValidateWorldSpatialObjectAdmission({Descriptor(), std::nullopt}, context),
                         WorldStreamingErrors::SpatialObjectDescriptorInvalid);

            context = Context();
            context.currentDescriptor = Descriptor();
            context.currentDescriptor->version.major = WorldSpatialObjectSchemaVersion::CurrentMajor + 1;
            RequireError(ValidateWorldSpatialObjectAdmission({Descriptor(), std::nullopt}, context),
                         WorldStreamingErrors::SpatialObjectVersionUnsupported);
        }

        TEST_CASE("Cancelling and closed spatial object owners reject new and replacement admission",
                  "[unit][world_streaming][spatial_object][lifecycle]") {
            for (const auto state : {WorldSpatialObjectOwnerState::Cancelling, WorldSpatialObjectOwnerState::Closed}) {
                auto context = Context();
                context.ownerState = state;
                RequireError(ValidateWorldSpatialObjectAdmission({Descriptor(), std::nullopt}, context),
                             WorldStreamingErrors::SpatialObjectLifecycleUnavailable);

                context.currentDescriptor = Descriptor(7, 1);
                RequireError(ValidateWorldSpatialObjectAdmission({.candidate = Descriptor(7, 2),
                                                                  .expectedRevision = IdentityFrom<WorldAuthoringRevision>(1)},
                                                                 context),
                             WorldStreamingErrors::SpatialObjectLifecycleUnavailable);
                CHECK(context.currentDescriptor->revision == IdentityFrom<WorldAuthoringRevision>(1));
            }
        }

        TEST_CASE("Exhausted spatial object revision cannot wrap during replacement",
                  "[unit][world_streaming][spatial_object][replacement]") {
            constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
            auto context = Context();
            context.currentDescriptor = Descriptor(7, maximum);
            const WorldSpatialObjectRequest request{.candidate = Descriptor(7, 1),
                                                    .expectedRevision = IdentityFrom<WorldAuthoringRevision>(maximum)};
            RequireError(ValidateWorldSpatialObjectAdmission(request, context), WorldStreamingErrors::GenerationExhausted);
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
