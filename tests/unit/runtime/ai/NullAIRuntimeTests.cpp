#include "AiTaskHarness.h"
#include "AiTestSupport.h"
#include "Horo/AI/BlackboardSchema.h"
#include "Horo/AI/NullAIRuntime.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

namespace Horo::AI {
    namespace {
        using TestSupport::AiTaskHarnessFault;
        using TestSupport::DeterministicAiTaskHarness;
        using TestSupport::DeterministicAiTaskHarnessDescriptor;
        using TestSupport::ExpectError;
        using TestSupport::MakeIdentity;
        using TestSupport::ScriptedAiTaskStep;
        using TestSupport::ScriptedTaskOutcome;

        struct HarnessFixture final {
            AiRuntimeIncarnation incarnation{AiRuntimeIncarnation::Create(91).Value()};
            AgentHandle agent{incarnation, {2, 5}};
            TaskHandle task{incarnation, {3, 7}};
            CancellationSource cancellation;

            [[nodiscard]] AiTaskOperationContext Context() const {
                return {
                    .taskDefinition = MakeIdentity<TaskId>(103),
                    .task = task,
                    .agent = agent,
                    .cancellation = cancellation.Token(),
                };
            }
        };

        [[nodiscard]] BlackboardSchema SmallSchema() {
            const std::array keys{
                BlackboardKeyDescriptor{.key = MakeIdentity<BlackboardKeyId>(11),
                                        .kind = BlackboardValueKind::Boolean,
                                        .defaultValue = BlackboardValue{BlackboardScalarValue{false}}},
                BlackboardKeyDescriptor{.key = MakeIdentity<BlackboardKeyId>(12),
                                        .kind = BlackboardValueKind::SignedInteger,
                                        .defaultValue = BlackboardValue{BlackboardScalarValue{std::int64_t{0}}}},
            };
            auto schema = BlackboardSchema::Capture({.identity = MakeIdentity<BlackboardSchemaId>(101), .keys = keys});
            REQUIRE(schema.HasValue());
            return std::move(schema).Value();
        }

        TEST_CASE("Null AI runtime reports explicit unavailability without task success", "[unit][ai][null]") {
            HarnessFixture fixture;
            AiTaskLifecycle lifecycle;
            const NullAiRuntime runtime;

            CHECK_FALSE(runtime.IsAvailable());
            ExpectError(runtime.StartTask(lifecycle, fixture.Context()), AIErrors::RuntimeUnavailable);
            CHECK(lifecycle.State() == AiTaskState::Idle);
            CHECK(lifecycle.TerminalResult() == nullptr);
        }

        TEST_CASE("Deterministic AI harness runs small schema and agent sequences headlessly", "[unit][ai][harness]") {
            const BlackboardSchema schema = SmallSchema();
            HarnessFixture fixture;
            DeterministicAiTaskHarness harness({schema.Identity(), 4});
            REQUIRE(harness.Start(fixture.Context()).HasValue());
            REQUIRE(harness.Apply({AiTaskResumeReason::FixedTick, ScriptedTaskOutcome::KeepRunning}, fixture.agent).HasValue());
            REQUIRE(harness.Apply({AiTaskResumeReason::Event, ScriptedTaskOutcome::Succeed}, fixture.agent).HasValue());
            CHECK(harness.Lifecycle().State() == AiTaskState::Succeeded);
            REQUIRE(harness.Lifecycle().TerminalResult() != nullptr);
            CHECK(harness.StateHash() != 0);
        }

        TEST_CASE("Deterministic AI harness hashes match for declared sequences", "[unit][ai][harness]") {
            const BlackboardSchema schema = SmallSchema();
            HarnessFixture firstFixture;
            HarnessFixture secondFixture;
            DeterministicAiTaskHarness first({schema.Identity(), 4});
            DeterministicAiTaskHarness second({schema.Identity(), 4});
            REQUIRE(first.Start(firstFixture.Context()).HasValue());
            REQUIRE(second.Start(secondFixture.Context()).HasValue());

            const std::array sequence{
                ScriptedAiTaskStep{AiTaskResumeReason::FixedTick, ScriptedTaskOutcome::KeepRunning},
                ScriptedAiTaskStep{AiTaskResumeReason::Event, ScriptedTaskOutcome::Fail},
            };
            for (const ScriptedAiTaskStep step : sequence) {
                REQUIRE(first.Apply(step, firstFixture.agent).HasValue());
                REQUIRE(second.Apply(step, secondFixture.agent).HasValue());
            }
            CHECK(first.StateHash() == second.StateHash());
            CHECK(first.StateHash() == 0x433f3e01243f9f50ULL);
        }

        TEST_CASE("Deterministic AI harness injects cancellation and stale completion", "[unit][ai][harness]") {
            const BlackboardSchema schema = SmallSchema();
            HarnessFixture cancellationFixture;
            DeterministicAiTaskHarness cancelled({schema.Identity(), 2});
            REQUIRE(cancelled.Start(cancellationFixture.Context()).HasValue());
            REQUIRE(cancelled
                        .Apply({AiTaskResumeReason::FixedTick, ScriptedTaskOutcome::Succeed, AiTaskHarnessFault::CancelBeforeResume},
                               cancellationFixture.agent)
                        .HasValue());
            CHECK(cancelled.Lifecycle().State() == AiTaskState::Cancelled);
            CHECK(cancelled.Lifecycle().TerminalResult()->cancellationReason == AiTaskCancellationReason::Requested);

            HarnessFixture staleFixture;
            DeterministicAiTaskHarness stale({schema.Identity(), 2});
            REQUIRE(stale.Start(staleFixture.Context()).HasValue());
            REQUIRE(stale
                        .Apply({AiTaskResumeReason::Event, ScriptedTaskOutcome::Succeed, AiTaskHarnessFault::RetireAgentBeforeCompletion},
                               staleFixture.agent)
                        .HasValue());
            CHECK(stale.Lifecycle().State() == AiTaskState::Cancelled);
            CHECK(stale.Lifecycle().TerminalResult()->cancellationReason == AiTaskCancellationReason::AgentGenerationRetired);
        }

        TEST_CASE("Deterministic AI harness reports injected and exhausted capacity", "[unit][ai][harness]") {
            const BlackboardSchema schema = SmallSchema();
            HarnessFixture fixture;
            DeterministicAiTaskHarness harness({schema.Identity(), 1});
            REQUIRE(harness.Start(fixture.Context()).HasValue());
            ExpectError(harness.Apply({AiTaskResumeReason::FixedTick, ScriptedTaskOutcome::KeepRunning,
                                       AiTaskHarnessFault::RejectForCapacity},
                                      fixture.agent),
                        AIErrors::TaskCapacityExceeded);
            CHECK(harness.Lifecycle().State() == AiTaskState::Running);
            REQUIRE(harness.Apply({AiTaskResumeReason::FixedTick, ScriptedTaskOutcome::KeepRunning}, fixture.agent).HasValue());
            ExpectError(harness.Apply({AiTaskResumeReason::Event, ScriptedTaskOutcome::Succeed}, fixture.agent),
                        AIErrors::TaskCapacityExceeded);
            CHECK(harness.Lifecycle().State() == AiTaskState::Running);
        }
    }  // namespace
}  // namespace Horo::AI
