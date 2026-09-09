#include "Horo/WorldStreaming/StreamingBudgetModel.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingTestUtils.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <limits>
#include <utility>

namespace Horo::WorldStreaming {
    namespace {
        using TestSupport::IdentityFrom;
        using TestSupport::RequireError;
        using namespace std::chrono_literals;

        constexpr std::array Dimensions{
            StreamingBudgetDimension::CpuResidentBytes,     StreamingBudgetDimension::GpuResidentBytes,
            StreamingBudgetDimension::StagingBytes,         StreamingBudgetDimension::IoBytesInFlight,
            StreamingBudgetDimension::QueueScratchBytes,    StreamingBudgetDimension::RetiredBytes,
            StreamingBudgetDimension::OwnerWorkNanoseconds,
        };

        using BudgetValues = std::array<std::uint64_t, StreamingBudgetDimensionCount>;

        [[nodiscard]] StreamingBudgetAmounts Amounts(const BudgetValues &values = {}) {
            std::array<StreamingBudgetAmount, StreamingBudgetDimensionCount> entries{};
            for (std::size_t index = 0; index < entries.size(); ++index)
                entries[index] = {.dimension = Dimensions[index], .value = values[index]};
            return StreamingBudgetAmounts::Create(entries).Value();
        }

        [[nodiscard]] BudgetValues WithValue(const StreamingBudgetDimension dimension, const std::uint64_t value,
                                             BudgetValues values = {}) {
            values[static_cast<std::size_t>(dimension)] = value;
            return values;
        }

        [[nodiscard]] std::array<StreamingBudgetLimit, StreamingBudgetDimensionCount> Limits(const std::uint64_t softTarget = 100,
                                                                                             const std::uint64_t hardLimit = 200) {
            std::array<StreamingBudgetLimit, StreamingBudgetDimensionCount> limits{};
            for (std::size_t index = 0; index < limits.size(); ++index)
                limits[index] = {.dimension = Dimensions[index], .softTarget = softTarget, .hardLimit = hardLimit};
            return limits;
        }

        [[nodiscard]] StreamingBudgetPolicy Policy(const std::uint64_t revision = 1, const std::chrono::nanoseconds window = 50ns,
                                                   std::array<StreamingBudgetLimit, StreamingBudgetDimensionCount> limits = Limits()) {
            return StreamingBudgetPolicy::Create(IdentityFrom<StreamingBudgetPolicyRevision>(revision), limits, window).Value();
        }

        [[nodiscard]] StreamingBudgetSample Sample(const StreamingBudgetPolicy &policy, const StreamingBudgetAmounts &usage = Amounts(),
                                                   const std::uint64_t revision = 1, const std::chrono::nanoseconds start = 100ns,
                                                   const std::chrono::nanoseconds observed = 125ns) {
            return StreamingBudgetSample::Create(policy, IdentityFrom<StreamingBudgetSampleRevision>(revision), start, observed, usage)
                .Value();
        }

        [[nodiscard]] StreamingBudgetEvaluationContext Context(const StreamingBudgetPolicy &policy, const StreamingBudgetSample &sample,
                                                               const std::chrono::nanoseconds now = 130ns) {
            return {
                .expectedPolicyRevision = policy.Revision(),
                .expectedSampleRevision = sample.Revision(),
                .evaluationTime = now,
            };
        }

        TEST_CASE("Budget amounts require every independent dimension exactly once", "[unit][world_streaming][budget]") {
            std::array<StreamingBudgetAmount, StreamingBudgetDimensionCount> reversed{};
            for (std::size_t index = 0; index < reversed.size(); ++index)
                reversed[index] = {.dimension = Dimensions[reversed.size() - index - 1], .value = index + 10};

            const auto amounts = StreamingBudgetAmounts::Create(reversed).Value();
            const auto canonical = amounts.Entries();
            REQUIRE(canonical.front().dimension == StreamingBudgetDimension::CpuResidentBytes);
            REQUIRE(canonical.back().dimension == StreamingBudgetDimension::OwnerWorkNanoseconds);
            REQUIRE(amounts.Value(StreamingBudgetDimension::OwnerWorkNanoseconds).Value() == 10);
            REQUIRE_FALSE(amounts.IsZero());
            REQUIRE(Amounts().IsZero());

            RequireError(StreamingBudgetAmounts::Create(std::span{reversed}.first(reversed.size() - 1)),
                         WorldStreamingErrors::BudgetModelInvalid);
            reversed.back().dimension = reversed.front().dimension;
            RequireError(StreamingBudgetAmounts::Create(reversed), WorldStreamingErrors::BudgetModelInvalid);
            reversed.back().dimension = static_cast<StreamingBudgetDimension>(255);
            RequireError(StreamingBudgetAmounts::Create(reversed), WorldStreamingErrors::BudgetDimensionUnsupported);
            RequireError(amounts.Value(StreamingBudgetDimension::Count), WorldStreamingErrors::BudgetDimensionUnsupported);
        }

