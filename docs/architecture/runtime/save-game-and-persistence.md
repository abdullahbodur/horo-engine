# Save Game And Persistence Architecture

## Purpose And Core Decisions

This document defines runtime save authority, coherent snapshot capture, durable
archives, transactional restore, migration, host isolation and security for
HORO-1391 #1391 [SAV-001.1]. These are normative implementation requirements; the
complete save service/archive protocol is not claimed as implemented by this change.
ADR-112 freezes the portable archive, canonical state, identity and compatibility
policy for HORO-1410 #1410 [SAV-002.1].
ADR-113 freezes product/environment/user/profile namespaces, logical slot addressing
and storage mapping for HORO-1425 #1425 [SAV-003.1].
ADR-114 freezes authoring/runtime state composition and subsystem-owned canonical
adapters for HORO-1438 #1438 [SAV-004.1].
ADR-115 freezes local/cloud authority, provider revisions and conflict preservation
for HORO-1466 #1466 [SAV-006.1].

- RuntimeSaveService coordinates one runtime save/restore authority under the
  application/session lifetime. It never retains an unleased reference to a scene
  that restore can replace.
- Runtime state is never serialized through SceneDocument or the authoring AST.
- Capture produces owned immutable data at a Scene lifecycle safe point; workers
  serialize, hash, sign and perform all durable file operations.
- A save is one final immutable file. Its `ArchiveContentHash` excludes the
  integrity trailer; signing finishes before the one durable publication, never
  afterward.
- Restore prepares every required participant before a no-fail owner-boundary
  publication. A failed preparation leaves the active runtime unchanged.
- Signature acceptance is trusted host policy, never a flag chosen by the archive.
- Persistent world deltas include inactive cells; account settings/achievements are
  independent of slot-specific player state.

## Persistence Domain Boundaries

| Domain | Authority | Stored content | Isolation |
|---|---|---|---|
| Runtime save (.horosave) | RuntimeSaveService plus subsystem canonical adapters | Stable entity structure and subsystem-owned canonical gameplay/world participants | User/server save namespace, never authored .horo files or runtime memory images |
| Scene document (.horo) | SceneDocumentPersistence | Authored hierarchy/defaults/assets and editor semantics | Project asset tree; no runtime mutation on save/load |
| Editor recovery (.horo_recovery) | ProjectSceneRecoveryRecord | Dirty document snapshot, revisions and recovery context | Bounded recovery namespace; cannot replace canonical data implicitly |
| Project/workspace metadata | ProjectSession / Workspace | Project configuration and editor layout/state | Separate JSON schemas, authority and transactions |
| Account/profile persistence | Account/profile service | Settings, accessibility, achievements, account-wide statistics | Independent user/account store; loading an old slot cannot roll these back |

The four original document/runtime/workspace categories stay separate. Account
persistence is explicitly a fifth independent domain rather than a chunk implicitly
restored from a runtime slot. SlotPlayerState contains only the host-declared
inventory, health, transform, local progression and similar slot-scoped state.
Each field belongs to one persistence owner; gameplay modules and slot player data
must not store two authoritative copies of the same field.

[ADR-068](../../adr/068-music-transport-and-cross-system-ownership.md) applies the
same rule to Audio. Gameplay/narrative/adaptive-music owners may persist semantic
music state, definition/section identity, transition variables, and an admitted
stable musical/content position. Audio may contribute that owned checkpoint at the
save barrier, but live voice/bus/stream/provider handles, callback/device epochs,
raw output sample indices, queues, decoder/DSP state, ring buffers, and native
objects never enter a save. Restore resolves current localized/cooked content and
prepares a new Audio transport generation before publication.

[ADR-092](../../adr/092-character-controller-determinism-and-state-composition.md)
applies the same single-authority rule to Character. Runtime Save contributes the
required `horo.character.state.v1` provider chunk through
`CharacterStateCodecV1`; it does not serialize only a transform, live
`CharacterWorld`, Jolt/proxy state or a save-specific Character schema. The chunk
belongs to the same capture epoch as its paired Physics/world checkpoint. Restore
decodes/migrates a complete canonical Character candidate, resolves stable support
bindings against the detached Physics candidate and publishes only with the
aggregate no-fail commit.

```text
SceneDocument -> one-way RuntimeSceneDefinition conversion
    -> application/session runtime ownership
        -> active SceneRuntime + gameplay state + persistent world deltas
        -> RuntimeSaveService -> immutable RuntimeSaveSnapshot
            -> worker serialization/integrity/signing -> single .horosave file
Account/profile service -> separate account store (not restored by slot load)
Editor document/recovery/workspace writers -> separate namespaces and schemas
```

## Runtime State Classification And Composition

Persistence is opt-in by semantic owner, not inferred from reflection, component
membership, trivial copyability or object reachability:

| State category | Owner | Runtime-save treatment |
|---|---|---|
| Authoring definition/default | SceneDocument, source asset and cook pipeline | Immutable compatible base reference; never rewritten by slot save/load |
| Runtime canonical state | Scene or owning gameplay subsystem | One registered canonical adapter may capture it |
| Derived/rebuildable | Owning runtime subsystem/cache | Excluded and rebuilt from base assets plus restored canonical state |
| Transient execution | Scheduler/operation/subsystem runtime | Excluded unless the owner promotes a semantic value through a versioned schema |
| Presentation | Renderer, Audio device/voice, Runtime UI | Excluded; gameplay/domain owner may persist semantic intent separately |
| Asset/cooked content | Asset Registry and cook/package authorities | Stable identity/revision dependency only, not copied into runtime-state chunks |
| Account/profile | Profile/account service | Separate store/transaction; slot restore cannot rewind it |
| Platform/network external state | Platform/network authority | Excluded and revalidated/reconnected after restore |

Restore resolves the compatible cooked Scene/world base, then applies saved state by
stable identity. Authored entities without saved overrides use compatible defaults;
declared overrides replace only owned fields; deletion is an explicit tombstone.
Durable runtime spawns carry `PersistentEntityId`, declared archetype/prefab/definition
identity and canonical owned values. A changed base requires declared migration or a
typed incompatibility. Runtime state never writes back to SceneDocument/cooked assets.

The Horo Scene adapter owns persistent entity existence, authored/spawn identity,
tombstones, hierarchy/ownership, stable reference remaps and explicitly assigned core
component fields. It does not walk arbitrary component memory or own gameplay,
Physics, Character, AI or dormant world state. Each such subsystem owns its canonical
participant; duplicate field/semantic ownership rejects composition.

Always excluded unless a semantic owner defines a different canonical value are
pointers/native objects, runtime handles/indices, container capacity, jobs/futures/
cancellation/callbacks, pending queues/events, GPU/render extraction/fences, Audio
voice/device/decoder/DSP state, native Physics solver/manifold/proxy/query state,
Navigation queries/rebuild scratch, network connections/keys/packet queues, UI widget/
focus/animation state and wall-clock progression.

## Authority, Lifetime And Public Operation Contract

