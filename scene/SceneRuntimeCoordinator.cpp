#include "scene/SceneRuntimeCoordinator.h"

namespace Monolith {

SceneRuntimeCoordinator::SceneRuntimeCoordinator() : m_lifecycle(std::make_unique<SceneLifecycle>()) {}

const RuntimeSceneDefinition* SceneRuntimeCoordinator::GetCurrentDefinition() const {
  return m_currentDefinition.has_value() ? &(*m_currentDefinition) : nullptr;
}

SceneRuntimeOperationResult SceneRuntimeCoordinator::Load(const RuntimeSceneDefinition& definition,
                                                          const RuntimeSceneApplyCallback& applyCallback) {
  m_lastOperation = SceneRuntimeOperation::Load;
  m_lastError.clear();
  m_hasTransitionFailure = false;

  if (!m_lifecycle->BeginLoading())
    return MakeLifecycleError(SceneRuntimeOperation::Load, "Failed to begin loading.");

  std::string callbackError;
  if (!applyCallback(definition, &callbackError)) {
    return MakeCallbackError(SceneRuntimeOperation::Load,
                             callbackError.empty() ? "Runtime scene load callback failed."
                                                   : callbackError,
                             SceneLifecycleState::Uninitialized);
  }

  if (!m_lifecycle->FinishLoading())
    return MakeLifecycleError(SceneRuntimeOperation::Load, "Failed to finish loading.");

  m_currentDefinition = definition;
  return MakeSuccess(SceneRuntimeOperation::Load);
}

SceneRuntimeOperationResult SceneRuntimeCoordinator::Reload(
    const RuntimeSceneDefinition& definition, const RuntimeSceneApplyCallback& applyCallback) {
  m_lastOperation = SceneRuntimeOperation::Reload;
  m_lastError.clear();
  m_hasTransitionFailure = false;

  if (!m_lifecycle->BeginReloading())
    return MakeLifecycleError(SceneRuntimeOperation::Reload, "Failed to begin reload.");

  std::string callbackError;
  if (!applyCallback(definition, &callbackError)) {
    return MakeCallbackError(SceneRuntimeOperation::Reload,
                             callbackError.empty() ? "Runtime scene reload callback failed."
                                                   : callbackError,
                             SceneLifecycleState::Active);
  }

  if (!m_lifecycle->CompleteReload())
    return MakeLifecycleError(SceneRuntimeOperation::Reload, "Failed to complete reload.");

  m_currentDefinition = definition;
  return MakeSuccess(SceneRuntimeOperation::Reload);
}

SceneRuntimeOperationResult SceneRuntimeCoordinator::Unload(
    const RuntimeSceneUnloadCallback& unloadCallback) {
  m_lastOperation = SceneRuntimeOperation::Unload;
  m_lastError.clear();
  m_hasTransitionFailure = false;

  if (!m_lifecycle->BeginUnloading())
    return MakeLifecycleError(SceneRuntimeOperation::Unload, "Failed to begin unload.");

  std::string callbackError;
  if (!unloadCallback(&callbackError)) {
    return MakeCallbackError(SceneRuntimeOperation::Unload,
                             callbackError.empty() ? "Runtime scene unload callback failed."
                                                   : callbackError,
                             SceneLifecycleState::Active);
  }

  if (!m_lifecycle->FinishUnloading())
    return MakeLifecycleError(SceneRuntimeOperation::Unload, "Failed to finish unload.");

  m_currentDefinition.reset();
  return MakeSuccess(SceneRuntimeOperation::Unload);
}

void SceneRuntimeCoordinator::ResetLifecycleTo(SceneLifecycleState state) {
  m_lifecycle = std::make_unique<SceneLifecycle>();
  switch (state) {
    case SceneLifecycleState::Uninitialized:
      return;
    case SceneLifecycleState::Loading:
      m_lifecycle->BeginLoading();
      return;
    case SceneLifecycleState::Active:
      m_lifecycle->BeginLoading();
      m_lifecycle->FinishLoading();
      return;
    case SceneLifecycleState::Reloading:
      m_lifecycle->BeginLoading();
      m_lifecycle->FinishLoading();
      m_lifecycle->BeginReloading();
      return;
    case SceneLifecycleState::Unloading:
      m_lifecycle->BeginLoading();
      m_lifecycle->FinishLoading();
      m_lifecycle->BeginUnloading();
      return;
  }
}

SceneRuntimeOperationResult SceneRuntimeCoordinator::MakeLifecycleError(
    SceneRuntimeOperation operation, const char* fallbackMessage) {
  m_hasTransitionFailure = true;
  m_lastError = m_lifecycle->GetError();
  if (m_lastError.empty() && fallbackMessage)
    m_lastError = fallbackMessage;
  return SceneRuntimeOperationResult{false, operation, m_lifecycle->GetState(), m_lastError};
}

SceneRuntimeOperationResult SceneRuntimeCoordinator::MakeCallbackError(
    SceneRuntimeOperation operation, const std::string& error, SceneLifecycleState recoveryState) {
  m_hasTransitionFailure = true;
  m_lastError = error;
  ResetLifecycleTo(recoveryState);
  return SceneRuntimeOperationResult{false, operation, m_lifecycle->GetState(), m_lastError};
}

SceneRuntimeOperationResult SceneRuntimeCoordinator::MakeSuccess(SceneRuntimeOperation operation) {
  m_lastError.clear();
  m_hasTransitionFailure = false;
  return SceneRuntimeOperationResult{true, operation, m_lifecycle->GetState(), std::string()};
}

}  // namespace Monolith
