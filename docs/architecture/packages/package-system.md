# Horo Package System

## Purpose

This document defines the core package model for Horo Engine: package kinds,
contribution kinds, source declarations, manifests, dependency resolution,
lockfiles, cache verification, trust levels, identity, and service boundaries.

Detailed restore, lifecycle, and release behavior is defined by:

- [Package Restore](./package-restore.md)
- [Package Lifecycle](./package-lifecycle.md)
- [Package Release Integration](./package-release-integration.md)

## Core Decision

A Horo project must be restorable, verifiable, portable, releasable, and secure
from committed project metadata plus declared package sources.

The package system is not a replacement for the asset importer or the extension
system:

- Asset importers convert individual source assets into project assets.
- Packages distribute resolved sets of assets, scripts, behaviors, services,
  samples, templates, and optional editor contributions.
- Editor/IDE addons remain high-trust Extension System contributions.

## Core Distinctions

| Concept | What it carries | Where it lives | Trust level |
|---|---|---|---|
| **Asset Importer** | One media file (`.fbx`, `.wav`, `.png`) | Project asset store | User file |
| **Asset Package** | Many media assets | Project asset store or package mount | User package |
| **Game Library** | Scripts, behaviors, services | Project dependency | Explicit trust |
| **Engine Extension / IDE Addon** | Editor/engine capabilities | Product host | Host-level trust |
| **Hybrid Package** | Assets + game code + optional editor tools | Project + host if trusted | Per contribution |
| **Template Package** | Project template or sample content | Project seed | User package |

An asset package must not silently carry editor host code. Editor tools are
contributed only through the existing [Extension System](../extensions/plugin-system.md)
and require separate trust approval.

## Package Kind And Contribution Kind

`HoroPackageKind` describes package intent:

```cpp
enum class HoroPackageKind {
    AssetPackage,
    GameLibrary,
    HybridPackage,
    TemplatePackage,
};
```

`PackageContributionKind` describes package contents:

```cpp
enum class PackageContributionKind {
    Assets,
    Scripts,
    Behaviors,
    RuntimeServices,
    EditorExtension,
    Samples,
    Templates,
    Documentation,
};
```

Contribution kinds are validated independently. A `HybridPackage` may declare
runtime and editor contributions, but editor contributions are not activated
unless the extension trust flow succeeds.

## Package Operations

| Operation | Meaning | Typical use |
|---|---|---|
| `ImportCopy` | Copy package content into the project as project-owned files | Asset packages, samples |
| `InstallDependency` | Add a package as a resolved project dependency | Game libraries |
| `MountReadOnly` | Reference verified package content read-only from cache | Large asset/library packages |
| `ImportSamples` | Copy samples into the project on demand | Any package with samples |

Install, trust, enable, and activation are separate lifecycle steps. See
[Package Lifecycle](./package-lifecycle.md).

## Manifest Format

```toml
[package]
id = "com.example.horo.gun-pack"
version = "1.2.3"
kind = "hybrid"
displayName = "Horo Gun Pack"
description = "Weapons, sounds, animations, and recoil behavior."
author = "Example Studio"

[compatibility]
engineMin = "0.9.0"
engineMax = "0.x"
sdkAbi = "horo-sdk-0.9"
scriptRuntime = "lua-1"
assetArchiveFormat = "1"
platforms = ["windows-x64", "linux-x64", "macos-arm64"]

[dependencies]
"com.horo.input" = { version = "^1.0.0", contributions = ["runtime"] }
"com.horo.editor.widgets" = { version = "^2.0.0", contributions = ["editor"] }

# Shorthand contribution roots. The verifier expands these into typed
# contribution descriptors before install/activation.
[contributions]
assets = ["assets/"]
scripts = ["scripts/"]
behaviors = ["behaviors/"]
services = ["services/"]
samples = ["samples/"]
editor = ["editor/"]
documentation = ["docs/"]

[[contribution]]
kind = "RuntimeServices"
id = "com.example.horo.gun-pack.recoil-service"
root = "services/"
required = true
capabilities = ["runtime.assets", "runtime.input"]

[[contribution]]
kind = "EditorExtension"
id = "com.example.horo.gun-pack.editor-tools"
root = "editor/"
required = false
capabilities = ["editor.panel", "asset.inspect"]

[license]
spdx = "MIT"
noticeFile = "NOTICE.md"
requiresAttribution = true
commercialUse = true
redistribution = "allowed"

[security]
declaredRisk = "game_code"
requiresSignature = true
```

