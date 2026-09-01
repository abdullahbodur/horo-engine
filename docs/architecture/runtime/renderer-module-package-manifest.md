# Renderer Module Package Manifest

## Purpose

This document defines the signed metadata, layout, compatibility model, native
module boundary, validation order, and install record for first-party Horo
renderer components.

Distribution and lifecycle behavior is defined by
[Renderer Distribution And Availability](./renderer-distribution-and-availability.md).
Renderer behavior after activation is defined by
[Render Backend Parity Contract](./render-backend-parity-contract.md).

## Scope

This format packages Horo-owned renderer modules that are versioned and released
with a known Horo renderer ABI. It is not the public third-party plugin manifest
and does not grant arbitrary projects permission to load native code.

[ADR-052](../../adr/052-first-party-renderer-component-scope.md) limits one
package to one stable `RenderBackendId`. In addition to its private renderer entry
module, the signed allowlist may carry only matching backend-private presentation/
editor adapters, backend-owned immutable runtime data, declared redistributable
local libraries, metadata and notices. Another renderer, host core contracts,
component/trust services, editor executable, project content and unrestricted
tools/scripts are forbidden.

`RenderApi`, `RenderFrontend`, `RenderModuleHost`, Platform, `RenderNull` and the
public extension SDK are host-owned and cannot be replaced by package files.
Application-owned shared runtimes use exact compatibility records and one product
version; a renderer package cannot ship a competing private copy.

The manifest is readable without loading the native library and contains no
native pointers, process-local handles, credentials, absolute installation paths,
or machine-local probe results.

[ADR-053](../../adr/053-renderer-module-manifest-parser.md) defines the mandatory
bounded canonical parser and semantic-validation contract. No archive extraction,
dependency resolution, platform probe, or native load may occur from raw manifest
fields.

## Package Identity

Canonical package IDs use reverse-domain-style lowercase segments:

```text
horo.renderer.opengl
horo.renderer.metal
horo.renderer.vulkan
horo.renderer.d3d12
```

The backend identity remains the shorter stable `RenderBackendId`:

```text
opengl
metal
vulkan
d3d12
```

Package identity, backend identity, package version, and renderer ABI are
independent fields. A package update does not rename the backend selected by a
project.

## Package Layout

Conceptual extracted layout:

```text
horo.renderer.metal/
    renderer-module.json
    renderer-module.sig
    bin/
        macos-arm64/
            libHoroRenderMetal.dylib
        macos-x86_64/
            libHoroRenderMetal.dylib
    licenses/
        notices.txt
```

One archive may contain multiple platform/architecture variants. Installation
publishes only verified declared files and records the exact selected variant.
Unknown executable files are rejected unless the schema explicitly permits
them.

Paths are normalized package-relative UTF-8 paths. Absolute paths, drive roots,
parent traversal, NUL bytes, ambiguous separators, symlink escape, case-colliding
entries, and undeclared native libraries are rejected.

## Manifest Example

```json
{
  "schemaVersion": 1,
  "package": {
    "id": "horo.renderer.metal",
    "version": "1.0.0",
    "channel": "stable",
    "displayName": "Metal Renderer"
  },
  "renderer": {
    "backendId": "metal",
    "presentation": "metal",
    "interactive": true,
    "moduleAbi": {
      "name": "horo-render-module",
      "major": 1,
      "minor": 0,
      "minimumHostMinor": 0,
      "entryPoint": "HoroRenderModule_Query"
    },
    "editorCompatibility": {
      "minimumVersion": "0.1.0",
      "maximumExclusiveVersion": "0.2.0"
    },
    "windowRequirements": {
      "resizable": true,
      "highPixelDensity": true
    }
  },
  "variants": [
    {
      "id": "macos-arm64",
      "os": "macos",
      "architecture": "arm64",
      "minimumOsVersion": "14.0",
      "library": "bin/macos-arm64/libHoroRenderMetal.dylib",
      "runtimeRequirements": [
        {
          "id": "apple.metal",
          "kind": "system-framework"
        }
      ],
      "files": [
        {
          "path": "bin/macos-arm64/libHoroRenderMetal.dylib",
          "size": 1234567,
          "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
          "executable": true
        }
      ]
    }
  ],
  "commonFiles": [
    {
      "path": "licenses/notices.txt",
      "size": 4096,
      "sha256": "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789",
      "executable": false
    }
  ],
  "capabilityHints": {
    "offscreenTargets": true,
    "timestampQueries": true
  },
  "signing": {
    "manifestKeyId": "horo-render-stable-1",
    "platformSignatureRequired": true
  },
  "licenses": [
    {
      "name": "Horo Engine",
      "notice": "licenses/notices.txt"
    }
  ],
  "extensions": []
}
```

