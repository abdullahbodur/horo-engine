# Perception Descriptor Registry Migration

Gameplay perception extensions now register stable typed descriptors before
`SceneRuntime` activation. Migrate ad hoc name-keyed sense and stimulus tables as
follows:

1. Issue non-zero `SenseTypeId`, `StimulusTypeId`, and
   `PerceptionListenerTypeId` values from the owning content or module namespace.
   Preserve those values across display-name changes and serialization versions.
2. Identify the contribution with a stable `PerceptionProviderId`, source kind,
   and non-zero version. Source kind is diagnostic metadata, not precedence.
3. Declare backend-neutral capability requirements. The application composition
   root supplies the exact available capability set during registry capture.
4. Declare inclusive compatible sense-contract and stimulus-payload version
   intervals on each listener. Version skew fails as a typed incompatibility and
   never silently selects another descriptor.
5. Mark listeners `Required` when activation must fail closed. Mark genuinely
   optional extensions `Optional`; only those listeners become unavailable when a
   referenced descriptor or capability is absent.
6. Capture one `PerceptionDescriptorRegistry` and retain it for every sensing job
   that borrows its sorted spans. Do not mutate caller contribution buffers or
   rebuild the registry while those jobs are active.

User-visible names are no longer lookup keys and must not be written as wire/save
identity. Duplicate stable identities are configuration errors even when they
come from different native, script, or package providers.
