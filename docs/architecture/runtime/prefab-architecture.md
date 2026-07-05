# Prefab Architecture

## Purpose

This document defines Horo Engine's prefab system: how an authored object or
object group is saved as a reusable prefab asset, how prefab instances are placed
in scenes, how per-instance overrides are applied, and how prefabs expand into
the runtime scene definition.

A prefab is an authoring-time template. It is stored on disk as a reusable asset
and instantiated into scenes by reference, not by embedding its contents into
every scene that uses it.

## Core Decisions

- A prefab is a first-class authored asset, distinct from imported mesh/texture/audio assets.
- Prefabs serialize as a subset of `SceneDocument` so the same scene serializer,
  validation, and runtime conversion logic applies.
- A scene object stores a stable prefab reference (`prefabId` + `sourcePath`), not the prefab's internal data.
- Per-instance overrides are limited, explicit, and applied after expansion.
- Runtime conversion expands prefabs into the containing scene's `RuntimeSceneDefinition`.
- Prefab-owned assets are merged into the scene's asset registry during conversion.
- Prefabs are not runtime ECS archetypes; they are authoring templates resolved before instantiation.

## Prefab As Authoring Asset

Prefabs live in the project under `assets/prefabs/`:

```text
assets/
  prefabs/
    player.prefab
    enemy.prefab
    weapons/
      rifle_demo.prefab
```

A `.prefab` file is a `SceneDocument` that describes the prefab root object, its
components, and any prefab-local asset references. The file uses the same schema
version and serialization rules as `.horo` scene files.

```cpp
struct ScenePrefabReference {
    std::string prefabId;    // stable logical prefab identity
    std::string sourcePath;  // project-relative path to the .prefab file
};

struct ScenePrefabInstance {
    ScenePrefabReference reference;
    std::optional<std::string> overrideId;
    std::optional<Vec3> overridePosition;
    std::optional<Vec3> overrideScale;
    std::optional<float> overrideYaw;
    std::optional<float> overridePitch;
    std::optional<float> overrideRoll;
    std::optional<std::string> overrideParentId;
};
```

The `prefabId` is the stable identity. The `sourcePath` resolves the file on
editor/conversion load. Both are persisted so the reference survives renames when
the path index or sidecar metadata is updated.

## Instance Placement

When a prefab is placed into a scene, the scene object stores the reference and
any allowed overrides:

```cpp
struct SceneObject {
    std::string id;
    std::string parentId;     // empty for root-level objects
    Vec3 position;
    Vec3 scale;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    std::optional<ScenePrefabInstance> prefabInstance;
    // ... other fields
};
```

Allowed overrides on the instance are applied during expansion:

- `id` — the instance's scene object ID (root identity in the containing scene).
- `position`, `scale`, `yaw`, `pitch`, `roll` — instance transform.
- `parentId` — optional reparenting inside the scene hierarchy.

All other properties come from the prefab file. Deep per-component overrides are
not supported in the initial system.

## Editor Workflow

```text
select object in editor
  -> Create Prefab command
    -> write .prefab file under assets/prefabs/
    -> replace selected object with prefab instance reference
    -> scene now references the prefab by path
```

The create-prefab command is an undoable document transaction. It:

1. captures the selected object's state,
2. writes the prefab asset to a unique project-relative path,
3. replaces the source object's contents with a `prefabInstance` reference,
4. preserves the original transform as the first instance override.

Placing an existing prefab into a scene creates a new scene object whose
`prefabInstance` points at the prefab file. The editor resolves and expands the
prefab for viewport preview and runtime conversion.

## Runtime Expansion

During `SceneDocument -> RuntimeSceneDefinition` conversion, prefab references
are resolved before the runtime scene is built.

```text
SceneDocument
  -> for each object with prefabInstance
       load prefab file
       merge prefab assets into scene asset registry
       copy prefab root object
       apply instance overrides
  -> continue normal conversion
```

Rules:

- A missing or unloadable prefab emits a conversion diagnostic; the instance is skipped.
- Prefab asset IDs that collide with scene asset IDs are resolved by the scene's
  asset merge policy. The scene's explicit asset definition wins.
- The expanded object retains its original scene object ID so behaviors, scripts,
  and references resolve correctly.
- Prefab expansion produces a flat `RuntimeSceneDefinition`. The expansion path
  is recursive, but because the initial prefab system is single-root and does not
  allow a prefab to contain another prefab reference, nested expansion does not
  occur in practice today.

## Prefab Root And Object Count

The initial implementation supports a **single root object** per prefab:

- The prefab file may contain exactly one authored object that defines the prefab.
- Multi-object prefabs (a root with children, or a group of sibling objects) are a
  recognized future extension.
- Scene hierarchy (`parentId`) may be used for simple parenting, but the prefab
  itself does not yet store a subtree.

This limitation means an object composed of multiple independent primitives — for
example, a castle built from many separate cubes — cannot currently be saved as
one prefab. Such a composition should either be merged into a single mesh asset
through an external DCC tool and imported, or split into one prefab per logical
part.

## Asset Ownership

A prefab may reference its own assets:

```cpp
struct SceneAssetDefinition {
    std::string id;
    std::string mesh;
    // maps, scale, guid, displayName, ...
};
```

When the prefab expands, its assets are merged into the scene model. The merge
policy is:

- Same `id` and compatible definition: keep the scene's existing entry.
- Same `id` with conflicting definition: emit a conversion diagnostic and keep
  the scene's entry.
- New `id`: add to the scene asset list.

## Identity, Renames, And References

Prefabs use the same stable GUID rules as other assets. The `prefabId` is the
primary identity; `sourcePath` is the resolution hint. If a prefab file is moved
or renamed, the path index must be updated. References that cannot be resolved
by path may be recovered through the `prefabId` if the project asset index maps
it to a new location.

## Cooking And Packaging

Prefabs participate in the asset pipeline as authored assets:

- The editor saves prefabs as source `.prefab` files.
- The cook step resolves prefab references inside scenes and inlines the expanded
  objects into the cooked scene artifact.
- Release packages do not ship raw `.prefab` files as runtime data; they ship
  cooked scenes where prefabs have already been expanded and validated.
- Prefab source files may be included in developer-facing source or editor package
  profiles, but not in release player builds.

## Relationship To Other Systems

- **Editor Document Model**: prefab creation is a document command; prefab
  placement creates a scene object with a `prefabInstance` reference.
- **Asset Pipeline**: prefabs are authored assets with sidecar metadata and
  stable identity. They are resolved and inlined during cook.
- **Scene Runtime**: runtime scenes never see prefab references directly; they
  receive an already-expanded `RuntimeSceneDefinition`.
- **Gameplay Behaviors**: a behavior attached to a prefab root is instantiated
  for each expanded instance using the same lifecycle as any scene object.

## Diagnostics

Conversion must report clear diagnostics for:

- missing prefab file
- prefab file with unsupported schema version
- prefab root object missing
- asset ID conflict between prefab and scene
- invalid instance override (unknown field, malformed transform)

## Future Extensions

- Multi-object prefabs with an explicit root/children subtree.
- Prefab variants that inherit from a base prefab and apply delta overrides.
- Nested prefab references where one prefab can place another prefab as a child.
- Component-level overrides on instances.
- Prefab thumbnails and viewport drag-and-drop placement.

## Related Documents

- [Prefab Editor UI Reference](./prefab-editor.html): hierarchy, instance overrides, variant chain, and apply/revert panel.

- [Asset Pipeline](./asset-pipeline.md)
- [Scene Runtime](./scene-runtime.md)
- [Editor Document Model](../editor/editor-document-model.md)
- [Built-In Scene Primitives](./built-in-scene-primitives.md)
- [Desired Project Trees](../desired-project-tree.md)
