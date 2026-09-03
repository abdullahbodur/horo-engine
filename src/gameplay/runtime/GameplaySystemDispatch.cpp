#include "GameplayRegistrationRuntimeDetail.h"
#include "GameplayRuntimeInvocation.h"
#include "Horo/Gameplay/GameplayErrors.h"

#include <cmath>

namespace Horo::Gameplay {
    namespace {
        [[nodiscard]] bool AllowsThread(const GameplayThreadAffinity declared, const GameplayThreadAffinity actual) noexcept {
            return declared == GameplayThreadAffinity::Any || declared == actual;
        }

        [[nodiscard]] Result<void> ValidateExecution(const auto &runtime, const GameplaySystemPhase phase, const double deltaSeconds) {
            if (runtime.shutdown)
                return Result<void>::Failure(MakeError(GameplayErrors::GameplayRuntimeInactive));
            if (static_cast<std::size_t>(phase) >= runtime.instancesByPhase.size() || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0)
                return Result<void>::Failure(MakeError(GameplayErrors::InvalidSystemDescriptor));
            if (runtime.cancellation.Token().IsCancellationRequested())
                return Result<void>::Failure(MakeError(GameplayErrors::GameplayCancelled));
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ExecuteInstance(const auto &runtime, const auto &instance, const GameplaySystemPhase phase,
                                                   const GameplayThreadAffinity thread, const double deltaSeconds) {
            if (!AllowsThread(instance.registration->descriptor.affinity, thread))
                return Result<void>::Failure(MakeError(GameplayErrors::GameplayThreadAccessViolation));
            const GameplaySystemContext context{runtime.cancellation.Token(),
                                                runtime.activeServices,
                                                runtime.capabilities,
                                                phase,
                                                thread,
                                                deltaSeconds};
            return Detail::ExecuteSystem(*instance.implementation, context);
        }
    }  // namespace

    /** @copydoc GameplaySystemRuntime::Execute */
    Result<void> GameplaySystemRuntime::Execute(const GameplaySystemPhase phase, const GameplayThreadAffinity thread,
                                                const double deltaSeconds) {
        if (!impl_)
            return Result<void>::Failure(MakeError(GameplayErrors::GameplayRuntimeInactive));
        if (Result<void> valid = ValidateExecution(*impl_, phase, deltaSeconds); valid.HasError())
            return valid;
        for (const std::size_t index : impl_->instancesByPhase[static_cast<std::size_t>(phase)]) {
            const Impl::Instance &instance = impl_->instances[index];
            if (Result<void> executed = ExecuteInstance(*impl_, instance, phase, thread, deltaSeconds); executed.HasError())
                return executed;
            if (impl_->cancellation.Token().IsCancellationRequested())
                return Result<void>::Failure(MakeError(GameplayErrors::GameplayCancelled));
        }
        return Result<void>::Success();
    }
}  // namespace Horo::Gameplay
