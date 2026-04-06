#include "scene/SceneLifecycle.h"

namespace Monolith {

bool SceneLifecycle::BeginLoading() {
  if (m_state != SceneLifecycleState::Uninitialized && 
      m_state != SceneLifecycleState::Unloading) {
    SetError("BeginLoading: can only load from Uninitialized or Unloading");
    return false;
  }
  m_state = SceneLifecycleState::Loading;
  SetError("");
  return true;
}

bool SceneLifecycle::FinishLoading() {
  if (m_state != SceneLifecycleState::Loading) {
    SetError("FinishLoading: not in Loading state");
    return false;
  }
  m_state = SceneLifecycleState::Active;
  SetError("");
  return true;
}

bool SceneLifecycle::BeginReloading() {
  if (m_state != SceneLifecycleState::Active) {
    SetError("BeginReloading: can only reload from Active");
    return false;
  }
  m_state = SceneLifecycleState::Reloading;
  SetError("");
  return true;
}

bool SceneLifecycle::CompleteReload() {
  if (m_state != SceneLifecycleState::Reloading) {
    SetError("CompleteReload: not in Reloading state");
    return false;
  }
  m_state = SceneLifecycleState::Active;
  SetError("");
  return true;
}

bool SceneLifecycle::BeginUnloading() {
  if (m_state != SceneLifecycleState::Active && 
      m_state != SceneLifecycleState::Reloading) {
    SetError("BeginUnloading: can only unload from Active or Reloading");
    return false;
  }
  m_state = SceneLifecycleState::Unloading;
  SetError("");
  return true;
}

bool SceneLifecycle::FinishUnloading() {
  if (m_state != SceneLifecycleState::Unloading) {
    SetError("FinishUnloading: not in Unloading state");
    return false;
  }
  m_state = SceneLifecycleState::Uninitialized;
  SetError("");
  return true;
}

}  // namespace Monolith
