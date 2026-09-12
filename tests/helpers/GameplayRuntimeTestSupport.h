#pragma once

#include "Horo/Gameplay/BehaviorRuntime.h"
#include "Horo/Runtime/Scene/RuntimeScene.h"

#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Horo::Tests {
    struct ActiveBehaviorRuntime {
        std::unique_ptr<Runtime::RuntimeScene> scene;
        std::unique_ptr<Gameplay::BehaviorRuntime> runtime;
    };

    inline Runtime::RuntimeSceneDefinition SingleBehaviorSceneDefinition(const Runtime::SceneDefinitionId definitionId,
                                                                         const Runtime::SceneDefinitionRevision revision,
                                                                         const Runtime::SceneObjectId objectId,
                                                                         const Gameplay::BehaviorInstanceId instanceId,
                                                                         Gameplay::BehaviorTypeId typeId,
                                                                         std::vector<Gameplay::BehaviorField> fields = {}) {
        Runtime::RuntimeComponentSet components;
        components.behaviors.emplace_back(instanceId, std::move(typeId), 1, true, std::move(fields));
        Runtime::SceneDefinitionBuilder builder{definitionId, revision};
        builder.Add({objectId, std::nullopt, {}, std::nullopt, std::move(components)});
        auto built = std::move(builder).Build();
        if (built.HasError())
            throw std::runtime_error{built.ErrorValue().message};
        return std::move(built).Value();
    }

    inline ActiveBehaviorRuntime ActivateBehaviorRuntime(Runtime::RuntimeSceneDefinition definition,
                                                         const Runtime::SceneRuntimeId runtimeId,
                                                         const Gameplay::BehaviorRegistry &registry) {
        auto createdScene = Runtime::RuntimeScene::Create(definition, runtimeId);
        if (createdScene.HasError())
            throw std::runtime_error{createdScene.ErrorValue().message};
        std::unique_ptr<Runtime::RuntimeScene> scene = std::move(createdScene).Value();
        auto createdRuntime = Gameplay::BehaviorRuntime::Create(*scene, registry);
        if (createdRuntime.HasError())
            throw std::runtime_error{createdRuntime.ErrorValue().message};
        return {std::move(scene), std::move(createdRuntime).Value()};
    }

    inline Math::Vec3 FixedUpdateAndReadPosition(Runtime::RuntimeScene &scene, Gameplay::BehaviorRuntime &runtime,
                                                 const Runtime::SceneObjectId object,
                                                 const std::span<const Gameplay::GameplayInputAction> input = {}) {
        if (const Result<void> updated = runtime.FixedUpdate(input, Gameplay::FixedDeltaTime{1.0 / 60.0}); updated.HasError())
            throw std::runtime_error{updated.ErrorValue().message};
        const Runtime::RuntimeSceneView sceneView = scene.View();
        const std::optional<Runtime::EntityRef> entity = sceneView.Find(object);
        if (!entity)
            throw std::runtime_error{"test scene object was not activated"};
        const Result<Runtime::RuntimeEntityView> view = sceneView.Get(*entity);
        if (view.HasError())
            throw std::runtime_error{view.ErrorValue().message};
        if (view.Value().localTransform == nullptr)
            throw std::runtime_error{"test scene object has no local transform"};
        return view.Value().localTransform->translation;
    }
}  // namespace Horo::Tests
