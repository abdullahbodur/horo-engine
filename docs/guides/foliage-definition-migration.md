# Foliage Definition Migration

TRF-004.2 replaces loose foliage mesh, placement, distance, wind and collision fields
with one validated `Horo::Terrain::FoliageTypeDefinitionData` value.

## Required migration

1. Derive and persist a stable `FoliageTypeId`; do not use an asset path, array index or
   runtime handle as the definition identity.
2. Convert mesh LOD, material and optional impostor references to their distinct stable
   `FoliageMeshAssetId`, `FoliageMaterialAssetId` and `FoliageImpostorAssetId` domains.
   Populate only the leading fixed LOD entries and keep all unused entries zero.
3. Express placement in canonical integer units: millimetres, milli-degrees and
   instances per square kilometre. Preserve an authored seed and version-1 algorithm.
4. Express LOD/impostor/cull thresholds in strictly increasing millimetres. Select
   `CpuDirect` or `GpuIndirect` explicitly; validation never switches recipes.
5. Zero every wind value for `None`, or provide the complete fixed-point selected model.
   Zero every collision value/flag for visual-only foliage.
6. Validate through `FoliageTypeDefinition::Create` against the exact immutable Terrain
   configuration and capability set before cooking or publication.
7. Insert without current state. Replace with the exact current definition revision and
   its non-wrapping successor; never mutate an admitted definition in place.

Definitions that exceed tier limits or require unavailable impostor, wind, collision,
navigation or culling capabilities now fail with a typed result. Products that need a
different representation must author and select an explicit compatible definition; a
silent runtime downgrade is not supported.
