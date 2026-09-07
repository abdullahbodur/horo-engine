#include "Horo/WorldStreaming/WorldAuthoringContract.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingTestUtils.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>

namespace Horo::WorldStreaming {
    namespace {
        using TestSupport::IdentityFrom;
        using TestSupport::RequireError;
        using TestSupport::World;

        [[nodiscard]] Assets::AssetId PageAsset(const std::uint8_t discriminator = 1) {
            std::array<std::uint8_t, 16> bytes{};
            bytes.back() = discriminator;
            return Assets::AssetId::FromBytes(bytes);
        }

        [[nodiscard]] WorldAuthoringContract Contract(const std::uint32_t maximumOpenPages = 4) {
            return WorldAuthoringContract::Create({}, WorldAuthoringGranularity::SpatialPage,
                                                  WorldAuthoringCollaborationMode::RevisionChecked, {maximumOpenPages})
                .Value();
        }

        [[nodiscard]] WorldAuthoringPageDescriptor Page(const std::uint64_t revision = 1, const Assets::AssetId sourceAsset = PageAsset()) {
            return {.partition = World(), .sourceAsset = sourceAsset, .revision = IdentityFrom<WorldAuthoringRevision>(revision)};
        }

        [[nodiscard]] WorldAuthoringAdmissionContext Context() {
            return {.expectedPartition = World(),
                    .currentPage = std::nullopt,
                    .openPageCount = 2,
                    .ownerState = WorldAuthoringOwnerState::Active};
        }

        TEST_CASE("World authoring contract chooses page storage independently from cooked cells", "[unit][world_streaming][authoring]") {
            const auto contract = Contract();
            REQUIRE(contract.Version() == WorldAuthoringContractVersion{});
            REQUIRE(contract.Granularity() == WorldAuthoringGranularity::SpatialPage);
            REQUIRE(contract.Collaboration() == WorldAuthoringCollaborationMode::RevisionChecked);
            REQUIRE(contract.Limits().maximumOpenPages == 4);

            const WorldAuthoringPageRequest request{.candidate = Page()};
            REQUIRE(request.candidate.IsValid());
            REQUIRE(ValidateWorldAuthoringAdmission(contract, request, Context()).Value() == WorldAuthoringAdmissionKind::Open);
        }

        TEST_CASE("World authoring contract rejects invalid versions policies and capacities", "[unit][world_streaming][authoring]") {
            RequireError(WorldAuthoringContract::Create({2, 0}, WorldAuthoringGranularity::SpatialPage,
                                                        WorldAuthoringCollaborationMode::RevisionChecked, {1}),
                         WorldStreamingErrors::AuthoringVersionUnsupported);
            RequireError(WorldAuthoringContract::Create({}, static_cast<WorldAuthoringGranularity>(255),
                                                        WorldAuthoringCollaborationMode::RevisionChecked, {1}),
                         WorldStreamingErrors::AuthoringPolicyUnsupported);
            RequireError(WorldAuthoringContract::Create({}, WorldAuthoringGranularity::SpatialPage,
                                                        static_cast<WorldAuthoringCollaborationMode>(255), {1}),
                         WorldStreamingErrors::AuthoringPolicyUnsupported);
            RequireError(WorldAuthoringContract::Create({}, WorldAuthoringGranularity::SpatialPage,
                                                        WorldAuthoringCollaborationMode::RevisionChecked, {}),
                         WorldStreamingErrors::AuthoringContractInvalid);
        }

