# ADR-132: Platform Services Project Salt, Stable ID, Tombstone and Provider Mapping

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Project-scoped Platform Services identity namespace, deterministic numeric ID algorithm and encoding, key aliases, tombstones, collision handling, salt cloning/regeneration, provider mappings and cooked/runtime consumption
- **Issue**: [PLS-003.1](https://github.com/abdullahbodur/horo-engine/issues/1888)
- **Jira**: [HORO-1844](https://horo-engine.atlassian.net/browse/HORO-1844)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-009](009-configuration-schema-precedence-and-secret-boundary.md), [ADR-054](054-extension-and-package-authority-boundary.md), [ADR-103](103-network-project-configuration-and-build-profile-ownership.md), [ADR-113](113-local-storage-user-profile-and-slot-ownership.md), [ADR-130](130-platform-services-frontend-request-lifetime-timeout-null-and-error-semantics.md), [ADR-131](131-platform-services-closed-sdk-extension-abi-package-and-composition-boundary.md)
- **Normative documents**: [Platform Services Architecture](../architecture/runtime/platform-services-architecture.md), [Project Model](../architecture/editor/project-model.md), [Project Versioning and Migration](../architecture/foundation/project-versioning-and-migration.md), [Release Security](../architecture/release/release-security.md)

## Context

Platform Services definitions need durable achievement, leaderboard, stat and
presence identities before their registries and provider manifests are implemented.
The architecture currently suggests hashing an authoring name with an unspecified
project salt. It does not define the hash input bytes, salt encoding, numeric byte
order, alias behavior or how a project clone decides whether to preserve identity.
Two clean builds could therefore disagree while still claiming to use the same
scheme.

A display-name rename must not change gameplay or remote-platform identity. Deleting
a definition must not make its value available to an unrelated future definition.
Collision recovery must also be independent of registry insertion order; silently
probing for the next value would make the same sources cook differently after a
merge or reorder.

Provider SDKs add another identity domain. Steam API names, trophy IDs and console
manifest indexes must map to Horo IDs, but cannot become identifiers serialized by
gameplay. Conversely, a provider manifest generated successfully once cannot become
a second registry authority or hide that the Horo source has changed.

This ADR fixes the project namespace, deterministic algorithm, canonical storage,
definition lifecycle and provider-mapping ownership. Later service-specific tickets
may add fields and limits, but cannot introduce a second ID algorithm or reinterpret
an existing value.

## Decision

### 1. One project ledger owns Platform Services identity

The Project domain owns one committed, versioned
`.horo/platform_services.ids.json` ledger. It contains active and tombstoned primary
definitions plus their key aliases for these disjoint kinds:

```cpp
enum class PlatformServiceIdKind : std::uint8_t {
    Achievement = 1,
    Leaderboard = 2,
    Stat = 3,
    PresenceStatus = 4,
};

struct PlatformServiceStableId {
    std::uint64_t value;
};

using AchievementId = StrongId<AchievementTag, PlatformServiceStableId>;
using LeaderboardId = StrongId<LeaderboardTag, PlatformServiceStableId>;
using StatId = StrongId<StatTag, PlatformServiceStableId>;
using PresenceStatusId = StrongId<PresenceStatusTag, PlatformServiceStableId>;
```

The wrappers are not implicitly interchangeable with one another or with a provider-
native identifier, array index, runtime handle or raw integer. `0` is invalid and reserved.
Primary numeric values are globally unique across all four kinds so logs, ABI
envelopes and generic tooling cannot ambiguously identify a record after losing a
static wrapper.

Service-specific authoring documents own display/localization, behavior, thresholds,
sort policy and other semantics. They reference a ledger ID; they do not allocate or
redefine it. Provider mapping documents and generated manifests are consumers. The
ledger is the only editable Horo identity authority.

The application project service loads and validates the complete ledger as a detached
candidate. Descriptor construction and parsing are inert. A host-owned composition/
cook step may publish one immutable snapshot only after project identity, salt,
bounds, uniqueness, aliases, references and provider mappings validate. No static
initializer or provider mutates it.

### 2. The project salt is generated once and committed

`PlatformServicesIdSalt` is exactly 128 uniformly random bits obtained from the
platform cryptographic random source during project creation. The all-zero value and
short/failed random reads are rejected; Horo never falls back to time, path, project
name, process ID, `std::random_device` assumptions or a fixed default.

The single authoritative value is the root `platformServicesIdSalt` field in
`.horo/project.json`, serialized as `psid1:` followed by exactly 32 lowercase
hexadecimal digits. It is non-secret, portable and source controlled. Environment,
user settings, CLI flags, provider packages and build profiles cannot override it.
The ledger records its algorithm version and owning `projectId`, but does not copy the
salt into a second mutable field.

Project creation commits `projectId`, salt and an empty ledger in one transaction.
Opening a project fails with a typed actionable diagnostic when a required salt is
missing, malformed, zero or disagrees with the ledger owner/algorithm. A migration
from a schema that predates this field generates the salt exactly once in the
transactional project-migration path; read-only inspection and ordinary cook never
repair project metadata.

The salt is a namespace seed, not a credential, authenticity proof or privacy
boundary. Hashing a guessable key with it does not conceal that key.

### 3. Canonical keys are bounded machine identity, not display text

Every primary definition has one immutable `canonicalKey`. Version 1 accepts 1–96
ASCII bytes matching:

```text
^[a-z][a-z0-9_.-]{0,95}$
```

The exact stored bytes are hashed. There is no trimming, case folding, Unicode
normalization, locale conversion, path normalization or implicit namespace prefix.
Keys are chosen for durable machine meaning, such as `campaign.first_blood`; localized
and user-visible names are independent fields in service-specific documents.

Once a definition has been committed, changing its `canonicalKey` is not a rename.
Presentation can be renamed freely. An author who needs a new accepted textual key
adds a validated alias to the existing primary entry while retaining the original
canonical key. An unrelated semantic definition receives a new canonical key and ID.

### 4. Version 1 IDs use one exact SHA-256 derivation

For a validated primary definition, Horo builds this byte sequence:

```text
ASCII("horo.platform-services.stable-id.v1")         (no terminating NUL)
0x00                                                   (one explicit separator byte)
project salt                                      (16 raw bytes)
kind                                              (one uint8 tag above)
canonical-key byte count                          (uint16, big-endian)
canonical-key                                     (exact ASCII bytes)
```

It computes SHA-256 over exactly that sequence, takes digest bytes 0 through 7 and
interprets them as one unsigned 64-bit big-endian integer. A zero result is a
collision with the reserved value and fails exactly like any other collision. The
algorithm name, domain separator, field order, widths, byte order and truncation are
part of ledger schema version 1; implementations use a shared golden-vector codec,
not `std::hash`, a provider hash or a platform-dependent library default.

The ledger stores the derived value beside its canonical key so code review and
merge diagnostics remain explicit. Validation always recomputes it and rejects a
mismatch. It never trusts or silently rewrites a stored numeric value.

### 5. Text, JSON, binary and ABI forms are canonical

Authoring JSON serializes every ID as `sid1:` followed by exactly 16 lowercase
hexadecimal digits, including leading zeroes. JSON numbers, signed decimal values,
uppercase hex, optional prefixes and shortened forms are non-canonical and rejected.
The typed in-memory value is `uint64_t`; cooked binary and the ADR-131 C ABI encode its
eight bytes in big-endian order. Adapters explicitly decode that order and do not pass
host-endian structs as wire bytes.

Canonical ledger serialization:

- uses schema version `1`, the exact owner `projectId` and algorithm
  `horo.platform-services.stable-id.v1`;
- orders primary entries by kind tag and then unsigned numeric ID;
- orders aliases for one entry by exact unsigned ASCII byte order;
- emits each schema-defined field once, rejects duplicate JSON object keys and
  omits no required state; and
- uses deterministic project JSON formatting and atomic sibling replacement.

The registry fingerprint is a domain-separated SHA-256 over those canonical ledger
bytes. It is provenance/freshness evidence, not a replacement for an ID or a security
signature.

### 6. Tombstones permanently reserve deleted identity

A primary entry is exactly `Active` or `Tombstoned`. Removing an active service
definition transitions its ledger entry to `Tombstoned` and retains kind, canonical
key, numeric ID, aliases and bounded removal provenance. It does not erase the entry.
Ordinary authoring, migration cleanup, cook and provider synchronization cannot prune
tombstones or assign their key, alias or numeric ID to another definition.

Adding a definition whose primary key or alias matches a tombstoned entry reports
`platform.identity.tombstoned`; it never silently restores the entry. A deliberate
restore is a separate reviewed operation that keeps the exact original entry and ID,
checks every affected provider mapping and records the transition. Product policy may
forbid restore after external publication. If the semantics are not exactly the same,
the author must choose a new canonical key.

Tombstones participate in every uniqueness and collision check. Cooked runtime
tables normally omit their service payloads, but carry enough registry revision/
fingerprint evidence to prove which identity namespace was used. Provider adapters
retain or retire remote entries according to platform policy; they never offer a
tombstoned Horo ID to a different definition.

### 7. Aliases resolve only to one existing primary entry

An alias is a version-1 canonical key owned by exactly one primary entry of the same
kind. It has no independently derived ID. Authoring/import/migration lookup resolves
the alias directly to its owner's existing numeric ID. Runtime requests and cooked
gameplay references contain the typed numeric ID, not alias strings.

Primary keys and aliases share one uniqueness namespace per kind across active and
tombstoned entries. Alias chains, cycles, wildcard/prefix matching, case-insensitive
lookup and cross-kind aliases are forbidden. Moving an alias between entries is an
explicit migration conflict, not last-writer-wins behavior. Tombstoning an entry keeps
its aliases reserved.

Aliases are for Horo authoring compatibility only. A Steam API name, trophy number,
console resource path or any other provider-native value cannot be registered as a
Horo alias merely to make provider lookup convenient.

### 8. Collisions fail before publication and never auto-probe

Candidate validation recomputes every primary ID and builds a global numeric index
covering active and tombstoned entries. Two different primary entries with the same
numeric value are a hard `platform.identity.hash_collision` failure even if their
kinds differ. Zero, duplicate keys/aliases, stored/derived mismatches and references
to unknown or tombstoned entries also fail with separate stable error codes.

Collision diagnostics contain the algorithm version, colliding canonical ID, both
kind/key pairs and safe source locations. They never report only “duplicate ID” or
select a winner. For a newly introduced, unpublished entry the remediation is to
choose another canonical key and review the resulting ID. An existing published
entry is immutable. If imported histories already contain two published meanings,
the merge remains blocked until a declared migration/fork maps all durable references;
Horo cannot guess which meaning wins.

Incrementing the value, adding an insertion-order salt, taking a wider substring on
one machine or retrying with a counter is forbidden. Such probing would make identity
depend on document order and hide collision evidence.

### 9. Clone and salt-regeneration intent are explicit

A filesystem copy, VCS clone, branch, checkout, path move, backup/restore or teammate
checkout preserves `projectId`, salt, ledger and all IDs. These operations represent
another working copy of the same product identity namespace.

An editor/CLI “Duplicate Project” operation must choose one of two named modes:

- `PreserveIdentity` copies project identity, salt, ledger, provider mappings and
  references unchanged for a product branch/port; or
- `ForkIdentity` generates a new `projectId` and cryptographic salt, then performs the
  namespace migration below for an independent product. It does not copy provider
  credentials or claim that old remote mappings are valid for the fork.

There is no path/name heuristic and no automatic regeneration after detecting a copy.
Opening two preserved clones concurrently is a project writer/revision concern, not a
reason to mutate identity.

Salt regeneration is not an ordinary setting edit. `ForkIdentity` and an explicitly
authorized `RegeneratePlatformServicesIdentity` command first produce a dry-run plan
containing every old-to-new active and tombstoned ID, alias, durable source reference,
provider mapping disposition, cooked/cache invalidation and external-publication
warning. They derive all candidate IDs with one newly generated salt and validate the
complete candidate before writing anything.

Commit atomically updates `project.json`, the ledger, all owned durable references and
eligible provider mapping documents; it invalidates every affected cooked manifest,
offline operation and cache entry. Failure leaves the old namespace wholly intact.
Historical tombstoned keys remain tombstoned under newly derived IDs. Unknown/opaque
references, unowned package payloads, unresolved collisions or incomplete mappings
block the transaction.

After any ID has shipped, been submitted remotely, entered a save/network protocol or
been published to another package, regeneration is prohibited by default. It requires
a separately reviewed product migration with complete external/provider transition
evidence or an explicit new-product reset that accepts incompatibility. A compatibility
alias cannot make two numeric namespaces equivalent.

### 10. Provider mappings are private adapter data keyed by Horo ID

Each target/provider mapping table belongs to the corresponding trusted provider
cook/package adapter and is keyed by `(ProviderId, PlatformServiceIdKind,
PlatformServiceStableId)`. Its value is bounded provider-specific data such as an API
name, trophy number or manifest index. The same Horo ID may map differently for each
provider; one provider value cannot map ambiguously to multiple active Horo entries
within the provider scope.

The adapter validates the Horo ledger snapshot/fingerprint, target product/profile,
provider package identity/version, mapping schema and provider constraints. Required
active definitions need exactly one compatible mapping. Optional omission must be
declared by typed product capability policy, not inferred from an absent row. Unknown,
tombstoned, duplicate, wrong-kind or stale-fingerprint mapping rows fail generation.

Provider manifests are deterministic derived artifacts. Provider reverse lookup may
exist privately for callback correlation and diagnostics, but resolves immediately to
a typed Horo ID under the captured provider/registry generation. Gameplay, saves,
offline queues, scripts, telemetry dimensions and public APIs never persist a provider
value or raw display string as their identity.

Provider packages cannot allocate Horo IDs, add aliases, reactivate tombstones or
rewrite the ledger. A provider mapping rename changes only adapter data when the
platform permits it. A provider value retained for a removed entry remains associated
with that tombstone/history and cannot be reassigned silently.

### 11. Cook and runtime consume immutable validated snapshots

Cook captures exact project identity, salt-algorithm version, ledger fingerprint,
service-definition revisions, provider mapping revision and target/product profile.
It resolves authoring keys/aliases once, rejects tombstoned or missing references and
emits canonical typed IDs in provider-neutral artifacts. Provider adapters then emit
their private manifests from the same captured snapshot. Any input revision change
invalidates the candidate rather than mixing generations.

Runtime loads bounded, sorted tables carrying the expected ledger fingerprint and
product manifest binding. It performs typed numeric lookup only; it never hashes a
runtime string, registers a definition, consults an editor alias or asks a provider to
invent identity. An immutable registry generation is retained by admitted ADR-130
requests and durable offline entries until they terminate/migrate. Hot reload publishes
a complete compatible generation at a safe point; salt/algorithm changes require the
explicit migration path and cannot hot reload.

Release/certification evidence freezes the canonical ledger fingerprint and every
selected provider mapping/manifest digest. Diagnostics use typed Horo IDs, safe
canonical keys where policy permits, algorithm/revision and source locations. They
redact provider credentials, restricted native values and account identity under
ADR-131 and the observability policy.

### 12. Qualification proves identity stability and fail-closed behavior

Required automated evidence includes:

- golden vectors for every kind, leading-zero outputs, canonical salt/ID text and
  exact SHA-256 input bytes/big-endian decoding on every supported host;
- identical IDs and canonical ledger bytes across clean builds, developer machines,
  source order, locale and filesystem location;
- display rename/reorder stability, alias resolution and cross-kind type rejection;
- malformed/zero/missing salt, malformed keys/IDs, stored/derived mismatch, duplicate
  primary/alias, zero result and synthetic hash-collision rejection;
- active-to-tombstone transition, no silent key/ID reuse, explicit compatible restore
  and tombstone preservation through clone/fork/migration;
- `PreserveIdentity` equivalence plus `ForkIdentity` dry-run, complete reference remap,
  unknown-reference rejection, rollback and cache/offline invalidation;
- missing/duplicate/stale/wrong-kind provider mappings and proof that provider-native
  strings never enter Horo runtime/save/offline identity fields; and
- cook/runtime snapshot fingerprint mismatch, stale generation and mixed-input
  publication rejection with the prior valid generation preserved.

Private provider repositories add manifest round-trip and certification fixtures, but
public Mock/Null/golden-vector tests establish the Horo identity contract without a
closed SDK.

## Consequences

- Every project has one reproducible Platform Services numeric identity namespace
  with byte-exact derivation and encoding.
- Display changes, source ordering, clean builds and provider changes cannot rewrite
  gameplay identity.
- Tombstones and aliases make deletion/authoring compatibility explicit while keeping
  old values reserved.
- Hash collisions and imported-history conflicts block publication with actionable
  evidence instead of becoming order-dependent IDs.
- Project forks can deliberately obtain a new namespace, but regeneration is a broad
  transactional migration with visible compatibility cost.
- Provider mappings remain replaceable adapter data and cannot become the engine's
  source of identity truth.
- The committed ledger and permanent tombstones add source-control/merge overhead,
  and 64-bit truncation still requires a tested collision path rather than an
  assumption that collisions cannot occur.

## Rejected Alternatives

### Use display names or provider-native strings as durable IDs

Rejected because localization, branding and provider constraints would rewrite
gameplay identity and couple portable content to one backend.

### Use `projectId`, project path or a fixed global salt

Rejected because project identity currently serves non-secret observability semantics,
paths vary per checkout and a global salt would collapse independent product
namespaces. The dedicated committed salt has explicit clone/fork lifecycle.

### Use UUIDs allocated independently for every definition

Rejected for this registry because the ticket requires deterministic numeric IDs and
clean-source reproducibility. Randomness enters once at the project namespace; the
versioned key derivation is then reviewable and reproducible.

### Serialize IDs as JSON numbers or host-endian integers

Rejected because JSON consumers may lose 64-bit precision and host byte order differs.
Fixed lowercase text and big-endian binary are canonical.

### Resolve collisions by incrementing, rehashing with a counter or insertion order

Rejected because registry ordering and merge history would change assigned values and
hide a conflict. Collisions must be explicit authoring/migration failures.

### Delete old records and rely on provider manifests as history

Rejected because manifests are target-specific derived artifacts, may be unavailable
and cannot prevent an unrelated definition from reusing an old Horo value.

### Let each provider allocate or translate public gameplay IDs

Rejected because a provider would become identity authority, cross-provider saves and
gameplay would diverge, and provider replacement could reinterpret durable state.
