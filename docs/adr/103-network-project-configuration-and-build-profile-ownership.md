# ADR-103: Network Project Configuration and Build-Profile Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Network project defaults, user preview preferences, release/build-role policy, runtime host overrides, credential references, product capability manifests, provider isolation, migration and cross-surface resolution
- **Issue**: [NET-009.1](https://github.com/abdullahbodur/horo-engine/issues/1185)
- **Jira**: [HORO-1185](https://horo-engine.atlassian.net/browse/HORO-1185)
- **Related**: [ADR-002](002-credential-handling.md), [ADR-009](009-configuration-schema-precedence-and-secret-boundary.md), [ADR-097](097-default-real-time-transport-backend.md), [ADR-098](098-protocol-session-and-trust-policy.md), [ADR-101](101-interest-priority-and-network-budget-model.md), [ADR-102](102-runtime-network-modes-and-authority-exposure.md)
- **Normative documents**: [Configuration System](../architecture/foundation/configuration-system.md), [Networking Architecture](../architecture/runtime/networking-architecture.md), [Build System](../architecture/delivery/build-system.md), [Release Architecture](../architecture/release/release.md), [Release Security](../architecture/release/release-security.md), [Application Security](../architecture/security/application-security.md)

## Context

ADR-102 separates the network modes supported by a product artifact from the one
mode selected by a runtime host. ADR-101 defines renderer-independent scheduling
profiles, and ADR-098 requires immutable exposure/trust policy before a listener or
connection is created. Horo still needs one ownership contract for the inputs that
select those policies and for the artifact that proves what a package can realize.

Without that contract, the editor preview, CLI, MCP, CI workflow and packaged host
may each merge project JSON, user state, command-line flags and environment values
differently. A release profile could be mistaken for a developer CMake build
profile; a renderer quality tier could become a network budget; or a runtime flag
could ask a client-only package to instantiate a server backend. Provider-specific
options could also escape as untyped strings.

Credentials cross an even stricter boundary. Project policy may declare that a
remote server identity or provider credential is required, but portable project
files and product manifests cannot contain passwords, tokens or private keys.
Machine-specific credential bindings do not belong in committed project data and
resolved secret values must not enter configuration snapshots, cache keys,
diagnostics or build artifacts.

ADR-009 already defines Horo's one general configuration precedence and credential
reference boundary. This decision specializes that model for networking rather
than creating a second settings resolver. It defines source legality, domain
projection, build-role artifacts, runtime validation and lifecycle. It does not
choose concrete default values, implement account login, define provider-native
configuration or replace release/signing policy.

## Decision

### 1. Foundation resolves settings; Network owns their domain meaning

Ownership is split without duplicating authority:

| Responsibility | Owner |
|---|---|
| Typed setting/source/provenance contracts, canonical ADR-009 precedence, immutable snapshots and atomic publication | Foundation Configuration |
| Stable `network.*` descriptors, legal sources, validation, cross-field invariants and projection into network policy | Network module |
| Collection of selected descriptors and construction of one resolver/use case | Application composition root |
| Portable project defaults | Project model through typed Network descriptors |
| Local preview preferences | User-configuration owner; never project or release authority |
| Product kind, supported network modes, included targets/providers and security/capability floors | Release profile plus Release/Pipeline services |
| Runtime selection within packaged support | Host adapter request validated by the application use case |
| Credential values and private-key operations | Injected credential/signing provider |
| Native/provider option translation | Selected private transport/provider adapter |

The Network module contributes inert descriptors under its registered namespace.
It cannot call `getenv()`, read project/user files, register globally or resolve a
credential. The application invokes one typed `ResolveNetworkConfiguration` use
case for editor, CLI, MCP, CI and packaged hosts. Surface adapters parse protocol
input and present typed results; they do not merge sources or reinterpret policy.

### 2. Inputs remain distinct typed layers

The network projector consumes these immutable inputs:

```cpp
struct NetworkConfigurationInputs {
    ConfigurationSnapshot resolvedSettings;
    NetworkProjectPolicySnapshot projectPolicy;
    std::optional<NetworkPreviewPreferences> previewPreferences;
    std::optional<NetworkReleaseProfile> releaseProfile;
    std::optional<NetworkProductCapabilityManifest> packagedCapabilities;
    NetworkHostRequest hostRequest;
};
```

`resolvedSettings` contains descriptor-backed scalar/ID selections and their
ADR-009 provenance. `projectPolicy` contains the versioned typed profile catalog
and capability requests referenced by those IDs; it is not a second precedence
map. Release/product constraints and the host request are likewise validation
inputs, not additional last-write-wins setting stores.

These values have different ownership and legal destinations:

| Layer | Examples | Legal persistence/use |
|---|---|---|
| Schema defaults | safe baseline mode request, Null/test policy, bounded limits | Module descriptor packaged with the owning code |
| Project policy | protocol/schema family, allowed product use, default trust/profile IDs, qualified network project profiles, provider capability requests | Versioned portable project configuration |
| User preview preferences | last local preview mode, loopback endpoint, simulated latency/loss preset, preview player count | Local user configuration only; editor/test preview only |
| Release profile | game-runtime versus dedicated-server product kind, supported ADR-102 modes, required targets/providers, security floors, public capability evidence | Versioned release input and job snapshot |
| Session/host request | requested supported mode, endpoint/bind/port, map/session intent, safe diagnostic toggles | Memory-only typed request; descriptor/source-policy gated |
| Environment/invocation map | explicitly bound safe host overrides | Captured once by the host and passed to ADR-009; never read by Network directly |
| Credential binding | requirement ID to provider-owned opaque reference | Private user/CI/host input; resolved only by the consuming operation |

Project policy cannot carry user preview state. Preview preferences cannot affect
cook, release, CI qualification or packaged defaults. Release policy cannot be
weakened by a project, user or session value. A packaged host does not reopen
authoring project/user documents; it validates a host request against its embedded
product capability manifest.

### 3. ADR-009 precedence applies only where a source is legal

Network settings use the canonical ADR-009 order: invocation, explicitly bound
environment, session, project, user, packaged profile and schema default. Every
descriptor declares its legal sources and reload policy. Illegal candidates produce
typed diagnostics even when a higher-precedence legal value exists; they are not
silently ignored.

Precedence chooses among candidates for one setting. It does not override
constraints. Product capability, security floors, trust requirements, supported
modes and provider availability form a validated constraint envelope. The
effective request is the intersection of that envelope and the resolved operator/
project intent. A higher-precedence value cannot:

- enable an ADR-102 mode or native target absent from the product manifest;
- lower remote authentication, transport protection or bind/exposure policy;
- select an unqualified provider or capability;
- expand ADR-101 budgets beyond the packaged/qualified project profile;
- import a renderer/device/backend tier as network policy; or
- turn a preview-only preference into packaged behavior.

Failure returns a typed source-aware result such as `ModeNotPackaged`,
`ProviderUnavailable`, `TrustFloorViolation`, `CredentialCapabilityUnavailable`
or `NetworkProfileUnavailable`. There is no silent provider, transport, role,
security or profile fallback.

### 4. One pure projection produces the effective configuration

The application use case invokes a pure Network-owned projector after ADR-009
resolution:

```cpp
struct EffectiveNetworkConfiguration {
    EffectiveNetworkConfigurationId id;
    ConfigurationRevision configurationRevision;
    NetworkPolicySchemaVersion schemaVersion;
    NetworkProjectPolicyRevision projectRevision;
    std::optional<NetworkReleaseProfileId> releaseProfile;
    std::optional<ProductBuildId> productBuild;
    RuntimeNetworkModeRequest modeRequest;
    NetworkProjectProfileId schedulingProfile;
    NetworkTrustPolicyId trustPolicy;
    NetworkTransportProviderId transportProvider;
    NetworkEndpointPolicy endpoints;
    CredentialRequirementSet credentialRequirements;
    SafeConfigurationProvenance provenance;
};
```

Projection validates all fields and cross-field invariants before publishing one
complete snapshot. It performs no socket/provider initialization, credential
resolution, file write or artifact publication. The ADR-102 host planner consumes
this snapshot plus probed packaged capabilities and either creates a complete mode
plan or fails before world/listener/session publication.

`SafeConfigurationProvenance` contains stable setting/source/profile/provider IDs,
schema/revision/digest and redacted source locations. It contains no raw secret,
credential reference, tokenized endpoint, unrestricted path or environment dump.
Configuration and role changes branch on typed fields, never on provenance text.

### 5. Release profiles own build-role selection

Developer CMake profiles (`editor`, `cli`, `runtime-only`, `tests`) choose a local
build target set and are not product network roles. Renderer/device feature tiers
describe graphics capability and are also orthogonal.

A typed `NetworkReleaseProfile` attached to a product release profile selects:

- product kind and exact supported ADR-102 runtime modes;
- required `NetworkApi`, `NetworkRuntime` and private transport/provider artifacts;
- protocol/schema compatibility and minimum security/trust policy revisions;
- allowed `NetworkProjectProfileId` values and qualification evidence;
- platform/architecture/provider variants and redistribution/license evidence;
- whether runtime endpoint/mode overrides are permitted and within which bounds;
  and
- public credential requirements, such as `server_identity`, without binding a
  provider entry or carrying a private value.

Release validation resolves project policy plus this release profile into an
immutable `NetworkArtifactPlan` pinned to the release job's input revision. The
plan is used by configure, build, cook, package and final verification. A later
editor/user/configuration change cannot mutate the running job.

Client/listen/dedicated targets are included only when named by the plan. An
artifact supporting multiple modes includes their union deliberately and records
that support. An artifact cannot infer server support from `Release`, headless,
`runtime-only`, renderer absence or executable naming.

### 6. Product artifacts carry capabilities, never authority or secrets

Packaging emits a versioned immutable `NetworkProductCapabilityManifest` that
contains only safe product facts:

- product build ID, manifest schema/revision/digest and target platform;
- supported ADR-102 modes and their required network target/provider IDs;
- protocol/schema compatibility ranges and fingerprints;
- allowed trust/profile IDs, security floors and safe public trust-anchor IDs;
- provider adapter/API versions, capability bits, license/notice identity and
  qualification evidence IDs; and
- allowed runtime override fields and bounds.

The manifest is descriptive capability evidence. It does not select the active
mode, create a session or grant server authority. It contains no password, token,
private key, reusable authorization header, credential reference/binding, provider
account secret, unrestricted environment value or machine-local credential-store
path. Public certificates/trust anchors may be packaged when required; the
corresponding private key remains provider-owned.

Final verification proves that every declared provider/target artifact is present,
hash-matched and permitted, and that no undeclared server/provider artifact leaked
into the package. Missing required capability fails packaging; optional capabilities
are absent only when the release profile explicitly planned their omission.

### 7. Credential requirements and values never share a model

Project/release policy names a stable `CredentialRequirementId` and required
capability, such as server identity signing or a relay-provider token. A private
host/CI binding maps that requirement to a typed opaque `CredentialReference`.
The reference is not portable project policy and is omitted from the product
manifest, persistent job history, cache keys and ordinary diagnostics.

The consuming operation resolves the reference through an explicitly injected
provider immediately before use. The resulting owning secure value/operation
handle is bounded, short-lived and never enters `ConfigurationSnapshot`,
`EffectiveNetworkConfiguration`, child-process arguments, logs, events, support
bundles or artifacts. Hardware/remote signing providers may expose only an
operation handle, never key bytes.

Environment-carried raw secrets are a compatibility-only private operation input
under ADR-002/ADR-009. The host removes them from the ordinary configuration map
and environment summary, passes them through the credential channel and excludes
them from subprocess inheritance unless that exact operation requires an approved
protected channel. Redaction is a backstop, not storage permission.

Missing provider capability, unresolved/expired reference, denied access or wrong
credential kind fails the consuming validation/activation stage. It cannot select
a weaker trust mode, embed the value or fall back to an anonymous provider.

### 8. Provider configuration is backend-neutral and isolated

Portable policy selects a stable `NetworkTransportProviderId` and Horo-owned typed
capabilities/limits. Provider-specific contributions use validated registered
descriptor namespaces owned by their adapter package. Public/project/release
contracts contain no GNS/Steam/WebRTC/console SDK structure, native enum, handle,
callback, raw option map or provider environment-variable convention.

The private adapter translates the frozen Horo configuration into native values at
activation and reports effective capabilities. Unknown provider fields, an absent
adapter, version/capability mismatch or prohibited platform/redistribution class
fails explicitly. A provider cannot read project files, environment or credential
stores directly, choose the runtime mode, widen bind exposure or register settings
through native initialization side effects.

### 9. Preview, automation and packaged hosts use one use case

All entry points submit the same typed resolution request:

- Editor may add user preview preferences and a memory-only session override.
- CLI and MCP translate their protocol inputs to the same host request and return
  the same ordered diagnostics/provenance.
- CI supplies a release profile, explicit safe inputs and private credential
  bindings through the automation credential provider.
- Packaged hosts load the embedded capability manifest and accept only its declared
  runtime overrides.

The editor Settings/Build & Release surfaces are adapters. They do not persist a
second network model or run provider probes while editing a draft. Preview first
resolves/validates a candidate; cancel discards it, and apply publishes through the
Foundation configuration transaction at the declared synchronization point.

Automation output is deterministic for equal public inputs. Safe job manifests and
cache identities include configuration/profile/schema/provider revisions and
artifact-plan digest, never credential values or machine-specific references.

### 10. Migration is versioned, bounded and source-aware

Network project policy and product capability manifests carry independent schema
versions. A pure migration stage runs before ADR-009/domain validation and emits an
ordered report. Migrations may rename/split typed keys, convert units and map a
known legacy role/profile identifier. They cannot read secrets/providers, probe
hardware, infer a mode from renderer tiers or silently invent a trust/profile
value.

Unknown future required fields, lossy/ambiguous legacy role mappings and malformed
credential-shaped values fail without changing the source. Editor migration writes
only through the project transaction with backup/rollback. CLI/MCP/CI dry-run and
apply use the same migration service and report. Packaged runtime does not rewrite
an old capability manifest; it rejects an unsupported schema and requires rebuild
or an explicitly shipped read-only compatibility decoder.

Legacy network values derived from renderer/device tiers are reported and removed
from authority. Migration requires an explicit network project profile selection
or the schema default only when the profile descriptor documents that safe default.

### 11. Reload, operation and shutdown lifetimes are explicit

Each preview, build, cook, release and runtime activation pins its effective
configuration and public input revisions. Setting reload policy is domain-specific:

- preview preferences apply to the next preview unless a descriptor safely permits
  a preview restart;
- release/build role, provider set, trust floors and supported modes are frozen for
  the release job/product artifact;
- runtime mode, transport provider, exposure and credential requirements require
  host restart/recomposition;
- ADR-101 profile replacement follows its network-tick generation contract; and
- existing ADR-098 sessions pin their negotiated policy/generations.

Replacement validates a complete candidate before publication. Failure retains
the last-good snapshot/artifact and returns typed diagnostics. Shutdown closes new
resolution/credential admission, cancels operations, revokes secure values,
retires pinned snapshots after consumers drain and then destroys providers. Late
completion cannot publish configuration or artifacts into a retired generation.

### 12. Tests cover source, artifact and credential boundaries

Focused automated coverage must include:

- identical effective results/ordered diagnostics across editor, CLI, MCP, CI and
  packaged-host adapters for equivalent typed inputs;
- every legal/illegal source and ADR-009 precedence pair, including security floors
  that cannot be weakened by higher-precedence values;
- project/user/preview/release/session separation and proof that preview settings do
  not enter builds, packages or automation;
- standalone/client/listen/dedicated artifact plans, multi-mode union, missing
  targets/providers, unsupported runtime mode and no silent fallback;
- proof that renderer backend/device/quality tiers and developer CMake profiles do
  not alter network mode, provider or scheduling profile;
- product manifest canonicalization, hashes, platform/provider variants, final
  artifact inventory and rejection of undeclared/missing content;
- secret/private-key/reference pattern scans over project files, product manifests,
  job history, cache keys, logs, diagnostics, environment summaries and artifacts;
- credential-provider success, missing/expired/wrong-kind/denied bindings, remote or
  hardware operation handles, cancellation and guaranteed secure-value retirement;
- provider namespace/type/version/capability validation with no native type or raw
  option/environment leakage;
- versioned project/manifest migration, dry-run/apply parity, unknown future fields,
  interrupted write rollback and renderer-tier legacy migration; and
- snapshot pinning, concurrent configuration changes, cancelled build/release,
  host restart requirements and shutdown with in-flight provider/credential work.

## Consequences

### Positive

- Every surface resolves the same typed network policy and can explain safe
  provenance without owning another precedence implementation.
- Product artifacts prove exactly which network modes/providers they support while
  runtime selection remains a separate validated decision.
- Project portability, local preview convenience and release security no longer
  compete as one loosely typed settings map.
- Secrets/private keys stay provider-owned and cannot leak through configuration,
  manifests, caches or ordinary environment reporting.
- Renderer/device tiers and developer build profiles cannot silently control
  network capability or budgets.

### Costs

- Network descriptors need explicit legal-source, reload and cross-field policy.
- Release/package validation must produce and verify a canonical capability
  manifest and artifact inventory.
- Adapters and migrations must translate legacy inputs into the shared use case
  instead of maintaining convenient host-local parsers.
- Credential requirements need provider binding and lifecycle plumbing even when a
  local prototype previously accepted a raw string.

## Rejected Alternatives

### Let each editor/CLI/MCP/CI host merge network settings

Rejected because precedence, source legality, diagnostics and security floors would
drift between authoring, automation and packaged execution.

### Store all network settings in the project

Rejected because user preview state, host endpoints, credential bindings and
release-role/security policy have different portability and trust owners.

### Put raw secrets or private keys in an encrypted project/build manifest

Rejected because portable artifacts still become long-lived secret containers,
expand decryption/key-distribution scope and leak through caches/history. Providers
own secret values and operations.

### Derive server/client builds from CMake configuration or renderer tier

Rejected because developer build mechanics and graphics capability do not describe
the supported runtime mode, trust policy, transport provider or network workload.

### Allow runtime overrides to add missing capabilities

Rejected because configuration cannot create unlinked targets, absent provider
artifacts, redistribution rights, qualification evidence or trust material.

### Expose provider-native option maps in project configuration

Rejected because native types/strings would leak backend identity, bypass Horo
validation and make migration/provider replacement unbounded.

### Resolve credentials while building the configuration snapshot

Rejected because snapshots are shared, persistent and observable. Secret resolution
belongs to the shortest consuming operation and returns no reusable configuration
value.