        TEST_CASE("World authoring admission distinguishes malformed conflict and capacity failures",
                  "[unit][world_streaming][authoring]") {
            const auto contract = Contract(2);
            const auto request = WorldAuthoringPageRequest{.candidate = Page()};

            SECTION("malformed request and context") {
                auto malformedRequest = request;
                malformedRequest.candidate.sourceAsset = {};
                RequireError(ValidateWorldAuthoringAdmission(contract, malformedRequest, Context()),
                             WorldStreamingErrors::AuthoringContractInvalid);

                malformedRequest = request;
                malformedRequest.expectedRevision = WorldAuthoringRevision{};
                RequireError(ValidateWorldAuthoringAdmission(contract, malformedRequest, Context()),
                             WorldStreamingErrors::AuthoringContractInvalid);

                auto malformedContext = Context();
                malformedContext.openPageCount = 3;
                RequireError(ValidateWorldAuthoringAdmission(contract, request, malformedContext),
                             WorldStreamingErrors::AuthoringContractInvalid);

                malformedContext = Context();
                malformedContext.ownerState = static_cast<WorldAuthoringOwnerState>(255);
                RequireError(ValidateWorldAuthoringAdmission(contract, request, malformedContext),
                             WorldStreamingErrors::AuthoringContractInvalid);

                malformedContext = Context();
                malformedContext.currentPage = WorldAuthoringPageDescriptor{};
                RequireError(ValidateWorldAuthoringAdmission(contract, request, malformedContext),
                             WorldStreamingErrors::AuthoringContractInvalid);
            }

            SECTION("partition conflict and exact capacity") {
                auto wrongPartition = request;
                wrongPartition.candidate.partition = World(2);
                RequireError(ValidateWorldAuthoringAdmission(contract, wrongPartition, Context()),
                             WorldStreamingErrors::AuthoringIdentityConflict);

                auto full = Context();
                full.openPageCount = contract.Limits().maximumOpenPages;
                RequireError(ValidateWorldAuthoringAdmission(contract, request, full), WorldStreamingErrors::AuthoringCapacityExceeded);
            }
        }

        TEST_CASE("World authoring replacement uses exact immutable revision compare and swap",
                  "[unit][world_streaming][authoring][lifecycle]") {
            const auto contract = Contract();
            auto context = Context();
            context.currentPage = Page(7);
            context.openPageCount = contract.Limits().maximumOpenPages;

            const WorldAuthoringPageRequest replacement{.candidate = Page(8), .expectedRevision = IdentityFrom<WorldAuthoringRevision>(7)};
            REQUIRE(ValidateWorldAuthoringAdmission(contract, replacement, context).Value() == WorldAuthoringAdmissionKind::Replace);

            auto staleExpected = replacement;
            staleExpected.expectedRevision = IdentityFrom<WorldAuthoringRevision>(6);
            RequireError(ValidateWorldAuthoringAdmission(contract, staleExpected, context), WorldStreamingErrors::AuthoringRevisionStale);

            auto skippedRevision = replacement;
            skippedRevision.candidate.revision = IdentityFrom<WorldAuthoringRevision>(9);
            RequireError(ValidateWorldAuthoringAdmission(contract, skippedRevision, context), WorldStreamingErrors::AuthoringRevisionStale);

            auto wrongPage = replacement;
            wrongPage.candidate.sourceAsset = PageAsset(2);
            RequireError(ValidateWorldAuthoringAdmission(contract, wrongPage, context), WorldStreamingErrors::AuthoringIdentityConflict);
        }

        TEST_CASE("World authoring cancellation shutdown and revision exhaustion preserve snapshots",
                  "[unit][world_streaming][authoring][lifecycle]") {
            const auto contract = Contract();
            const auto request = WorldAuthoringPageRequest{.candidate = Page()};

            for (const auto state : {WorldAuthoringOwnerState::Cancelling, WorldAuthoringOwnerState::Closed}) {
                auto context = Context();
                context.ownerState = state;
                const auto original = context;
                RequireError(ValidateWorldAuthoringAdmission(contract, request, context),
                             WorldStreamingErrors::AuthoringLifecycleUnavailable);
                REQUIRE(context.expectedPartition == original.expectedPartition);
                REQUIRE(context.currentPage == original.currentPage);
                REQUIRE(context.openPageCount == original.openPageCount);
                REQUIRE(context.ownerState == original.ownerState);
            }

            auto exhausted = Context();
            exhausted.currentPage = Page(std::numeric_limits<std::uint64_t>::max());
            const WorldAuthoringPageRequest impossible{.candidate = Page(std::numeric_limits<std::uint64_t>::max()),
                                                       .expectedRevision =
                                                           IdentityFrom<WorldAuthoringRevision>(std::numeric_limits<std::uint64_t>::max())};
            RequireError(ValidateWorldAuthoringAdmission(contract, impossible, exhausted), WorldStreamingErrors::GenerationExhausted);
            REQUIRE(exhausted.currentPage->revision.Value() == std::numeric_limits<std::uint64_t>::max());
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