        TEST_CASE("Budget policy owns canonical soft hard and sampling limits", "[unit][world_streaming][budget]") {
            auto shuffled = Limits();
            std::swap(shuffled.front(), shuffled.back());
            const auto policy = StreamingBudgetPolicy::Create(IdentityFrom<StreamingBudgetPolicyRevision>(4), shuffled, 2ms).Value();

            REQUIRE(policy.Revision() == IdentityFrom<StreamingBudgetPolicyRevision>(4));
            REQUIRE(policy.SamplingWindow() == 2ms);
            REQUIRE(policy.Limits().front().dimension == StreamingBudgetDimension::CpuResidentBytes);
            REQUIRE(policy.Limit(StreamingBudgetDimension::RetiredBytes).Value().hardLimit == 200);
            RequireError(policy.Limit(StreamingBudgetDimension::Count), WorldStreamingErrors::BudgetDimensionUnsupported);
        }

        TEST_CASE("Budget policy rejects malformed and unsupported definitions transactionally", "[unit][world_streaming][budget]") {
            const auto valid = Limits();
            RequireError(StreamingBudgetPolicy::Create({}, valid, 1ns), WorldStreamingErrors::BudgetModelInvalid);
            RequireError(StreamingBudgetPolicy::Create(IdentityFrom<StreamingBudgetPolicyRevision>(1), valid, 0ns),
                         WorldStreamingErrors::BudgetModelInvalid);
            RequireError(StreamingBudgetPolicy::Create(IdentityFrom<StreamingBudgetPolicyRevision>(1), valid, -1ns),
                         WorldStreamingErrors::BudgetModelInvalid);
            RequireError(StreamingBudgetPolicy::Create(IdentityFrom<StreamingBudgetPolicyRevision>(1),
                                                       std::span{valid}.first(valid.size() - 1), 1ns),
                         WorldStreamingErrors::BudgetModelInvalid);

            auto malformed = valid;
            malformed.front().hardLimit = 0;
            RequireError(StreamingBudgetPolicy::Create(IdentityFrom<StreamingBudgetPolicyRevision>(1), malformed, 1ns),
                         WorldStreamingErrors::BudgetModelInvalid);
            malformed = valid;
            malformed.front().softTarget = malformed.front().hardLimit + 1;
            RequireError(StreamingBudgetPolicy::Create(IdentityFrom<StreamingBudgetPolicyRevision>(1), malformed, 1ns),
                         WorldStreamingErrors::BudgetModelInvalid);
            malformed = valid;
            malformed.back().dimension = malformed.front().dimension;
            RequireError(StreamingBudgetPolicy::Create(IdentityFrom<StreamingBudgetPolicyRevision>(1), malformed, 1ns),
                         WorldStreamingErrors::BudgetModelInvalid);
            malformed.back().dimension = static_cast<StreamingBudgetDimension>(255);
            RequireError(StreamingBudgetPolicy::Create(IdentityFrom<StreamingBudgetPolicyRevision>(1), malformed, 1ns),
                         WorldStreamingErrors::BudgetDimensionUnsupported);
        }

