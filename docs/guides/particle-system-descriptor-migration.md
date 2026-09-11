# Particle-System Descriptor Migration

Particle authoring and import code must use the typed descriptor boundary instead of
passing permissive JSON, ad-hoc maps or backend-native render and collision values into
runtime code.

1. Persist the exact `ParticleDescriptorSchemaVersion`, stable `EmitterId` and
   canonical material `AssetId`. Numeric enum values and native renderer or physics
   handles are not source contracts.
2. Call `ParseParticleSystemDescriptor` with project-lowerable limits. Treat malformed,
   duplicate, oversized and unsupported-version results as typed operation failures;
   do not retry with a permissive parser.
3. Build one immutable error registry containing all particle validation descriptors,
   then call `ValidateParticleSystemDescriptor`. Present every returned diagnostic.
   Only the optional admitted descriptor may cross into cook planning.
4. Capture material evidence from the exact immutable asset snapshot owned by the cook
   composition boundary. Pass that evidence and an explicit canonical
   `ParticleCookProfile` to `BuildParticleSystemCookPlan`.
5. Publish cooked output only when the returned validation is accepted. Missing,
   mistyped or unloadable materials, tier overflow and incompatible policies are
   terminal typed cook findings; no implicit material, tier or execution-domain
   fallback is permitted.

Existing sources without `schemaVersion`, stable identities, finite bounds or an
explicit kill condition require an offline migration before import. Version 1.0 is the
only directly readable schema; future migrations must preserve the original source
until the new candidate validates and cooks successfully.
