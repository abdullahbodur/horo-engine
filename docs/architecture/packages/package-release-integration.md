# Package Release Integration

## Purpose

This document defines how packages participate in release validation, cooking,
`assets.horo`, chunks, DLC, license/notice collection, deterministic builds, and
editor-only exclusion.

## Core Decision

Development-time package sources are not runtime distribution dependencies.

Editor/development:

```text
package assets may be mounted from verified package cache
```

Release:

```text
required runtime package assets are cooked and copied into assets.horo or
explicit release chunks / DLC packages
```

Packaged games never depend on the editor package cache.

## Lockfile Freeze

During release build:

1. `.horo/packages.lock` is read and treated as frozen.
2. No implicit resolution occurs; release may restore/download artifacts already
   pinned by `packages.lock`, but it must not select newer package versions,
   change package sources, or rewrite the lockfile.
3. Missing, changed, or unverified lock entries fail release validation.
4. Package contents are cooked through the asset pipeline.
5. Editor-only contributions are excluded.
6. Source packages and development metadata are excluded unless using a developer
   diagnostics profile.
7. The release manifest records exact package versions, hashes, and package
   contribution closure.

## Release Validation

The release `Validating` phase includes:

- `packages.lock` exists when packages are used
- lockfile `requestHash` matches canonical dependency request from `packages.json`
- all required packages restored
- package hashes and signatures verified
- package cache not trusted without verification
- editor-only contributions excluded
- runtime contributions compatible with target platform/profile
- package licenses and notices collected
- no missing mounted asset references
- source/prebuilt game library compatibility verified
- private package credentials resolved through credential handles

A release must not succeed because the developer's local cache happens to
contain undeclared content.

## Contribution Inclusion Rules

| Contribution | Runtime release behavior |
|---|---|
| `Assets` | Include if referenced, selected, or chunk policy requires it |
| `Scripts` | Include only runtime scripts |
| `Behaviors` | Include if runtime behavior is used or required |
| `RuntimeServices` | Include if runtime service is used or required |
| `EditorExtension` | Exclude |
| `Documentation` | Exclude unless developer diagnostics profile requests it |
| `Samples` | Exclude unless imported/referenced as project assets |
| `Templates` | Exclude from game runtime artifacts |

## Assets, Chunks, And DLC

Development package content is input to runtime packaging:

```text
AssetPackage source content
  -> import/cook
  -> cooked runtime assets
  -> assets.horo / release chunk / DLC package
```

`AssetPackage` and `HybridPackage` are development-time source packages.
`Release Chunk` and `DLC` are shipped runtime packages controlled by the release
pipeline.

DLC packages are validated against the same manifest, signature, compatibility,
and dependency rules as the base game package. DLC must never replace base-game
files implicitly; it mounts through the runtime asset-provider contract.

A DLC package declares:

- base game identity
- compatible base release version range
- required base content manifest hash or compatibility ID
- required base chunks
- DLC package ID, version, and hash
- chunk dependencies
- mount priority
- conflict and override policy

DLC cannot replace base-game assets unless an explicit patch or override policy
allows it. DLC and patch packages are separate release concepts.


## License, Notice, And Attribution

Package manifests may declare:

```toml
[license]
spdx = "MIT"
noticeFile = "NOTICE.md"
requiresAttribution = true
commercialUse = true
redistribution = "allowed"
```

Release validation:

- collects required notices
- fails forbidden licenses according to product policy
- includes attribution files where required
- records package license inventory in release metadata

## Private Sources In Release

Release jobs may restore private packages only through credential handles.
Secrets must not be embedded in release manifests, logs, diagnostic bundles,
command lines, or job history.

## Determinism

Given the same source tree, target profile, toolchain, and `packages.lock`, the
package closure used by release is deterministic. Network package resolution is
not part of release unless the operation is explicitly a pre-release restore that
updates the lockfile.

## Required Tests

- package asset included in `assets.horo`
- editor-only contribution excluded
- missing package fails release
- deterministic release with same `packages.lock`
- runtime contribution incompatible with target fails release
- package license/notice collection
- forbidden license fails release
- DLC chunk dependency validation
- DLC base-game compatibility metadata validation
- release restores pinned artifacts without rewriting lockfile
- no dependency on editor package cache in packaged game
- private package auth uses credential handle and redacts logs

## Related Documents

- [Horo Package System](./package-system.md)
- [Package Restore](./package-restore.md)
- [Package Lifecycle](./package-lifecycle.md)
- [Asset Pipeline](../runtime/asset-pipeline.md)
- [Release Architecture](../release/release.md)
- [Distribution And Update](../release/distribution-and-update.md)
- [Release Security](../release/release-security.md)
