# Character World Manager Migration

## Purpose

`Horo/Physics/CharacterWorld.h` replaces ad hoc ownership of inert Character
controller descriptors with one bounded manager per active scene generation. This
is a new runtime contract; there is no legacy controller owner to preserve in
parallel.

## Host migration

During aggregate scene candidate preparation:

1. Capture and validate one immutable `CharacterWorldSettings` snapshot.
2. Supply the exact scene, paired `PhysicsWorldId`, collision-filter, and local-origin
   generations in a `CharacterWorldPreparationDescriptor`. The manager issues a
   never-reused process-local `CharacterWorldId`; callers cannot select or recycle it.
3. Call `CharacterWorld::Prepare`. This allocates the complete controller slot table
   and returns an unpublished candidate with its completed owner descriptor.
4. Create candidate controllers with descriptors bound to the same three owner
   generations. A failure leaves existing slots and generations unchanged.
5. Call `Activate` only when the Scene, Physics, and Character candidates can be
   published together without further allocation.

On unload or failed aggregate publication, call `Shutdown` before retiring the
paired Physics world. Shutdown is idempotent and drains every owned controller
record. Destruction remains a final safety net.

## Handle migration

Store `CharacterControllerHandle` only as process-local runtime binding state.
Resolve persistent authored identity again after reload or world replacement.
Destroying and reusing a slot increments its generation, so all previous handles
remain stale. A slot at the generation ceiling is permanently retired; replace the
Character world when all slots report `CharacterErrors::GenerationExhausted`.

`ControllerDescriptor` returns an owned copy rather than a pointer into slot
storage. Controller creation and destruction are admitted only on the preparation
thread while the world remains unpublished. Active-world changes are rejected
until the tick-addressed safe-point command ingestion introduced by CHR-001.4 is
available.

Production hosts inject `PhysicsSceneActivationParticipant` into
`RuntimeSceneService`. The aggregate service prepares detached paired Physics and
Character worlds, preserves the old bundle when either participant preparation or
activation fails, and shuts Character down before Physics during replacement,
unload, and host shutdown.
