# Terrain Descriptor Migration

Use `TerrainConfigurationSnapshot` as the immutable project/host capture for shared
Terrain and Foliage validation. Construct it from explicit configuration and capability
revisions, an exact `TerrainFeatureTier`, and finite limits no wider than the canonical
version-1 profile returned by `GetTerrainTierProfile`. A tier name does not install or
grant a renderer, Physics, Navigation, streaming, authoring, or runtime-mutation
capability.

Use `TerrainDatasetDescriptor` for stable dataset/content identity, revisioned inclusive
integer-millimetre bounds, regular-grid shape, and the complete combined Terrain/Foliage
peak footprint. Validate this descriptor before allocating decoded storage, reserving
provider work, or publishing a candidate. Invalid dimensions, unordered bounds,
incoherent foliage counts, and one-over-limit counts/bytes/work fail transactionally;
the validator never clamps or drops content.

At owner admission, call `ValidateTerrainDescriptorAdmission` with a freshly captured
context. First publication uses `Insert` and carries no current state. Replacement uses
`Replace`, supplies the exact expected current content revision, and publishes only its
non-wrapping successor. If bounds are unchanged, retain the bounds revision. If any
axis changes, advance the bounds revision by exactly one. Stale configuration,
capability, content, or bounds revisions and non-Active lifecycle states reject before
work.

`ResolveTerrainFeatureTier` is intentionally exact: a request for `High` fails when only
`Baseline` is supported. Product/application policy may explicitly construct a new
configuration for another tier, but runtime admission never searches lower tiers or
silently substitutes a provider. These public values contain no native handles,
callbacks, mutable owner pointers, registries, or backend selection logic.
