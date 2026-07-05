# Package Lifecycle

## Purpose

This document defines package install, trust, enable, activation, update,
uninstall, migration, conflicts, ownership states, transactional staging, and
rollback.

## Lifecycle State Machine

Install, trust, enable, and activation are separate.

```text
Resolve
  -> Download
  -> Verify
  -> Stage
  -> Install
  -> Trust
  -> Enable
  -> Activate
```

| Step | Meaning |
|---|---|
| `Resolve` | Find package version matching request, lockfile, and compatibility. |
| `Download` | Fetch package archive into temporary cache staging. |
| `Verify` | Check hash, signature, manifest, contribution descriptors, and format. |
| `Stage` | Extract with path/resource limits into an incomplete staging directory. |
| `Install` | Atomically record package availability and lock/cache state. |
| `Trust` | User or policy approves code/native/editor contribution execution. |
| `Enable` | Mark selected contributions active for the project. |
| `Activate` | Register contributions in runtime/editor registries. |

Install does not imply trust. Trust does not imply activation. Activation does
not bypass contribution validation.

## Transactional Install

Package install uses the same safety posture as distribution staging:

1. Download into a temporary location.
2. Verify expected size and hash.
3. Extract with path traversal, symlink, depth, file count, and size limits.
4. Verify extracted layout and manifest hash.
5. Write an install record.
6. Atomically move verified content into the active cache.
7. Remove or quarantine failed staging directories.

The editor/runtime must never load package content from an incomplete staged
installation.

## Trust And Activation

Data-only assets may be mounted after verification. The following require trust
before activation:

- `ScriptRestricted`
- `GameCodeTrusted`
- `NativeTrusted`
- `EditorExtensionTrusted`

Native code is trusted code, not sandboxed. A native contribution cannot be made
safe merely by reducing permissions.

In non-interactive mode, new trust requirements fail unless a policy allowlist or
previous user-local trust state exists.


## Enablement State

Enablement is not the same as trust.

| Contribution | Enablement storage |
|---|---|
| Runtime-required assets/scripts/behaviors/services | Portable project metadata when it affects project behavior |
| Optional runtime features | Portable project metadata or profile-specific project setting |
| Editor extension requested by project | Portable request only |
| Actual editor extension activation | User-local or organization policy-gated state |
| Trust decision | User-local or policy-local state only |

A project may request an editor extension, but activation still requires local or
organization policy approval. Trust decisions are never portable project state.

## Ownership States

| State | Meaning | Update behavior | Uninstall behavior |
|---|---|---|---|
| `MountedPackageAsset` | Read-only asset from verified package cache | Follows package update | Removed when unmounted if no refs |
| `ImportedProjectAsset` | Copied from package and now project-owned | Never overwritten silently | Remains unless user deletes |
| `PackageOverride` | Project override layered over mounted package asset | Preserved | Preserved or detached |
| `SampleImportedAsset` | Sample/demo copied to project | Never managed by package | Remains unless user deletes |
| `GeneratedAsset` | Import/cook/build output | Regenerated from source | Deleted as derived state |

Package updates must not silently overwrite project-owned files.

## Update Policy

Package update flow:

```text
Resolve new graph
  -> compare contributions
  -> compute migration plan
  -> dry-run diagnostics
  -> stage new packages
  -> verify
  -> apply atomically
  -> rollback on failure
```

Update validation checks:

- package ID and source authenticity
- compatibility with engine/editor/runtime
- contribution removals or renames
- migration descriptors
- mounted asset references
- behavior and service usage in scenes
- license changes
- trust level escalation

Trust escalation requires explicit approval.

## Uninstall Policy

Before uninstall:

- detect mounted asset references in scenes/assets
- detect behavior type usage in scenes
- detect service dependencies
- close or detach editor panels contributed by the package
- check package dependency graph reverse edges

If required references remain, policy may:

- block uninstall
- convert mounted assets to project-owned copies
- leave explicit missing-reference diagnostics

Silent dangling references are forbidden.


## Rollback Contract

Package lifecycle operations write a transaction journal before mutating project
metadata, cache indexes, enablement state, activation state, or document files.
Rollback restores metadata and activation state when failure occurs before
irreversible project document migrations commit.

Migrations that modify project-owned scenes or assets are previewed first and
then committed through editor document transactions or explicit file migration
transactions. After a migration commits, rollback requires restoring from the
transaction backup or a user-approved VCS/workspace recovery path.

## Migration

Package updates may require migration:

```toml
[migrations."1.x_to_2.0"]
renamedAssets = [
  { from = "assets/rifle.mesh", to = "assets/weapons/rifle.mesh" }
]
renamedBehaviors = [
  { from = "com.vendor.OldWeapon", to = "com.vendor.Weapon" }
]
fieldMigrations = ["migrations/weapon_fields_v2.json"]
requiresUserReview = true
```

Migration plans must be previewable. Any migration that changes project-owned
scene or asset files uses editor document commands/transactions where applicable.

## Conflict Policy

Conflicts are contribution-specific:

- package ID conflict
- asset ID conflict
- behavior type ID conflict
- service ID conflict
- schedule node ID conflict
- script module name conflict
- editor command/menu ID conflict
- input action ID conflict
- shader keyword conflict
- component type ID conflict
- system ID conflict
- asset importer ID conflict
- CLI command path conflict
- MCP tool name conflict
- settings key conflict
- event type name conflict
- file template ID conflict
- project template ID conflict

Possible resolutions:

- fail
- namespace
- override
- skip
- import copy
- explicit user rename

Runtime identifiers should not be automatically renamed because serialized scene
references can break.

## Package UI Contract

The package modal is a presentation adapter over `PackageService`. It shows a
precomputed plan and does not own business rules.

The modal must show:

- package source
- exact version
- hash/signature status
- package kind
- contribution kinds
- required trust
- dependencies
- conflicts
- files to import/copy
- mounted vs copied content
- script/native/editor code warnings
- license/notice requirements
- disk size
- restore portability status

## Required Tests

- path traversal and symlink escape in package archive
- oversized archive bomb rejection
- invalid signature rejection
- native contribution without trust rejection
- non-interactive trust failure
- mounted asset update preserves references
- imported copy update is not overwritten
- uninstall with scene references blocks or diagnoses
- package override resolution
- migration preview and rollback
- transaction journal restores metadata and activation state on failed update
- runtime enablement is portable while trust remains user-local
- editor contribution deactivation closes panels safely

## Related Documents

- [Horo Package System](./package-system.md)
- [Package Restore](./package-restore.md)
- [Editor Modal Host](../editor/editor-modal-host.md)
- [Editor Document Model](../editor/editor-document-model.md)
- [Extension System](../extensions/plugin-system.md)
- [Application Security](../security/application-security.md)
