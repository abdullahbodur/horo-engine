#include "Horo/AI/NullAIRuntime.h"

#include "Horo/AI/AIErrors.h"

namespace Horo::AI {
    /** @copydoc NullAiRuntime::StartTask */
    Result<void> NullAiRuntime::StartTask(AiTaskLifecycle &lifecycle, const AiTaskOperationContext &context) const {
        static_cast<void>(lifecycle);
        static_cast<void>(context);
        return Result<void>::Failure(MakeError(AIErrors::RuntimeUnavailable));
    }
}  // namespace Horo::AI
