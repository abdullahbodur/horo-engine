#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "scene/RuntimeSceneDefinition.h"
#include "scene/SceneLifecycle.h"

namespace Monolith {

using RuntimeSceneApplyCallback =
    std::function<bool(const RuntimeSceneDefinition& definition, std::string* error)>;
using RuntimeSceneUnloadCallback = std::function<bool(std::string* error)>;

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

  SceneRuntimeCoordinator(const SceneRuntimeCoordinator&) = delete;
  SceneRuntimeCoordinator& operator=(const SceneRuntimeCoordinator&) = delete;

  SceneRuntimeOperationResult Load(const RuntimeSceneDefinition& definition,
                                   const RuntimeSceneApplyCallback& applyCallback);
  SceneRuntimeOperationResult Reload(const RuntimeSceneDefinition& definition,
                                     const RuntimeSceneApplyCallback& applyCallback);
  SceneRuntimeOperationResult Unload(const RuntimeSceneUnloadCallback& unloadCallback);

  const SceneLifecycle& GetLifecycle() const { return *m_lifecycle; }
  bool IsActive() const { return m_lifecycle->IsActive(); }
  bool HasTransitionFailure() const { return m_hasTransitionFailure; }
  SceneRuntimeOperation GetLastOperation() const { return m_lastOperation; }
  const std::string& GetLastError() const { return m_lastError; }
  const RuntimeSceneDefinition* GetCurrentDefinition() const;

 private:
  std::unique_ptr<SceneLifecycle> m_lifecycle;
  std::optional<RuntimeSceneDefinition> m_currentDefinition;
  std::string m_lastError;
  SceneRuntimeOperation m_lastOperation = SceneRuntimeOperation::None;
  bool m_hasTransitionFailure = false;

  void ResetLifecycleTo(SceneLifecycleState state);
  SceneRuntimeOperationResult MakeLifecycleError(SceneRuntimeOperation operation,
                                                 const char* fallbackMessage = "");
  SceneRuntimeOperationResult MakeCallbackError(SceneRuntimeOperation operation,
                                                const std::string& error,
                                                SceneLifecycleState recoveryState);
  SceneRuntimeOperationResult MakeSuccess(SceneRuntimeOperation operation);
};

}  // namespace Monolith
