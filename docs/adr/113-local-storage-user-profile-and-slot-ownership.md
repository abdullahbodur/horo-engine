# ADR-113: Local Storage, User Profile and Slot Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Product and environment storage identity, platform-user partitions, game-profile ownership, save categories, logical slot addressing, physical storage mapping, multi-user fallback, profile switching and cloud/UI boundaries
- **Issue**: [SAV-003.1](https://github.com/abdullahbodur/horo-engine/issues/1425)
- **Jira**: [HORO-1425](https://horo-engine.atlassian.net/browse/HORO-1425)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-112](112-save-archive-container-and-compatibility-policy.md)
- **Normative documents**: [Save Game And Persistence](../architecture/runtime/save-game-and-persistence.md), [Platform Services](../architecture/runtime/platform-services-architecture.md), [Platform Abstraction](../architecture/foundation/platform-abstraction.md)

## Context

The runtime save service owns capture/restore, ADR-112 separates logical state,
archive content and publication generation, and the storage adapter owns safe durable
file operations. The remaining namespace language still combines project, account,
user and environment informally. The platform-services cloud example also represents
a slot as a free-form string.

Display product names, executable paths, platform gamertags and save labels are not
stable or safe storage keys. Platform backends vary from no user concept, through one
current user, to several concurrently signed-in users. PIE, development and packaged
production builds must not discover each other's save files by accident. UI and cloud
sync need stable addressing, but neither should receive a host path.

A game profile is also not the platform account itself. One platform user may own
multiple game profiles, an offline installation may have no platform identity, and
account linking/import must not silently retarget existing slots. The ownership and
switching transaction must be fixed before slot lifecycle and cloud synchronization
build on it.

## Decision

### 1. Save addressing is a typed hierarchy

The domain uses distinct nonzero opaque identities:

```cpp
struct ProductStorageId { Uuid value; };
struct EnvironmentStorageId { Uuid value; };
struct LocalUserStorageId { Uuid value; };
struct GameProfileId { Uuid value; };
struct ServerStorageOwnerId { Uuid value; };
struct SaveGameSlotId { Uuid value; };

using SaveOwnerId = Variant<UserProfileOwner, ServerWorldOwner>;

struct SaveNamespaceId {
    ProductStorageId product;
    EnvironmentStorageId environment;
    SaveOwnerId owner;
};

struct SaveAddress {
    SaveNamespaceHandle namespaceHandle;
    SaveGameSlotId slot;
};
```

`UserProfileOwner` contains one `LocalUserStorageId` and one `GameProfileId`.
`ServerWorldOwner` contains one `ServerStorageOwnerId` and stable world/tenant scope.
The variant prevents a dedicated-server world from masquerading as a client profile.

`SaveNamespaceHandle` is a generation-checked runtime capability obtained from the
active profile/session binding. Callers do not construct it from IDs. `SaveAddress`
is sufficient for Runtime Save, catalog UI and cloud-sync coordination; it contains
no path, filename, display label, platform SDK handle or personally identifiable
value.

IDs are scoped by their parent tuple. Implementations may allocate UUIDs globally,
but correctness does not assume a profile or slot ID is unique outside its product,
environment and owner namespace.

### 2. Product and environment identities are configuration authority

`ProductStorageId` is assigned once in product/project configuration and copied into
release manifests. Product display name, publisher text, bundle/executable name,
install directory, semantic version, branch and update channel do not derive or
change it. A deliberate product fork allocates a new ID and uses explicit import if
old saves are supported; copying a project directory does not silently create a new
storage identity.

`EnvironmentStorageId` partitions storage policy. Packaged production releases that
are intended to share saves across updates use the same production environment ID.
Developer standalone, automated tests, editor preview and PIE use explicitly distinct
IDs. Each PIE session receives an ephemeral unique environment ID. A staging/channel
build shares production storage only through an explicit product policy reviewed as a
compatibility/security choice, never because executable names match.

The application composition root validates both identities before constructing save
or profile services. Missing, zero or duplicated product/environment configuration
fails save capability admission; no fallback uses the current directory or product
name.

### 3. Platform users map to private local storage partitions

Platform Services owns the live `PlatformUserHandle` and authentication session. A
game identity/profile service maps that handle, within the selected platform backend,
to a persistent opaque `LocalUserStorageId`. It stores no gamertag, email, display
name or raw provider token in directory names. A handle from one backend/provider
namespace is never compared directly with another backend's handle.

A backend claiming multi-device/cloud persistence provides a stable opaque
authenticated-user scope so the same provider account maps consistently on each
device. If it cannot, cloud save is unavailable and local fallback uses an
installation-local identity. Hashing a mutable gamertag or email is never a fallback.

The platform adapter reports one of these capabilities:

| Capability | Required behavior |
|---|---|
| `SingleLocalUser` | No stable platform-user selection exists. Bind one installation-local user partition; platform-user switching returns `NotSupported`. |
| `CurrentPlatformUser` | Exactly the authenticated/current platform user may be bound. Sign-out closes that binding; another user cannot inherit it. |
| `MultiplePlatformUsers` | The host may select among explicit signed-in handles; every selected handle maps to a distinct local partition. |

Lack of multi-user capability never means all platform users share a guessed global
directory. An offline/guest partition is a distinct installation-local identity. A
later sign-in does not merge, rename or re-own guest data automatically; the product
may offer an explicit verified import/copy transaction.

If a platform cannot provide a stable user mapping across launches, it advertises
`SingleLocalUser` or an unavailable persistence capability. It must not hash a mutable
display name and claim durable multi-user isolation.

### 4. The profile service owns game profiles, not save files

Within one local-user partition, the game profile service owns the profile catalog,
`GameProfileId`, display metadata, account association and which profile is active.
It may support one default profile or multiple profiles according to product policy.
Platform Services does not create/select a game profile, and Runtime Save does not
own account linking or profile metadata.

Profile/account data such as settings, accessibility, achievements and account-wide
statistics is stored through the profile authority in a separate schema and
transaction domain. Runtime slot state may reference its owning profile namespace but
loading an old slot cannot roll back the profile store. Deleting a slot does not
delete a profile; deleting a profile is a higher-level confirmed operation that first
closes its namespace and applies retention/cloud policy.

Moving/copying a profile across local users or products is explicit migration/import.
It validates destination ownership, archive scope and compatibility, allocates new
destination slot publications where required, and preserves source data until the
transaction succeeds. Rebinding the same IDs or renaming directories is forbidden.

### 5. Save category and display metadata do not identify a slot

`SaveGameSlotId` is the logical slot identity. It is unrelated to
`SlotGenerationId`: the former survives overwrites, while ADR-112 allocates the latter
for each durable publication. A slot catalog record contains its typed ID, category,
bounded display metadata, latest generation/content/state identities and lifecycle
state.

`SaveCategoryId` is typed policy metadata such as Manual, Quick, Auto, Checkpoint or a
registered product category. It controls capacity, rotation, overwrite and UI
presentation. A quicksave alias or autosave ring maps to slot IDs in the catalog;
neither is a filename. Reclassification changes catalog metadata transactionally and
does not change slot identity or require a new archive publication.

Display names are bounded UTF-8 user metadata and may be duplicated, localized or
changed. UI selection always retains `SaveAddress`; it never loads/deletes by row
index, label or timestamp.

### 6. Only the storage adapter maps namespaces to physical storage

The platform abstraction resolves an application state root for the validated
`ProductStorageId`. `SaveStorageAdapter` exclusively maps
`SaveNamespaceId + SaveGameSlotId` to storage. On ordinary filesystems the conceptual
shape is:

```text
<product-state-root>/
  <environment-id>/
    <owner-id>/
      catalog
      slots/<save-slot-id>.horosave
      staging/<owned-operation-id>.temporary
```

Every component uses a fixed lossless filesystem-safe encoding of its opaque ID.
Free-form product/user/profile/category/slot labels are never path components. The
adapter validates containment and no-follow semantics and owns catalog publication,
per-slot leases, temporary naming, atomic replacement, durability reconciliation and
cleanup. Secure consoles may map the same typed keys to container records without
exposing any path.

The profile/account store, cloud retry journal, editor recovery, authored project and
PIE sandbox are separate sibling/virtual namespaces with separate owners and schemas.
They cannot share a live catalog or mutation lease merely because they use the same
platform root.

### 7. User/profile switching is an owner transaction

The application/session owner performs a switch in this order:

1. close new save, load, delete, import and cloud-apply admission for the old binding;
2. request cooperative cancellation for pre-commit work and boundedly settle or
   retain ownership for work already beyond its commit gate;
3. stop cloud scheduling for the old namespace, release catalog/slot leases and
   invalidate its `SaveNamespaceHandle` generation;
4. bind the selected platform user and game profile, validate product/environment,
   open its catalog and publish a new immutable catalog snapshot; then
5. reopen admission and cloud scheduling for the new binding.

No operation changes namespace in flight. Completions capture the old namespace
generation and may finalize/report only against that authority; they cannot update the
new user's UI/catalog. Sign-out follows the same close path. A post-commit local save
may finish in the old namespace but cannot be uploaded under the new platform user.

Failure opening the new profile leaves save capability in an explicit NoActiveProfile
state. The host may rebind the old profile through a new transaction; it does not keep
half of each binding active. UI surface closure never switches or destroys a profile.

### 8. UI and cloud consume typed projections, not filesystem knowledge

Catalog UI receives immutable bounded records keyed by `SaveAddress` and submits
typed commands with expected namespace/catalog revisions. It receives presentation
metadata and typed status, never local paths, raw platform handles or mutable catalog
objects.

The save/cloud coordinator derives a bounded `CloudSaveObjectKey` from the typed
product/environment/profile/slot address and authenticated provider-user context;
installation-local user IDs and host paths are not serialized into it. Platform
Services treats that key and finalized archive as opaque. Gameplay/UI cannot submit
arbitrary strings as cloud object keys, and the backend cannot resolve or edit a local
path. Replicas retain the archive's logical slot/generation identity; importing into
a different typed namespace is a Runtime Save operation, not a provider rename.

This decision freezes addressing and ownership only. The cloud authority, provider
revision/write preconditions, offline state and divergent-generation resolution are
owned by SAV-006. Provider timestamps remain presentation/provenance and are not an
automatic selection rule.

### 9. Qualification proves isolation and switching

Required evidence includes:

- two products with identical display/executable names, two environment IDs and two
  local users with identical display names never collide;
- `SingleLocalUser`, current-user and multi-user capabilities, sign-out/restart,
  unavailable stable identity and guest-to-signed-in import behavior;
- multiple profiles per user, duplicate/renamed display labels, category changes,
  quicksave/autosave aliases and slot overwrites without identity confusion;
- switch races before and after the commit gate, stale async completions, catalog
  open failure, cloud retry suspension and no cross-user UI publication;
- traversal, symlink/reparse, case, non-ASCII metadata and malformed/duplicate catalog
  attacks without using labels as paths;
- production/development/PIE/test/server isolation and explicit approved sharing; and
- UI/cloud operations using typed addresses on filesystem and secure-container test
  adapters with no path exposure.

## Consequences

### Positive

- Product, user, profile and environment boundaries prevent accidental storage
  collision.
- Platforms without multi-user support have explicit safe behavior.
- Slot/category/display/publication concepts are no longer conflated.
- UI and cloud sync remain portable across filesystem and secure-container backends.

### Costs

- Product configuration needs stable storage IDs and fork/import tooling.
- Identity/profile services need persistent platform-user mapping and switch fencing.
- Catalog and cloud adapters must translate typed addresses without leaking paths.

## Rejected Alternatives

### Build paths from product, user or save display names

Rejected because names are mutable, non-unique, locale-sensitive and unsafe as
cross-platform path identity.

### Treat the current platform user as the game profile

Rejected because offline platforms, multiple game profiles, account linking and
dedicated servers have different ownership and lifetime.

### Use quicksave/manual/autosave names as slot IDs

Rejected because category and presentation change independently while a logical slot
must survive overwrites and retain generation lineage.

### Give UI or cloud backends local save paths

Rejected because it bypasses namespace validation, leases, secure containers and the
single storage authority.