Hashes and versions above are illustrative. Production manifests use canonical
serialized values generated by the release pipeline.

## Required Top-Level Fields

| Field | Meaning |
|---|---|
| `schemaVersion` | Manifest schema version. Unknown major schemas fail closed. |
| `package` | Product component identity, semantic package version, channel, and display metadata. |
| `renderer` | Stable backend identity, presentation requirements, module ABI, and editor compatibility. |
| `variants` | Bounded platform/architecture-specific native artifacts. |
| `commonFiles` | Platform-independent declared package files such as notices. |
| `capabilityHints` | Bounded, non-authoritative catalog/preflight hints from a sealed vocabulary. |
| `signing` | Trust-root key identity and platform-signature policy. |
| `licenses` | Required license and notice references. |
| `extensions` | Bounded explicit extension envelope; an empty array is valid. |

Schema version 1 rejects unknown fields outside `extensions`. Unknown required
extensions reject the manifest; unknown optional extensions are retained as
canonical opaque data but grant no file, capability, dependency, runtime, loader,
or policy authority.

## Canonical Parser Contract

Schema version 1 is strict canonical UTF-8 JSON with no BOM, duplicate member
names, comments, trailing commas, invalid Unicode, alternate numeric spellings,
or trailing non-whitespace bytes. The parser re-encodes the validated typed and
extension-preserved document with the pinned release encoder and requires exact
byte equality. It never normalizes a different byte sequence into acceptance.

Parsing is streaming/token-based and applies the hard limits in ADR-053 before
allocation or recursion: 1 MiB input, depth 16, 128 members per object, 8,192
members total, 2,048 elements per array, 16,384 JSON values, 4 KiB ordinary
strings, 1,024-byte relative paths, 64 variants, 4,096 files, 256 runtime
requirements, 256 licenses/extensions and 32 returned errors. Counts and byte
sums use checked arithmetic. Cancellation, allocation failure, or any limit breach
returns no partial manifest and publishes no component state.

The result is an immutable Horo-owned typed value plus its canonical SHA-256
digest. It exposes no parser-library DOM, native path/handle, or backend API type.
Validation includes exact package/backend identity, version and ABI ranges,
variant uniqueness, sealed enums, typed runtime requirements, reference ownership,
and conservative cross-platform path/case/Unicode collision checks without
filesystem access. Native executable/library files must be non-empty; declared
non-executable data files may be empty and remain hash-bound.

## Renderer Metadata

`renderer.backendId` obeys `RenderBackendId` canonical slug rules. It is the only
value persisted by renderer selection settings.

`renderer.presentation` uses backend-neutral values:

```text
none
opengl
metal
vulkan
```

The value determines host window/surface requirements before native module load.
It does not expose a native handle or prove that a device can initialize.

`renderer.interactive` distinguishes presentation-capable modules from headless
modules. HoroEditor accepts only interactive modules for graphical startup.

`renderer.windowRequirements` contains platform-neutral policy. SDL, Cocoa,
Win32, X11, Wayland, OpenGL, Metal, and Vulkan native structures are forbidden.

## Compatibility And ABI

A renderer component is compatible only when all checks pass:

- manifest schema is supported;
- package and backend IDs are valid;
- an exact host OS and architecture variant exists;
- current OS version satisfies the variant minimum;
- editor version is inside the declared range;
- renderer module ABI major matches the host;
- ABI minor negotiation succeeds;
- required entry point exists;
- integrity and signing policy pass;
- runtime requirements pass or remain eligible for probe-based validation.

Renderer module ABI is private, first-party, and versioned. Private does not mean
unvalidated. The boundary must not rely on compiler-specific C++ layout across a
dynamically loaded module.

The module exports one C entry point:

```c
HoroRenderModuleStatus HoroRenderModule_Query(
    const HoroRenderHostApi* host,
    HoroRenderModuleApi* out_module);
```

The exact structures are defined in a dedicated ABI header when implementation
begins. ABI rules include:

- fixed-width integer and explicitly sized POD fields;
- structure size and ABI version on every extensible table;
- opaque handles rather than C++ object pointers;
- host-provided allocation, logging, and diagnostic callbacks;
- no STL containers, `std::string`, exceptions, RTTI types, or compiler-owned
  ownership across the boundary;
- no callbacks after module shutdown;
- explicit thread-affinity and lifetime rules;
- error values and bounded UTF-8 diagnostic views owned according to the table
  contract;
- unknown trailing fields ignored only when negotiated minor-version rules permit
  it.

