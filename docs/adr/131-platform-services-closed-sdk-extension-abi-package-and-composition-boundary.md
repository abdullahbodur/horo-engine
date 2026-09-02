# ADR-131: Platform Services Closed SDK, Extension ABI, Package and Composition Boundary

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Public/private target boundary, provider discovery and trust, platform-services extension ABI values, candidate activation/rollback, provider selection and headless/test/unsupported/certification composition
- **Issue**: [PLS-002.1](https://github.com/abdullahbodur/horo-engine/issues/1882)
- **Jira**: [HORO-1838](https://horo-engine.atlassian.net/browse/HORO-1838)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md), [ADR-054](054-extension-and-package-authority-boundary.md), [ADR-055](055-extension-manifest-v1-typed-model.md), [ADR-056](056-external-editor-ui-boundary.md), [ADR-116](116-save-data-threat-model-and-trust-policy.md), [ADR-130](130-platform-services-frontend-request-lifetime-timeout-null-and-error-semantics.md)
- **Normative documents**: [Platform Services Architecture](../architecture/runtime/platform-services-architecture.md), [Extension System](../architecture/extensions/plugin-system.md), [Internal Module Descriptor](../architecture/foundation/internal-module-descriptor.md), [Release Security](../architecture/release/release-security.md)

## Context

Platform Services must integrate Steamworks and certification-controlled console SDKs
without placing proprietary headers, libraries or policy inside the public engine.
The architecture currently says providers are extension packages, but it does not
assign discovery, package trust, ABI adaptation, capability registration and final
selection to distinct owners or define their rollback boundaries.

The generic extension ABI forbids C++ ownership across external module boundaries.
Platform providers add asynchronous operations, session callbacks, credentials and
SDK-native objects that can outlive an entry call. Without a platform-specific ABI
profile, an implementation could leak a provider user/native request handle into the
frontend, return SDK-allocated memory, throw through the host or unload code while a
callback is still pending.

Composition also differs by product. Headless servers, unit tests, unsupported targets
and certification builds cannot use the same discovery/fallback assumptions as an
editor development host. The selection must remain explicit while ADR-130 continues
to own public request state and error semantics.

## Decision

### 1. Public Horo targets contain no closed SDK dependency

The public repository owns only backend-neutral Horo contracts, manifests/schema,
extension ABI definitions/adapters, `PlatformServicesFrontend`, Null, public ABI
conformance fixtures and Mock test support. No public or core target may include,
forward-declare, link, delay-load, fetch or generate from proprietary platform SDK
headers/libraries. Their paths and license-controlled identifiers do not appear in
installed public headers or public/interace CMake usage requirements.

Closed SDK integration lives in a separately built private provider package, for
example `horo-platform-steam` or a certification-controlled console package. That
package owns SDK includes/libraries, SDK initialization, native callbacks, credential
leases, error translation and platform-holder compliance. It exposes only the Horo
platform-services extension ABI.

The public engine builds, tests and packages with Null/Mock and ABI fixtures when no
private SDK is installed. A private package can update its SDK and internal C++ without
changing gameplay-facing contracts while it still implements an accepted ABI and
Horo semantic version.

### 2. Provider lifecycle stages have separate owners

| Stage | Owner | Output / rollback boundary |
|---|---|---|
| Package discovery | `PackageService` over verified install records | Inert package candidates only; never `dlopen` directory scans |
| Integrity, signature, license/trust and enablement | `PackageLifecycleService` / `TrustService` | Accepted immutable package graph or fail closed; no module code run |
| Descriptor and binary resolution | `ExtensionHost` | Exact target/architecture entry from verified manifest; normalized contained path |
| Dynamic load and ABI negotiation | `ExtensionHost` platform-services adapter | Loaded candidate generation or unload candidate; no live capability registration |
| SDK/provider candidate initialization | Private package behind ABI | Opaque candidate state; failure destroys partial native resources through its ABI destroy path |
| Capability/limit enumeration and validation | Platform Services composition adapter | Copied Horo-only candidate snapshot; invalid/duplicate claims destroy candidate |
| Product-policy selection | Application/process composition root | Exactly one selected provider generation or explicit Null; required absence fails startup |
| Frontend activation and request ownership | `PlatformServicesFrontend` | ADR-130 immutable capability binding and request store |
| Native operation execution/retirement | Private package | SDK objects never leave package; completion copied through generation-checked sink |
| Shutdown/unload | Frontend, then adapter, then `ExtensionHost` | Close/drain/retire callbacks and operations before module unload/package release |

Package discovery does not imply trust; trust does not imply activation; capability
registration does not select a provider. No stage consults a global service locator or
mutates a live registry from descriptor construction/static initialization.

### 3. Provider packages are manifest-selected, not probed by loading

The `.horopkg` manifest and verified file record declare package/module identity,
version, target OS/architecture/product profile, extension ABI range, platform-services
provider contribution, SDK/runtime dependencies, permissions, certification class and
content digests. Discovery reads this inert metadata only.

The host loads only the exact binary named by the resolved verified install record.
It never scans PATH/system library folders, tries backend names in order, loads every
candidate to ask what it supports or follows symlinks/undeclared dependencies. A
repository clone cannot grant local native trust or auto-load a project-declared
provider.

Selection inputs are frozen product/release manifest, host target, approved package
graph, trust decision, requested service policy and explicit user/developer selection
where permitted. Native SDK availability or initialization evidence is candidate
validation, not an authority to rewrite product selection.

### 4. Platform Services uses a versioned C ABI profile

The extension point is a versioned `platform.services.provider` contribution layered
on `HoroExtensionAbi`. Every exported struct starts with `structSize` and the ABI/
semantic version is negotiated before any provider function is invoked. New optional
tail fields are append-only; incompatible required semantics use a new ABI version.

Values permitted across the boundary are:

- fixed-width integers, C enums and explicitly sized flags;
- stable Horo IDs represented by fixed canonical bytes/integers;
- byte-counted UTF-8/string and byte spans borrowed for the documented call only;
- host-owned bounded output/completion sinks whose data is copied during the call;
- opaque ABI adapter context cookies and provider-operation tokens scoped to one
  provider generation, never SDK-native handles; and
- versioned Horo request/result/error structs containing bounded tagged fields.

Forbidden across the ABI are:

- STL/C++ classes, templates, RTTI, exceptions, virtual objects or compiler-owned ABI;
- ownership of memory allocated by the other side, allocator/deleter transfer, raw
  owning pointers or retained borrowed spans;
- Steam/PSN/Xbox/Nintendo/native OS types, SDK request/user/session handles, vtables or
  error objects;
- credentials/tokens, raw account IDs, unrestricted provider response bodies or
  provider-owned display strings as durable Horo identity; and
- callbacks into arbitrary engine services or a host service locator.

An adapter context cookie is only dispatch identity for the ABI function table. The
host never dereferences or exposes it beyond the private adapter; the package owns and
destroys it after callback quiescence. A provider-operation token is a bounded opaque
ABI token mapped privately to native work. It cannot become `PlatformRequestId`, user
identity or gameplay-visible data.

### 5. Async completion is host-sink based and generation fenced

The host supplies a narrow completion/event sink table with context and generation.
Provider SDK callbacks translate native state inside the private package, then submit
one bounded Horo result to that sink. The sink copies accepted data immediately into
host-owned storage and returns an ABI status; the provider retains no sink output
pointer and the host retains no borrowed provider buffer.

Provider code never calls gameplay/frontend observers directly. It does not publish
after its sink generation is revoked. Every completion carries provider generation,
operation token, service/operation kind and terminal evidence; ADR-130 frontend owner
maps it to the one request record. Duplicate, late, malformed, oversized and stale
completion is rejected for publication while still permitting private retirement.

Exceptions are caught inside the package at every exported function/callback and
inside the host at every module call. None crosses the C ABI. ABI status reports call/
transport validity; a valid provider operation failure is returned as the versioned
Horo result envelope, not conflated with an ABI crash/version error.

### 6. Candidate activation is transactional

The composition transaction executes in this order:

```text
resolve verified package graph
  -> resolve exact module binary and entry symbol
  -> load candidate library and negotiate ABI
  -> create provider candidate and initialize closed SDK
  -> copy and validate capability/limit/error descriptors
  -> select against frozen product policy and reserve frontend capacity
  -> atomically publish one provider generation and activate frontend admission
```

Failure before publication destroys provider candidate/native state, revokes sinks,
unloads the candidate library and releases package leases in reverse order. It leaves
the old active provider/frontend untouched and publishes no partial capability.

Replacement prepares and validates the full candidate while the old generation stays
active and charged. At the safe point, the frontend closes old admission, drains or
cancels according to ADR-130, generation-fences late callbacks, publishes the new
binding only after old live semantic authority is closed, and retires old code after
all operations/callback epochs release it. There is never routing by whichever of two
providers answers first.

An unload timeout or unknown native tail retains the module/package lease and reports
typed shutdown failure. It does not force-unload code that may receive a callback.

### 7. Composition is explicit per host and release profile

- **Interactive development/editor:** may select one locally trusted compatible
  private provider or explicit Null. Project requirement prompts for trust but cannot
  auto-load code. Provider diagnostics are available under redaction policy.
- **Unit/contract tests:** use built-in Mock/Null and public ABI conformance fixtures.
  Private packages run SDK integration/certification tests in their controlled repos.
- **Headless/server:** defaults to Null with all remote client-platform capabilities
  unavailable. A trusted server-capable provider requires an explicit product manifest
  and cannot reuse client credentials/session assumptions.
- **Unsupported OS/architecture:** an absent exact binary/provider capability yields
  typed unavailable for optional services or startup failure for required services.
  The host never loads a closest platform variant or silently selects another provider.
- **Certification/shipping:** freezes exact package/module hashes, ABI, SDK runtime,
  entitlements, capabilities and provider selection in the signed product manifest.
  Marketplace/install/update, arbitrary local extensions, Mock and development fallback
  are excluded. Required initialization/certification preflight failure blocks startup
  or the certified feature according to explicit product policy; it never falls back
  to an uncertified provider.

Null remains ADR-130 fail-closed: it does not acknowledge writes. Required product
services cannot resolve to Null. Optional application intent may be explicitly
suppressed by its semantic owner before submission.

### 8. Gameplay depends only on Horo frontend capabilities

Gameplay receives narrow Horo application capabilities backed by
`PlatformServicesFrontend`. It cannot discover provider packages, select a backend,
call the extension ABI, inspect provider tokens or include SDK headers. Requests,
stable IDs, values and errors remain Horo-owned and preserve ADR-130 semantics.

Provider-specific mapping, manifests, entitlements and SDK conversions remain inside
the private package/cook adapter. A provider upgrade may add internal features, but a
new gameplay-visible semantic requires a versioned Horo contract and public review; it
cannot escape as a native property map or handle.

### 9. Distribution and dependency checks enforce the boundary

Public CI configures and builds core, Null, Mock and ABI tests with no SDK environment.
Dependency policy rejects proprietary include roots, link libraries, runtime loader
names and package binaries from public/core target closures. Installed public headers
are scanned for native SDK names/types and ABI-unsafe C++ ownership.

Shipping assembly verifies the selected private package outside the public source tree,
records its manifest/hash/license/provenance and includes it only in authorized target
products. Symbols, credentials, SDK logs and restricted headers never enter public
artifacts or ordinary diagnostic bundles.

Qualification covers wrong ABI/size/version, missing entry, tampered package, wrong
target, denied trust, initialization/capability failure, partial rollback, replacement
with requests in flight, duplicate/late callback, retained borrowed-buffer attempts,
throwing exports, unload timeout and certification manifest mismatch. Each failure must
leave either the prior generation intact or the frontend explicitly unavailable with
no partial registry mutation.

## Consequences

- The public repository and all core consumers build without proprietary SDK access.
- Each discovery, trust, load, validation, selection, request and unload stage has one
  owner and a reversible pre-publication boundary.
- Provider-native types and memory cannot leak into frontend/gameplay or lock the public
  API to a private SDK version.
- Private package and SDK evolution is isolated behind a versioned Horo C ABI/semantic
  contract.
- Certification builds gain a frozen auditable composition and cannot silently fall
  back to a development provider.
- The ABI adapter requires careful copied envelopes, callback epochs, generation
  fencing and conformance tests; direct C++ interfaces would be simpler locally but
  would not preserve binary or ownership boundaries.

## Rejected Alternatives

### Link closed SDKs into the public Platform Services target conditionally

Rejected because configuration flags do not prevent proprietary headers/libraries
from entering dependency closures, installed interfaces, CI or redistribution.

### Let providers self-register or select themselves during static initialization

Rejected because trust, target/product policy, rollback and deterministic selection
must precede activation; inert descriptors cannot mutate ambient registries.

### Expose a C++ provider interface across package binaries

Rejected because STL, exceptions, allocator ownership, RTTI and compiler ABI differ and
would bind public contracts to private toolchains/SDK lifetimes.

### Pass native provider handles through opaque `void*` gameplay values

Rejected because opacity does not define ownership, generation, privacy or unload
safety. Only adapter-scoped cookies/tokens may cross the C ABI and never reach gameplay.

### Auto-fallback to another installed provider after initialization failure

Rejected because provider choice is product/certification policy. Optional services
may be explicitly unavailable or Null; required services fail composition visibly.
