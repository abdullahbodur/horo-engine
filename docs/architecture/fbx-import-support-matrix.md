# FBX import — support matrix and limitations

This document describes the FBX import pipeline shipped under the HORO-89 epic.
It captures what is currently supported, what is intentionally out of scope,
and how to read the diagnostics emitted by the importer.

> **Status:** import-side complete. Runtime-side consumption of the
> `.skinned.bin` and `.anim.bin` artefacts (ECS animation system reading
> animations, full skinned render path through the new manifest) is a separate
> follow-up tracked under the cook/package work.

## Pipeline at a glance

```
.fbx source file
       │
       ▼
   FbxAssetImporter           (ui/editor/AssetImporterRegistry.cpp)
       │
       ├── FbxLoader (renderer/FbxLoader.h)
       │       └── ufbx (vendor/ufbx, MIT, v0.21.3)
       │
       ▼
managed assets/<guid>/
       ├── <stem>.mesh.bin       (static path, HoroMeshBin v1)
       ├── <stem>.skinned.bin    (skinned path, HoroSkinnedBin v1)
       ├── <stem>.anim.bin       (skeletal + animation only)
       ├── <copied textures>     (external + embedded)
       └── asset.meta.json       (metadata sidecar)
```

The importer auto-detects whether the FBX has a skin deformer and dispatches
to the correct path. Static and skinned imports never produce both
`.mesh.bin` and `.skinned.bin` for the same asset.

## Vendored library

| Library | Version | License | Purpose                                              |
|---------|---------|---------|------------------------------------------------------|
| ufbx    | v0.21.3 | MIT     | Single-source FBX parser; no external dependencies. |

Library lives under `vendor/ufbx/` and is private-linked into `HoroEngine`.
ufbx symbols are intentionally invisible on the public engine surface.

## What is supported

| Capability                       | Supported | Notes                                                                                         |
|----------------------------------|-----------|-----------------------------------------------------------------------------------------------|
| Static-mesh extraction           | ✅        | Concatenates all renderable meshes; missing normals are auto-generated; UV defaults to (0, 0).|
| Multi-mesh files                 | ✅        | Combined into a single output mesh.                                                          |
| Right-handed Y-up axes           | ✅        | ufbx target axes set to RHS Y-up; one-time conversion at load.                               |
| Diffuse / albedo channel         | ✅        | First diffuse-flagged texture drives `AssetDef::albedoMap`.                                  |
| Embedded textures                | ✅        | Bytes from `scene->videos[].content` written into managed storage; magic-byte sniff for missing extensions. |
| External texture resolution      | ✅        | Searches absolute / sibling / `textures/` / sourceDir candidates.                            |
| Skeletal mesh (skin deformer)    | ✅        | Top-4 bone influences per vertex, weights renormalised to 1.0; bones topologically sorted.   |
| Bind-pose skeleton               | ✅        | `inverseBindMatrix` taken from each cluster's `geometry_to_bone`.                            |
| Animation clips                  | ✅        | One `AnimationClip` per ufbx anim_stack; uniform 30 fps sampling via `ufbx_evaluate_transform`.|
| Per-bone TRS tracks              | ✅        | Position / rotation / scale stored as separate time arrays; rotations as quaternions.        |
| Diagnostics taxonomy             | ✅        | Typed `DiagnosticCodes::Fbx*` constants, persisted in metadata.                              |
| Source dependency tracking       | ✅        | FBX file + every resolved external texture path recorded as `Source`.                        |
| Reimport propagation             | ✅        | Driven by `AssetImportService::ReimportAssetWithDependents`.                                 |
| Editor file-drop                 | ✅        | OS file-drop dispatcher accepts both `.obj` and `.fbx`.                                      |
| Editor "Import Asset" modal      | ✅        | `EditorImportAssetModal`; multi-format chooser via `AssetImporterRegistry`.                  |
| Thumbnail preview (static)       | ✅        | `EditorAssetThumbnailPreview` reads `.mesh.bin` via `MeshBin::ReadStaticMesh`.               |

## Known limitations / out of scope

