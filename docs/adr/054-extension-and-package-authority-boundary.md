# ADR-054: Extension and Package Authority Boundary

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Durable package identity, extension descriptors, trust and activation hand-off for extension-only and hybrid packages
- **Issue**: [EXT-001.1](https://github.com/abdullahbodur/horo-engine/issues/69)
- **Jira**: [HORO-69](https://horo-engine.atlassian.net/browse/HORO-69)
- **Normative documents**: [Horo Package System](../architecture/packages/package-system.md), [Package Lifecycle](../architecture/packages/package-lifecycle.md), [Extension System](../architecture/extensions/plugin-system.md), [Desired Project Trees](../architecture/desired-project-tree.md), [Application Security](../architecture/security/application-security.md)

## Context

Horo currently describes two overlapping distribution models. The package system
uses `horo-package.toml`, `files.manifest.json`, `.horopkg`,
`.horo/packages.json` and a verified package cache. The implemented extension
inventory instead discovers directories containing a top-level `extension.json`,
copies them under `~/.horo/extensions`, stores separate enable/trust state and may
download a ZIP through a separate marketplace path. `extension.json` repeats
package identity, version, compatibility, dependencies and permissions.

Keeping both models as durable authorities would make identity, dependency
resolution, trust, update, rollback and uninstall disagree. A hybrid package
could be resolved at one version by `PackageService` while `ExtensionHost` loads a
different directory/version. A project could also request the same add-on through
both `.horo/packages.json` and `.horo/plugins.json`, with no canonical lock or
transaction owner.

Extension modules still need their own ABI version, entry artifacts,
contributions and permissions. Those facts belong to a module descriptor, but do
not require a second package manager. This ADR assigns every durable fact and
lifecycle transition to one authority and defines migration from the current
extension-directory layout.

## Decision

### 1. Every installable extension is a Horo package

`.horopkg` is the only portable installable distribution artifact for asset,
game-library, extension-only, hybrid and template packages. A package containing
one or more `EditorExtension` or other approved extension contribution descriptors
is an extension package; it is not a separate artifact kind or package authority.

`horo-package.toml` owns the durable package ID, package version, kind, source-
independent compatibility, package dependency edges, contribution roots and
license/security declarations. `files.manifest.json` owns the canonical signed
file allowlist, normalized paths, sizes, modes and hashes. Together with the
archive signature and trusted source metadata they produce one immutable
`VerifiedPackageInstallRecord`.

Neither a directory name, registry entry, marketplace card, `extension.json`,
module return value nor dynamic-library filename may replace or amend that
identity. Duplicate `(packageId, version)` values with different artifact or
manifest digests are conflicts, not alternate extension installations.

### 2. Manifest roles do not overlap

| Artifact/value | Authority |
|---|---|
| `.horo/packages.json` | Portable project request, source and requested contribution groups |
| `.horo/packages.lock` | Exact resolved package graph and artifact/manifest identities |
| `horo-package.toml` | Durable package identity, version, kind, compatibility, dependencies and contribution descriptor paths |
| `files.manifest.json` | Exact package file allowlist, ownership, sizes, modes and digests |
| `extensions/<module-id>/extension.json` | One extension module ABI/entry descriptor plus its contributions and requested permissions |
| `VerifiedPackageInstallRecord` | Runtime authority that binds all verified manifests, digests, extracted roots and selected host variants |
| local/organization trust record | Approval for a specific verified publisher/package/artifact identity and capability set |
| package lifecycle state | Durable enablement intent, pending restart/update and activation outcome |
| `ExtensionHost` activation generation | Live module, registry and callback leases for one exact install record |

`extension.json` schema v1 contains a canonical module ID/version, extension ABI
range, platform/architecture entry variants, contribution descriptors, requested
permissions and optional package-owner binding. An owner package ID/version in the
descriptor is a defensive assertion and must exactly equal the install record; it
is not another source of truth. Cross-package dependencies, source locations,
package trust decisions, update channels and package enablement do not appear in
the extension descriptor.

The package manifest lists every extension descriptor as a typed contribution
with stable contribution ID and package-relative descriptor path. The file
manifest lists the descriptor and every referenced binary/resource. An extension
descriptor cannot discover another file by scanning its directory, broaden its
declared root or introduce an undeclared executable.

Module-to-module ordering within one package may use bounded local module IDs in
the package contribution graph. Any dependency on another package is declared
only in `horo-package.toml`, resolved once by `PackageResolver` and pinned once in
`.horo/packages.lock`.

### 3. Extension-only and hybrid layouts are explicit

An extension-only package has no special top-level manifest:

```text
com.vendor.fbx-importer-1.2.0.horopkg
├── horo-package.toml
├── files.manifest.json
├── publisher.cert.json
├── signature.sig
├── extensions/
│   └── com.vendor.fbx-importer.native/
│       ├── extension.json
│       ├── bin/
│       │   ├── macos-arm64/libhoro_fbx_importer.dylib
│       │   ├── linux-x64/libhoro_fbx_importer.so
│       │   └── windows-x64/horo_fbx_importer.dll
│       └── resources/icons/fbx.svg
├── licenses/
└── NOTICE.md
```

A hybrid package uses the same package manifests and adds ordinary content plus
one or more extension descriptors:

```text
com.vendor.weapon-tools-2.0.0.horopkg
├── horo-package.toml
├── files.manifest.json
├── assets/
├── scripts/
├── behaviors/
├── services/
├── extensions/
│   ├── com.vendor.weapon-tools.backend/extension.json
│   └── com.vendor.weapon-tools.editor/extension.json
├── samples/
├── docs/
└── NOTICE.md
```

Backend, GUI, CLI and MCP contributions may share a module or use separate
modules, but the package manifest declares each descriptor and contribution root.
The backend capability remains authoritative for behavior; presentation
contributions remain adapters. Asset/runtime contribution activation can be
portable project intent while native editor activation remains locally trusted.

### 4. Package services hand an exact activation candidate to ExtensionHost

The ordered hand-off is:

```text
PackageService / PackageResolver
    -> resolve one package graph and exact artifacts
PackageLifecycleService
    -> verify archive, manifests, files, signature and compatibility
TrustService
    -> compute required trust and match explicit local/organization approval
PackageLifecycleService
    -> resolve enabled contribution groups and build activation candidates
ExtensionHost
    -> validate descriptor/ABI binding and build a candidate registry batch
ExtensionHost
    -> atomically publish live registrations and return generation leases
PackageLifecycleService
    -> publish activation outcome against the same install-record revision
```

`ExtensionActivationCandidate` is immutable and contains an install-record ID and
lease, package ID/version/digests, one validated descriptor value, the selected
declared module artifact, approved capability/permission set, host compatibility
result and target activation generation. It contains no caller-selected absolute
path. `ExtensionHost` resolves only package-relative identities through the
leased verified install record.

`ExtensionHost` does not download, resolve, install, update, trust, enable or
uninstall packages. It validates the extension ABI and module-returned identity,
loads only the selected declared artifact, builds candidate contributions and
owns live module/registry/callback leases. It cannot write package lifecycle
state directly.

`PackageLifecycleService` owns durable enablement intent and coordinates restart,
update, rollback and uninstall. It cannot mark a native extension active until
`ExtensionHost` returns a committed generation. Failed activation records a typed
outcome without changing the verified install record or trusting the package.
Deactivation first closes/detaches contributions and drains leases; cache removal
waits for all live activation and content leases.

### 5. Trust is computed outside both manifests

Install and verification grant no execution trust. `TrustService` computes the
required level from verified files, package contribution kinds, extension module
kind, native/script artifacts, requested permissions, publisher/signature,
source policy and product/organization rules. Package and extension manifests may
declare risk and requested permissions, but cannot lower the computed result.

A trust record binds the publisher identity, package ID, artifact/manifest digest
or an explicit bounded update policy, approved capability set and policy revision.
Changing executable bytes, publisher, requested permissions or trust class makes
the prior decision insufficient until policy explicitly accepts the transition.
Portable project metadata can request an extension contribution but cannot carry
the local trust grant.

### 6. One project request and lock graph owns package dependencies

`.horo/packages.json` is the only portable package request file and
`.horo/packages.lock` is the only exact resolution. A request may select required
or optional contribution groups such as `runtime`, `editor`, `tools` or a stable
package-defined feature. Editor-only edges are still represented in this graph so
restore, release exclusion, license closure and conflicts see the same dependency
truth.

`.horo/plugins.json` is deprecated and is not consulted as a second resolver after
migration. User-local development overrides live under the package-system local
override policy and still require a valid `horo-package.toml`, file inventory and
synthetic verified development install record. Arbitrary extension-root or
current-directory scanning is not a production discovery path.

### 7. Legacy extension directories migrate transactionally

The current layout is recognized only by an explicit bounded legacy migration:

```text
~/.horo/extensions/<legacy-id>/extension.json
.horo/plugins.json
```

Migration performs these steps without loading code:

1. inventory each direct child once, rejecting links, escapes, duplicate IDs,
   oversized files/trees and invalid legacy manifests;
2. map the legacy package ID/version to `horo-package.toml`, move module facts to
   `extensions/<module-id>/extension.json` and generate a complete
   `files.manifest.json` in private staging;
3. show identity, file, dependency, permission, trust and project-request diffs;
4. validate the generated package exactly like a local development package;
5. atomically publish one package install record and migrate project requests to
   `.horo/packages.json` in the project transaction;
6. retain the original directory and metadata as a recoverable backup until the
   transaction commits, then quarantine or archive it under bounded policy.

Legacy `extension.json` package fields are migration input only. The generated
package manifest becomes authoritative after commit; the legacy file is never
read again for activation. Existing enabled state becomes pending enablement, not
an automatic native-code activation. Unsigned/directory content requires trust
review against the generated digest; the migration does not silently copy a trust
grant to changed bytes.

The compatibility reader may exist for one documented migration window, but it
can only produce a migration plan. New installs, marketplace downloads, project
creation and authoring tools write the canonical package layout immediately.
Rollback restores the old project request/state metadata and leaves legacy files
untouched; it cannot leave both authorities active.

### 8. Verification proves authority and hand-off boundaries

Required coverage includes:

- extension-only and hybrid package fixtures with one and multiple modules;
- package/descriptor owner ID and version mismatch rejection;
- undeclared descriptor, binary, resource and executable rejection;
- cross-package dependency in `extension.json` rejection;
- one deterministic packages request/lock graph for editor and runtime groups;
- install without trust, trust without enablement and enablement without host
  compatibility remaining inactive;
- activation candidate digest/lease, path containment and stale-generation tests;
- all-or-nothing contribution registration and activation-outcome publication;
- update/uninstall waiting for module, registry, callback and content leases;
- legacy inventory limit, duplicate, traversal, link and malformed-manifest
  rejection;
- dry-run migration diff, trust reapproval, project request conversion, rollback
  and idempotent resume after interruption; and
- proof that production `ExtensionHost` cannot activate a raw directory or fetch,
  resolve, trust or mutate package state.

## Consequences

Package identity, files, dependencies, signatures, source resolution, update and
rollback now have one authority for every package shape. ExtensionHost becomes a
narrow live-code host rather than a second package manager, while extension
descriptors retain independent module/ABI/contribution identity. The cost is a
breaking authoring-layout migration, removal of `.horo/plugins.json` as an active
resolver and new activation-candidate/install-record plumbing before the current
directory loader can become a production path.

## Rejected Alternatives

| Option | Decision and reason |
|---|---|
| Keep `extension.json` and `horo-package.toml` as peer package manifests | Rejected: identity, dependencies, trust and updates would have competing authorities. |
| Treat extension packages as ZIPs outside `.horopkg` | Rejected: duplicates source, verification, cache, lock, transaction and rollback machinery. |
| Put module ABI and every contribution directly in `horo-package.toml` | Rejected: package and module schemas evolve at different boundaries and multi-module packages need bounded independent descriptors. |
| Let ExtensionHost scan package directories for descriptors/binaries | Rejected: undeclared files and caller-selected paths could bypass the verified install record. |
| Store native extension trust in `.horo/packages.json` | Rejected: cloning a project must not grant code-execution trust on another machine. |
| Preserve `.horo/plugins.json` indefinitely as an overlay | Rejected: dependency resolution, restore and release closure would still have two graphs. |
| Auto-activate migrated legacy directories | Rejected: migration changes the verified identity and old directory installs may be unsigned or insufficiently bounded. |