RuntimeSaveService is application-owned. SaveGameAuthority is its internal
transaction state, not a competing coordinator. The application/session outlives
both this service's in-flight work and every active/replacing SceneRuntime.
IRuntimeSessionHost is a typed injected capture/restore seam implemented by host
composition; runtime codecs do not depend on editor types or application internals.
A scene borrow is valid only inside its owner callback. Workers receive immutable
snapshots, candidates, asset/provider leases and generation-scoped completion data,
never SceneRuntime references or callbacks capturing raw scene pointers.

Canonical participant descriptors are inert, stable-type-ID keyed metadata.
Composition binds one `ICanonicalStateAdapter` per participant, validates unique
semantic ownership, schemas, scopes, capture/restore roles, required/optional
behavior, cost bounds and dependency DAG, then seals a registry revision. An operation
pins that revision and module leases. Registry changes require an explicit host
quiescent rebind; they cannot unload codecs under a worker or register through ambient
service discovery.

Schematic interface shapes (not new installed headers):

```cpp
struct ProductStorageId { Uuid value; };
struct EnvironmentStorageId { Uuid value; };
struct LocalUserStorageId { Uuid value; };
struct GameProfileId { Uuid value; };
struct SaveGameSlotId { Uuid value; }; // nonzero opaque identity, never a path
struct SaveNamespaceHandle { GenerationCheckedHandle value; };
struct SaveAddress {
    SaveNamespaceHandle namespaceHandle;
    SaveGameSlotId slot;
};
struct SaveOperationHandle { OperationId value; };
struct RestoreOperationHandle { OperationId value; };
struct CanonicalStateHash { Sha256 value; };
struct ArchiveContentHash { Sha256 value; };
struct SlotGenerationId { Opaque128 value; }; // nonzero, never time/hash-derived

enum class SaveOperationPhase : uint8_t {
    Queued, CapturingSnapshot, Serializing, FinalizingArchive, WritingTemporary,
    CommitStarted, VerifyingArchive, Migrating, PreparingRestore, ReadyToCommit,
    ApplyingState, Completed, Failed, Cancelled
};
enum class SaveCommitOutcome : uint8_t {
    NotCommitted, Committed, Unknown
};
struct SaveOperationSnapshot {
    OperationId operation;
    SaveAddress address;
    SaveOperationPhase phase;
    SaveCommitOutcome commitOutcome;
    float progress;
    OptionalError terminalError;
    OptionalSlotGenerationId slotGeneration;
    OptionalCanonicalStateHash canonicalState;
    OptionalArchiveContentHash archiveContent;
};
struct SaveRequest {
    BoundedUtf8 displayName;       // catalog metadata only, never archive/path identity
    SaveRequestKind kind;         // manual, quicksave, autosave
    ThumbnailPolicy thumbnail;   // optional/omitted or explicitly required
};

class RuntimeSaveService final {
public:
    RuntimeSaveService(IRuntimeSessionHost&, CanonicalStateParticipantRegistry&,
                       SaveStorageAdapter&, SaveNamespaceBinding&,
                       PlatformServices&, JobSystem&, OperationStore&);
    Result<SaveOperationHandle> SaveSlotAsync(
        SaveAddress, SaveRequest, CancellationToken);
    Result<RestoreOperationHandle> LoadSlotAsync(
        SaveAddress, RestoreRequest, CancellationToken);
    Result<OperationId> RefreshCatalogAsync(CancellationToken);
    Result<OperationId> DeleteSlotAsync(SaveAddress, CancellationToken);
    SaveCatalogSnapshot GetCatalogSnapshot() const; // cached immutable data; no I/O
    Result<SaveOperationSnapshot> GetOperationSnapshot(OperationId) const;
    void PumpOwnerThread();              // bounded record drain; never filesystem I/O
    void OnLifecycleCommitSafePoint();   // explicit lifecycle participant
    Result<ShutdownTicket> BeginShutdown();
};
```

Initial `Result<SaveOperationHandle>` / `Result<RestoreOperationHandle>` reports only
admission. Disk-full, signing, migration or participant-adapter failure later becomes Failed
with the original typed cause in that operation's snapshot. It cannot retroactively
change the return value of SaveSlotAsync. Handles use the application-owned ADR-010
OperationStore/OperationId; no independent save job scheduler/store is created.
GetOperationSnapshot returns the pending/terminal value without blocking; an unknown
or expired handle is a typed lookup error, never apparent success.

Terminal snapshots remain immutable per operation until acknowledged/evicted under
the store's bounded retention policy; a new request does not erase the previous
caller's result. Completed has progress 1 and Committed for a mutation. Failed may
carry Unknown publication outcome; Cancelled always means cancellation won before
its commit gate. Completion/event delivery capacity is reserved at admission.

### Concurrency, Autosave And Safe Points

At most one save/restore/delete mutation is active per session. Per-slot storage
leases additionally serialize cloud/import/other-process writers. Conflicting manual
requests return save.operation.in_progress with the current operation ID. Enumeration
uses a separately bounded worker and publishes a catalog snapshot; it does not read
files synchronously from a browser or owner-thread getter.

The host autosave scheduler retains one coalesced pending autosave intent when busy,
marked visibly Pending rather than silently discarded. It captures the latest state
when admitted, not the stale state when its timer fired. It retries at the next
eligible idle safe point, with bounded cooldown/age diagnostics; persistent I/O/quota
failure uses bounded retries and an explicit exhausted/error indication. Manual work
can take priority but cannot silently clear pending autosave. Stop/session replacement
invalidates the old intent; the host reports cancellation or explicitly schedules a
new-session checkpoint. Autosave ring rotation advances only after durable success;
a failed save never consumes the last good ring entry.

The host registers capture and restore publication **inside**
CommitDeferredLifecycleChanges, after pending structural changes are resolved and
before the next simulation step. PumpOwnerThread runs on the owner and drains ready
worker records with a phase budget. A pump after that lifecycle phase queues actions
for the next valid boundary; it cannot claim that a past safe point is still active.
No worker invokes the pump, commits Scene state, calls UI, or waits on nested jobs.
SaveLimits validates finite positive limits for archive/decoded bytes, chunk/string
counts, snapshot/COW and candidate memory, queue/completion slots, per-pump work,
retry counts and stage/readback/signing deadlines. Aggregate admission counts old
and new snapshots/runtimes/retired resources together; declared local participant
limits are not extra capacity. Exceeding a bound fails or defers explicitly, never
silently unbounds a worker or guarantees a frame-time target.
Serialization, compression, hashing, signing, quota queries, directory scans, flush,
AtomicReplace, deletion and cleanup run on admitted worker/storage roles. No normal
owner/render/transport frame waits for them. ADR-010 governs allowed teardown drains.

## Coherent Immutable Capture

RuntimeSaveSnapshot owns a StableTypeId-sorted set of `OwnedCanonicalSnapshot` values,
the core Scene identity snapshot, SlotPlayerState and PersistentWorld participants,
base asset/dataset revisions and a coherent CanonicalCaptureEpoch. It contains stable
persisted identities and owned/copy-on-write payload leases, never EntityRef addresses,
live component spans, renderer handles or editor objects.

