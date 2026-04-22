#include "scene/SceneRuntimeCoordinator.h"

namespace Monolith {
    SceneRuntimeCoordinator::SceneRuntimeCoordinator() = default;

    const RuntimeSceneDefinition *
    SceneRuntimeCoordinator::GetCurrentDefinition() const {
        return m_currentDefinition.has_value() ? &(*m_currentDefinition) : nullptr;
    }

    void SceneRuntimeCoordinator::ResetLifecycleTo(SceneLifecycleState state) {
        m_lifecycle = std::make_unique<SceneLifecycle>();
        using enum SceneLifecycleState;
        switch (state) {
            case Uninitialized:
                return;
            case Loading:
                m_lifecycle->BeginLoading();
                return;
            case Active:
                m_lifecycle->BeginLoading();
                m_lifecycle->FinishLoading();
                return;
            case Reloading:
                m_lifecycle->BeginLoading();
                m_lifecycle->FinishLoading();
                m_lifecycle->BeginReloading();
                return;
            case Unloading:
                m_lifecycle->BeginLoading();
                m_lifecycle->FinishLoading();
                m_lifecycle->BeginUnloading();
                return;
        }
    }

    SceneRuntimeOperationResult
    SceneRuntimeCoordinator::MakeLifecycleError(SceneRuntimeOperation operation,
                                                const char *fallbackMessage) {
        m_hasTransitionFailure = true;
        m_lastError = m_lifecycle->GetError();
        if (m_lastError.empty() && fallbackMessage)
            m_lastError = fallbackMessage;
        return SceneRuntimeOperationResult{
            false, operation, m_lifecycle->GetState(),
            m_lastError
        };
    }

    SceneRuntimeOperationResult
    SceneRuntimeCoordinator::MakeCallbackError(SceneRuntimeOperation operation,
                                               std::string_view error,
                                               SceneLifecycleState recoveryState) {
        m_hasTransitionFailure = true;
        m_lastError = error;
        ResetLifecycleTo(recoveryState);
        return SceneRuntimeOperationResult{
            false, operation, m_lifecycle->GetState(),
            m_lastError
        };
    }

    SceneRuntimeOperationResult
    SceneRuntimeCoordinator::MakeSuccess(SceneRuntimeOperation operation) {
        m_lastError.clear();
        m_hasTransitionFailure = false;
        return SceneRuntimeOperationResult{
            true, operation, m_lifecycle->GetState(),
            std::string()
        };
    }
} // namespace Monolith