## Package Archive Layout

Canonical `.horopkg` archives use a declared layout:

```text
package.horopkg
  horo-package.toml
  files.manifest.json
  assets/
  scripts/
  behaviors/
  services/
  editor/
  samples/
  docs/
  NOTICE.md
```

`files.manifest.json` is the canonical file list and includes per-entry hashes,
normalized paths, size, executable bit, and contribution root. In strict mode,
every file in the archive must be declared. The verifier rejects undeclared
executable files, duplicate normalized paths, unsafe links, path traversal, and
files outside declared contribution roots.

## Contribution Descriptors

Folder-based contribution declarations are shorthand only. Before install or
activation, the verifier expands them into typed descriptors that declare:

- contribution kind
- stable contribution ID
- root path
- whether the contribution is required or optional
- runtime/editor scope
- entry point or descriptor file
- script runtime when applicable
- behavior, component, system, service, command, tool, or template IDs
- supported platform and architecture for native binaries
- required capabilities

Activation, release inclusion, conflict checks, and dependency resolution use the
expanded descriptors, not raw folder names.

## Package Sources

The package system is source-agnostic. A package may come from:

- local `.horopkg` file
- local package directory
- direct URL
- Git repository pinned to an immutable revision
- static package index
- optional registry

Only portable sources may be required for project restore. Local path overrides
are user-local and must not be required on another machine.

Example `packages.json` source declarations:

```json
{
  "dependencies": {
    "com.vendor.weapon-pack": {
      "url": "https://cdn.vendor.com/weapon-pack-2.0.0.horopkg",
      "sha256": "..."
    },
    "com.team.vendored-pack": {
      "file": "vendor/packages/com.team.vendored-pack-1.0.0.horopkg"
    },
    "com.example.dialogue": {
      "git": "https://github.com/example/dialogue-pack.git",
      "rev": "abc123"
    },
    "com.horo.audio": {
      "registry": "official",
      "version": "^1.0.0"
    }
  }
}
```

Mutable Git branches, unpinned URLs, and local absolute paths are not valid
portable restore sources.

### Source Exactness

Direct file and direct URL sources must resolve to one exact artifact and must
include an expected content hash. Version ranges require an index, registry, or
other metadata source that can enumerate versions.

Portable file sources must be project-relative and inside approved project roots,
for example `vendor/packages/*.horopkg`. Absolute local paths are allowed only as
user-local development overrides.

Git sources are advanced package sources. Restore may fetch and verify a pinned
revision, but it must not execute package build scripts or generated code during
restore. If a Git source must be converted into a `.horopkg`, that packaging step
is an explicit trusted authoring/build operation, not an implicit restore side
effect.

MVP portable sources are:

- project-relative vendored `.horopkg`
- direct URL `.horopkg` with `sha256`
- static index entry resolving to `.horopkg`

Git sources, full registries, and marketplace flows are later extensions.

### Package Source Policy

Package sources are resolved under package source policy. The policy defines:

- allowed source types
- allowed domains and URL schemes
- redirect behavior
- maximum artifact size
- TLS requirements
- credential requirements and credential scope
- proxy handling
- offline behavior

HTTP without TLS, unbounded redirects, unpinned mutable artifacts, and secrets in
URLs are rejected unless an explicit local development policy allows them.


## Dependency Request And Lockfile

Project package metadata:

```text
.horo/packages.json       # portable requested dependencies and sources
.horo/packages.lock       # resolved exact versions and verification metadata
.horo/local/packages.json # user-local dev overrides and trust references
```

`packages.json` contains user intent:

- package ID
- version/range
- source declaration
- desired feature flags
- requested contribution group when needed

