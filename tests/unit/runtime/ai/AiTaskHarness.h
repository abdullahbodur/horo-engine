#pragma once

#include "Horo/AI/AIErrors.h"
#include "Horo/AI/AITaskLifecycle.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace Horo::AI::TestSupport {
    inline const ErrorCodeDescriptor HarnessExecutionFailure{
        .domain = ErrorDomainId{"horo.ai.test"},
        .code = ErrorCode{"ai.test.injected_execution_failure"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The deterministic AI harness injected an execution failure.",
        .remediationHint = "Inspect the scripted test step; this error is deterministic test evidence.",
        .retryable = false,
        .userActionable = false,
    };

    enum class ScriptedTaskOutcome : std::uint8_t {
        KeepRunning,
        Succeed,
        Fail,
        Count,
    };

    enum class AiTaskHarnessFault : std::uint8_t {
        None,
        CancelBeforeResume,
        RetireAgentBeforeCompletion,
        RejectForCapacity,
        Count,
    };

    struct ScriptedAiTaskStep final {
        AiTaskResumeReason resume{AiTaskResumeReason::FixedTick};
        ScriptedTaskOutcome outcome{ScriptedTaskOutcome::KeepRunning};
        AiTaskHarnessFault fault{AiTaskHarnessFault::None};
    };

    struct DeterministicAiTaskHarnessDescriptor final {
        BlackboardSchemaId schema;
        std::size_t maximumSteps{16};
    };

    class DeterministicAiTaskHarness final {
    public:
        explicit DeterministicAiTaskHarness(DeterministicAiTaskHarnessDescriptor descriptor) noexcept : descriptor_(descriptor) {}

        [[nodiscard]] Result<void> Start(AiTaskOperationContext context) {
            if (!descriptor_.schema.IsValid() || descriptor_.maximumSteps == 0 || descriptor_.maximumSteps > MaximumSteps)
                return Result<void>::Failure(MakeError(AIErrors::TaskContextInvalid));
            const auto started = lifecycle_.Start(std::move(context));
            if (started.HasError())
                return Result<void>::Failure(started.ErrorValue());
            const AiTaskOperationContext *captured = lifecycle_.Context();
            RecordInteger(descriptor_.schema.Value());
            RecordInteger(captured->taskDefinition.Value());
            RecordInteger(captured->agent.incarnation.Value());
            RecordInteger(captured->agent.slot.index);
            RecordInteger(captured->agent.slot.generation);
            RecordInteger(captured->task.slot.index);
            RecordInteger(captured->task.slot.generation);
            RecordByte(static_cast<std::uint8_t>(lifecycle_.State()));
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> Apply(const ScriptedAiTaskStep step, const AgentHandle activeAgent) {
            if (step.resume >= AiTaskResumeReason::Count || step.outcome >= ScriptedTaskOutcome::Count ||
                step.fault >= AiTaskHarnessFault::Count)
                return Result<void>::Failure(MakeError(AIErrors::TaskContextInvalid));
            if (stepCount_ >= descriptor_.maximumSteps || step.fault == AiTaskHarnessFault::RejectForCapacity)
                return Result<void>::Failure(MakeError(AIErrors::TaskCapacityExceeded));
            ++stepCount_;
            RecordByte(static_cast<std::uint8_t>(step.resume));
            RecordByte(static_cast<std::uint8_t>(step.outcome));
            RecordByte(static_cast<std::uint8_t>(step.fault));
            if (const auto executed = ExecuteStep(step, activeAgent); executed.HasError())
                return Result<void>::Failure(executed.ErrorValue());
            RecordLifecycleState();
            return Result<void>::Success();
        }

        [[nodiscard]] const AiTaskLifecycle &Lifecycle() const noexcept {
            return lifecycle_;
        }

        [[nodiscard]] std::uint64_t StateHash() const noexcept {
            return stateHash_;
        }

    private:
        static constexpr std::size_t MaximumSteps = 64;
        static constexpr std::uint8_t NoDetail = 0xff;
        static constexpr std::uint64_t FnvOffset = 14'695'981'039'346'656'037ULL;
        static constexpr std::uint64_t FnvPrime = 1'099'511'628'211ULL;

        [[nodiscard]] Result<void> ExecuteStep(const ScriptedAiTaskStep step, const AgentHandle activeAgent) {
            if (step.fault == AiTaskHarnessFault::CancelBeforeResume) {
                const auto cancelled = lifecycle_.RequestCancellation(AiTaskCancellationReason::Requested);
                return cancelled.HasError() ? Result<void>::Failure(cancelled.ErrorValue()) : Result<void>::Success();
            }
            if (step.fault == AiTaskHarnessFault::RetireAgentBeforeCompletion) {
                static_cast<void>(lifecycle_.RetireAgentGeneration());
                const auto completed = lifecycle_.CompleteSuccess(activeAgent);
                return completed.HasError() ? Result<void>::Failure(completed.ErrorValue()) : Result<void>::Success();
            }
            return ResumeAndApplyOutcome(step, activeAgent);
        }

        [[nodiscard]] Result<void> ResumeAndApplyOutcome(const ScriptedAiTaskStep step, const AgentHandle activeAgent) {
            const auto resume = lifecycle_.PrepareResume(step.resume, activeAgent);
            if (resume.HasError())
                return Result<void>::Failure(resume.ErrorValue());
            if (resume.Value() != AiTaskResumeDisposition::Ready || step.outcome == ScriptedTaskOutcome::KeepRunning)
                return Result<void>::Success();
            if (step.outcome == ScriptedTaskOutcome::Succeed) {
                const auto completed = lifecycle_.CompleteSuccess(activeAgent);
                return completed.HasError() ? Result<void>::Failure(completed.ErrorValue()) : Result<void>::Success();
            }
            const auto completed =
                lifecycle_.CompleteFailure(activeAgent, {AiTaskFailureKind::Execution, MakeError(HarnessExecutionFailure)});
            return completed.HasError() ? Result<void>::Failure(completed.ErrorValue()) : Result<void>::Success();
        }

        void RecordLifecycleState() noexcept {
            RecordByte(static_cast<std::uint8_t>(lifecycle_.State()));
            if (const AiTaskTerminalResult *terminal = lifecycle_.TerminalResult(); terminal != nullptr) {
                RecordByte(terminal->failure ? static_cast<std::uint8_t>(terminal->failure->kind) : NoDetail);
                RecordByte(terminal->cancellationReason ? static_cast<std::uint8_t>(*terminal->cancellationReason) : NoDetail);
            }
        }

        void RecordByte(const std::uint8_t value) noexcept {
            stateHash_ ^= value;
            stateHash_ *= FnvPrime;
        }

        template <typename Integer> void RecordInteger(Integer value) noexcept {
            for (std::size_t byteIndex = 0; byteIndex < sizeof(Integer); ++byteIndex) {
                const std::size_t shift = (sizeof(Integer) - byteIndex - 1) * 8;
                RecordByte(static_cast<std::uint8_t>(value >> shift));
            }
        }

        DeterministicAiTaskHarnessDescriptor descriptor_;
        AiTaskLifecycle lifecycle_;
        std::size_t stepCount_{};
        std::uint64_t stateHash_{FnvOffset};
    };
}  // namespace Horo::AI::TestSupport
