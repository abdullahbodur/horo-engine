# ADR-058: Package Source Policy

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Public, private, local, vendored and offline package-source identity, deterministic precedence, pinning, mirrors, credential isolation, development overrides and failure behavior
- **Issue**: [PKG-001.3](https://github.com/abdullahbodur/horo-engine/issues/117)
- **Jira**: [HORO-117](https://horo-engine.atlassian.net/browse/HORO-117)
- **Parent**: [PKG-001](https://github.com/abdullahbodur/horo-engine/issues/36)
- **Related**: [ADR-057](057-package-manifest-v1-typed-model.md), [ADR-002](002-credential-handling.md)
- **Normative documents**: [Horo Package System](../architecture/packages/package-system.md), [Package Restore](../architecture/packages/package-restore.md), [Package Lifecycle](../architecture/packages/package-lifecycle.md), [Application Security](../architecture/security/application-security.md), [Configuration System](../architecture/foundation/configuration-system.md)

## Context

Horo packages may come from a public registry, an organization/private registry,
a static index, an exact URL, a vendored project artifact, a user-local development
directory or an air-gapped mirror. The current package architecture lists these
possibilities but does not define one source identity model or deterministic
selection order. It is unclear whether a private registry overrides public
metadata, whether a mirror becomes a new authority, how a development override
interacts with a lockfile, or when fallback after a source failure is safe.

Without a policy, two machines with the same project metadata could resolve a
different artifact because their registries were discovered in another order. A
malicious or stale mirror could replace a package ID/version, a local override
could leak into a committed lockfile, and private credentials or URLs could reach
project files and diagnostics.

ADR-057 separates package author metadata from source/resolution/install state.
This ADR defines the later source policy and the exact provenance record carried
by the lockfile and verified install candidate.

## Decision

### 1. Source identity, transport endpoint and artifact identity are separate

The source model uses distinct bounded values:

```cpp
struct PackageSourceId;
struct PackageSourceConfigurationDigest;
struct PackageSourceArtifactId;
struct PackageIndexRevision;
struct PackageMirrorId;
struct CredentialHandle;
struct ArtifactDigest;

enum class PackageSourceKind {
    PublicRegistry,
    PrivateRegistry,
    StaticIndex,
    DirectArtifact,
    VendoredArtifact,
    LocalDevelopmentOverride,
};
```

`PackageSourceId` names a logical configured authority such as `horo.public`,
`team.release` or `project.vendored`. A source owns package/version metadata and a
stable safe artifact coordinate. One source may have ordered transport endpoints
(mirrors and origin). Endpoint URLs are fetch details, not package identity.

`ArtifactDigest` is the immutable byte identity. The tuple of package ID, exact
version and artifact digest is globally authoritative after verification. A
source cannot replace an existing locked tuple by winning precedence.

The content-addressed cache is not a source. It may satisfy a fetch only after its
bytes and provenance are verified against the requested artifact digest.

### 2. Source configuration layers are explicit and canonical

Sources are composed from four layers:

| Layer | Allowed contents | Portability |
|---|---|---|
| Product policy | Built-in public source IDs, trusted TLS/signing roots, allowed source/transport kinds and hard security limits | Product-defined |
| Organization policy | Approved private source IDs, mirrors, allow/deny rules and credential-provider bindings | Organization-managed, not project-authored |
| Project portable configuration | Named public/static/direct/vendored source references and explicit source choice per dependency | Committed and credential-free |
| User-local configuration | Explicit development overrides, user mirrors/proxies where policy permits and credential handles | Never committed or locked |

Each layer has a typed schema and deterministic canonical digest. Loading order,
map iteration, filesystem enumeration and environment-variable order never define
precedence. A higher layer cannot weaken product/organization TLS, signing,
redirect, domain, size or source-kind restrictions.

Package manifest files do not configure sources. A package cannot select the
registry from which its dependencies must be downloaded or supply credentials for
that registry.

### 3. Locked restore has exactly one authoritative source record

For a dependency already in `.horo/packages.lock`, the lock entry is authoritative
and contains a credential-free `ResolvedPackageSource`:

```cpp
struct ResolvedPackageSource {
    PackageSourceId source;
    PackageSourceKind kind;
    PackageSourceArtifactId artifact;
    std::optional<PackageIndexRevision> indexRevision;
    HoroPackageId package;
    PackageVersion version;
    ArtifactDigest artifactDigest;
    PackageManifestDigest manifestDigest;
    PackageFileManifestDigest fileManifestDigest;
    PublisherId publisher;
};
```

Restore may fetch the exact artifact from an approved mirror, origin, vendored
copy or verified cache entry associated with that logical source, but every byte
must match the locked digest and signature/publisher policy. It cannot consult
another registry for a newer version, reinterpret the package ID/version or
rewrite provenance.

If the locked source ID no longer exists, policy may map that source to an
approved mirror/archival transport only when the artifact/digests remain exact.
Otherwise the result is `SourceConfigurationMissing`, not implicit re-resolution.

### 4. Fresh resolution uses an explicit deterministic candidate order

When no accepted lock entry exists, one package request produces candidates in
this order:

1. An explicitly enabled user-local development override for that exact package
   and operation profile.
2. The portable source explicitly named by the project dependency request.
3. Project-declared vendored/static sources explicitly assigned to that package.
4. Organization-approved named sources assigned by package/namespace routing.
5. Project-configured named indexes ordered by numeric priority and then canonical
   `PackageSourceId`.
6. The product default public source, when policy allows it.

Private is not intrinsically higher priority than public, and a registry is never
selected merely because it responds first. Namespace routing and source assignment
are exact policy entries, not prefix guessing. Parallel metadata queries preserve
the precomputed order; completion timing cannot change selection.

Within one source, versions are filtered by request range, compatibility, yanked/
policy state and publisher requirements, then selected by the resolver's canonical
semantic-version rules. Source precedence chooses where candidates may come from;
it does not override version/digest conflicts or compatibility.

The ordered canonical source configuration and request inputs produce a
`PackageSourceConfigurationDigest`. The resolver records it with diagnostics and
the lock transaction so equal inputs prove equal precedence decisions.

### 5. Development overrides are explicit non-portable graph substitutions

A local development override names one exact package ID and a normalized local
path stored only in user-local configuration. It is disabled by default in CI,
release, non-interactive and offline-reproducibility profiles. Enabling it requires
an explicit development profile or invocation flag permitted by policy.

The override is validated and packaged into a transient ADR-057 bundle before
use; restore never executes arbitrary build scripts or loads files from an
unverified working directory. Its provenance is `LocalDevelopmentOverride`, and
all UI/CLI/diagnostics mark the graph non-portable.

A local path, file identity, digest, credential handle or resolved override result
never enters committed `packages.json` or `packages.lock`. Lock generation while
an override is active must either resolve and lock the portable baseline separately
or fail with `NonPortableOverrideActive`; it cannot lock the override bytes as if
they came from the assigned portable source. Release ignores overrides and proves
the locked portable graph.

### 6. Mirrors change transport, not authority or precedence

A logical source owns an ordered list of approved mirrors followed by its origin
when origin fallback is allowed. Mirror order is explicit numeric priority then
canonical `PackageMirrorId`. Offline mode may select only endpoints marked
offline/local.

An availability failure such as unreachable endpoint, DNS failure or policy-
bounded timeout may advance to the next endpoint for the same logical artifact.
An integrity, signature, publisher, TLS, redirect-policy, authentication-scope or
content-identity failure is security-significant: the artifact is quarantined and
automatic fallback stops. Fallback must not turn an attack into a transient miss.

A mirror cannot add versions, change index metadata or serve different bytes under
the source's authority unless organization policy explicitly configures it as a
separate source. Index metadata is signed/pinned or bound to an expected revision/
digest according to source kind.

### 7. Pinning requirements depend on source capability

| Source kind | Portable pin |
|---|---|
| Registry/static index | Source ID + immutable index/package coordinate + exact version + artifact/manifest/file-manifest digests + publisher |
| Direct artifact URL | Credential-free canonical locator or opaque artifact coordinate + expected artifact digest |
| Vendored artifact | Project-relative normalized file identity + expected artifact digest |
| Git source, when enabled later | Canonical repository identity + immutable commit/tree + produced artifact digest; packaging is an explicit trusted operation |
| Local development override | Never portable or lockable; transient verified bundle only |

Mutable branches, floating tags, unversioned “latest” endpoints, URL artifacts
without expected hashes and local absolute paths are invalid portable pins. A
redirect does not change the recorded source/artifact identity and must remain
within the configured scheme/domain/credential scope.

### 8. Credentials are resolved just in time and never become package data

Private source authentication uses opaque `CredentialHandle` values stored only
in user/organization credential configuration. Project files, package manifests,
lockfiles, source configuration digests, cache metadata exported to projects,
operation history and diagnostic bundles contain neither secret values nor
credential handles.

The source operation asks the credential service for a short-lived scoped lease
immediately before one approved network request. The lease is bound to source ID,
endpoint origin, operation and requested auth mechanism. It is not inherited by a
redirect to another origin, subprocess, package hook or mirror with a different
scope. Raw secrets never enter URLs, command-line arguments or environment
variables when a platform-secure request adapter exists.

Logging/audit records use safe source/mirror IDs, operation IDs, status codes and
redacted endpoint origins. They do not record query strings, authorization/cookie
headers, private repository paths, usernames, credential-provider keys or response
bodies that may contain secrets. Error text from remote servers is bounded and
redacted before becoming a diagnostic.

### 9. Offline and air-gapped operation filter transports without reordering

Offline mode freezes the same resolver/source policy and removes network-capable
endpoints. It may use:

- a verified content-addressed cache hit for the locked digest;
- a project-vendored artifact assigned by portable configuration;
- an approved local static index/mirror with pinned metadata; or
- an organization-managed air-gapped source.

Offline does not make arbitrary local directories trusted, relax hash/signature
checks, reorder package sources or permit a missing lock to select a different
version. Required unavailability returns `OfflineArtifactUnavailable`. An
interactive tool may offer a future vendoring plan, but cannot mutate project
source configuration as a side effect of restore.

### 10. Conflicts are not resolved by precedence

The resolver returns typed outcomes:

| Condition | Result |
|---|---|
| Same package ID/version and identical verified digest from approved transports of one source | Same artifact; provenance records the successful transport without duplicating a candidate. |
| Same package ID/version with different digest from any sources | `PackageIdentityConflict`; stop and report both safe source IDs/digests. |
| Higher-priority source has no matching version | Continue to the next configured source only when the request does not require the first source exclusively. |
| Explicitly assigned source unavailable | `AssignedSourceUnavailable`; do not fall through to another authority. |
| Locked artifact unavailable | `LockedArtifactUnavailable`; do not re-resolve. |
| Integrity/signature/publisher failure | Quarantine and stop automatic fallback. |
| Authentication missing/denied | `SourceAuthenticationRequired`/`Denied`; never try another authority to bypass private routing. |
| Package yanked | Existing exact lock may restore only when policy permits and bytes remain verifiable; fresh resolution excludes it. |
| Local override invalid | Reject override; do not silently use portable package while presenting a development graph. |

Diagnostics preserve candidate order and safe reasons. Network racing or retry
timing cannot choose the winner. Retry budgets, backoff and endpoint health affect
availability only, never source precedence or artifact identity.

### 11. Source resolution is bounded and side-effect disciplined

Index and response parsing uses bounded bytes, entries, versions, redirects,
headers, diagnostics and time/in-flight request budgets. TLS is mandatory for
network sources except explicit loopback/local development policy. Domain,
certificate/pinning, proxy, redirect, maximum artifact size and content-type rules
are evaluated before accepting bytes.

Metadata query is read-only. Download writes only to a unique staging file,
verifies size/digest/signature/bundle, then atomically publishes to the content-
addressed cache. Cancellation or failure removes/quarantines staging and does not
update the lockfile, source health as authority, install record or active package
graph. Lockfile publication is a separate journaled resolver transaction.

### 12. Migration normalizes inline sources into named policy

Legacy `.horo/packages.json` entries with inline `url`, `file`, `git` or
`registry` fields are migration input. Migration creates/reuses canonical named
portable sources, assigns each dependency explicitly, moves local paths to user-
local overrides, removes all credentials/query secrets, requires missing hashes/
immutable revisions and previews the resulting precedence order.

Existing lock entries are accepted only when their source/artifact identity and
digests can be represented without guessing. Otherwise restore requires explicit
re-resolution. Migration never uploads, downloads or rewrites a lock merely to
inspect the old configuration.

## Verification

Contract tests cover:

- identical canonical source configuration/request inputs produce identical
  ordered candidates and source-configuration digest;
- public/private/static/direct/vendored resolution with explicit assignment;
- exact locked restore through origin, approved mirror, verified cache and
  offline vendored artifact without provenance drift;
- equal package/version with different digests is a hard identity conflict;
- assigned/locked source unavailability never falls through to another authority;
- mirror availability fallback versus integrity/security stop behavior;
- redirect, TLS, size, index-revision, publisher and artifact-pin enforcement;
- local override enablement, non-portable diagnostics and lock/release exclusion;
- offline filtering without reordered candidates or relaxed verification;
- missing/denied/expired credentials and cross-origin redirect scope rejection;
- exhaustive log, diagnostic, operation, lockfile and project-file scans proving
  no secret or credential handle is serialized; and
- cancellation/failure before and after staging verification with no partial
  lock, cache publication or install-state mutation.

Determinism fixtures randomize map insertion, filesystem enumeration, request
completion and mirror response order while expecting the same selection. Private
source tests use synthetic secrets in URLs, headers, cookies, remote error bodies
and provider identifiers to verify redaction at every output boundary.

## Consequences

Every resolved artifact has one durable safe source identity and exact digest
proven in the lockfile. Mirrors improve availability without becoming shadow
authorities; private/local/offline behavior cannot reorder resolution implicitly;
credentials remain outside portable and diagnostic data. The cost is explicit
named source configuration, strict legacy migration, additional provenance types
and refusal to “helpfully” fall back when assignment, identity or security fails.

## Rejected Alternatives

| Option | Decision and reason |
|---|---|
| Search all registries and use the first response | Rejected: network timing would define precedence and reproducibility. |
| Always prefer private over public | Rejected: privacy classification is not an authority/priority rule; explicit routing owns precedence. |
| Let mirrors override metadata or versions | Rejected: a mirror is transport for one authority, not a second resolver. |
| Fall back after hash/signature failure | Rejected: security failures must not be disguised as endpoint availability. |
| Store authenticated URLs/tokens in project or lock files | Rejected: portable metadata would leak and replay credentials. |
| Store credential-handle names in the lockfile | Rejected: even opaque provider identifiers are user/organization-local and can disclose infrastructure. |
| Lock a local development directory | Rejected: it is mutable, non-portable and not a reproducible source artifact. |
| Treat cache contents as a source | Rejected: cache presence is an optimization and cannot establish provenance or authority. |
| Let offline mode relax signature/hash policy | Rejected: lack of network does not make local bytes trusted. |
| Resolve source precedence through generic configuration precedence | Rejected: package assignment, locks, mirrors and conflict semantics require a dedicated typed policy. |
