#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "math/Quaternion.h"
#include "math/Vec3.h"
#include "renderer/Light.h"
#include "scene/components/BehaviorComponent.h"

namespace Horo {
    struct RuntimeScenePanel {
        Vec3 center = Vec3::Zero();
        Vec3 half = Vec3::One();
        Quaternion rotation = Quaternion::Identity();
    };

    struct RuntimeSceneProp {
        std::string id;
        Vec3 position = Vec3::Zero();
        float yaw = 0.0f;
        float pitch = 0.0f;
        float roll = 0.0f;
        Vec3 meshHalf = {0.5f, 0.5f, 0.5f};
        Vec3 scale = Vec3::One();
        std::string meshTag;
        std::string albedoMap;
        bool isLight = false;
        std::string scriptTag;
        std::string normalMap;
        std::string metallicRoughnessMap;
        std::string emissiveMap;
        std::string occlusionMap;
    };

    struct RuntimeSceneRoom {
        std::string id;
        Vec3 gravity = {0.0f, -9.81f, 0.0f};
        std::vector<RuntimeScenePanel> panels;
        std::vector<RuntimeSceneProp> props;
    };

    struct RuntimeSceneCamera {
        Vec3 position = Vec3::Zero();
        float yaw = 0.0f;
        float pitch = 0.0f;
        float fovY = 55.0f;
        float nearClip = 0.1f;
        float farClip = 200.0f;
    };

    struct RuntimeSceneDefinition {
        std::vector<RuntimeSceneRoom> rooms;
        std::vector<Light> lights;
        Vec3 spawnPoint = {2.0f, 0.9f, 3.0f};
        std::optional<RuntimeSceneCamera> sceneCamera;
    };

    using RuntimeBehaviorFactory =
    std::function<std::unique_ptr<Behavior>(const std::string &tag)>;

    struct RuntimeSceneBuildIssue {
        enum class Severity { Error, Warning };

        Severity severity = Severity::Error;
        std::string path;
        std::string message;
    };

    struct RuntimeSceneBuildResult {
        RuntimeSceneDefinition definition;
        std::vector<RuntimeSceneBuildIssue> issues;

        bool HasErrors() const;

        std::size_t ErrorCount() const;

        std::size_t WarningCount() const;
    };
} // namespace Horo
