/**
 * @file SceneDocument.h
 * @brief Editor-side scene data model representing the authorable scene state.
 */
#pragma once
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/StringHash.h"
#include "math/Vec3.h"

namespace Horo::Editor {
    /** @brief Classifies the broad category of a scene object. */
    enum class SceneObjectType {
        Panel,  /**< Generic authored object without specialised runtime behavior. */
        Prop,   /**< Rendered prop instance, usually backed by an asset definition. */
        Light,  /**< Light-emitting object (point or directional). */
        Camera, /**< Camera object that can drive view/projection state. */
    };

    /**
     * @brief A single component attached to a SceneObject, analogous to a Unity
     *        Inspector component.
     *
     * @note Built-in component types and their properties:
     *   - "light"     → intensity (float 0-10), color ("r,g,b"), radius (float)
     *   - "rigidbody" → mass (float), isKinematic ("true"/"false"), useGravity ("true"/"false")
     *   - "script"    → behaviorTag (string)
     */
    struct ComponentDesc {
        std::string type; /**< Component type name; one of "light", "rigidbody", or "script". */
        std::unordered_map<std::string, std::string, StringHash, std::equal_to<> >
        props; /**< Type-specific key-value properties. */

        bool operator==(const ComponentDesc &) const = default;
    };

    /**
     * @brief Reusable mesh and scale definition shared by multiple scene objects.
     *
     * Multiple scene objects can reference the same asset by ID instead of
     * repeating mesh or renderScale in every object's props.
     */
    struct AssetDef {
        std::string mesh;        /**< Mesh tag, e.g. "stone.obj". */
        std::string renderScale; /**< Render scale as "x,y,z"; matches the renderScale prop convention. */
        std::string
        albedoMap;               /**< Optional diffuse texture path, e.g. "assets/models/foo.png". */
        std::string guid;        /**< Stable path-independent identity for imported content. */
        std::string displayName; /**< Human-facing label shown in the editor. */

        /** @brief Constructs an empty asset definition. */
        AssetDef() = default;

        /** @brief Constructs an asset definition from explicit field values.
         *  @param meshValue         Mesh tag/path string.
         *  @param renderScaleValue  Render scale encoded as "x,y,z".
         *  @param albedoMapValue    Optional albedo texture path.
         *  @param guidValue         Stable GUID for imported content.
         *  @param displayNameValue  Human-facing asset label.
         */
        AssetDef(std::string meshValue, std::string renderScaleValue,
                 std::string albedoMapValue = {}, std::string guidValue = {},
                 std::string displayNameValue = {})
            : mesh(std::move(meshValue)), renderScale(std::move(renderScaleValue)),
              albedoMap(std::move(albedoMapValue)), guid(std::move(guidValue)),
              displayName(std::move(displayNameValue)) {
        }

        /** @brief Returns true when all persisted asset fields are equal. */
        bool operator==(const AssetDef &) const = default;
    };

    /**
     * @brief Records that a scene object was instantiated from a named prefab.
     */
    struct ScenePrefabInstance {
        std::string prefabId;   /**< Identifier of the source prefab asset. */
        std::string sourcePath; /**< File path from which the prefab was loaded. */

        /** @brief Returns true when both prefab identifier and source path are equal. */
        bool operator==(const ScenePrefabInstance &) const = default;
    };

    /**
     * @brief One generic scene object: transform, optional asset reference, and
     *        a type-specific properties bag.
     *
     * @note When @c assetId is non-empty, mesh and renderScale are resolved from
     *       SceneDocument::assets; per-object props carry only type-specific overrides.
     *       The runtime entity ID (_eid) is never written to disk.
     */
    struct SceneObject {
        std::string id;                                    /**< Unique identifier within the scene. */
        SceneObjectType type = SceneObjectType::Panel;     /**< Broad category of this object. */
        Vec3 position = Vec3::Zero();                      /**< World-space position. */
        Vec3 scale = Vec3::One();                          /**< World-space AABB half-extents. */
        float yaw = 0.0f;                                  /**< Rotation around the Y axis in degrees. */
        float pitch = 0.0f;                                /**< Rotation around the X axis in degrees; clamped to ±89 for Camera objects. */
        float roll = 0.0f;                                 /**< Rotation around the Z axis in degrees. */
        std::string assetId;                               /**< Asset ID; empty means inline props are used directly. */
        std::optional<ScenePrefabInstance> prefabInstance; /**< Set when this object was instantiated from a prefab. */
        std::unordered_map<std::string, std::string, StringHash, std::equal_to<> >
        props;                                             /**< Type-specific key-value overrides. */
        std::vector<ComponentDesc>
        components;                                        /**< Attached components (light, rigidbody, script, etc.). */

        /** @brief Returns true when all authored fields and attached components are equal. */
        bool operator==(const SceneObject &) const = default;
    };

    /**
     * @brief Top-level container for the editor-side scene: settings, asset
     *        definitions, and the flat list of scene objects.
     */
    struct SceneDocument {
        int version = 1;                   /**< Schema version for forward compatibility. */
        std::string sceneId = "scene";     /**< Stable identifier for this scene. */
        std::string sceneName = "Scene";   /**< Display name shown in the hierarchy header. */
        std::string filePath;              /**< Absolute path to the serialized scene file on disk. */
        bool dirty = false;                /**< True when unsaved changes are present. */
        std::unordered_map<std::string, std::string, StringHash, std::equal_to<> >
        settings;                          /**< Global scene-level settings as key-value pairs. */
        std::unordered_map<std::string, AssetDef, StringHash, std::equal_to<> >
        assets;                            /**< Named asset definitions keyed by asset ID. */
        std::vector<SceneObject> objects;  /**< Ordered list of scene objects. */

        /** @brief Returns true when metadata, settings, assets, and object lists are equal. */
        bool operator==(const SceneDocument &) const = default;
    };
} // namespace Horo::Editor