The host adapter converts a successfully negotiated module function table into
the engine's in-process `IRenderBackendProvider`/`IRenderBackend` contracts.
`RenderFrontend` does not perform discovery, dynamic loading, or ABI negotiation.

A module ABI major change requires a new compatible renderer package. Automatic
binary shims are forbidden unless separately designed and tested.

## Variant Selection

Variant selection is deterministic:

1. exact OS identifier;
2. exact process architecture;
3. current OS version meeting minimum;
4. policy-compatible package channel;
5. exact package version selected by the component resolver.

The loader does not execute a foreign-architecture binary through implicit
translation unless the package and host policy explicitly permit that mode.
Universal/fat binaries declare their supported architectures explicitly.

If multiple variants match with equal priority, the manifest is ambiguous and
validation fails.

## Runtime Requirements

For D3D12, [ADR-032](../../adr/032-d3d12-baseline-and-agility-sdk-policy.md)
requires compatibility with the executable-owned Agility release contract.
The signed component compatibility record must identify the exact package pin,
SDK integer, architecture, and dependency digest from that contract; a minimum
SDK integer alone cannot distinguish patch payloads. Delivery owns the shared
application-local runtime, while component installation verifies/stages it before
launch. A mismatching renderer requires a compatible host update, not new SDK
exports from its DLL or replacement of System32 files. This extends the future
compatibility-record realization; it is not a claim that a parser exists today.

For Vulkan, [ADR-031](../../adr/031-vulkan-loader-platform-and-version-baseline.md)
requires the system loader plus separately validated Vulkan 1.3 device/features
and the selected WSI path. A `khronos.vulkan-loader` requirement cannot stand in
for an ICD, GPU, or surface probe. `horo.moltenvk` below is a reserved runtime
vocabulary example; portability packages are outside the initial product scope.

Runtime requirements use known typed IDs and kinds. Examples:

```text
apple.metal                 system-framework
microsoft.opengl            system-runtime
khronos.vulkan-loader       system-loader
horo.moltenvk               bundled-runtime
```

A requirement may declare minimum version, bundled/system policy, and whether it
can be fully validated before probe. It cannot declare an arbitrary executable
or library search path.

For `apple.metal`, the variant's `minimumOsVersion` and native admission policy
in [ADR-030](../../adr/030-metal-platform-and-feature-baseline.md) are authoritative;
a marketing-level Metal version is not a framework/GPU compatibility predicate.
The example therefore requires the system framework without `minimumVersion`.
Probe and initialization still check the actual device family and effective
support. MSL requirements belong to cooked shader compatibility records, not a
claim that installing the renderer supplies a compiler or GPU driver. Shipped
Metal variants must verify architecture slices and binary deployment targets
against the manifest; the layout examples do not certify both architectures.

Bundled runtime files appear in the variant's `files` list and receive the same
hash, signature, layout, and load-path validation as the main module. System
runtime resolution uses platform services and controlled loader policy.

Development SDKs and shader tools are not runtime requirements unless the
runtime architecture explicitly depends on them. Tool paths belong to Toolchains
settings.

## Capability Hints

`capabilityHints` exist for catalog presentation and preflight planning only.
They are not authoritative. The frontend's effective capability snapshot after
device initialization and driver-policy adjustment, as defined by
[ADR-028](../../adr/028-renderer-capability-limits-and-product-profiles.md),
is authoritative.

A manifest cannot use capability hints to bypass parity requirements or backend
validation. A mismatch between required declared behavior and probe/runtime
behavior is a component defect and may trigger quarantine.

## Integrity And Signatures

The release pipeline signs the exact canonical manifest bytes accepted by the
ADR-053 parser. The parser returns their digest without choosing a trust root;
the detached signature and trusted release catalog provide the expected signing
identity. The release manifest also records the renderer archive hash and signed
manifest identity.

Verification covers:

- archive size and hash;
- canonical renderer manifest hash;
- detached Horo manifest signature;
- every declared extracted file size and SHA-256 hash;
- platform native-code signature where required;
- extracted layout and file modes;
- package/install record consistency.

Every extracted file except `renderer-module.json` and its detached signature
must appear exactly once in either `commonFiles` or the selected variant's
`files`. Undeclared files and duplicate declarations are rejected.

Transport TLS does not replace artifact signatures. A valid Horo signature does
not replace macOS/Windows platform signing requirements.

On macOS, the module must satisfy hardened-runtime library validation, code-sign
identity, notarization, architecture, and entitlement policy. Renderer packages
must not request entitlements not declared by the release profile.

## Install Record

