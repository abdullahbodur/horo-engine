#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "scene/RuntimeSceneDefinition.h"
#include "scene/SceneLifecycle.h"

namespace Horo {
    using RuntimeSceneApplyCallback = std::function<bool(
        const RuntimeSceneDefinition &definition, std::string *error)>;
    using RuntimeSceneUnloadCallback = std::function<bool(std::string *error)>;

    enum class SceneRuntimeOperation { None, Load, Reload, Unload };

    struct SceneRuntimeOperationResult {
        bool ok = false;
        SceneRuntimeOperation operation = SceneRuntimeOperation::None;
        SceneLifecycleState state = SceneLifecycleState::Uninitialized;
        std::string error;
    };

    class SceneRuntimeCoordinator {
    public:
        SceneRuntimeCoordinator();

        ~SceneRuntimeCoordinator() = default;

        SceneRuntimeCoordinator(const SceneRuntimeCoordinator &) = delete;

        SceneRuntimeCoordinator &operator=(const SceneRuntimeCoordinator &) = delete;

        template<typename ApplyFn>
        SceneRuntimeOperationResult Load(const RuntimeSceneDefinition &definition,
                                         ApplyFn &&applyCallback) {
            m_lastOperation = SceneRuntimeOperation::Load;
            m_lastError.clear();
            m_hasTransitionFailure = false;

            if (!m_lifecycle->BeginLoading())
                return MakeLifecycleError(SceneRuntimeOperation::Load,
                                          "Failed to begin loading.");

            if (std::string callbackError; !applyCallback(definition, &callbackError)) {
                return MakeCallbackError(SceneRuntimeOperation::Load,
                                         callbackError.empty()
                                             ? "Runtime scene load callback failed."
                                             : callbackError,
                                         SceneLifecycleState::Uninitialized);
            }

            if (!m_lifecycle->FinishLoading())
                return MakeLifecycleError(SceneRuntimeOperation::Load,
                                          "Failed to finish loading.");

            m_currentDefinition = definition;
            return MakeSuccess(SceneRuntimeOperation::Load);
        }

        template<typename ApplyFn>
        SceneRuntimeOperationResult Reload(const RuntimeSceneDefinition &definition,
                                           ApplyFn &&applyCallback) {
            m_lastOperation = SceneRuntimeOperation::Reload;
            m_lastError.clear();
            m_hasTransitionFailure = false;

            if (!m_lifecycle->BeginReloading())
                return MakeLifecycleError(SceneRuntimeOperation::Reload,
                                          "Failed to begin reload.");

            if (std::string callbackError; !applyCallback(definition, &callbackError)) {
                return MakeCallbackError(SceneRuntimeOperation::Reload,
                                         callbackError.empty()
                                             ? "Runtime scene reload callback failed."
                                             : callbackError,
                                         SceneLifecycleState::Active);
            }

            if (!m_lifecycle->CompleteReload())
                return MakeLifecycleError(SceneRuntimeOperation::Reload,
                                          "Failed to complete reload.");

            m_currentDefinition = definition;
            return MakeSuccess(SceneRuntimeOperation::Reload);
        }

        template<typename UnloadFn>
        SceneRuntimeOperationResult Unload(UnloadFn &&unloadCallback) {
            m_lastOperation = SceneRuntimeOperation::Unload;
            m_lastError.clear();
            m_hasTransitionFailure = false;

            if (!m_lifecycle->BeginUnloading())
                return MakeLifecycleError(SceneRuntimeOperation::Unload,
                                          "Failed to begin unload.");

            if (std::string callbackError; !unloadCallback(&callbackError)) {
                return MakeCallbackError(SceneRuntimeOperation::Unload,
                                         callbackError.empty()
                                             ? "Runtime scene unload callback failed."
                                             : callbackError,
                                         SceneLifecycleState::Active);
            }

            if (!m_lifecycle->FinishUnloading())
                return MakeLifecycleError(SceneRuntimeOperation::Unload,
                                          "Failed to finish unload.");

            m_currentDefinition.reset();
            return MakeSuccess(SceneRuntimeOperation::Unload);
        }

        const SceneLifecycle &GetLifecycle() const { return *m_lifecycle; }
        bool IsActive() const { return m_lifecycle->IsActive(); }
        bool HasTransitionFailure() const { return m_hasTransitionFailure; }
        SceneRuntimeOperation GetLastOperation() const { return m_lastOperation; }
        const std::string &GetLastError() const { return m_lastError; }

        const RuntimeSceneDefinition *GetCurrentDefinition() const;

    private:
        std::unique_ptr<SceneLifecycle> m_lifecycle =
                std::make_unique<SceneLifecycle>();
        std::optional<RuntimeSceneDefinition> m_currentDefinition;
        std::string m_lastError;
        SceneRuntimeOperation m_lastOperation = SceneRuntimeOperation::None;
        bool m_hasTransitionFailure = false;

        void ResetLifecycleTo(SceneLifecycleState state);

        SceneRuntimeOperationResult
        MakeLifecycleError(SceneRuntimeOperation operation,
                           const char *fallbackMessage = "");

        SceneRuntimeOperationResult
        MakeCallbackError(SceneRuntimeOperation operation, std::string_view error,
                          SceneLifecycleState recoveryState);

        SceneRuntimeOperationResult MakeSuccess(SceneRuntimeOperation operation);
    };
} // namespace Horo