`packages.lock` contains resolved immutable identity:

- exact version
- source type and stable source identity
- artifact hash
- manifest hash
- resolved transitive dependencies
- package format version
- compatibility result
- resolved contribution list
- `requestHash`, the hash of canonical dependency-request fields from `packages.json`

`packages.lock` must not contain:

- local absolute paths
- user trust decisions
- credentials or tokens
- user-specific cache paths
- editor workspace state

Trust is user-local or policy-local state. Teams must not commit a trusted-code
decision by accident. Non-interactive restore and release validation fail when
`packages.lock.requestHash` does not match the canonical dependency request in
`packages.json`.

## Resolver Rules

The resolver is deterministic:

- same `packages.json`, `packages.lock`, platform, and package sources produce
  the same graph
- semantic versioning is supported but the lockfile pins exact versions
- circular dependencies fail validation
- dependency edges may name required contribution groups (`runtime`, `editor`,
  `assets`, etc.)
- editor-only dependency edges are excluded from runtime release closure
- prebuilt native dependencies are selected by platform, architecture, and SDK
  ABI

A dependency graph is not just package-to-package. Contribution-level dependency
metadata is required so release, restore, and activation can exclude editor-only
or unsupported contributions safely.

## Source And Prebuilt Game Libraries

A game library may be source or prebuilt:

| Type | Build | Trust | Compatibility |
|---|---|---|---|
| Source Game Library | Compiled by the project build pipeline | Medium | engine/script runtime compatibility |
| Prebuilt Game Library | Native binary per platform/arch/ABI | High | engine runtime, SDK ABI, platform, arch |

Prebuilt native libraries require explicit trust. Native code is trusted code,
not sandboxed.

## Package-Scoped Asset Identity

Mounted package assets normally use authoring references without concrete package
versions:

```text
package://com.vendor.weapon-pack/assets/rifle.mesh
```

The lockfile resolves package ID to the exact package version and artifact hash.
Version-pinned URLs are allowed only for explicit compatibility or migration
scenarios:

```text
package-pinned://com.vendor.weapon-pack@1.2.0/assets/rifle.mesh
```

Imported project assets use ordinary project GUIDs. Imported asset metadata keeps
source provenance:

```text
sourcePackageId
sourcePackageVersion
sourcePackageAssetId
```

This preserves update matching without allowing package updates to overwrite
project-owned edits.

## Trust Levels

| Trust level | Code allowed | Risk |
|---|---|---|
| `DataOnly` | None | Lowest |
| `ScriptRestricted` | Scripts in restricted runtime | Medium |
| `GameCodeTrusted` | Gameplay/native code | High |
| `EditorExtensionTrusted` | Editor tools and host capabilities | Highest |
| `NativeTrusted` | Native binary contribution | Highest |

Rules:

- install/restore does not grant trust
- native code is trusted code, not sandboxed
- permissions reduce authority but do not make native code safe
- editor extensions activate through the Extension System
- trust approval, denial, revocation, and activation are audit events


### Trust Metadata Is Not Authoritative

Declared package security metadata is advisory. The host computes required trust
from contribution kinds, expanded contribution descriptors, executable content,
native binaries, editor extension descriptors, source policy, signature status,
publisher identity, and product security policy.

A package cannot lower its own trust requirement. A package that declares
`declaredRisk = "data_only"` but contains scripts, native binaries, or editor
extension descriptors is rejected or escalated to the computed trust level.

## Publisher Identity And Signing Roots

Package signatures are verified against trusted publisher identities, not against
public keys supplied only by the package archive itself.

Trusted publisher keys may come from:

- built-in Horo trusted publisher store
- user-approved publisher trust
- organization/enterprise policy
- local offline trust store

A package archive may include certificate metadata, but that metadata is not a
trust root by itself. Unknown publishers require explicit approval or policy.
Signing key rotation and revocation are handled by the trusted publisher store or
organization policy.

## Cache Model

Package cache is a performance input, not a correctness input.

The active package cache is content-addressed by artifact hash and validated
package identity. Package ID and version are metadata indexes, not the sole cache
identity:

