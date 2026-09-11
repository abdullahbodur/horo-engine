# World Spatial Object Descriptor Migration

`WorldSpatialObjectDescriptor` is the canonical inert value for authored object
identity, source asset, bounds, revision and placement class. Code that previously
carried these fields in unrelated structs should construct this descriptor at the
world-authoring boundary and validate it before spatial cook input is published.

The stable object identity is `WorldAuthoringObjectAddress`; it was moved from
`WorldSpatialAssignment.h` to `WorldSpatialObjectDescriptor.h`. Existing source that
includes `WorldSpatialAssignment.h` remains source-compatible because that header
includes the new owner header. Consumers that use only the address should include
the new header directly.

`sourceAsset` is not interchangeable with `address.page`: the former names reusable
content while the latter names the source-controlled authoring page containing the
stable local object ID. Bounds are inclusive canonical integer millimeters, never
rebased floats or live Scene-object pointers.

Use `ValidateWorldSpatialObjectAdmission` with an immutable owner snapshot before
publishing a descriptor. Inserts omit `expectedRevision`; replacements provide the
current revision and an exact non-wrapping successor. The function has no ambient
registration or mutation side effects. Always-present placement classifies authored
metadata only; WST-001.7 owns residency and runtime-spawned handoff behavior.