At the owner safe point the session validates scene incarnation, participant registry
revision and capture budget, then copies bounded state or pins a versioned immutable/
COW root for **one logical tick**. Every required participant/world-delta root belongs
to that same capture epoch. Independent native owners prepare immutable semantic
capture versions asynchronously before that boundary; unavailable coherent versions
defer or fail capture instead of mixing ticks.

Capture never leaves live ECS pools, gameplay adapters or streaming state locked or
frozen after the safe point. Workers serialize only the detached snapshot. Large
captures require admitted COW/pages or versioned chunks with bounded owner work;
naive partial copies across subsequent live ticks are not coherent snapshots. COW
write headroom and old/new pages are reserved before pinning; if the budget cannot
support a consistent cut, report CaptureBudgetExceeded or defer. Do not hold the
simulation locked until a background serializer finishes.

Scene replacement after capture does not invalidate an already accepted detached
save: its source scene/revision remains explicit and its storage leases keep it safe.
PIE stop/session shutdown can still request cancellation before the durable commit
gate. A capture not yet completed against its expected incarnation fails stale.

### Thumbnail Capture

Thumbnail acquisition is a separate renderer-owned asynchronous readback request
with scene/view/frame generation and bounded staging/fence lifetime. No save capture
safe point performs a synchronous GPU readback or waits on a render target. Prefer
an already completed compatible thumbnail; otherwise request a later completion
and record its source frame so it is not falsely advertised as the exact capture tick.
Optional thumbnails are omitted on timeout, stale scene or headless execution.
An explicitly required thumbnail may fail the save after its configured finite
deadline, without blocking the owner. Late readbacks only retire their own resources.

## Canonical Logical State

Every required participant produces a canonical logical byte stream before entry
packing, compression, signing or publication metadata. The whole-save canonical
stream contains the stable project/world/base-scene and base-dataset identities,
`SaveSchemaVersion`, then participant ID, `ParticipantSchemaVersion` and canonical
payload tuples sorted by `StableTypeId` bytes. Optional participant presence is part
of the logical stream when its state participates in restore.

Canonical participant codecs use stable numeric field IDs and schema-defined
presence/default rules. Integers have explicit widths and signed representation;
booleans are 0 or 1; enums use declared stable values. Declared IEEE 754 floats are
encoded explicitly, normalize negative zero to positive zero and reject non-finite
values unless that schema defines one exact canonical representation. Strings are
length-delimited validated UTF-8 scalar sequences with no locale or implicit
normalization. Maps and sets sort by canonical encoded key bytes and reject duplicate
canonical keys; sequences retain schema-defined semantic order.

Compiler padding, pointer/native-handle values, native enum widths, unordered
iteration, wall/capture timestamps, play duration, display name, slot/generation,
entry layout, codec/compression and signature metadata never enter canonical state.
Changing a canonical rule requires a `SaveSchemaVersion` or owning
`ParticipantSchemaVersion` change and migration. The identity is:

```text
CanonicalStateHash = SHA256(
    ASCII("HoroSave.CanonicalState.v1") || 0x00 || canonicalSaveState)
```

`CanonicalStateHash` establishes logical equivalence under the same schemas and
semantic dependencies. It is not a file-integrity value, conflict generation or
authentication proof. Readers validate it from decoded canonical participant state;
they do not reconstruct it by reserializing metadata JSON with a local library.

## Single-File Archive And Integrity Envelope

A durable .horosave is **one regular file**, never a live directory bundle. Payload
entries may be compressed independently. Directory layouts are tooling exports only
and must be finalized into this container before publication. Platforms without the
required same-storage atomic transaction semantics must provide a qualified save
container adapter or return UnsupportedAtomicStorage; no copy-over-live fallback.

The following v1 envelope fixes the byte range covered by integrity. All integers
are unsigned little-endian; fields are encoded explicitly, not native struct dumps.

| Preamble field | Width / rule |
|---|---|
| magic | 8 bytes, ASCII HOROSAVE |
| archiveFormatVersion | uint32, v1 = 1 |
| flags | uint32, v1 = 0; unknown bits rejected |
| payloadByteLength | uint64, bounded before allocation/read |
| trailerByteLength | uint32, exactly 52 unsigned or 116 Ed25519-signed |
| reserved | uint32, must be zero |

Preamble size is 32 bytes. The payload follows immediately. The integrity trailer
begins at offset `32 + payloadByteLength`; the file ends exactly after the trailer.
Checked arithmetic rejects overflow, truncation, trailing bytes and contradictory
lengths. Archive size is derived from this framing/filesystem result, not a mutable
header field later patched during hashing.

Payload is the versioned container's bounded entry table plus exact stored entry
bytes. Entries use stable typed IDs, deterministic order, explicit codec IDs and
stored/decoded lengths, never filesystem extraction paths. The v1 container codec
must reject duplicate IDs, overlap, gaps/unreferenced bytes, unsupported codecs and
unbounded decompression. Metadata encoding is deterministic UTF-8 with stable key
order and duplicate-key rejection. Verification hashes the **stored bytes**, not
JSON reserialized by the reader.

| Logical payload entry | Content |
|---|---|
| header.json | Logical slot ID, `SlotGenerationId`, optional parent generation, producing `ProductSaveCompatibilityVersion`, project/world/account scope, baseSceneAsset and bounded provenance timestamps |
| manifest.json | `SaveSchemaVersion`, `CanonicalStateHash`, StableTypeId-sorted participant/chunk IDs, each `ParticipantSchemaVersion`, required flags, lengths, codecs and per-chunk SHA-256 |
| core Scene participant | Stable authored/spawn entity identities, owned core fields, tombstones, hierarchy and reference remaps |
| subsystem participant entries | One canonical payload per registered owner/StableTypeId, including gameplay services/components |
| slot-player participant | Slot-scoped player state only; no global settings/achievements or duplicated subsystem fields |
| Persistent World participant/cell deltas | Persistent state of active and inactive cells with base dataset revision |
| thumbnail.png | Optional bounded image; source capture/view metadata |

These names describe entry roles, not directories to extract on disk. Header metadata
has no checksum field; manifest has no archiveSignature field. Manifest per-chunk
hashes cover raw decoded **data** entries only, not header/manifest themselves.
`ArchiveContentHash` authenticates all stored metadata and compressed bytes. After
outer verification, bounded decode checks each data entry's raw digest and length.
Every required entry has exactly one manifest record; unknown required data fails.

The trailer is encoded in this exact order:

```text
archiveContentHash  32 bytes
signatureAlgorithm   2 bytes (uint16: 0 None, 1 Ed25519)
signerKeyId         16 bytes (opaque key ID; all zero only for None)
signatureByteLength  2 bytes (uint16: 0 or 64)
signature            signatureByteLength bytes
```