| Limitation                                  | Workaround / follow-up                                                                                                            |
|---------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------|
| Up to 4 bone influences per vertex           | Standard four-bone skinning; smallest weights are dropped, surviving weights renormalised. Lift in a future revision when needed. |
| Animation tangent / curve shapes             | Uniform 30 fps sampling is the deliberate trade-off (smaller runtime + simpler engine sampler). Bump `kAnimBinVersion` if a tangent variant becomes necessary. |
| PBR base / normal / metallic split           | Importer captures all `scene->textures` but currently only the first diffuse drives `AssetDef`. Add additional texture slots when the asset model grows. |
| Thumbnail preview for skinned meshes         | `.mesh.bin` thumbnail support is in; `.skinned.bin` thumbnail rendering follows the runtime work.                                |
| Runtime-side `.skinned.bin` / `.anim.bin`    | `MeshCache` loads `.mesh.bin`. A `SkinnedMeshCache` and ECS-driven animation playback are planned alongside the cook/package work.|
| Cook / package integration                  | Out of HORO-89 scope; tracked under HORO-24 (Cooked asset cache generation and release artifact packaging).                       |

## On-disk artefact formats

All formats are little-endian and have versioned headers; readers reject
mismatched magic / version cleanly.

### `<stem>.mesh.bin` — HoroMeshBin v1 (static mesh)

48-byte header followed by `Vertex` records and `uint32_t` indices.
See `renderer/MeshBin.h` for the full layout.

### `<stem>.skinned.bin` — HoroSkinnedBin v1 (skinned mesh + skeleton)

64-byte header followed by `SkinnedVertex` records, `uint32_t` indices, and
per-bone records (parent index + 16-float column-major inverse-bind matrix +
length-prefixed name).
See `renderer/SkinnedMeshBin.h` for the full layout.

### `<stem>.anim.bin` — HoroAnimBin v1 (animation clips)

32-byte header followed by per-clip records (name, duration, track count) and
per-track records (`boneIndex` + position / rotation / scale time arrays with
their value arrays).
See `renderer/AnimBin.h` for the full layout.

## Diagnostic codes

The importer emits typed diagnostic codes from
`Horo::Editor::DiagnosticCodes` (header: `ui/editor/AssetImportDiagnosticCodes.h`).
Codes are part of the public diagnostic contract — never rename or repurpose
an existing constant.

| Code                                      | Severity | Meaning                                                                  |
|-------------------------------------------|----------|--------------------------------------------------------------------------|
| `asset.fbx.unsupported_type`              | Error    | Source file is not `.fbx`.                                                |
| `asset.fbx.source_missing`                | Error    | Source path does not resolve to a regular file.                          |
| `asset.fbx.create_directory_failed`       | Error    | Managed asset directory could not be created.                            |
| `asset.fbx.parse_failed`                  | Error    | ufbx rejected the file (corrupt or unsupported variant).                 |
| `asset.fbx.no_geometry`                   | Error    | File parsed but contains no triangulable mesh data.                      |
| `asset.fbx.mesh_write_failed`             | Error    | `.mesh.bin` could not be written to managed storage.                     |
| `asset.fbx.skeleton_missing`              | Warning/Error | Skin clusters reference bones that could not be linked.             |
| `asset.fbx.skeleton_write_failed`         | Error    | `.skinned.bin` could not be written.                                     |
| `asset.fbx.animation_write_failed`        | Warning  | `.anim.bin` could not be written; mesh import still succeeds.            |
| `asset.fbx.external_texture_missing`      | Warning  | External texture referenced by the FBX could not be resolved on disk.    |
| `asset.fbx.external_texture_copy_failed`  | Warning  | External texture was located but could not be copied into managed storage. |
| `asset.fbx.embedded_texture_extract_failed` | Warning | Embedded texture blob could not be written.                              |
| `asset.fbx.unit_scale_warning`            | Warning  | Reserved for future unit-scale auditing.                                 |
| `asset.fbx.unsupported_feature_warning`   | Warning  | Reserved for future per-feature gating.                                  |

## Test fixtures

The repository ships small permissive FBX fixtures vendored from the upstream
ufbx test suite (same MIT license):

| Fixture                                       | Coverage                                       |
|-----------------------------------------------|------------------------------------------------|
| `cube_5800_binary.fbx`                        | HORO-94 static mesh, HORO-108 animation extraction (`Take 001`). |
| `cube_texture_5800_binary.fbx`                | HORO-95/96 — embedded video texture.           |
| `embedded_textures_7400_binary.fbx`           | HORO-95/96 — multi-channel embedded textures.  |
| `external_texture_6100_binary.fbx`            | HORO-95 — external reference, no embedded.     |
| `skinned_7400_binary.fbx`                     | HORO-107 — skinned mesh + skeleton.            |

## Related documents

- [module-boundaries.md](./module-boundaries.md)
- [ownership-lifecycle.md](./ownership-lifecycle.md)