The component manager writes a machine-local install record only after atomic
publication. It references but does not modify the signed package manifest.

The record contains:

- package ID and exact version;
- backend ID;
- selected variant ID;
- canonical installed component root;
- manifest and archive hashes;
- verification timestamp and verifier version;
- signing identities and platform-verification result;
- active/rollback component pointers;
- revisioned orthogonal component-state snapshot dimensions relevant to the
  installed version;
- probe cache key and last typed probe result;
- quarantine/repair metadata;
- lease count or process ownership required for safe removal.

Machine paths, probe results, trust state, and quarantine state never enter
portable project settings.

## Loader And Unload Policy

The loader resolves the exact native library path from the verified install
record. It does not concatenate user input or rely on the working directory.
Transitive native dependencies must resolve through controlled platform loader
policy or declared bundled files.

A loaded module holds a component lease. Update may stage another version while
a lease exists, but removal or replacement of leased files is forbidden.

Module unload is allowed only after:

- all frontends and backend instances are shut down;
- all module-created resources and callbacks are released;
- no helper or editor thread may enter the module;
- the module shutdown function returns successfully;
- host callback tables remain valid through shutdown completion.

If safe unload cannot be proven, the module remains loaded until process exit.
Correct shutdown is more important than reclaiming one module mapping.

## Validation Order

Validation is ordered from side-effect-free and cheapest to native execution:

1. enforce archive/manifest byte limits and read only the bounded manifest and
   detached-signature entries without publishing extracted files;
2. perform ADR-053 canonical syntax, schema, semantic, path, collision, and
   internal-reference validation;
3. match the typed identity and manifest digest to the trusted release catalog/
   expected key;
4. verify the exact manifest signature and archive hash;
5. resolve one host variant and validate editor/module ABI compatibility;
6. validate the complete archive entry allowlist and resource-expansion bounds;
7. extract into private staging and verify every declared file size, mode and hash;
8. verify platform signatures, typed runtime requirements, layout and install
   record consistency;
9. atomically publish the install record/component directory;
10. load and negotiate the module only inside the probe helper;
11. publish `Available` only after probe success.

Failure at any step prevents editor-process loading.

## Stable Diagnostic Categories

At minimum, component services distinguish:

```text
renderer.component.not_installed
renderer.component.manifest_invalid
renderer.component.variant_missing
renderer.component.host_unsupported
renderer.component.abi_mismatch
renderer.component.hash_invalid
renderer.component.signature_invalid
renderer.component.platform_signature_invalid
renderer.component.runtime_missing
renderer.component.entry_point_missing
renderer.component.negotiation_failed
renderer.component.probe_failed
renderer.component.probe_crashed
renderer.component.probe_timeout
renderer.component.quarantined
renderer.component.repair_required
renderer.component.in_use
```

Diagnostics may include safe package/module versions and host facts. They do not
include unrestricted environment dumps, credentials, or untrusted native loader
messages without sanitization and bounds.

## Required Tests

- schema-version, missing/unknown/wrong-type field and required/optional extension
  acceptance/rejection;
- duplicate-key, UTF-8/escape/numeric grammar and canonical byte/digest fixtures;
- every parser depth/count/string/scratch/error limit at equality and just over;
- canonical package/backend ID, ABI/range, enum and reference validation;
- path traversal, absolute/drive/UNC/device path, symlink escape, reserved-name,
  case/Unicode collision and duplicate ownership rejection;
- exact variant selection and ambiguous variant rejection;
- editor/module ABI major and minor negotiation cases;
- archive, manifest, and per-file hash rejection;
- Horo and platform signature rejection;
- undeclared executable/native library rejection;
- controlled runtime dependency resolution;
- missing entry point and malformed function-table rejection;
- no C++ ABI types crossing the dynamic boundary;
- install record publication only after successful verification;
- loader uses only the exact verified path;
- active lease blocks uninstall/replacement;
- probe helper is the first process to execute the module;
- packaged macOS, Windows, and Linux artifact verification.
- deterministic property/fuzz corpus with sanitizer coverage and parse-before-
  extraction/load proof.

## Related Documents

- [Renderer Distribution And Availability](./renderer-distribution-and-availability.md)
- [Render Backend Parity Contract](./render-backend-parity-contract.md)
- [Rendering Architecture](./rendering-architecture.md)
- [Package Lifecycle](../packages/package-lifecycle.md)
- [Distribution And Update Architecture](../release/distribution-and-update.md)
- [Release Security](../release/release-security.md)
- [Extension System](../extensions/plugin-system.md)
- [ADR-053: Renderer Module Manifest Parser](../../adr/053-renderer-module-manifest-parser.md)
