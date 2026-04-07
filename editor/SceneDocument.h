#pragma once
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "math/Vec3.h"

namespace Monolith {
namespace Editor {

enum class SceneObjectType { Panel, Prop, Light, Camera };

// A single component attached to a SceneObject (like Unity Inspector components).
// Built-in types and their props:
//   "light"      → intensity (float 0-10), color ("r,g,b"), radius (float)
//   "rigidbody"  → mass (float), isKinematic ("true"/"false"), useGravity ("true"/"false")
//   "script"     → behaviorTag (string)
struct ComponentDesc {
  std::string type;  // "light", "rigidbody", "script"
  std::unordered_map<std::string, std::string> props;  // type-specific props
};

// Reusable mesh + scale definition.
// Multiple scene objects can reference the same asset by id instead of
// repeating mesh/renderScale in every object's props.
struct AssetDef {
  std::string mesh;         // mesh tag, e.g. "stone.obj"
  std::string renderScale;  // "x,y,z" — matches the renderScale prop convention
  std::string albedoMap;    // optional diffuse texture path (e.g. assets/models/foo.png)
  std::string guid;         // stable path-independent identity for imported content
  std::string displayName;  // human-facing label shown in the editor

  AssetDef() = default;
  AssetDef(std::string meshValue,
           std::string renderScaleValue,
           std::string albedoMapValue = {},
           std::string guidValue = {},
           std::string displayNameValue = {})
      : mesh(std::move(meshValue)),
        renderScale(std::move(renderScaleValue)),
        albedoMap(std::move(albedoMapValue)),
        guid(std::move(guidValue)),
        displayName(std::move(displayNameValue)) {}
};

// One generic scene object: transform + optional asset reference + props bag.
// If assetId is set, mesh and renderScale are resolved from SceneDocument::assets;
// the per-object props only carry type-specific overrides (behavior, etc.).
// _eid is a runtime-only handle and is never written to disk.
struct SceneObject {
  std::string id;
  SceneObjectType type = SceneObjectType::Panel;
  Vec3 position = Vec3::Zero();
  Vec3 scale = Vec3::One();  // world-space AABB half-extents
  float yaw = 0.0f;          // degrees around Y axis
  float pitch = 0.0f;        // degrees around X axis; clamped ±89 for Camera objects
  float roll = 0.0f;         // degrees around Z axis
  std::string assetId;       // empty → use inline props
  std::unordered_map<std::string, std::string> props;
  std::vector<ComponentDesc> components;  // attached components (light, rigidbody, script, …)
};

struct SceneDocument {
  int version = 1;
  std::string sceneId = "scene";
  std::string sceneName = "Scene";  // display name shown in the hierarchy header
  std::string filePath;
  bool dirty = false;
  std::unordered_map<std::string, std::string> settings;  // scene-level settings (gravity, ambient, etc.)
  std::unordered_map<std::string, AssetDef> assets;  // id → definition
  std::vector<SceneObject> objects;
};

}  // namespace Editor
}  // namespace Monolith
