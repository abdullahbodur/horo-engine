# ADR-055: Extension Manifest V1 Typed Model

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Backend- and GUI-neutral typed model for package-scoped extension modules, contributions, permissions, settings, events, errors, service exports and script bindings
- **Issue**: [EXT-001.2](https://github.com/abdullahbodur/horo-engine/issues/70)
- **Jira**: [HORO-70](https://horo-engine.atlassian.net/browse/HORO-70)
- **Related**: [ADR-054](054-extension-and-package-authority-boundary.md)
- **Normative documents**: [Extension System](../architecture/extensions/plugin-system.md), [Horo Package System](../architecture/packages/package-system.md), [Extension Module Development Guide](../guides/extension-module-development.md), [System Design](../architecture/foundation/system-design.md)

## Context

The current public `ExtensionManifest` stores package, module, contribution,
compatibility and platform facts as unrelated strings. It accepts implicit module
defaults, carries an absolute `rootPath`, knows only three contribution fields and
cannot represent permissions, settings, events, errors, service exports or script
bindings. Callers must interpret values such as `kind`, `type`, `sdkAbi` and
platform names after parsing. Invalid references can therefore survive until a
dynamic library is selected or a contribution callback registers.

ADR-054 removes the larger ambiguity: package identity, files, dependencies,
trust and enablement belong to the package system, while each package-scoped
`extension.json` describes one module and its contributions. A complete typed v1
model must now bind that module descriptor to the exact verified package record,
represent GUI-only, backend-only, script-consumable and mixed roles without
inference, and validate every reference before ExtensionHost may activate code.

The model is a public Horo contract used by package validation, trust planning,
ExtensionHost, CLI/MCP/UI projections and tests. It cannot expose JSON-library
nodes, filesystem/native paths, ImGui types, backend handles, dynamic-library
objects or mutable registry state.

## Decision

### 1. Parsing, validation and activation use distinct immutable values

The boundary exposes three stages:

```text
bounded syntax/schema decode
    -> DecodedExtensionModuleManifestV1
semantic and cross-reference validation against VerifiedPackageInstallRecord
    -> ValidatedExtensionModuleManifestV1
package graph composition and trust/host resolution
    -> ExtensionActivationCandidate
```

Only `ValidatedExtensionModuleManifestV1` may enter package contribution indexes
or trust planning. Only an `ExtensionActivationCandidate` may enter
ExtensionHost. A decoded value is never “partially valid” and cannot expose an
entry artifact or register a contribution.

All values own their data. Validation returns a new immutable value and indexes;
it does not mutate the decoded object in place. The result retains the canonical
descriptor digest and exact package install-record revision used for validation.

### 2. Identity types are not interchangeable strings

V1 uses distinct bounded canonical value types:

```cpp
struct ExtensionManifestSchemaVersion;
struct ExtensionManifestDigest;
struct ExtensionModuleId;
struct ExtensionContributionId;
struct ExtensionSettingId;
struct ExtensionEventId;
struct ExtensionErrorDomainId;
struct ExtensionErrorCode;
struct ExtensionServiceExportId;
struct ExtensionScriptApiId;
struct ExtensionPointSchemaId;
struct ExtensionPermissionId;
struct ExtensionCapabilityId;
struct LocalizationMessageId;
struct PackageResourceId;
```

Package ID/version, package file identity, semantic versions/ranges, operating
system, CPU architecture, hashes and install-record IDs reuse the package and
foundation typed contracts from ADR-054. A module ID cannot be passed where a
contribution, service, event or script API ID is required even when the canonical
text is identical.

Canonical IDs use registered domain-specific grammar and bounds. Human labels,
descriptions and localization keys are presentation metadata, never identity.
Paths are validated package-relative resource/file identities from the verified
file manifest; the public model contains no absolute path and never joins paths.

### 3. One descriptor owns exactly one explicitly role-tagged module

The module envelope is:

```cpp
enum class ExtensionModuleRuntimeKind {
    NativeCAbi,
    Lua54,
    Declarative,
};

enum class ExtensionModuleRole {
    BackendCapability,
    EditorPresentation,
    HeadlessTooling,
    ScriptProvider,
    RuntimeParticipant,
};

struct ExtensionModuleDescriptorV1 {
    ExtensionModuleId id;
    SemanticVersion version;
    ExtensionModuleRuntimeKind runtimeKind;
    EnumSet<ExtensionModuleRole> roles;
    ExtensionAbiRequirement abi;
    std::vector<ExtensionEntryVariant> entries;
    ExtensionLifecyclePolicy lifecycle;
};
```

`roles` is non-empty and explicit. Runtime kind answers how code/data executes;
roles answer what the module is allowed to contribute. They are orthogonal:

| Shape | Required roles | Typical contributions |
|---|---|---|
| GUI-only | `EditorPresentation` | panel, tab, settings page over an existing capability |
| Backend/library-only | `BackendCapability` and optionally `HeadlessTooling` | importer, cooker, validator, command provider |
| Script-consumable | `BackendCapability` + `ScriptProvider` | typed service export plus approved script API binding |
| Mixed-role | explicit union of its real roles | backend service plus GUI/CLI/MCP adapters |

A module cannot gain a role because an unknown contribution string resembles a
GUI or runtime type. Contributions, service exports and script bindings must be
permitted by the declared role set. `RuntimeParticipant` remains restricted by
gameplay/runtime policy; declaring the role does not grant activation.

Native entry variants contain typed host OS, architecture, build/ABI profile,
minimum platform version and one `PackageFileId`. Lua/declarative entries use
their matching typed artifact/schema identity. Duplicate or overlapping selector
keys are invalid. A validated descriptor may have no current-host entry, but host
resolution must return typed `UnsupportedHost`; it cannot choose another variant.

### 4. The complete manifest is a typed aggregate

```cpp
struct ExtensionPackageBindingV1 {
    HoroPackageId packageId;
    SemanticVersion packageVersion;
    PackageManifestDigest packageManifest;
    PackageInstallRecordId installRecord;
    PackageInstallRecordRevision revision;
};

struct ValidatedExtensionModuleManifestV1 {
    ExtensionManifestSchemaVersion schema;
    ExtensionPackageBindingV1 package;
    ExtensionModuleDescriptorV1 module;
    std::vector<ExtensionPermissionRequirement> permissions;
    std::vector<ExtensionContributionDescriptorV1> contributions;
    std::vector<ExtensionServiceExportDescriptorV1> serviceExports;
    std::vector<ExtensionScriptApiDescriptorV1> scriptApis;
    std::vector<ExtensionSettingDescriptorV1> settings;
    std::vector<ExtensionEventDescriptorV1> events;
    std::vector<ExtensionErrorDescriptorV1> errors;
    std::vector<ExtensionManifestExtensionV1> extensions;
    ExtensionManifestDigest digest;
};
```

The JSON descriptor may contain an `ownerPackage` ID/version assertion. The
validator replaces no package data from that assertion: it checks equality and
builds `ExtensionPackageBindingV1` exclusively from the leased verified install
record. Package display name, author, source, dependencies, trust and enablement
are not extension-manifest fields.

A package with multiple modules owns multiple validated descriptor values.
`ValidatedExtensionPackageComposition` binds them to one package record, rejects
duplicate IDs across descriptors and builds immutable lookup indexes. No “default
module” is synthesized and module version never inherits silently from package
version in canonical v1.

### 5. Contributions use typed payload variants and registered schemas

Every contribution has a common envelope:

```cpp
struct ExtensionContributionDescriptorV1 {
    ExtensionContributionId id;
    SemanticVersion version;
    ExtensionModuleId owner;
    ExtensionPointSchemaId point;
    ExtensionContributionRequirement requirement;
    EnumSet<ExtensionCapabilityId> requiredCapabilities;
    EnumSet<ExtensionPermissionId> requiredPermissions;
    ExtensionContributionPayloadV1 payload;
};
```

`ExtensionContributionPayloadV1` is a closed `std::variant` of the typed payload
models supported by schema v1, including asset importer/cooker/preview, project
validator, application capability, process observer, pipeline step, toolchain
provider, command, MCP tool and approved editor-surface descriptors. A payload
contains Horo resource IDs, placement enums/IDs, localization keys, field schemas,
operation/thread policies and other typed values required by its extension point.
It contains no ImGui callback, renderer/native handle, service locator, arbitrary
property map or backend enum.

The host owns a versioned `ExtensionPointSchemaCatalog`. The manifest names the
exact schema ID/version represented by the payload. A known point with a wrong
payload alternative/version is invalid. An unsupported required point rejects the
package; an optional unavailable point remains an explicit inactive outcome and
never becomes an opaque live contribution.

Backend/library behavior is exposed as an application capability, provider,
operation or service export. GUI, command, CLI and MCP contributions are separate
adapters that reference that typed authority. A GUI contribution cannot hide a
backend operation in arbitrary UI payload data.

### 6. Permissions are typed requirements, not grants

```cpp
enum class ExtensionPermissionRequirementKind {
    Required,
    Optional,
};

struct ExtensionPermissionRequirement {
    ExtensionPermissionId id;
    ExtensionPermissionRequirementKind requirement;
    LocalizationMessageId rationale;
};
```

Permission IDs resolve through the host's sealed/versioned permission catalog.
Unknown required IDs reject the descriptor; optional IDs make only their dependent
contributions unavailable. A descriptor's permission set must be a subset of the
package contribution capability envelope from ADR-054. The validated model records
requests and dependency edges, not trust approval. `TrustService` supplies the
approved subset later in the activation candidate.

Every contribution, service export and script binding names the permissions it
requires. A top-level unused permission is rejected rather than silently granting
future authority, and a referenced undeclared permission is invalid.

### 7. Settings are typed, scoped and reload-aware

```cpp
using ExtensionSettingValue = std::variant<
    bool,
    std::int64_t,
    double,
    BoundedUtf8String,
    RegisteredChoiceId,
    PackageResourceId>;

struct ExtensionSettingDescriptorV1 {
    ExtensionSettingId id;
    ExtensionModuleId owner;
    ExtensionSettingScope scope;
    ExtensionSettingValueKind kind;
    ExtensionSettingValue defaultValue;
    ExtensionSettingConstraints constraints;
    ExtensionSettingReloadPolicy reload;
    LocalizationMessageId label;
    std::optional<LocalizationMessageId> description;
    bool includeInPresets;
};
```

Scope is one of user, workspace, project, project-profile or session under the
configuration contract. Reload policy is a closed enum such as `ImmediateSafe`,
`NextOperation`, `NextActivation` or `RestartRequired`. Kind, default and
constraints must match exactly. Secret values are not manifest defaults; settings
that need credentials use typed credential-reference capability contracts.

Settings are declared once and referenced by ID from contributions or service
exports. Prefix/string convention is not used to infer ownership. Preset opt-in is
explicit and false by default for paths, credentials and instance-specific data.

### 8. Events and errors use host-owned semantic contracts

```cpp
struct ExtensionEventDescriptorV1 {
    ExtensionEventId id;
    ExtensionModuleId owner;
    ExtensionEventScope scope;
    ExtensionEventPayloadSchema payload;
    DiagnosticPrivacyClass privacy;
    ExtensionEventDeliveryPolicy delivery;
};

struct ExtensionErrorDescriptorV1 {
    ExtensionErrorDomainId domain;
    ExtensionErrorCode code;
    ErrorSeverity defaultSeverity;
    LocalizationMessageId summary;
    bool userActionable;
    bool retryable;
    DiagnosticPrivacyClass privacy;
};
```

Event scope, delivery, payload schema and privacy are typed. Events are
notifications, not mutation or lifecycle authority. Payloads use registered
bounded Horo schemas and cannot carry arbitrary JSON, native pointers or borrowed
views into module memory.

Error `(domain, code)` identities are unique within the package composition and
map to the foundation error model. Runtime diagnostic evidence is bounded and
separate from manifest localization metadata. An error cannot smuggle an
undeclared remediation command, URL or permission.

### 9. Service exports and script APIs preserve separate identities

```cpp
struct ExtensionServiceExportDescriptorV1 {
    ExtensionServiceExportId id;
    ExtensionModuleId owner;
    ExtensionCapabilityId capability;
    ExtensionApiContractId contract;
    SemanticVersion apiVersion;
    ExtensionInvocationPolicy invocation;
    ExtensionServiceLifetime lifetime;
    EnumSet<ExtensionPermissionId> requiredPermissions;
};

struct ExtensionScriptApiDescriptorV1 {
    ExtensionScriptApiId id;
    ExtensionModuleId owner;
    ExtensionServiceExportId service;
    ScriptRuntimeId runtime;
    ScriptNamespaceId nameSpace;
    SemanticVersion apiVersion;
    PackageResourceId bindingSchema;
    EnumSet<ExtensionPermissionId> requiredPermissions;
};
```

A service export identifies a backend-neutral callable contract; it is not a
process-global C++ service object. Invocation policy declares operation ownership,
thread affinity, cancellation/result-store behavior and lifecycle. Concrete
function tables or adapters remain private to the C ABI/host implementation.

A script API is an explicit binding over one compatible declared service export.
It has independent stable identity/version because script compatibility may
evolve separately from the native service. The binding schema is a verified
package resource with bounded typed signatures; it cannot expose raw host/module
pointers, C++ names, arbitrary reflection or undeclared functions. A
`ScriptProvider` role without a script API, or a script API referencing a missing/
incompatible service/runtime/permission, is invalid.

### 10. Cross-reference validation is complete before activation

Validation builds finite indexes and rejects at least:

- owner package assertion or descriptor digest not bound to the install record;
- duplicate package/module/contribution/setting/event/error/service/script IDs;
- missing, duplicate or ambiguous entry variants and undeclared package files;
- contribution payload/schema mismatch or contribution forbidden by module role;
- GUI contribution without `EditorPresentation`, backend export without
  `BackendCapability`, script API without `ScriptProvider`, or restricted runtime
  participation without the matching role/policy;
- undeclared/unknown permission, capability, extension point, runtime, event
  payload, setting kind, reload policy or lifecycle enum;
- permission use outside the package envelope or unused privilege requests;
- references to missing settings, events, errors, services, script APIs,
  resources or foreign modules;
- service/script API version or runtime incompatibility;
- duplicate/cyclic local module/service ordering; and
- required optional-feature edges whose target is unavailable.

Indexes use checked bounded counts and deterministic duplicate reporting. No
validation path loads a library, resolves an absolute filesystem path, registers
a service, invokes a module or consults GUI/backend ambient state.

### 11. Schema v1 fails closed and has one extension envelope

Canonical descriptors use integer `schemaVersion: 1`. Every v1 top-level and
nested field has an exact type, required/optional rule and owner in the typed
model. Unknown fields reject the descriptor except inside the explicit bounded
`extensions` array:

```json
{
  "id": "com.vendor.example.metadata",
  "schemaVersion": 1,
  "required": false,
  "payload": {}
}
```

Known extensions use registered inert schemas and produce typed extension values.
Unknown required extensions reject the descriptor. Unknown optional extensions
are retained as bounded canonical opaque data for digest/round-trip fidelity but
cannot add files, entries, permissions, capabilities, contributions, services,
script bindings, settings, events, errors or activation behavior.

A newer core schema is not accepted as v1 by dropping fields. Migration is an
explicit pure transformation between fully validated typed models. Serialization
emits one deterministic canonical form and never serializes install-record IDs,
absolute paths, trust decisions, resolved host variants or activation state back
into `extension.json`.

### 12. Public headers remain Horo-owned and presentation-neutral

The public declaration owns typed IDs, enums, immutable descriptors, validation
results and serialization contracts. JSON decoding and package/install-record
adapters remain implementation-private. Public headers do not include
`nlohmann::json`, `std::filesystem`, dynamic-loader/native API, ImGui, editor
widget, renderer backend or service-locator types.

Editor, CLI and MCP surfaces project the same validated descriptors into their own
view models. They may render registered labels/icons/forms, but cannot reinterpret
unknown strings or activate a contribution that typed validation rejected.

## Migration And Verification

The current `ExtensionManifest`, `ExtensionModuleManifest` and
`ExtensionContributionManifest` string carriers are transitional. The legacy
ADR-054 converter maps package fields to `horo-package.toml`, requires explicit
module IDs/versions/roles, maps known kinds/types to v1 enums/schema payloads and
reports every lossy or unknown field. It removes `rootPath`; package resources and
entry artifacts bind through the verified file manifest. No compatibility model
silently defaults module identity/version/kind from the package.

Canonical fixtures cover:

- GUI-only module over a built-in backend capability;
- backend/headless asset importer with settings, events and errors;
- script-consumable service export with a Lua 5.4 binding schema;
- mixed backend plus editor/CLI/MCP adapters in one module;
- hybrid package containing separate backend and editor module descriptors; and
- multi-module package composition with local ordering and no role ambiguity.

Every fixture round-trips to the same typed value and canonical bytes/digest.
Negative tests cover every ID domain, enum, variant, permission/capability edge,
payload alternative, setting value/constraint, resource/file reference,
service/script version and required/optional unknown-field path. Compile-only
consumer tests prove public headers remain backend- and GUI-neutral. Activation
tests prove decoded/unvalidated values cannot construct an
`ExtensionActivationCandidate` or reach ExtensionHost.

## Consequences

Package, module, contribution, service and script identities remain distinct and
role intent becomes explicit. Trust planning, UI/CLI/MCP inspection and
ExtensionHost consume the same immutable model, so invalid cross-references fail
before native code runs. The cost is a larger public type vocabulary, versioned
payload variants/catalogs, canonical migration of every legacy manifest and
deliberate schema revisions when new core fields or extension points are added.

## Rejected Alternatives

| Option | Decision and reason |
|---|---|
| Keep strings and validate in each consumer | Rejected: consumers would disagree and invalid references could reach activation. |
| Use one map of arbitrary contribution properties | Rejected: field ownership, types, permissions and GUI/backend neutrality would be unprovable. |
| Infer module roles from contribution names | Rejected: unknown/new points and mixed modules would be ambiguous and could gain authority accidentally. |
| Put all package modules in one `extension.json` | Rejected: ADR-054 gives each module a bounded independently versioned descriptor and package composition index. |
| Let module versions default to package version | Rejected: package and module compatibility evolve independently and provenance must remain explicit. |
| Expose JSON DOM or filesystem paths in the public model | Rejected: parser behavior and machine-local/native types would leak across the stable boundary. |
| Treat script API as the service export identity | Rejected: script and backend contracts have separate compatibility and permission surfaces. |
| Ignore unknown fields | Rejected: typos and future security fields could silently lose effect. |
| Reject all optional future metadata | Rejected: the bounded inert extension envelope permits round-trip-compatible evolution without granting behavior. |
