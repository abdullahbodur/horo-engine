# ADR-057: Package Manifest V1 Typed Model

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Versioned typed package identity, dependencies, compatibility, modules, artifacts, content, contributions, licenses, signature requirements and bounded manifest processing
- **Issue**: [PKG-001.1](https://github.com/abdullahbodur/horo-engine/issues/115)
- **Jira**: [HORO-115](https://horo-engine.atlassian.net/browse/HORO-115)
- **Parent**: [PKG-001](https://github.com/abdullahbodur/horo-engine/issues/36)
- **Related**: [ADR-054](054-extension-and-package-authority-boundary.md), [ADR-055](055-extension-manifest-v1-typed-model.md)
- **Normative documents**: [Horo Package System](../architecture/packages/package-system.md), [Package Lifecycle](../architecture/packages/package-lifecycle.md), [Package Restore](../architecture/packages/package-restore.md), [Package Release Integration](../architecture/packages/package-release-integration.md), [System Design](../architecture/foundation/system-design.md)

## Context

The package architecture currently shows one illustrative `horo-package.toml`
and lists package/contribution enums, but it does not define the complete public
value model or the boundary between author claims and verified facts. Package
IDs, versions, dependency ranges, platforms, ABI names, capabilities, paths,
artifacts, signatures and contribution data are still described primarily as
strings. Different resolver, restore, install, release and extension consumers
could therefore parse the same manifest into incompatible meanings.

ADR-054 establishes `horo-package.toml`, `files.manifest.json` and the verified
install record as the single package authority. ADR-055 gives package-scoped
extension modules a separate typed descriptor. The package layer now needs one
bounded immutable v1 model that represents pure data, tool, extension,
gameplay-library, template and hybrid packages without arbitrary maps or
consumer-specific defaults.

No package manifest implementation or public package target exists in the current
tree. The existing extension directory/JSON loader is transitional and cannot be
promoted into package authority. The first implementation must begin from this
contract, with deliberate target/header ownership, rather than creating another
temporary string carrier.

## Decision

### 1. Decode, semantic validation and bundle verification are separate

Package processing has four immutable stages:

```text
bounded TOML syntax/schema decode
  -> DecodedPackageManifestV1
semantic validation and canonicalization
  -> ValidatedPackageManifestV1
bind files.manifest.json + archive digest + signature evidence
  -> VerifiedPackageBundle
source/resolver/install policy
  -> VerifiedPackageInstallCandidate
```

A decoded value cannot enter dependency resolution, cache indexes, trust planning,
mounting, activation or release. `ValidatedPackageManifestV1` is inert author
metadata. `VerifiedPackageBundle` proves that every referenced file/artifact and
signature statement matches the exact archive. A later install candidate adds
source-policy and host-compatibility decisions without mutating the manifest.

Every transition is a pure result with bounded diagnostics. Parsing or validation
does not read the filesystem, download, extract, verify a key, update a cache,
write a lockfile, grant trust, load code or register contributions.

### 2. Identity domains are explicit bounded value types

V1 uses distinct types rather than interchangeable strings:

```cpp
struct PackageManifestSchemaVersion;
struct HoroPackageId;
struct PackageVersion;
struct PackageManifestDigest;
struct PackageDependencyId;
struct PackageFeatureId;
struct PackageModuleId;
struct PackageArtifactId;
struct PackageContributionId;
struct PackageContentSetId;
struct PackageFileId;
struct PackageCapabilityId;
struct PackageFormatVersion;
struct EngineApiVersionRange;
struct SdkAbiId;
struct ScriptRuntimeId;
struct PlatformId;
struct CpuArchitectureId;
struct PublisherId;
struct SigningKeyId;
struct SignatureAlgorithmId;
struct LicenseExpression;
```

Semantic versions and ranges have parsed canonical types. Platform selection is a
typed predicate over registered operating-system, architecture, minimum-version,
ABI and build-profile values. Package-relative paths are validated normalized
`PackagePath`/`PackageFileId` values; the public model has no absolute filesystem
path or source URL.

Display names, descriptions and author text are metadata, not identity. A module,
artifact, contribution, capability, feature and file ID cannot be substituted for
another domain even when their canonical text matches.

### 3. One aggregate owns every supported manifest field

The canonical value is conceptually:

```cpp
struct ValidatedPackageManifestV1 {
    PackageManifestSchemaVersion schema;
    PackageIdentity identity;
    HoroPackageKind kind;
    PackageDisplayMetadata metadata;
    PackageCompatibility compatibility;
    std::vector<PackageDependencyDescriptor> dependencies;
    std::vector<PackageFeatureDescriptor> features;
    std::vector<PackageModuleDescriptor> modules;
    std::vector<PackageArtifactDescriptor> artifacts;
    std::vector<PackageContributionDescriptor> contributions;
    std::vector<PackageContentSetDescriptor> contentSets;
    PackageLicenseDescriptor license;
    PackageSecurityDeclaration security;
    std::vector<PackageManifestExtensionV1> extensions;
    PackageManifestDigest digest;
};
```

The digest is computed from deterministic canonical manifest bytes and is not an
author-controlled serialized field. Every optional field uses explicit absence;
empty strings, magic values and inherited defaults do not encode absence.

Package source, credentials, trust approval, cache path, install state, enablement,
selected host artifact, lock resolution, activation state and local policy are not
manifest fields. They belong to later source/resolver/lifecycle records.

### 4. Package kinds are closed intents with structural invariants

```cpp
enum class HoroPackageKind {
    Data,
    Tool,
    Extension,
    GameplayLibrary,
    Hybrid,
    Template,
};
```

Kind is not inferred from a directory name, and it does not grant authority. The
validated contribution/artifact closure must satisfy its shape:

| Kind | Required/allowed shape |
|---|---|
| `Data` | Contains only non-executable content, schemas, documentation and optional import/cook declarations; no module entry or executable bit. |
| `Tool` | Contains declared build/authoring tools and resources; tool execution remains capability/trust gated and is never an implicit restore hook. |
| `Extension` | Contains one or more package-scoped ADR-055 extension descriptors plus only their declared artifacts/resources; no unrelated gameplay/content authority. |
| `GameplayLibrary` | Contains source/prebuilt gameplay modules, scripts, behaviors, services and supporting content under gameplay compatibility/trust policy. |
| `Hybrid` | Explicitly combines two or more otherwise valid shapes; each contribution retains its own scope, trust, release and activation rules. |
| `Template` | Contains inert project seed/sample content and template operations; applying it copies through a transaction and never activates packaged code. |

A package cannot call itself `Data` while declaring executable/script/module
content, or call itself `Extension` to smuggle project assets. Security policy may
raise the required trust for any shape; a declared kind never lowers computed
trust.

The legacy serialized kind names `asset-package`, `game-library`,
`extension-package`, `hybrid-package` and `template-package` migrate to these v1
values. `tool` is a first-class v1 kind rather than an undocumented hybrid.

### 5. Dependencies and features are typed portable intent

```cpp
enum class PackageDependencyRequirement {
    Required,
    Optional,
};

enum class PackageDependencyScope {
    Authoring,
    Build,
    Runtime,
    Editor,
    Test,
};

struct PackageDependencyDescriptor {
    PackageDependencyId id;
    HoroPackageId package;
    SemanticVersionRange versions;
    PackageDependencyRequirement requirement;
    EnumSet<PackageDependencyScope> scopes;
    EnumSet<PackageFeatureId> requiredFeatures;
    EnumSet<PackageContributionId> requiredContributions;
    PackageCompatibilityConstraint compatibility;
};
```

Dependencies name packages and portable requirements only. They do not embed a
registry, URL, Git revision, credential, local path, resolved version or hash;
those belong to project requests/source policy and the lockfile. Feature and
contribution references must resolve in the target package metadata during graph
resolution and cannot be loose group-name conventions.

Duplicate dependency IDs, conflicting ranges/scopes, references to undeclared
local features or self-dependencies reject the manifest. Cross-package cycles and
version selection remain resolver outcomes because they require a candidate graph.

### 6. Compatibility is a typed predicate, not consumer string matching

`PackageCompatibility` contains package-format version, engine API range, optional
SDK ABI, supported host roles, script runtime requirements and a finite set of
typed platform selectors. A selector explicitly states OS, architecture, minimum
OS version, ABI/toolchain profile and allowed build profiles as applicable.

An empty selector set means platform-independent only when all artifacts and
contributions are portable. Native artifacts require non-overlapping explicit
selectors. Unsupported host compatibility is a typed result; consumers do not
silently choose another artifact or treat an unknown platform token as portable.

Engine product version, SDK ABI, extension C ABI, gameplay module ABI, package
format and script runtime versions remain distinct domains. One cannot satisfy
another through textual equality.

### 7. Modules, artifacts and files form a verified reference graph

```cpp
enum class PackageModuleKind {
    Extension,
    Gameplay,
    Script,
    Tool,
};

enum class PackageArtifactKind {
    DataTree,
    NativeLibrary,
    NativeExecutable,
    ScriptBundle,
    Descriptor,
    Schema,
    Documentation,
};

struct PackageModuleDescriptor {
    PackageModuleId id;
    PackageModuleKind kind;
    PackageArtifactId descriptor;
    std::vector<PackageArtifactId> entries;
    PackageModuleCompatibility compatibility;
};

struct PackageArtifactDescriptor {
    PackageArtifactId id;
    PackageArtifactKind kind;
    std::vector<PackageFileId> files;
    std::optional<PlatformSelector> platform;
    PackageArtifactUsage usage;
};
```

Modules identify lifecycle/code boundaries. Artifacts group exact files for one
portable or host-specific purpose. Contributions reference modules/artifacts by
typed ID; they do not name an undeclared root and ask a consumer to scan it.

`files.manifest.json` has its own typed `ValidatedPackageFileManifestV1` with one
entry per normalized `PackageFileId`, content hash, size, mode/executable policy
and optional contribution ownership. `VerifiedPackageBundle` binds the two
manifest digests and archive digest, proves every reference exists, rejects
undeclared/duplicate files and ensures executable bits agree with artifact kind.

Directory shorthand in authoring tools is expanded before canonical package
publication. Shorthand is never present in the validated distribution model.

### 8. Contributions use a closed typed variant

```cpp
using PackageContributionPayload = std::variant<
    PackageDataContribution,
    PackageToolContribution,
    PackageExtensionContribution,
    PackageGameplayContribution,
    PackageScriptContribution,
    PackageServiceContribution,
    PackageSampleContribution,
    PackageTemplateContribution,
    PackageDocumentationContribution>;

struct PackageContributionDescriptor {
    PackageContributionId id;
    PackageContributionRequirement requirement;
    EnumSet<PackageContributionScope> scopes;
    EnumSet<PackageCapabilityId> declaredCapabilities;
    PackageContributionPayload payload;
};
```

Each payload owns its required typed references. Extension payloads point to an
ADR-055 descriptor artifact. Gameplay payloads point to gameplay module/artifact
IDs. Tool payloads declare operation IDs and executable/script artifacts but do
not create install/restore hooks. Data/content payloads declare mount/import/cook
intent. Template/sample payloads declare transactional copy/seed intent.

Contribution scope drives graph, release and activation decisions. `Editor` does
not imply `Runtime`; required editor contributions may block an editor-ready
profile without contaminating a shipping-runtime closure. Capabilities are author
requests/claims and are checked against host catalogs and trust policy later.

### 9. Content is identified independently from archive layout

`PackageContentSetDescriptor` gives stable identity to assets, sources, cooked
data, scripts, schemas, documentation, samples and template trees. It references
finite artifact/file IDs and declares one typed disposition: read-only mount,
import copy, cook input, template seed, sample copy or documentation-only.

Package-relative layout is not semantic identity. Moving a file between canonical
archive folders requires a manifest update but does not rename a stable asset/
content ID accidentally. Import/mount destinations are typed logical namespaces;
manifests cannot contain machine-local absolute destinations or overwrite project-
owned content without an explicit later transaction plan.

### 10. License and signature data do not create trust roots

`PackageLicenseDescriptor` contains a parsed SPDX expression, verified notice/
license file IDs, redistribution classification and attribution requirements.
Free-form legal text does not replace referenced files or policy evaluation.

`PackageSecurityDeclaration` may state the minimum expected trust class, whether
a signature is required and publisher identity expected by the author. These are
claims that policy may strengthen but never weaken.

Cryptographic signature bytes are carried in a detached typed signature envelope,
not inside the signed TOML payload. The envelope identifies algorithm, publisher,
signing-key ID, signed package-manifest digest, file-manifest digest and archive
digest. `VerifiedSignatureEvidence` is produced only after `TrustService` resolves
that key through a trusted built-in/user/organization signing root. A public key
or publisher string supplied only by the archive is not a trust root.

### 11. Schema v1 is bounded and fails closed

`schemaVersion = 1` is mandatory. Every top-level and nested field has an exact
type, cardinality, required/optional rule and owner. Limits cover input bytes,
TOML depth/table/array counts, UTF-8 length, dependency/module/artifact/
contribution/file counts, path segments, selector alternatives and diagnostic
count. Integer conversion and aggregate byte arithmetic are checked.

Unknown fields reject v1 except inside one bounded `extensions` array. Each
extension has a registered ID/version, required flag and bounded canonical
payload. Unknown required extensions reject the manifest. Unknown optional
extensions round-trip as inert bytes and cannot add identity, files, dependencies,
artifacts, modules, capabilities, signatures, content or lifecycle behavior.

Duplicate TOML keys/tables, invalid UTF-8, non-canonical IDs/versions, ambiguous
selectors, unresolved local references, kind/shape mismatch and contradictory
security/executable declarations reject the value with stable typed diagnostics.

### 12. Canonical serialization and round-trip are deterministic

One canonical serializer emits normalized UTF-8 TOML with deterministic field,
map and ID ordering, canonical semantic-version/range text and no comments or
machine-local state. Parsing canonical bytes and serializing again produces the
same bytes and `PackageManifestDigest`.

Authoring files may retain comments and preferred formatting outside the public
model, but publication/verification uses canonical bytes. Consumers compare typed
identity/digests, never raw authoring formatting. No serializer writes verified
signature evidence, source policy, trust, resolution or install state back into
`horo-package.toml`.

### 13. Failure and lifecycle behavior is recoverable

Manifest decode/validation returns no partial live object. Diagnostics identify
field path, stable code and bounded safe evidence without echoing credentials or
sensitive source data. Failed download/extraction/bundle verification remains in
staging or quarantine and cannot publish cache/install records.

Install and update journal the exact validated manifest/file-manifest/signature
digests before atomic publication. Cancellation or failure before publication
deletes/quarantines staging and leaves the prior install record active. A manifest
model never owns filesystem cleanup, activation rollback or lockfile mutation; the
package lifecycle transaction owns those state transitions.

### 14. Migration replaces shorthand and string carriers explicitly

The existing documentation TOML is legacy authoring input. Migration:

1. parses it under strict legacy bounds without activating content;
2. maps known kind/contribution/platform/compatibility tokens to v1 values;
3. expands contribution directories into stable modules, artifacts, content sets
   and exact file-manifest entries;
4. requires explicit IDs/versions/selectors where the legacy form inferred them;
5. separates project source requests and local state from package metadata;
6. emits diagnostics for every lossy, unknown or trust-relevant conversion; and
7. validates and canonicalizes the complete v1 bundle before transactional
   publication.

`AssetPackage`, `GameLibrary`, `ExtensionPackage`, `HybridPackage` and
`TemplatePackage` C++/documentation names migrate once to the v1 kind vocabulary.
No compatibility API keeps two active kind/contribution models.

The implementation introduces a narrow package-model public target/header set
only when its CMake ownership and consumer dependencies are defined together.
TOML decoding, signature libraries, archive/file adapters and source handling stay
private. Public headers expose Horo types and results only.

## Verification

Canonical fixtures cover:

- pure data with read-only mount and import/cook content;
- tool package with a host-gated executable and no restore hook;
- extension-only package containing multiple ADR-055 descriptors;
- source and prebuilt gameplay libraries with typed compatibility;
- hybrid package with data, gameplay and optional editor contributions; and
- template package with transactional project seed content.

Every fixture round-trips to identical typed values, canonical bytes and digest.
Negative coverage includes all ID/version/range domains, bounds, duplicate keys,
kind/shape mismatch, ambiguous selectors, missing files/artifacts/modules,
executable-mode mismatch, invalid license references, self-dependency, unknown
required fields/extensions and signature digest/key-root mismatch.

Integration tests prove the same validated model drives resolver, restore,
install, trust, activation and release projections; decoded/invalid values cannot
construct their input types. Failure-injection tests cover cancellation and every
pre/post-publication stage with the previous install record preserved. Compile-
only consumers prove the public model has no TOML, filesystem, archive, crypto,
GUI, renderer or platform-native dependency.

## Consequences

All package shapes share one versioned model and one reference graph. Resolver,
restore, lifecycle, trust, extension/gameplay activation and release cannot invent
their own meanings for manifest strings. Author claims remain distinct from
verified bundle/signature/source facts. The cost is a larger typed vocabulary,
strict migration of shorthand manifests and deliberate schema/catalog revisions
for new contribution/artifact families.

## Rejected Alternatives

| Option | Decision and reason |
|---|---|
| Keep the TOML DOM/string maps as the service model | Rejected: every consumer would reinterpret identity, compatibility, paths and authority. |
| Use one `kind` plus arbitrary contribution property maps | Rejected: pure data, tools, extensions, gameplay and hybrids would have unverifiable shape/security invariants. |
| Infer kind only from files or directories | Rejected: layout is not semantic intent, and inference can grant or hide executable authority. |
| Put package sources and credentials in the package manifest | Rejected: packages cannot choose their own resolver source or carry portable secrets. |
| Treat the archive publisher key as its own trust root | Rejected: self-asserted signing identity proves integrity at most, not trusted provenance. |
| Make `files.manifest.json` fields part of `horo-package.toml` | Rejected: semantic package intent and exact archive inventory have different digest/update responsibilities. |
| Let modules scan contribution roots | Rejected: undeclared files, platform ambiguity and consumer-dependent behavior would bypass bundle verification. |
| Accept unknown fields and drop them | Rejected: typos or future security fields could silently lose effect. |
| Mutate/install while parsing | Rejected: malformed input, cancellation and partial failure would create unrecoverable ambient state. |
| Preserve legacy and v1 models indefinitely | Rejected: two package authorities would recreate the ambiguity ADR-054 removed. |