        TEST_CASE("Budget samples enforce bounded half-open monotonic windows", "[unit][world_streaming][budget][sample]") {
            const auto policy = Policy();
            const auto usage = Amounts();
            const auto atStart = StreamingBudgetSample::Create(policy, IdentityFrom<StreamingBudgetSampleRevision>(1), 100ns, 100ns, usage);
            const auto atLastTick =
                StreamingBudgetSample::Create(policy, IdentityFrom<StreamingBudgetSampleRevision>(2), 100ns, 149ns, usage);
            REQUIRE(atStart.HasValue());
            REQUIRE(atLastTick.HasValue());
            REQUIRE(atLastTick.Value().WindowStart() == 100ns);
            REQUIRE(atLastTick.Value().ObservedAt() == 149ns);
            REQUIRE(atLastTick.Value().Usage() == usage);

            RequireError(StreamingBudgetSample::Create(policy, {}, 100ns, 100ns, usage), WorldStreamingErrors::BudgetSampleInvalid);
            RequireError(StreamingBudgetSample::Create(policy, IdentityFrom<StreamingBudgetSampleRevision>(1), -1ns, 0ns, usage),
                         WorldStreamingErrors::BudgetSampleInvalid);
            RequireError(StreamingBudgetSample::Create(policy, IdentityFrom<StreamingBudgetSampleRevision>(1), 100ns, 99ns, usage),
                         WorldStreamingErrors::BudgetSampleInvalid);
            RequireError(StreamingBudgetSample::Create(policy, IdentityFrom<StreamingBudgetSampleRevision>(1), 100ns, 150ns, usage),
                         WorldStreamingErrors::BudgetSampleInvalid);
            const auto nearMaximum = std::chrono::nanoseconds::max() - 20ns;
            RequireError(StreamingBudgetSample::Create(policy, IdentityFrom<StreamingBudgetSampleRevision>(1), nearMaximum, nearMaximum,
                                                       usage),
                         WorldStreamingErrors::BudgetSampleInvalid);
        }

        TEST_CASE("Budget evaluation separates exact soft pressure from hard rejection", "[unit][world_streaming][budget][admission]") {
            const auto policy = Policy();
            const auto usage = Amounts(WithValue(StreamingBudgetDimension::CpuResidentBytes, 50));
            const auto sample = Sample(policy, usage);

            const auto exactSoft = Amounts(WithValue(StreamingBudgetDimension::CpuResidentBytes, 50));
            const auto admitted = EvaluateStreamingBudget(policy, sample, exactSoft, Context(policy, sample)).Value();
            REQUIRE(admitted.Decision() == StreamingBudgetDecision::Admit);
            REQUIRE_FALSE(admitted.PressureDimension().has_value());
            REQUIRE(admitted.ProjectedUsage().Value(StreamingBudgetDimension::CpuResidentBytes).Value() == 100);

            const auto aboveSoft = Amounts(WithValue(StreamingBudgetDimension::GpuResidentBytes, 101));
            const auto deferred = EvaluateStreamingBudget(policy, sample, aboveSoft, Context(policy, sample)).Value();
            REQUIRE(deferred.Decision() == StreamingBudgetDecision::DeferAboveSoftTarget);
            REQUIRE(deferred.PressureDimension() == StreamingBudgetDimension::GpuResidentBytes);
            REQUIRE(sample.Usage() == usage);

            const auto aboveHard = Amounts(WithValue(StreamingBudgetDimension::CpuResidentBytes, 151));
            RequireError(EvaluateStreamingBudget(policy, sample, aboveHard, Context(policy, sample)),
                         WorldStreamingErrors::BudgetCapacityExceeded);
            REQUIRE(sample.Usage() == usage);
        }

        TEST_CASE("Budget pressure selection is canonical regardless of caller entry order", "[unit][world_streaming][budget][admission]") {
            const auto policy = Policy();
            const auto sample = Sample(policy);
            std::array<StreamingBudgetAmount, StreamingBudgetDimensionCount> requestEntries{};
            for (std::size_t index = 0; index < requestEntries.size(); ++index)
                requestEntries[index] = {.dimension = Dimensions[requestEntries.size() - index - 1], .value = 101};

            const auto request = StreamingBudgetAmounts::Create(requestEntries).Value();
            const auto evaluation = EvaluateStreamingBudget(policy, sample, request, Context(policy, sample)).Value();

            REQUIRE(evaluation.Decision() == StreamingBudgetDecision::DeferAboveSoftTarget);
            REQUIRE(evaluation.PressureDimension() == StreamingBudgetDimension::CpuResidentBytes);
        }

        TEST_CASE("Budget dimensions are checked independently without additive double counting",
                  "[unit][world_streaming][budget][admission]") {
            BudgetValues usageValues{};
            BudgetValues requestValues{};
            usageValues.fill(90);
            requestValues.fill(10);
            const auto policy = Policy();
            const auto sample = Sample(policy, Amounts(usageValues));

            const auto evaluation = EvaluateStreamingBudget(policy, sample, Amounts(requestValues), Context(policy, sample)).Value();
            REQUIRE(evaluation.Decision() == StreamingBudgetDecision::Admit);
            for (const auto dimension : Dimensions)
                REQUIRE(evaluation.ProjectedUsage().Value(dimension).Value() == 100);
        }

