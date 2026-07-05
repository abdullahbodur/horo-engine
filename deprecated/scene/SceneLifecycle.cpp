#include "scene/SceneLifecycle.h"

namespace Horo {
    bool SceneLifecycle::BeginLoading() {
        using enum SceneLifecycleState;
        if (m_state != Uninitialized && m_state != Unloading) {
            SetError("BeginLoading: can only load from Uninitialized or Unloading");
            return false;
        }
        m_state = Loading;
        SetError("");
        return true;
    }

    bool SceneLifecycle::FinishLoading() {
        using enum SceneLifecycleState;
        if (m_state != Loading) {
            SetError("FinishLoading: not in Loading state");
            return false;
        }
        m_state = Active;
        SetError("");
        return true;
    }

    bool SceneLifecycle::BeginReloading() {
        using enum SceneLifecycleState;
        if (m_state != Active) {
            SetError("BeginReloading: can only reload from Active");
            return false;
        }
        m_state = Reloading;
        SetError("");
        return true;
    }

    bool SceneLifecycle::CompleteReload() {
        using enum SceneLifecycleState;
        if (m_state != Reloading) {
            SetError("CompleteReload: not in Reloading state");
            return false;
        }
        m_state = Active;
        SetError("");
        return true;
    }

    bool SceneLifecycle::BeginUnloading() {
        using enum SceneLifecycleState;
        if (m_state != Active && m_state != Reloading) {
            SetError("BeginUnloading: can only unload from Active or Reloading");
            return false;
        }
        m_state = Unloading;
        SetError("");
        return true;
    }

    bool SceneLifecycle::FinishUnloading() {
        using enum SceneLifecycleState;
        if (m_state != Unloading) {
            SetError("FinishUnloading: not in Unloading state");
            return false;
        }
        m_state = Uninitialized;
        SetError("");
        return true;
    }
} // namespace Horo
