# Destruction Product Composition Migration

Product hosts now resolve destruction through `DestructionComposition` before admitting runtime work. The contract is inert,
fixed-size, backend-neutral data; it does not install services, discover providers, or retain service/native handles.

## Host migration

1. Select exactly one `DestructionProductProfile`: `Null`, `Headless`, `Editor`, `Standalone`, `Client`, or `Server`.
2. Publish one `DestructionCapabilityFact` for each of Physics, VFX, Audio, and Networking. Available facts carry a non-zero
   provider-neutral revision; unavailable facts carry the reserved zero revision.
3. Call `DestructionComposition::Create`. A missing required capability fails with
   `CompositionCapabilityUnavailable`; it never installs a provider, changes tier, or selects another profile.
4. Retain the immutable result with its revision. Before queued work is admitted, call
   `ValidateDestructionCompositionAdmission` with the current revision and lifecycle state. Replaced, cancelling, and shut-down
   compositions reject old work explicitly.

Optional unavailable capabilities remain observable as `Unavailable`. Capabilities excluded by the profile remain `Omitted`, even
when the host has an available implementation. `Null` explicitly omits every capability and disables destruction. Headless and
server profiles omit presentation-only VFX and Audio; client and server profiles require Networking.

This contract deliberately does not depend on the pending bounded destruction registry. A later registry integration may publish
its own immutable snapshot as capability evidence without changing product-profile semantics.
