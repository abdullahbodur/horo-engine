# ADR-053: Renderer Module Manifest Parser

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Bounded parsing and semantic validation of signed first-party renderer manifests before native loading
- **Issue**: [RND-002.2](https://github.com/abdullahbodur/horo-engine/issues/153)
- **Jira**: [HORO-153](https://horo-engine.atlassian.net/browse/HORO-153)
- **Related**: [ADR-052](052-first-party-renderer-component-scope.md)
- **Normative documents**: [Renderer Module Package Manifest](../architecture/runtime/renderer-module-package-manifest.md), [Renderer Distribution And Availability](../architecture/runtime/renderer-distribution-and-availability.md), [Application Security](../architecture/security/application-security.md), [System Design](../architecture/foundation/system-design.md)

## Context

Renderer manifests describe native product code and therefore cross a stronger
trust boundary than ordinary project data. Parsing must happen before any library
load, dependency resolution, archive extraction or device probe, but a permissive
JSON parser can accept duplicate keys, alternate numeric spellings, invalid Unicode
or semantically equivalent byte sequences that do not match the signed canonical
document. Schema validation alone also does not catch path collisions, ambiguous
variants, incompatible ABI ranges or duplicate artifact ownership.

The parser must return backend-neutral immutable values without consulting the
filesystem or native APIs. Later verification stages still own archive hashes,
signatures, extracted layout, platform code signatures, runtime requirements and
probe execution.

## Decision

### 1. Parsing is a side-effect-free application/component service

`RendererModuleManifestParser` accepts an owned/read-only byte span plus an
immutable parser-policy snapshot and returns one owned immutable typed manifest or
a bounded typed error set:

```cpp
Result<ParsedRendererModuleManifest, RendererManifestErrorSet>
ParseRendererModuleManifest(
    std::span<const std::byte> bytes,
    const RendererManifestParserPolicy& policy,
    CancellationToken cancellation);
```

Construction and parsing perform no service registration, filesystem access,
archive extraction, signature verification, environment lookup, dynamic loading,
native API call, network access, logging side effect or component-state mutation.
The component manager composes the parser and commits a parsed value only after
subsequent trust/compatibility stages succeed against the same manifest digest.

The parser lives in the private product-component boundary. Its result uses Horo
IDs, versions, relative paths, hashes, enums and finite vectors; it exposes no
JSON-library node, native path object, platform handle or backend API type.

### 2. The input grammar is strict canonical JSON

Schema version 1 accepts UTF-8 JSON with no BOM. It rejects invalid UTF-8,
unpaired surrogates, NUL/control characters in strings, duplicate object member
names at any depth, comments, trailing commas, non-JSON values and trailing
non-whitespace bytes. Object keys and string values are Unicode-normalized only
for validation/comparison where the field contract explicitly requires it; the
parser never silently rewrites signed text.

Numbers are accepted only for schema-defined unsigned integer fields. Negative,
fractional, exponent, leading-plus, leading-zero and out-of-range spellings are
rejected before conversion. Semantic versions are strict typed strings, not JSON
numbers or locale-dependent comparisons. Hashes are exact lowercase hexadecimal
of their declared fixed width.

Release tooling emits the repository's pinned canonical JSON encoding: UTF-8,
minimal escaping, no insignificant whitespace, deterministic lexicographic object
key order and shortest permitted integer spelling. After syntax/schema parsing,
the parser re-encodes the typed/extension-preserved document with that pinned
encoder and requires byte-for-byte equality with the input. A non-canonical but
otherwise valid document is `NonCanonicalEncoding`; it is never normalized and
then accepted under a signature for different bytes.

The detached signature and release catalog identify the expected key outside the
untrusted document. Signature verification later signs/verifies the exact
canonical manifest bytes and digest returned by parsing; `signing.manifestKeyId`
must agree with that trusted expectation but cannot choose a new trust root.

### 3. Hard parser limits precede allocation and recursion

Version-1 defaults and hard maxima are identical for product parsing:

| Resource | Hard maximum |
|---|---:|
| Manifest bytes | 1 MiB |
| Nesting depth | 16 |
| Members in one object | 128 |
| Total object members | 32,768 |
| Elements in one array | 4,096 |
| Total JSON values | 65,536 |
| UTF-8 bytes in one ordinary string | 4 KiB |
| UTF-8 bytes in one relative path | 1,024 |
| Variants | 64 |
| Files across common and all variants | 4,096 |
| Runtime requirements | 256 total, 64 per variant |
| Licenses and extensions | 256 each |
| Returned errors | 32, first-seen deterministic order |

The byte limit is checked before parser construction. Every length/count addition
uses checked arithmetic before reserve/allocation. Streaming/token parsing tracks
depth and aggregate counts without first constructing an unbounded DOM. Exceeding
any limit returns `ResourceLimitExceeded` and stops; the implementation cannot
raise limits from fields inside the manifest.

Parsing accepts a cancellation token at bounded token/collection checkpoints.
Cancellation returns no partial manifest. It is safe on a worker, owns all memory
through completion and has a finite scratch-memory budget of at most four times
the manifest byte limit. No exception or allocation failure crosses the service
boundary; it becomes `OutOfMemory`/`ParserFailure` with no committed state.

### 4. Schema and extension behavior fail closed

All required top-level and nested fields must appear exactly once with exact type.
Schema version `1` rejects unknown fields everywhere except the explicit
top-level `extensions` array. This prevents a misspelled or future security field
from being silently ignored.

```json
"extensions": [
  {
    "id": "horo.renderer.example",
    "schemaVersion": 1,
    "required": false,
    "payload": {}
  }
]
```

Extension IDs are unique canonical IDs. A known extension is parsed by a statically
registered, inert, sealed schema table under the same global limits. An unknown
`required: true` extension rejects the manifest as `UnknownRequiredExtension`.
An unknown optional extension is retained as bounded canonical opaque bytes for
signature/install-record fidelity but grants no capability, dependency, file,
runtime, loader or policy behavior. Extension payloads cannot declare executable
paths or override core fields unless a later ADR/schema version explicitly assigns
that authority.

Schema major/version compatibility is exact for version 1. A future migration is
an explicit pure transformation from one fully validated typed schema to another;
the parser never guesses, drops fields or treats a newer version as an older one.

### 5. Semantic validation produces canonical typed identities

After syntax/schema success, semantic validation constructs:

```cpp
struct ParsedRendererModuleManifest {
    RendererManifestSchemaVersion schema;
    RendererPackageIdentity package;
    RendererModuleDescriptor renderer;
    std::vector<RendererVariantDescriptor> variants;
    std::vector<RendererFileDescriptor> commonFiles;
    RendererCapabilityHints capabilityHints;
    RendererSigningDeclaration signing;
    std::vector<RendererLicenseDescriptor> licenses;
    std::vector<RendererManifestExtension> extensions;
    Sha256Digest canonicalDigest;
};
```

Validation requires:

- package ID exactly `horo.renderer.<backendId>` and one ADR-052 backend identity;
- canonical bounded IDs/slugs and non-empty display/license names;
- strict semantic package/editor/OS versions with ordered non-empty ranges;
- known product channel, OS, architecture, presentation and runtime-kind enums;
- private module ABI name, nonzero supported major, coherent minor/
  `minimumHostMinor` range and exact allowlisted entry-point identifier;
- `interactive: true` only with a non-`none` presentation kind and compatible
  backend/window-requirement vocabulary;
- at least one variant, unique variant IDs and no two variants with the same
  effective OS/architecture/minimum-OS selection key;
- known fixed capability-hint names with boolean values only; hints remain
  non-authoritative; and
- signing/license references that identify declared files exactly once.

The parser validates manifest-internal compatibility only. Current host/editor/
ABI policy and variant selection are separate resolver inputs; therefore a valid
manifest may have no variant for the current host. The parser never labels it
available or selects a variant.

### 6. Paths and file declarations are normalized without filesystem access

Every path is non-empty package-relative UTF-8 using `/` separators. Validation
rejects absolute/drive/UNC/device paths, leading/trailing slash, empty or `.`/`..`
segments, backslash, repeated separators, colon, NUL/control characters, trailing
dot/space segments, reserved platform device names and any segment/whole path over
its bound. The parser produces a normalized `PackageRelativePath`; it never joins
the path to an install root or resolves symlinks.

All common and every variant file declaration participate in one global collision
index using both canonical byte identity and the repository's conservative
cross-platform case/Unicode comparison key. Exact duplicates, case/normalization
collisions and a path declared by multiple owners are rejected even when variants
target different hosts. Each variant's `library`, each license notice and each
bundled runtime reference must resolve to exactly one compatible declared file.

File sizes are bounded unsigned integers; native libraries and executables must
be non-empty, while a declared non-executable data/notice file may be empty.
Per-file hashes are valid SHA-256, executable flags agree with field role, and
summed declared bytes use checked arithmetic under the package-policy maximum.
The parser does not stat, hash or trust archive entries; installer verification
later proves that the archive contains exactly these paths, sizes, modes and
bytes without symlink escape.

### 7. Runtime requirements and dependencies are typed and finite

Runtime requirement IDs/kinds come from a sealed first-party vocabulary. Entries
are unique per variant by `(id, kind, architecture)` and contain only typed
versions/digests and declared bundled/system policy. They cannot contain arbitrary
loader paths, environment variables, commands, URLs or executable arguments.

Bundled requirements reference exactly one declared file set and cannot replace a
host-owned shared runtime prohibited by ADR-052. System requirements are metadata
for later platform/probe validation, not proof that a loader, driver, framework or
device exists. Dependency cycles in any schema-defined component reference graph
are rejected with a bounded stable ID path.

### 8. Errors are typed, bounded and safe to display

`RendererManifestError` contains stable code, severity, bounded JSON Pointer-like
field location, optional array index and safe expected/actual category. It never
copies an untrusted value, full path, manifest fragment or parser exception text
into logs/UI. At minimum codes distinguish:

```text
InputTooLarge, InvalidUtf8, InvalidJson, DuplicateField,
NonCanonicalEncoding, ResourceLimitExceeded, UnsupportedSchema,
MissingRequiredField, UnknownField, UnknownRequiredExtension,
InvalidType, InvalidIdentity, InvalidVersion, IncompatibleRange,
InvalidHash, InvalidPath, DuplicateDeclaration, PathCollision,
AmbiguousVariant, InvalidReference, InvalidAbi, InvalidDependency,
Cancelled, OutOfMemory, ParserFailure
```

Errors are collected only while safe and deterministic; structural/depth/memory
failures stop immediately. Diagnostics record manifest digest and safe package ID
only after those fields validate. Parsing failure never publishes an install
record, component snapshot revision or “repair” action based on untrusted fields.

### 9. Parser success is not trust, compatibility or availability

The ordered product flow is:

```text
bounded canonical parse + internal semantic validation
    -> trusted catalog/key expectation match
    -> detached signature and archive/file digest verification
    -> host/editor/private-ABI compatibility and exact variant resolution
    -> safe extraction/layout/platform-signature verification
    -> install-record publication
    -> helper-process runtime probe
    -> availability snapshot update
```

Every later stage binds the canonical digest and typed package/version/backend
identity returned by parsing. A stage cannot reparse with different options or
use raw fields independently. Parser success cannot authorize extraction, loading,
selection, activation, capability support or fallback.

## Migration And Verification

The illustrative manifest gains an explicit empty `extensions` array and the
release generator emits/verifies the pinned canonical bytes. Existing manually
formatted examples are documentation only and are not signed production fixtures.
Implementation uses one pinned JSON/token library behind Horo-owned limits and
typed conversion; callers never consume its DOM directly.

Tests must cover empty/truncated/oversized input; all UTF-8/escape/numeric grammar
boundaries; duplicate keys at every depth; canonical key/escape/integer encoding;
every count/depth/string/scratch/error limit at equality and just over; missing,
unknown and wrong-type fields; required/optional extensions; all ID/version/ABI/
enum/range invariants; duplicate/ambiguous variants; absolute/traversal/UNC/drive/
reserved/case/Unicode path collisions; duplicate files and dangling library/
license/runtime references; checked file-size sums; cancellation/allocation
failure; stable safe diagnostics; parse-before-load proof; deterministic
round-trip/digest fixtures; property/fuzz tests with sanitizer coverage; and corpus
regressions for every accepted parser bug.

## Consequences

Renderer metadata becomes deterministic, bounded and safe to inspect before native
code or archive content is trusted. Signed bytes, typed values and install/probe
identity cannot drift through permissive parsing. The cost is a deliberately strict
v1 schema, canonical generator, extension registry, conservative cross-platform
path policy and substantial fuzz/boundary coverage.

## Rejected Alternatives

| Option | Decision and reason |
|---|---|
| Parse after loading the renderer library | Rejected: untrusted native code would execute before identity/compatibility validation. |
| Accept any JSON and normalize before signature verification | Rejected: signed bytes and interpreted values could differ. |
| Keep the JSON DOM as the application contract | Rejected: leaks parser semantics, strings and mutable/unbounded values across layers. |
| Ignore duplicate keys or let last value win | Rejected: signer, parser and reviewer may interpret different values. |
| Ignore unknown fields everywhere | Rejected: typos and future security requirements could silently lose effect. |
| Reject all future optional metadata | Rejected: the bounded non-authoritative extension envelope permits safe evolution. |
| Validate paths only during extraction | Rejected: collisions and ambiguous ownership must fail before archive mutation. |
| Select the current-host variant inside the parser | Rejected: parsing is host-independent; resolution has separate Platform/policy inputs. |
| Treat capability hints as availability | Rejected: only initialized effective capabilities are authoritative. |
