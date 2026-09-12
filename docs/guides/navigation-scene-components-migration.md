# Navigation Scene Components Migration

Scene navigation authoring now uses `NavigationSurfaceComponent` and
`NavigationRegionComponent` from `Horo/Runtime/Scene/NavigationSceneComponents.h`.
Importers must assign non-zero stable surface and region identities instead of
deriving identity from object order, names, paths, entity slots, or pointers.

Surface components reference the exact navigation-definition `AssetId`, one or
more grounded profile identities, a version/generation, and either the owning
object subtree or explicit finite local bounds. Region components reference an
existing surface identity and declare finite local bounds, include/exclude mode,
and explicit-contributor or bounded-static-collision source selection. Provider
handles, generated polygons, and backend flags are not valid authored fields.

All mutations go through `SetSceneNavigationSurfaceCommand` or
`SetSceneNavigationRegionCommand`. These commands validate the complete committed
Scene identity/reference set before changing history. Removing a referenced
surface therefore fails until its regions are removed or explicitly retargeted.
Object duplication regenerates component identities and retargets a co-located
region that referenced the duplicated surface. Prefab instances retain their
source-owned component identities until the existing pinned prefab-resolution
step materializes a containing-Scene snapshot; overrides must then use the same
typed commands. Nested instances do not inherit or discover ambient surfaces.

Persistence stores these typed values under `navigationSurface` and
`navigationRegion`. Runtime conversion copies only enabled values from one
committed `DocumentStateId`, which becomes the `SceneDefinitionRevision`; it never
reads editor previews or an in-progress modal/property draft.