```text
~/.cache/horo/packages/by-hash/sha256/<artifactHash>/
~/.cache/horo/packages/by-id/com.vendor.weapon-pack/1.2.0 -> by-hash/...
```

If the same package ID and version resolve to a different artifact hash from a
different source, the resolver reports a package identity conflict unless an
explicit source override policy accepts it.

Every cache hit must still verify:

- artifact hash
- manifest hash
- signature when required
- extracted layout
- package format version
- contribution descriptors

The cache store tracks:

```text
PackageCacheStore
PackageCacheEntry
PackageCacheLease
PinnedPackage
QuarantinedPackage
```

Failed extraction or verification moves data to quarantine, not to the active
cache. Cache garbage collection obeys leases, pins, disk budget, and project
references.

## Credentials And Private Sources

Private sources use credential handles. Raw secrets must not appear in:

- `packages.json`
- `packages.lock`
- command-line arguments when avoidable
- logs
- diagnostic bundles
- job history

Credential access follows [Application Security](../security/application-security.md)
and [Release Security](../release/release-security.md).

## Package Validation And Authoring Commands

Package production and validation are first-class:

```bash
horopak validate package.horopkg
horopak pack ./MyPackage --output dist/
horopak sign dist/package.horopkg
horopak verify dist/package.horopkg
```

Validation checks:

- manifest schema validity
- declared files exist
- no undeclared files in strict mode
- no path traversal or symlink escape
- package ID namespace validity
- semantic version validity
- dependency graph validity
- contribution descriptor validity
- platform binaries match declared platform/arch
- license and notice files exist
- size limits are respected
- signatures and hashes verify

## Service Boundaries

| Service | Responsibility |
|---|---|
| `PackageService` | Coordinates high-level package use cases |
| `PackageResolver` | Resolves requests into a deterministic graph |
| `PackageRestoreService` | Restores clean-machine project package state |
| `PackageCache` | Stores verified archives and extracted read-only content |
| `PackageLifecycleService` | Install, enable, activate, update, uninstall, migrate |
| `AssetImportService` | Imports individual assets from packages |
| `GameplayModuleBoundary` | Registers game library modules, behaviors, services |
| `ExtensionHost` | Activates trusted editor extension contributions |
| `TrustService` | Evaluates signature, trust level, and user/policy approval |

Editor modals, CLI commands, MCP tools, and CI jobs are adapters. They call these
shared application services and do not own package business rules.

## Observability

Package operations emit structured records:

```text
PackageRestoreOperation
PackageDownloadOperation
PackageVerifyOperation
PackageInstallOperation
PackageActivateOperation
PackageUninstallOperation
PackageUpdateOperation
```

Records include safe fields such as operation ID, package ID, version, source
type, phase, duration, byte count, cache hit, verification result, and trust
requirement. They must not include URL query tokens, auth headers, raw secrets,
or sensitive local paths.


## Required Tests

- manifest schema validation
- package archive layout validation
- package source validation
- direct URL without hash rejected
- absolute local path rejected as portable source
- same package ID/version with different artifact hash conflicts
- lockfile excludes trust, credentials, absolute paths, and cache paths
- lockfile request hash detects stale lockfile
- contribution-level dependency graph resolution
- editor-only contribution excluded from runtime graph
- source vs prebuilt game library compatibility
- cache hit still verifies artifact and manifest
- package-scoped asset identity resolves through lockfile
- package-declared low trust cannot bypass computed trust requirement
- package signature requires a trusted publisher root

## Related Documents

- [Package Manager](./package-manager.html): HTML reference design for package
  dependencies, sources, restore state, overrides, and lockfile diff.
- [Package Restore](./package-restore.md)
- [Package Lifecycle](./package-lifecycle.md)
- [Package Release Integration](./package-release-integration.md)
- [Asset Pipeline](../runtime/asset-pipeline.md)
- [Extension System](../extensions/plugin-system.md)
- [Gameplay Module Boundary](../extensions/gameplay-module-boundary.md)
- [Application Security](../security/application-security.md)
- [Release Security](../release/release-security.md)
