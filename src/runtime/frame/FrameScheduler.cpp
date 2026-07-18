#include "Horo/Runtime/FrameScheduler.h"

#include "../lifecycle/RuntimeErrors.h"
#include "Horo/Runtime/RuntimeLifecycle.h"

namespace Horo::Runtime {
    namespace {
        constexpr Duration kZero{};

        [[nodiscard]] Result<void> CancelledResult() {
            return Result<void>::Failure(MakeError(RuntimeErrors::Cancelled));
        }

        [[nodiscard]] bool IsPositive(const Duration value) noexcept {
            return value > kZero;
        }
    } // namespace

    /** @copydoc FrameClock::FrameClock */
    FrameClock::FrameClock(Clock &clock) noexcept : clock_(&clock) {
    }

    /** @copydoc FrameClock::Sample */
    Duration FrameClock::Sample() noexcept {
        const Duration now = clock_->MonotonicNow();
        if (!hasPrevious_) {
            previous_ = now;
            hasPrevious_ = true;
            return {};
        }
        const Duration elapsed = now - previous_;
        previous_ = now;
        return elapsed;
    }

    /** @copydoc FrameClock::Reset */
    void FrameClock::Reset() noexcept {
        hasPrevious_ = false;
        previous_ = {};
    }

    /** @copydoc FrameScheduler::Create */
    Result<std::unique_ptr<FrameScheduler> > FrameScheduler::Create(Clock &clock, FrameSchedulerConfig config) {
        if (!IsPositive(config.fixedStep) || !IsPositive(config.maximumFrameDelta) || config.maximumCatchUpSteps == 0) {
            return Result<std::unique_ptr<FrameScheduler> >::Failure(MakeError(RuntimeErrors::InvalidSchedulerConfig));
        }
        return Result<std::unique_ptr<FrameScheduler> >::Success(
            std::unique_ptr<FrameScheduler>(new FrameScheduler(clock, config)));
    }

    FrameScheduler::FrameScheduler(Clock &clock, const FrameSchedulerConfig config) noexcept
        : clock_(clock), config_(config) {
    }

    /** @copydoc FrameScheduler::RunFrame */
    Result<void> FrameScheduler::RunFrame(RuntimeLifecycle &lifecycle, const CancellationToken &cancellation,
                                          const bool suspended) {
        ++frameNumber_;
        Duration rawDelta = suspended ? Duration{} : clock_.Sample();
        Duration variableDelta = rawDelta;
        bool clamped = false;
        if (variableDelta < kZero) {
            variableDelta = {};
            ++statistics_.negativeDeltaNormalizationCount;
        } else if (variableDelta > config_.maximumFrameDelta) {
            variableDelta = config_.maximumFrameDelta;
            clamped = true;
            ++statistics_.maximumDeltaClampCount;
        }

        FrameContext frameContext{
            .frameNumber = frameNumber_,
            .variableDelta = variableDelta,
            .interpolationAlpha = 0.0,
            .completedSimulationTick = completedSimulationTick_,
            .droppedSimulationTime = {},
            .realDeltaWasClamped = clamped,
            .cancellation = cancellation
        };

        const auto dispatch = [&](const RuntimePhase phase) -> Result<void> {
            if (cancellation.IsCancellationRequested())
                return CancelledResult();
            Result<void> result = lifecycle.DispatchPhase(phase, frameContext);
            if (result.HasError())
                return result;
            if (cancellation.IsCancellationRequested())
                return CancelledResult();
            return Result<void>::Success();
        };

        if (Result<void> result = dispatch(RuntimePhase::BeginFrame); result.HasError())
            return result;
        if (Result<void> result = dispatch(RuntimePhase::PollPlatformEvents); result.HasError())
            return result;
        if (!suspended) {
            if (Result<void> result = dispatch(RuntimePhase::BuildInputSnapshot); result.HasError())
                return result;
        }
        if (Result<void> result = dispatch(RuntimePhase::ApplyQueuedOwnerThreadCommands); result.HasError())
            return result;

        if (suspended) {
            frameContext.variableDelta = {};
            if (Result<void> result = dispatch(RuntimePhase::EndFrame); result.HasError())
                return result;
            clock_.Reset();
            return Result<void>::Success();
        }

        accumulator_ += variableDelta;
        std::uint32_t executedSteps = 0;
        while (accumulator_ >= config_.fixedStep && executedSteps < config_.maximumCatchUpSteps) {
            if (cancellation.IsCancellationRequested())
                return CancelledResult();
            const FixedStepContext fixedContext{
                .simulationTick = completedSimulationTick_ + 1,
                .fixedDelta = config_.fixedStep,
                .cancellation = cancellation
            };
            Result<void> result = lifecycle.DispatchFixedUpdate(fixedContext);
            if (result.HasError())
                return result;
            if (cancellation.IsCancellationRequested())
                return CancelledResult();
            accumulator_ -= config_.fixedStep;
            ++completedSimulationTick_;
            statistics_.completedSimulationTick = completedSimulationTick_;
            ++executedSteps;
        }

        if (accumulator_ >= config_.fixedStep) {
            const std::uint64_t droppedSteps =
                    static_cast<std::uint64_t>(accumulator_.ToNanoseconds() / config_.fixedStep.ToNanoseconds());
            const Duration dropped =
                    Duration::FromNanoseconds(
                        static_cast<std::int64_t>(droppedSteps) * config_.fixedStep.ToNanoseconds());
            accumulator_ -= dropped;
            frameContext.droppedSimulationTime = dropped;
            statistics_.totalDroppedSimulationTime += dropped;
            statistics_.totalDroppedFixedSteps += droppedSteps;
            ++statistics_.catchUpLimitedFrameCount;
        }

        frameContext.completedSimulationTick = completedSimulationTick_;
        frameContext.interpolationAlpha =
                static_cast<double>(accumulator_.ToNanoseconds()) / static_cast<double>(config_.fixedStep.
                    ToNanoseconds());

        if (Result<void> result = dispatch(RuntimePhase::VariableUpdate); result.HasError())
            return result;
        if (Result<void> result = dispatch(RuntimePhase::RenderExtraction); result.HasError())
            return result;
        const bool extractionComplete = true;
        if (Result<void> result = dispatch(RuntimePhase::RenderExecution); result.HasError())
            return result;
        const bool executionComplete = true;
        if (Result<void> result = dispatch(RuntimePhase::RenderGui); result.HasError())
            return result;
        const bool guiComplete = true;
        if (!extractionComplete || !executionComplete || !guiComplete) {
            return Result<void>::Failure(MakeError(RuntimeErrors::PresentationPrerequisitesMissing));
        }
        if (Result<void> result = dispatch(RuntimePhase::Presentation); result.HasError())
            return result;
        if (Result<void> result = dispatch(RuntimePhase::CommitDeferredLifecycleChanges); result.HasError())
            return result;
        return dispatch(RuntimePhase::EndFrame);
    }

    /** @copydoc FrameScheduler::ResetClock */
    void FrameScheduler::ResetClock() noexcept {
        clock_.Reset();
    }

    /** @copydoc FrameScheduler::Statistics */
    FrameSchedulerStatistics FrameScheduler::Statistics() const noexcept {
        return statistics_;
    }
} // namespace Horo::Runtime