        TEST_CASE("Budget hard limits allow equality and reject checked arithmetic overflow",
                  "[unit][world_streaming][budget][admission]") {
            const auto policy = Policy();
            const auto sample = Sample(policy, Amounts(WithValue(StreamingBudgetDimension::StagingBytes, 150)));
            const auto exactHard = Amounts(WithValue(StreamingBudgetDimension::StagingBytes, 50));
            REQUIRE(EvaluateStreamingBudget(policy, sample, exactHard, Context(policy, sample)).HasValue());

            auto maximumLimits = Limits(std::numeric_limits<std::uint64_t>::max(), std::numeric_limits<std::uint64_t>::max());
            const auto maximumPolicy = Policy(2, 50ns, maximumLimits);
            const auto maximumSample =
                Sample(maximumPolicy,
                       Amounts(WithValue(StreamingBudgetDimension::IoBytesInFlight, std::numeric_limits<std::uint64_t>::max())));
            const auto oneMore = Amounts(WithValue(StreamingBudgetDimension::IoBytesInFlight, 1));
            RequireError(EvaluateStreamingBudget(maximumPolicy, maximumSample, oneMore, Context(maximumPolicy, maximumSample)),
                         WorldStreamingErrors::BudgetCapacityExceeded);
        }

        TEST_CASE("Budget evaluation fences policy sample and completed-window revisions", "[unit][world_streaming][budget][fence]") {
            const auto firstPolicy = Policy(1);
            const auto firstSample = Sample(firstPolicy);
            const auto request = Amounts(WithValue(StreamingBudgetDimension::QueueScratchBytes, 1));

            auto context = Context(firstPolicy, firstSample);
            context.expectedPolicyRevision = IdentityFrom<StreamingBudgetPolicyRevision>(2);
            RequireError(EvaluateStreamingBudget(firstPolicy, firstSample, request, context), WorldStreamingErrors::BudgetRevisionStale);
            context = Context(firstPolicy, firstSample);
            context.expectedSampleRevision = IdentityFrom<StreamingBudgetSampleRevision>(2);
            RequireError(EvaluateStreamingBudget(firstPolicy, firstSample, request, context), WorldStreamingErrors::BudgetRevisionStale);

            const auto replacementPolicy = Policy(2);
            RequireError(EvaluateStreamingBudget(replacementPolicy, firstSample, request,
                                                 {.expectedPolicyRevision = replacementPolicy.Revision(),
                                                  .expectedSampleRevision = firstSample.Revision(),
                                                  .evaluationTime = 130ns}),
                         WorldStreamingErrors::BudgetRevisionStale);
            RequireError(EvaluateStreamingBudget(firstPolicy, firstSample, request, Context(firstPolicy, firstSample, 150ns)),
                         WorldStreamingErrors::BudgetSampleStale);
            RequireError(EvaluateStreamingBudget(firstPolicy, firstSample, request, Context(firstPolicy, firstSample, 124ns)),
                         WorldStreamingErrors::BudgetModelInvalid);
            context = Context(firstPolicy, firstSample);
            context.expectedPolicyRevision = {};
            RequireError(EvaluateStreamingBudget(firstPolicy, firstSample, request, context), WorldStreamingErrors::BudgetModelInvalid);
            RequireError(EvaluateStreamingBudget(firstPolicy, firstSample, request, Context(firstPolicy, firstSample, -1ns)),
                         WorldStreamingErrors::BudgetModelInvalid);
            RequireError(EvaluateStreamingBudget(firstPolicy, firstSample, Amounts(), Context(firstPolicy, firstSample)),
                         WorldStreamingErrors::BudgetModelInvalid);
        }

        TEST_CASE("Lowering policy limits never erases already observed usage", "[unit][world_streaming][budget][replacement]") {
            auto lowerLimits = Limits();
            lowerLimits[static_cast<std::size_t>(StreamingBudgetDimension::RetiredBytes)] = {
                .dimension = StreamingBudgetDimension::RetiredBytes,
                .softTarget = 100,
                .hardLimit = 120,
            };
            const auto replacementPolicy = Policy(2, 50ns, lowerLimits);
            const auto retainedUsage = Amounts(WithValue(StreamingBudgetDimension::RetiredBytes, 150));
            const auto replacementSample = Sample(replacementPolicy, retainedUsage, 2);
            const auto request = Amounts(WithValue(StreamingBudgetDimension::CpuResidentBytes, 1));

            RequireError(EvaluateStreamingBudget(replacementPolicy, replacementSample, request,
                                                 Context(replacementPolicy, replacementSample)),
                         WorldStreamingErrors::BudgetCapacityExceeded);
            REQUIRE(replacementSample.Usage() == retainedUsage);
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
