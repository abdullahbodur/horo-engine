#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "editor/SceneDocument.h"
#include "editor/SceneRuntimeCoordinatorBridge.h"
#include "physics/RigidBody.h"
#include "renderer/Light.h"
#include "scene/Scene.h"
#include "scene/SceneRuntimeCoordinator.h"

namespace Monolith {
struct SceneReferenceStats {
  int panelCount = 0;
  int propCount = 0;
  int entityCount = 0;
  int staticBodyCount = 0;
  int behaviorCount = 0;
};

struct SceneReferencePanel {
  SceneReferencePanel() = default;

  SceneReferencePanel(const Vec3 &centerValue, const Vec3 &halfValue)
      : center(centerValue), half(halfValue) {}

  Vec3 center = Vec3::Zero();
  Vec3 half = Vec3::One();
};

class SceneReferenceRuntime {
public:
  using BehaviorFactory = RuntimeBehaviorFactory;
  using PropEntityCreatedCallback = std::function<void(
      const RuntimeSceneProp &prop, Entity entity, Scene &scene)>;

  explicit SceneReferenceRuntime(Scene *scene);

  SceneReferenceRuntime(const SceneReferenceRuntime &) = delete;

  SceneReferenceRuntime &operator=(const SceneReferenceRuntime &) = delete;

  SceneRuntimeOperationResult
  LoadDocument(const Editor::SceneDocument &document);

  SceneRuntimeOperationResult
  ReloadDocument(const Editor::SceneDocument &document);

  SceneRuntimeOperationResult Unload();

  bool UpdateLiveLight(const Editor::SceneObject &object,
                       std::string *error = nullptr);

  void SetBehaviorFactory(BehaviorFactory factory) {
    m_behaviorFactory = std::move(factory);
  }

  void SetPropEntityCreatedCallback(PropEntityCreatedCallback callback) {
    m_propEntityCreatedCallback = std::move(callback);
  }

  Scene &GetScene() { return *m_scene; }
  const Scene &GetScene() const { return *m_scene; }

  const SceneRuntimeCoordinator &GetCoordinator() const {
    return m_coordinator;
  }

  const std::vector<Light> &GetLights() const { return m_lights; }

  const std::optional<RuntimeSceneCamera> &GetSceneCamera() const {
    return m_sceneCamera;
  }

  const std::vector<SceneReferencePanel> &GetPanels() const { return m_panels; }
  const SceneReferenceStats &GetStats() const { return m_stats; }

private:
  Scene *m_scene = nullptr;
  SceneRuntimeCoordinator m_coordinator;
  std::vector<Light> m_lights;
  std::vector<std::string> m_lightObjectIds;
  std::vector<std::string> m_pendingLightObjectIds;
  std::optional<RuntimeSceneCamera> m_sceneCamera;
  std::vector<SceneReferencePanel> m_panels;
  SceneReferenceStats m_stats;
  BehaviorFactory m_behaviorFactory;
  PropEntityCreatedCallback m_propEntityCreatedCallback;

  bool ApplyRuntimeDefinition(const RuntimeSceneDefinition &definition,
                              std::string *error);

  bool ClearRuntimeState(std::string *error);

  void ResetReferenceState();
};
} // namespace Monolith
