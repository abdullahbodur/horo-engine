#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "math/Vec3.h"

namespace Monolith {
namespace Editor {

enum class SceneObjectType { Panel, Prop, Light };

// Reusable mesh + scale definition.
// Multiple scene objects can reference the same asset by id instead of
// repeating mesh/renderScale in every object's props.
struct AssetDef {
  std::string mesh;         // mesh tag, e.g. "stone.obj"
  std::string renderScale;  // "x,y,z" — matches the renderScale prop convention
  std::string albedoMap;    // optional diffuse texture path (e.g. assets/models/foo.png)
};

// One generic scene object: transform + optional asset reference + props bag.
// If assetId is set, mesh and renderScale are resolved from SceneDocument::assets;
// the per-object props only carry type-specific overrides (behavior, isLight, etc.).
// _eid is a runtime-only handle and is never written to disk.
struct SceneObject {
  std::string id;
  SceneObjectType type = SceneObjectType::Panel;
  Vec3 position = Vec3::Zero();
  Vec3 scale = Vec3::One();  // world-space AABB half-extents
  float yaw = 0.0f;          // degrees around Y axis
  std::string assetId;       // empty → use inline props
  std::unordered_map<std::string, std::string> props;
};

struct SceneDocument {
  int version = 1;
  std::string sceneId = "dungeon";
  std::string filePath;
  bool dirty = false;
  std::unordered_map<std::string, std::string> settings;  // scene-level settings (gravity, ambient, etc.)
  std::unordered_map<std::string, AssetDef> assets;  // id → definition
  std::vector<SceneObject> objects;
};

}  // namespace Editor
}  // namespace Monolith