None requires a zero key ID, length 0 and a 52-byte trailer. Ed25519 requires a
nonzero key ID, length 64 and a 116-byte trailer. Unknown/mismatched combinations
fail validation. Ed25519's signature format and verification follow
[RFC 8032](https://www.rfc-editor.org/rfc/rfc8032#section-5.1); use a vetted crypto
implementation, not a custom signing algorithm.

Hash/signature inputs are normative (`||` concatenates bytes; each ASCII tag
includes exactly one terminating zero byte):

```text
ArchiveContentHash = SHA256(
    ASCII("HoroSave.ArchiveContent.v1") || 0x00 || preamble || payload)

signatureMessage =
    ASCII("HoroSave.Signature.v1") || 0x00 ||
    uint16LE(signatureAlgorithm) || signerKeyId ||
    uint16LE(signatureByteLength) || ArchiveContentHash

signature = Ed25519.Sign(trustedHostPrivateKey, signatureMessage)
```

This is Ed25519 over the framed message, not Ed25519ph. `ArchiveContentHash` is the
typed identity of the final immutable preamble/payload bytes; the self-referential
integrity trailer that carries it is excluded. Neither that hash nor the signature
is part of its own input. Choose the signature scheme/length before constructing the
preamble, hash the final preamble/payload once, then append the complete trailer. No
header/manifest/archive bytes are patched after durable publication. Changing slot
generation, timestamps, metadata, chunks or compression requires a new hash/signature
and a new transaction, even when `CanonicalStateHash` is unchanged.

### Trusted Signature Policy

SaveSignaturePolicy is a trusted host/project/namespace configuration with Disabled,
Optional and Required modes. The file cannot choose or weaken it.

- Disabled is explicit local/PIE unsigned-only policy: reject signed input unless
  the host explicitly changes to a verifying policy. It never silently ignores a
  present invalid signature.
- Optional permits unsigned files; every present signature must verify against a
  trusted, scope-authorized key. It offers corruption detection for unsigned files,
  **not** tamper protection or prevention of signature stripping.
- Required rejects missing, stripped, malformed, untrusted or invalid signatures.
  Secure namespaces cannot fall back to Optional based on file contents or error.

The host binds trusted key IDs to project, account/server/world scope and permitted
algorithm. Public keys/trust roots are not supplied by the archive. Signed headers
bind these identities, the logical slot and `SlotGenerationId`; load compares them
to the requested namespace. Required signing failure or unavailable signer fails the
save before publication; ordinary clients hold no server private key and cannot
invent a local unsigned substitute. A trusted asynchronous signer may be injected
with bounded timeout, but no signing network call blocks the simulation thread.

No mode stores credentials/private keys in saves. Digests are not authentication;
a valid signature also does not prevent replay of an old valid save. Titles needing
anti-rollback require separately trusted generation/anti-replay state, not a
timestamp inside the attacker-controlled file. The archive is not encrypted by this
protocol.

## Save Pipeline, Publication And Cancellation

```text
Admit operation and namespace/slot mutation lease
    -> capture coherent immutable snapshot at owner lifecycle safe point
    -> worker serialize/compress, per-chunk hashes and final payload
    -> allocate SlotGenerationId and finalize publication metadata
    -> compute ArchiveContentHash, required/optional signing, append final trailer
    -> write unique sibling temporary file and flush final bytes
    -> enter non-cancellable commit gate
    -> worker AtomicReplace plus required directory/container durability
    -> publish owner catalog/result and cloud-sync intent for exact slot generation
    -> Completed (local durable success)
```

SaveRequest carries user metadata only. The service fills format versions, IDs,
scope, capture revision, chunk hashes, lengths and signature metadata internally.
The storage adapter resolves paths, performs quota checks and reserves peak space
for old file, temporary file and any required recovery copy. Quota estimates do not
replace handling a later disk-full/write error.

Cancellation is cooperative until the worker atomically enters CommitStarted after
its final cancellation check and before replacement. That gate is the practical point
of no return: cancellation arriving afterward returns TooLate and cannot label a
possibly published archive Cancelled. Before the gate, stop supported work, retire
I/O/signing/readback leases and remove only this operation's temporary file once it
is no longer in use. Cleanup failure is reported and swept safely later; it is not
an assumption of immediate removal or physical I/O interruption.

Worker-side AtomicReplace is the final **content publication**, followed only by
required durability synchronization and result delivery. The file is never reopened
to add a signature or thumbnail. The slot lease stays held through publication/outcome
reconciliation. Owner Pump updates an immutable catalog and emits completion records;
it does no rename, fsync or directory I/O.

### Publication Outcomes And Crash Recovery

Distinguish atomic visibility from acknowledged durability. SaveStorageAdapter must
report NotPublished, PublishedDurable or OutcomeUnknown. The current Foundation
DurableFileSystem::AtomicReplace returns `Result<void>` and can report a directory
sync failure after rename; generic failure therefore does **not** prove the old file
is unchanged. The save adapter must conservatively classify that case as unknown
and reconcile under the slot lease; this documentation does not claim the primitive
already exposes richer outcomes.

- Failure proven before publication leaves the previous archive intact; operation
  fails with its cause and eventually cleans its temporary file.
- PublishedDurable yields Committed/Completed. Late cancellation is ignored/reported
  TooLate; cloud upload failure cannot undo the local save.
- OutcomeUnknown yields Failed with commitOutcome Unknown, quarantines further slot
  mutation/sync, and retains a recovery diagnostic/transaction record. Reopen/validate
  the destination under the lease, compare its `SlotGenerationId` and
  `ArchiveContentHash` with old/new expectations and retry durability where supported.
  Do not delete a possibly published archive or blindly retry over it. Report
  old/new/unresolved truthfully.
  Reconciliation has a separate operation/catalog revision; it does not rewrite the
  original immutable Failed/Unknown result into a retroactive success.

A filesystem/process crash before rename leaves the old archive and perhaps a temp;
after rename it may expose old or new according to the qualified durability contract,
never a partly rewritten destination. Startup validates published files and worker-side
sweeps only operation-owned stale temporaries under their namespace/lease rules.
Temporary or migration files are never catalogued as save slots. Cleanup is not run
from a synchronous EnumerateSaveSlots call. No portable crash guarantee is assumed
for an unqualified filesystem/platform container.

Cloud registration occurs only for the final validated local slot generation after
PublishedDurable. The coordinator durably journals the exact address, generation,
parent, archive hash, expected provider revision and retry identity, then pins or
revalidates that archive before transfer. A concurrent later save explicitly
supersedes intent; an older upload cannot be reported as the newer generation.
Startup reconciles the journal rather than blindly replaying a generic FIFO. Completed
means local durability; cloud state is separate and cannot turn local success into
false rollback.

## Transactional Restore

### Prepare Without Mutating The Active Runtime

1. Pin a read lease/version of the source archive. Bound/check the framing and outer
   `ArchiveContentHash`, enforce trusted signature policy, then decode metadata/chunks
   with all length/hash/schema/identity checks. Verify project/account/world scope and
   base AssetId/dataset/package dependencies. Browsing unverified headers shows
   untrusted metadata, not an authenticated playable slot.
2. Migrate only detached staging data if needed. Workers decode the core Scene and
   subsystem canonical participants, SlotPlayerState and persistent world deltas into
   a private PreparedRuntimeBundle. Pin participant registry and AssetRegistry
   revisions/leases.
   Persisted stable IDs are remapped to new runtime identities; old EntityRef and
   streaming epochs are never restored from disk.
3. The owner queues RuntimeSceneService::QueuePreparation through the existing
   scene activation admission path; heavy preparation still runs in background. Each
   required adapter's PrepareRestore stages independent candidate state on its
   declared worker/native role and returns Pending/Prepared or typed failure. Native
   resources obey ADR-012/011 admission and retirement barriers.
   No worker creates a published RuntimeScene identity or mutates the live scene.
4. Prepare the entire commit ticket: candidate Scene storage, gameplay/participant
   roots, slot player state, persistent world ledger, reference remaps and admitted
   lifecycle events/retirement capacity. Required initial cells must reach their
   Ready/Prepared barrier; remaining dormant deltas need not load every world cell.

Each adapter exposes fallible PrepareRestore and a bounded no-fail PublishPrepared
(or equivalent noexcept ownership transfer), plus asynchronous candidate retirement.
An adapter that can only mutate live state through a fallible commit cannot join an
atomic required restore; reject the composition before accepting a load. Optional
participants need an explicit validated absence/substitute policy, not a swallowed
required failure. Account/profile services and unrelated external side effects do
not participate in the slot transaction.

### Commit At One Observable Boundary

The session adapter coordinates a PreparedRuntimeBundle ticket with the existing
Scene lifecycle service; it does not introduce a bypass public activation API.
This is a required extension of the commit integration, not a claim that SCN-001
already supports a composite gameplay restore. The current one-pending-scene-operation
rule remains enforced. The scene candidate must remain behind the shared bundle
commit gate: its own readiness cannot trigger early automatic scene activation while
a required gameplay participant is still Pending. A host lacking that integration
rejects composite restore rather than applying the scene first.

At CommitDeferredLifecycleChanges, under the exclusive owner mutation boundary,
revalidate the operation cancellation gate, expected session/scene incarnation,
source/archive and participant/asset registry revisions, and all Prepared acknowledgements.
A mismatch rejects the candidate before publication. Then publish Scene and all
candidate participant/world/player roots as one unobservable-to-readers transaction.
Publication performs only prevalidated ownership transfers: no allocation, I/O,
blocking wait, arbitrary callback or new recoverable failure. Runtime identity is
created/published by the Scene service at this boundary, not by a worker candidate.

No simulation tick, renderer extraction or observer can see Scene applied but
inventory/quests unapplied. Observers receive SceneRestoredEvent only after the
complete bundle is published. The old bundle is queued for dependency-ordered
retirement; its destructors/jobs/GPU fences do not run as unbounded commit work.
Logical success may precede physical old-resource release, with all leases charged.

Cancellation wins only before the final commit gate. After publication, report
Committed/Completed and TooLate for late cancel. Recoverable preparation failure
retires the candidate and leaves the active bundle unchanged. An unexpected adapter
violation after publication is a host/session fault, not a fictional safe rollback;
contain/fail the session under host policy and retain unsafe-to-free dependencies.
The normal atomicity guarantee relies on validating the no-fail publish contract.

### Host Failure Presentation

RestoreRequest captures a host failure policy, not an engine hardcoded Main Menu.
With a usable active runtime, default failure preserves it and returns diagnostics.
At initial load with no usable runtime, remain in host Loading/NoActiveRuntime and
report failure. A host may explicitly request a named safe-scene transition or UI
recovery flow **after** the failed transaction; that is a separate transition, not
partial restore commit or implicit destruction of a running scene.

## World Streaming And Inactive Cell Persistence

ADR-012 StreamingPartitionAuthority remains the sole residency authority. The session
owns PersistentWorldState, a revisioned persistence ledger, not a second cell loader.
Its keys combine stable world/dataset identity and the full cell tuple
(x,y,z,lod,layerId). Entries store stable entity IDs, spawned records, mutations and
tombstones relative to the cooked base revision. Runtime PartitionEpoch, per-attempt
StreamingGeneration, slots, pointers and GPU/resource handles are not durable IDs.

Before eviction removes the last live copy of dirty cell state, the owning gameplay/
Scene providers publish its delta into that ledger at the registered owner safe
point. This is an explicit edge in ADR-012's retirement DAG while needed Scene handles
are still valid. Failed capture/admission holds retirement and keeps resources
charged; it must not discard unsaved state merely because a cell becomes Unloaded.
Eviction need not durably save a user slot on every cell change, but it must retain
an owned recoverable-in-session representation of the delta.

Active-cell changes, inactive ledger entries and tombstones join the same coherent
SaveCaptureEpoch. An entity/field has one authoritative record at that cut, not two
copies from both live ECS and the ledger. Resident-but-unpublished provider candidates
are not live gameplay state and are not captured as spawned entities. Loading/Failed/
Evicting cells use the last committed delta plus any owner-committed live changes,
never half-decoded candidates or discarded tombstones. Unloaded cells retain their
last state even when no ECS entity is present.

Ledger memory and spill space are bounded. Large dormant deltas may live in immutable
spill chunks written on workers; ledger roots pin exact chunk revisions through the
save. Eviction cannot release its source until the replacement representation is
owned and available. A missing/corrupt referenced spill is a save failure, not an
empty cell. Save archives are self-contained for their persisted deltas and do not
depend on ephemeral spill paths after completion.

Restore stages a new ledger and base dataset revision with the candidate runtime.
Required initial cells use the normal streaming admission/readiness barriers and
apply their saved deltas before activation. Other deltas remain dormant until normal
residency requests load those cells, then apply exactly once per new incarnation.
Fresh partition/cell generations fence old asynchronous work. Cross-cell entities
have explicit persistent or cell ownership and stable relocation records; saving or
evicting a cell cannot duplicate/delete a network/persistent actor based on position.
A base dataset change needs an explicit delta migration or typed incompatibility.

Full-world saves/restore in multiplayer are server-authorized operations. Clients
cannot restore authoritative server state from local slots. Host network admission,
peer quiescence/relevance and resynchronization policy are explicit prerequisites for
a server restore; old replication messages are fenced by the new runtime/session
incarnation. Local cosmetic/client state must not overwrite server gameplay state.

## Archive, Schema And Product Compatibility

The four version axes are independent:

| Version | Authority / use |
|---|---|
| `ArchiveFormatVersion` | Horo-owned envelope, entry table and trailer codec; framing preflight and archive migration |
| `SaveSchemaVersion` | Whole-save canonical root, required roles and cross-participant composition |
| `ParticipantSchemaVersion` | StableTypeId-keyed participant payload codec and independent migration chain, including ECS and slot player state |
| `ProductSaveCompatibilityVersion` | Product release's declared compatibility matrix/policy epoch; never a codec selector |

Base asset/dataset revisions are semantic dependency identities, not schemas.
`engineVersion`, `projectBuildId` and product semantic version are bounded diagnostic
provenance; none determines readability. A newer engine build is not rejected solely
by its build string.

SaveGameManifest contains `SaveSchemaVersion`, `CanonicalStateHash` and a
StableTypeId-sorted list of participant entries with explicit
`ParticipantSchemaVersion`, required flag and chunk IDs. It is not a string-keyed map
of engine-version aliases. Header metadata contains stable project, base-scene,
logical-slot and `SlotGenerationId` values. Display names and timestamps never select
codecs, resolve assets, establish causality or participate in logical-state identity.
The header also records the producing `ProductSaveCompatibilityVersion` as policy
provenance, never as a participant codec selector.

The inert SaveMigrationRegistry has three distinct step kinds:

```cpp
struct ArchiveMigrationStep {
    ArchiveFormatVersion from;
    ArchiveFormatVersion to;
    ArchiveMigrationFn migrate;
};
struct SaveSchemaMigrationStep {
    SaveSchemaVersion from;
    SaveSchemaVersion to;
    SaveSchemaMigrationFn migrate;
};
struct ParticipantMigrationStep {
    StableTypeId participant;
    ParticipantSchemaVersion from;
    ParticipantSchemaVersion to;
    ParticipantMigrationFn migrate;
};
```

Composition validates unique edges, complete deterministic paths, bounded resource
costs and declared cross-participant dependencies, then seals a migration-catalog
identity. Registry functions operate only on staging readers/writers, never on live
ECS, account profiles or source files. Archive migration changes container structure;
save-schema migration changes root composition; participant migration advances only
that participant (for example quests 7 -> 8 independently of inventory 2 -> 5). No
implicit backward schema downgrade is permitted.

Compatibility preflight proceeds through framing/limits, archive version, outer
integrity/signature, save schema, required participant set/schema, then semantic
dependency identities and decoded hashes. Direct load is allowed only when every
required axis is in the release manifest's inclusive direct-readable range. Migration
is allowed only when every required axis is in a declared migration-source range and
the sealed registry has one complete path to current directly readable writer
versions. Unknown optional participants may be skipped only when no required
participant depends on them. Missing/unknown required participants reject.

Any newer archive format, save schema, required participant schema or product
compatibility version outside the declared range fails with a typed unsupported-newer
result before live mutation. Readers do not best-effort interpret forward input.

Verify the original archive under its trusted signature policy **before** migration.
Migrate into operation-owned staging and validate the output. For restore, an in-memory
candidate may use a trusted deterministic migration of an authenticated source; that
does not make newly serialized bytes carry the old signature. A durable upgraded
archive always gets a new `SlotGenerationId`, `ArchiveContentHash`, signature and the
current writer versions, then traverses the same finalize/flush/commit pipeline.
`CanonicalStateHash` remains unchanged only if logical state and semantic dependency
identities remain equivalent. Required signing without an authorized signer forbids
durable replacement; it may still permit host-approved in-memory restore migration.
Never copy the old signature onto changed payload or weaken Required for an upgrade.

Original slots remain unchanged unless explicit user confirmation or captured host
auto-upgrade policy authorizes replacement. Consent binds the source
`SlotGenerationId`, `ArchiveContentHash`, migration plan and destination namespace;
revalidate them under the slot lease before commit so a new save/cloud update is not
overwritten by stale consent. Migration failure/cancellation only retires its staging.
Unique sibling migration temporaries are not save slots and follow the same
lease-aware cleanup/crash reconciliation.

Before product 1.0, each shipped preview declares exact supported ranges and retains
fixtures for every claimed version; compatibility is not inferred across previews.
From product 1.0, every stable release migrates saves from at least the previous two
stable minor release lines and for at least 12 months after each source line's last
release, whichever is longer. Patch releases cannot narrow ranges or remove migration
paths. Later removal requires deprecation in two preceding stable minor release lines,
release notes and a final bridge release. A security-blocked vulnerable decoder may
shorten this only through an explicit release-manifest exception and typed security
diagnostic, never a false corrupt-save result.

Legacy proposed `checksumSha256`/`archiveSignature`, generic archive-revision,
single-`saveFormatVersion` and `player_profile.bin` layouts have no mechanically valid
implicit v1 interpretation. A supported older implementation needs an explicit
versioned reader/migration with bounded verification and declared release policy;
otherwise reject `UnsupportedArchiveVersion`. Never guess whether self-referential
hashing or signature stripping was intended. Account-global data in an old slot is
not restored into the account store; any explicit import is a separate
user-authorized account migration with conflict/merge policy.

## Product, User, Profile And Slot Storage

### Typed namespace hierarchy

`ProductStorageId` is stable product configuration copied into release metadata; it
is never derived from display name, executable/install path, semantic version, branch
or update channel. `EnvironmentStorageId` explicitly separates production,
development, tests and each unique PIE session. A product fork allocates a new product
ID and imports old saves deliberately. Missing/zero identities disable save admission
rather than falling back to a current directory or name.

The namespace owner is a typed variant. A client `UserProfileOwner` combines one
opaque `LocalUserStorageId` with one product-owned `GameProfileId`. A dedicated/headless
`ServerWorldOwner` combines a `ServerStorageOwnerId` with stable world/tenant scope.
The resulting `SaveNamespaceId { product, environment, owner }` is opened by the
application/profile owner and exposed to Runtime Save as a generation-checked
`SaveNamespaceHandle`. Callers cannot construct a handle from raw IDs.

Platform Services owns live platform authentication handles. The game identity/profile
service privately maps a provider-scoped handle to `LocalUserStorageId`; gamertag,
email, display name and raw provider tokens are never directory components. It owns
the per-user profile catalog, active `GameProfileId`, account association and profile
display metadata. Runtime Save owns slot operations, not user sign-in, profile
creation/linking or profile-store state.

When multi-device/cloud persistence is advertised, the backend must provide a stable
opaque authenticated-user scope so the same platform account maps consistently on
each device. If it cannot, cloud persistence is unavailable and local fallback uses
an installation-local partition; mutable user text is never hashed as a substitute.

Platform user capability is explicit:

| Capability | Behavior |
|---|---|
| `SingleLocalUser` | Bind one installation-local user partition. Platform-user selection/switching returns `NotSupported`; multiple product profiles may still be allowed. |
| `CurrentPlatformUser` | Bind only the authenticated current user; sign-out closes the binding and no new user inherits it. |
| `MultiplePlatformUsers` | Select an explicit signed-in handle; each maps to a distinct local partition. |

Guest/offline storage is its own installation-local user partition. Later sign-in does
not merge or re-own it automatically. Cross-user/product profile movement is an
explicit verified import/copy transaction that preserves source data until destination
publication succeeds.

### Slot, category and catalog identity

`SaveGameSlotId` is an opaque nonzero UUID scoped to its namespace and remains stable
across overwrites. ADR-112's `SlotGenerationId` instead identifies one durable
publication to that slot. `SaveAddress { namespaceHandle, slot }` is the only runtime,
UI and cloud-coordinator address; it contains no path or label.

Manual, Quick, Auto, Checkpoint and registered product `SaveCategoryId` values are
catalog policy controlling capacity, rotation, overwrite and presentation. Quicksave
aliases and autosave rings map to slot IDs. Category, display name, list index and
timestamp never identify a slot or form a filename. Reclassification changes catalog
metadata transactionally without changing slot identity or archive bytes.

The storage authority owns one immutable revisioned catalog projection per open
namespace. Records include address, category, bounded presentation metadata, current
generation/content/state identities and lifecycle state. UI retains addresses and
expected catalog revisions; it never loads/deletes by label, timestamp or row index.

### Physical mapping and safety

Platform Abstraction resolves a product state root for the validated
`ProductStorageId`. SaveStorageAdapter alone maps
`SaveNamespaceId + SaveGameSlotId` to storage. The conceptual filesystem shape is:

```text
<product-state-root>/
  <environment-id>/
    <owner-id>/
      catalog
      slots/<save-slot-id>.horosave
      staging/<owned-operation-id>.temporary
```

Every component uses a fixed lossless filesystem-safe encoding of its opaque identity.
Free-form product/user/profile/category/slot labels are never path components. Secure
console containers may map the same typed keys to records without exposing paths.

The adapter enforces containment using safe handle-relative/no-follow platform
operations, not a string prefix test. Reject symlink/reparse redirection, traversal,
alternate-stream/device/path separators and unexpected file types. Never follow entry
names or embedded paths from the archive into the filesystem. Authorized imports copy
untrusted input through bounded verification into the destination namespace.

Mutations acquire a namespace/slot lease covering final source/destination checks and
publication; other processes/cloud coordination use the same locking/generation
protocol. Credentials, scope authority and lock paths come from trusted composition,
not archive metadata. Cleanup only targets owned temporary records under a valid
lease; a wildcard sweep must not unlink another process's active work. Catalog,
delete and import apply the same typed-ID, containment and generation checks.

Foundation's path-based DurableFileSystem primitives are building blocks, not proof
that no-follow/containment/outcome guarantees exist. SaveStorageAdapter must implement
and qualify them before production saves are enabled. Profile/account storage, cloud
retry journals, editor recovery, authored projects and PIE sandboxes remain distinct
sibling/virtual namespaces with separate schemas and mutation leases.

### Profile-switch transaction

The application/session owner closes old save/load/delete/import/cloud-apply admission,
cancels pre-commit work, boundedly settles or retains ownership for post-commit work,
stops old cloud scheduling, releases catalog/slot leases and invalidates the old
namespace handle generation. It then binds the selected user/profile, validates the
product/environment, opens its catalog, publishes a new snapshot and reopens admission.

Operations never change namespace in flight. Completion captures the old handle
generation and can report/finalize only against that authority, never update the new
profile's catalog/UI. A local save already past commit may finish in the old namespace
but cannot upload under the new platform user. Sign-out uses the same close path.
Failure to open the new catalog yields explicit `NoActiveProfile`; rollback/rebind is
a fresh transaction, not a half-old/half-new binding.

## Host Environments, Account State And Cloud

| Host | Required behavior |
|---|---|
| PIE | Unique `EnvironmentStorageId` sandbox or bounded virtual store; never writes production slots, account profile or canonical .horo documents |
| Development/test | Explicit non-production product/environment partition; sharing requires reviewed product policy, never path/name coincidence |
| Packaged standalone | Validated product environment plus installation/platform-user and active game-profile namespace; manual/quicksave/rotating autosave catalog |
| Dedicated/headless | Server world namespace and authoritative gameplay state; no viewport, client prediction, HUD or client account preferences |

The host decides signature policy from its trust needs; being a server alone is not
an algorithm for choosing keys. Secure signed namespaces require Required. PIE
unsigned policy is isolated and cannot be carried into a production namespace by
renaming/importing files. Headless saves omit thumbnails and graphics dependencies.
Stopping PIE cancels before commit where possible and retires sandbox I/O; cleanup
waits for ownership release, never erases production storage.

Slot load does not restore audio volume, accessibility settings, account achievements
or account-wide stats. Account changes triggered by gameplay are independent typed
profile operations. A successful save/load is not a transaction over external platform
achievement/cloud-account APIs. Account policy may merge monotonic progression, but
that is never an automatic rewind from slot_player_state.bin.

Catalog UI consumes immutable records keyed by `SaveAddress`; no filesystem path or
raw platform handle crosses the surface. The save/cloud coordinator derives a bounded
`CloudSaveObjectKey` from the typed address and authenticated provider context.
Platform Services treats that key and finalized archive bytes as opaque and cannot
edit local storage. The application/profile-owned `CloudSaveCoordinator` is the one
sync state authority; Platform Services and UI are transport/presentation adapters.

Automatic remote mutation requires provider-enforced conditional revision. A read
followed by unconditional write, a process-local mutex, advisory lease or provider
timestamp is insufficient. An uncoordinated blob backend disables background
upload/delete while local save/load remains functional.
The provider's opaque revision is only a compare-and-swap token; it is not
`SlotGenerationId`, ordered gameplay state or portable archive identity.

On startup/reconnect the coordinator opens local state first, then reconciles verified
local and remote heads. Same generation/hash is InSync. A known remote ancestor of
local permits conditional upload; a known local ancestor of remote permits verified
download and local durable application preserving that remote generation. Same
generation/different hash is an integrity violation. Unknown ancestry, absent data
without verified deletion evidence, or two non-ancestor complete heads is Conflict.
`CanonicalStateHash` may report logical equivalence but cannot authorize overwrite.
Provider/device timestamps and play duration remain presentation/provenance only.

Downloaded data is untrusted until bounds, content hash, signature/scope and
compatibility checks pass. It enters local storage only through a slot lease, expected
generation recheck and atomic durable transaction; Platform Services never edits a
local file. Failed verification/publication leaves the previous local generation.

Divergent complete generations are never field/record merged and never resolved by
automatic last-write-wins. Before KeepLocal or KeepRemote can replace anything, both
verified byte sets must be durable: the active local slot plus a conflict recovery
record/blob carrying exact archive identity and provenance. KeepBoth may import one as
a newly finalized slot publication when scope/signing policy permits. Quota/retention
failure blocks destructive resolution instead of discarding the losing archive.

Conflict UI or headless host policy sees immutable coordinator snapshots and submits
a typed KeepLocal, KeepRemote, KeepBoth or Defer command capturing local generation/
hash and remote provider revision/generation/hash. The coordinator revalidates local
lease and provider CAS before applying it; stale input returns to reconciliation.
Closing UI changes no state. Signed slot identity is never rewritten merely to fit a
conflict filename.

Offline saves advance local lineage and retain bounded visible retry state. Profile
switch/sign-out closes old scheduling so intent never replays under a new provider
user. Required-signature policy also applies to downloads and migration; cloud
availability cannot authorize unsigned downgrade. Remote absence is not automatic
delete authority until a synchronized tombstone contract exists.

## Failure, Cancellation And Shutdown Summary

All failures follow ADR-008 Result/Error, retaining operation, `SaveAddress`, source
`SlotGenerationId`/`ArchiveContentHash` when available and nested asset/provider/
filesystem cause. Admission errors are distinct from later operation outcomes.

| Failure | Observable result |
|---|---|
| Busy / invalid slot / invalid scope | Immediate typed rejection; no admitted mutation |
| Capture budget/coherence failure | Defer under bounded policy or Failed; no live ECS freeze |
| Write/full/signing failure before commit | Failed/NotCommitted; previous archive intact; owned temp eventually retired |
| AtomicReplace or durability outcome uncertain | Failed/Unknown; reconcile/quarantine slot, no cloud publication or blind retry |
| Corrupt archive-content hash/chunk / missing or invalid required signature | Failed before restore staging/publication |
| Missing asset / incompatible schema / required participant failure | Retire candidate; active runtime preserved |
| Stale scene/registry/archive before commit | Reject candidate; never publish into a replacement incarnation |
| Cancellation before commit gate | Cancelled/NotCommitted after owned work reaches safe terminal cleanup |
| Cancellation after commit gate | TooLate; report actual commit success/failure/unknown outcome |
| Shutdown deadline exceeded | ShutdownIncomplete; retain outstanding dependency ownership safely |

BeginShutdown closes admission, invalidates pending capture/restore tickets and
requests cooperative cancellation. Worker I/O/signers and renderer readbacks retain
operation storage/module/asset leases until completion. Only ADR-010-permitted bounded
teardown drains may wait. Timeout returns ShutdownIncomplete with outstanding owners
and retained bytes; a retirement owner keeps all referenced dependencies alive. No
raw callback reaches a destroyed session and no worker is detached from freed state.
A commit already past its gate finishes or reports Unknown and leaves recovery data;
shutdown must not relabel it Cancelled or discard a potentially committed archive.

## Qualification And Implementation Status

The following are required implementation tests, not test suites delivered by this
documentation change:

- Authoring/recovery/workspace/account isolation, PIE namespace separation and
  headless exclusion of UI/client profile data.
- Product/environment/user/profile collision tests, all three platform-user
  capabilities, guest import, sign-out and restart with no display name/path identity.
- Profile-switch races before/after commit, stale completion fencing, catalog-open
  failure, production/development/PIE/server separation and UI/cloud path opacity.
- Duplicate/renamed display names, category reclassification, quicksave/autosave
  aliases and stable slot identity across overwrite.
- Capture one tick across Scene/subsystem adapters/world deltas; resume live mutation immediately
  after the safe point; race scene replacement, COW exhaustion and stale readbacks.
- Byte fixtures for the 32-byte preamble, unsigned/signed trailer lengths, exact hash
  ranges and signature message. Tamper header, lengths, compressed bytes, raw chunks,
  trailer, algorithm/key ID and trailing data; verify no self-reference or post-commit edit.
- Cross-platform/compiler golden canonical-state vectors, map ordering, negative zero,
  non-finite policy and proof that timestamps/display/compression/generation do not
  change `CanonicalStateHash` while covered archive-byte changes do change
  `ArchiveContentHash`.
- Required signature stripping, wrong project/account/world, untrusted key, signer
  timeout, malformed inputs, decompression bombs and unsupported schema versions.
- Snapshot admission versus eventual worker failure, per-operation sticky outcomes,
  cancellation races on both commit gates and no UI-thread filesystem calls.
- Disk-full/flush/rename/directory-sync fault injection, especially rename-success plus
  sync-failure; process-crash recovery and no false old-file-preserved guarantee.
- Restore all required participants from isolated candidates; fail each prepare stage;
  prove no observer sees partial Scene/inventory/quest publication. Validate no-fail
  commit and asynchronous retirement under native fence delay.
- Save/restore unloaded/resident/evicting cells, dropped-item/tombstone/cross-cell cases,
  bounded spill chunks, stale generations and changed base dataset revisions.
- Independent archive/save/participant migration paths, minimum support horizon,
  current/oldest/one-newer fixtures, stale consent and signed migration without
  signing authority; unchanged source on pre-publication failure.
- Repeated equivalent-state publications with distinct `SlotGenerationId` values,
  replica preservation, parent-generation compare-and-swap and timestamp skew.
- Slot ID/path attack cases, symlink/reparse races, active-temp cleanup exclusion,
  other-process mutation, named-slot import and signed conflict-copy identity mapping.
- Busy autosave coalescing, bounded retry exhaustion, rotation only after success,
  cloud conditional upload/download conflicts, offline lineage branches, clock skew,
  stale retries and crashes at journal/provider/local-publication boundaries.
- Same/ancestor/divergent/unknown/same-generation-different-hash classification;
  KeepLocal/KeepRemote/KeepBoth/Defer preserve both verified conflict archives and do
  not perform semantic record merge.
- Shutdown while a save is past commit, while candidates hold modules/assets, and
  while optional GPU thumbnails are pending; no early free or silent incomplete unload.

The v1 container's entry-table codec, participant/session commit integration, secure
storage adapter and platform-specific durability qualification are implementation
work under these contracts. They must be completed before enabling runtime saves;
this specification does not claim that current Foundation/Scene APIs alone implement
all guarantees. Any public API/format implementation must include its own migration
and regression coverage.

## Related Documents

- [Scene Runtime](./scene-runtime.md): Candidate preparation, runtime identities and lifecycle commit.
- [Runtime Lifecycle](./runtime-lifecycle.md): Host frame phases and owner boundaries.
- [World Streaming](./world-streaming-architecture.md): Residency, generation fences and provider retirement.
- [VFX And Particles](./vfx-and-particles-architecture.md): Deferred renderer resource lifetime.
- [Editor Document Model](../editor/editor-document-model.md): Authoring and recovery authority.
- [Project Model](../editor/project-model.md): Project/workspace persistence.
- [Platform Services](./platform-services-architecture.md): User namespaces and cloud adapters.
- [Gameplay Module Boundary](../extensions/gameplay-module-boundary.md): Versioned provider contracts.
- [Concurrency And Jobs](../foundation/concurrency-and-jobs.md): Worker/owner completion ownership.
- [ADR-010](../../adr/010-job-waiting-and-operation-store-ownership.md): OperationStore and non-blocking work.
- [ADR-012](../../adr/012-world-streaming-partition-authority-and-subsystem-boundaries.md): Cell authority and barriers.
- [ADR-008](../../adr/008-error-model-exception-boundary-and-registry.md): Typed error propagation.
- [ADR-112](../../adr/112-save-archive-container-and-compatibility-policy.md): Portable container, canonical state, version axes, identities and compatibility horizon.
- [ADR-113](../../adr/113-local-storage-user-profile-and-slot-ownership.md): Product/user/profile namespaces, logical slot addressing and physical storage ownership.
- [ADR-114](../../adr/114-canonical-runtime-world-persistence-boundary.md): Authoring/runtime composition, state classification and subsystem-owned canonical adapters.
- [ADR-115](../../adr/115-cloud-save-authority-revision-and-conflict-policy.md): Local/cloud authority, provider CAS revisions, offline lineage and conflict preservation.
- [Application Security](../security/application-security.md): Trust and untrusted-input boundaries.
