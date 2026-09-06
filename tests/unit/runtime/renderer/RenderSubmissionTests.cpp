#include "Horo/Runtime/Render/RenderSubmission.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <limits>

namespace {
    using namespace Horo::Render;
    using namespace std::chrono_literals;
}  // namespace

TEST_CASE("Render queue roles and identities stay backend-neutral", "[runtime][renderer][submission]") {
    REQUIRE(IsRenderQueueRoleValid(RenderQueueRole::Graphics));
    REQUIRE(IsRenderQueueRoleValid(RenderQueueRole::Compute));
    REQUIRE(IsRenderQueueRoleValid(RenderQueueRole::Transfer));
    REQUIRE_FALSE(IsRenderQueueRoleValid(static_cast<RenderQueueRole>(255)));

    REQUIRE_FALSE(RenderQueueId{}.IsValid());
    REQUIRE(RenderQueueId{1}.IsValid());
    REQUIRE(RenderQueueAssignment{RenderQueueRole::Graphics, RenderQueueId{1}}.IsValid());
    REQUIRE(RenderQueueAssignment{RenderQueueRole::Compute, RenderQueueId{1}}.IsValid());
    REQUIRE_FALSE(RenderQueueAssignment{static_cast<RenderQueueRole>(255), RenderQueueId{1}}.IsValid());
    REQUIRE_FALSE(RenderQueueAssignment{RenderQueueRole::Transfer, RenderQueueId{}}.IsValid());

    constexpr RenderQueueAssignment graphics{RenderQueueRole::Graphics, RenderQueueId{1}};
    constexpr RenderQueueAssignment sameGraphics{RenderQueueRole::Graphics, RenderQueueId{1}};
    constexpr RenderQueueAssignment compute{RenderQueueRole::Compute, RenderQueueId{1}};
    STATIC_REQUIRE(graphics == sameGraphics);
    STATIC_REQUIRE(graphics < compute);
}

TEST_CASE("Submission orders and timeline points reserve invalid and wrap values", "[runtime][renderer][submission]") {
    constexpr std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    REQUIRE_FALSE(RenderSubmissionOrder{}.IsValid());
    REQUIRE(RenderSubmissionOrder{1}.IsValid());
    REQUIRE_FALSE(RenderSubmissionOrder{maximum}.IsValid());

    REQUIRE_FALSE(RenderTimelinePoint{RenderQueueId{}, 1}.IsValid());
    REQUIRE_FALSE(RenderTimelinePoint{RenderQueueId{1}, 0}.IsValid());
    REQUIRE(RenderTimelinePoint{RenderQueueId{1}, 1}.IsValid());
    REQUIRE_FALSE(RenderTimelinePoint{RenderQueueId{1}, maximum}.IsValid());
}

TEST_CASE("Single queue submissions preserve one deterministic total order", "[runtime][renderer][submission]") {
    constexpr RenderQueueId queue{1};
    constexpr RenderTimelinePoint prior{queue, 3};
    static constexpr std::array waits{prior};
    static constexpr RenderQueueSubmission submission{queue, RenderSubmissionOrder{7}, waits, RenderTimelinePoint{queue, 4}};

    STATIC_REQUIRE(submission.IsValid());
    STATIC_REQUIRE(submission.order < RenderSubmissionOrder{8});
    STATIC_REQUIRE(submission.waits.front() == prior);
}

TEST_CASE("Cross queue dependencies remain explicit GPU timeline waits", "[runtime][renderer][submission]") {
    constexpr RenderQueueId graphics{1};
    constexpr RenderQueueId compute{2};
    constexpr RenderQueueId transfer{3};
    static constexpr std::array waits{RenderTimelinePoint{compute, 9}, RenderTimelinePoint{transfer, 4}};
    static constexpr RenderQueueSubmission submission{graphics, RenderSubmissionOrder{12}, waits, RenderTimelinePoint{graphics, 6}};

    STATIC_REQUIRE(submission.IsValid());
    STATIC_REQUIRE(submission.waits[0].queue == compute);
    STATIC_REQUIRE(submission.waits[1].queue == transfer);
}

TEST_CASE("Submission validation rejects malformed ordering and completion", "[runtime][renderer][submission]") {
    constexpr RenderQueueId graphics{1};
    constexpr RenderQueueId compute{2};
    constexpr std::array<RenderTimelinePoint, 0> noWaits{};
    constexpr std::array invalidWait{RenderTimelinePoint{RenderQueueId{}, 1}};
    constexpr std::array samePointWait{RenderTimelinePoint{graphics, 5}};
    constexpr std::array futureWait{RenderTimelinePoint{graphics, 6}};

    STATIC_REQUIRE_FALSE(RenderQueueSubmission{RenderQueueId{}, RenderSubmissionOrder{1}, noWaits, {graphics, 1}}.IsValid());
    STATIC_REQUIRE_FALSE(RenderQueueSubmission{graphics, RenderSubmissionOrder{}, noWaits, {graphics, 1}}.IsValid());
    STATIC_REQUIRE_FALSE(RenderQueueSubmission{graphics, RenderSubmissionOrder{1}, noWaits, {compute, 1}}.IsValid());
    STATIC_REQUIRE_FALSE(RenderQueueSubmission{graphics, RenderSubmissionOrder{1}, invalidWait, {graphics, 5}}.IsValid());
    STATIC_REQUIRE_FALSE(RenderQueueSubmission{graphics, RenderSubmissionOrder{1}, samePointWait, {graphics, 5}}.IsValid());
    STATIC_REQUIRE_FALSE(RenderQueueSubmission{graphics, RenderSubmissionOrder{1}, futureWait, {graphics, 5}}.IsValid());
}

TEST_CASE("CPU completion waits are explicit and always finite", "[runtime][renderer][submission]") {
    constexpr RenderTimelinePoint completion{RenderQueueId{1}, 8};

    STATIC_REQUIRE(RenderCpuWait{completion, RenderCpuWaitPurpose::Poll, 0ns}.IsValid());
    STATIC_REQUIRE(RenderCpuWait{completion, RenderCpuWaitPurpose::BoundedReadback, 2ms}.IsValid());
    STATIC_REQUIRE(RenderCpuWait{completion, RenderCpuWaitPurpose::DeterministicTest, 1s}.IsValid());
    STATIC_REQUIRE(RenderCpuWait{completion, RenderCpuWaitPurpose::Teardown, 3s}.IsValid());
    STATIC_REQUIRE(RenderCpuWait{completion, RenderCpuWaitPurpose::Recovery, 5s}.IsValid());

    STATIC_REQUIRE_FALSE(RenderCpuWait{{}, RenderCpuWaitPurpose::Poll, 0ns}.IsValid());
    STATIC_REQUIRE_FALSE(RenderCpuWait{completion, RenderCpuWaitPurpose::Poll, 1ns}.IsValid());
    STATIC_REQUIRE_FALSE(RenderCpuWait{completion, RenderCpuWaitPurpose::BoundedReadback, 0ns}.IsValid());
    STATIC_REQUIRE_FALSE(RenderCpuWait{completion, RenderCpuWaitPurpose::Teardown, -1ns}.IsValid());
    STATIC_REQUIRE_FALSE(RenderCpuWait{completion, static_cast<RenderCpuWaitPurpose>(255), 1ms}.IsValid());
}
