# Internal Module Descriptor Contract

## Purpose

This document defines the metadata boundary used to describe built-in Horo
modules before an application host composes or activates them. The contract is
owned by `HoroFoundation` so runtime, backend, application, presentation, and
headless targets can describe themselves without depending on a concrete host.

The C++ contract is `Horo/Foundation/ModuleDescriptor.h`.

## Inert Descriptor Boundary

`ModuleDescriptor` is owned value metadata. Constructing, copying, moving, or
validating one must not register services, inspect global state, select a
backend, allocate a runtime object, or invoke a lifecycle callback. A descriptor
may name paired activation/deactivation entry points, but only a later
host-owned composition stage may call them.

Production modules use the same shape regardless of target role:

| Target role | Descriptor representation |
|---|---|
| API/model | Stable module ID, contract version, provided capabilities |
| Runtime/application service | Required modules and capabilities, budgets, observability |
| Concrete backend | Backend-specific module ID behind backend-neutral capability IDs |
| GUI/CLI/MCP adapter | Optional presentation module requiring application capabilities |
| Headless/null implementation | Ordinary provider of the same backend-neutral capability |

Module IDs and capability IDs are canonical lowercase namespaced identifiers.
Budget and observability IDs additionally live below their owning module ID.
IDs remain stable when CMake target names or source-tree locations change.

## Version And Dependency Rules

Every descriptor owns a semantic contract version. A dependency names a module,
the minimum accepted contract version, and whether absence is required or
optional. An optional dependency creates an ordering edge when present; it does
not make the graph invalid when absent. A present dependency below the declared
minimum is invalid regardless of optionality.

Capability requirements are separate from target dependencies. At least one
selected module must provide every required capability. Each selected provider
precedes the consumer in the validated graph; later composition policy decides
which approved capability bindings are injected.

## Validation Boundary

`ValidateModuleGraph` runs before registration or initialization and returns a
typed `Result`. It rejects:

- malformed descriptors and unpaired lifecycle callbacks;
- duplicate module identities or descriptor-local entries;
- missing required modules and incompatible dependency versions;
- missing required capabilities;
- cycles introduced by module or capability edges.

Successful validation returns a deterministic provider-before-dependant order.
Independent modules are ordered by stable module ID, so the result does not
depend on descriptor input order.

Validation does not prove trust, permissions, feature flags, or host resource
policy. Those remain later host-owned gates. Explicit registration, activation
rollback, lifecycle states, callback drainage, and reverse shutdown are also
outside this descriptor contract.

## Ownership And Migration

The caller owns the descriptor collection passed to validation. The returned
graph owns copies of module IDs and does not borrow descriptor storage. Callback
function addresses, when present, must remain valid for the later composition
lifetime; the descriptor does not own callback state.

Existing built-in module-specific descriptors may remain at ABI or package
boundaries. Composition adapters translate their stable metadata into this
internal contract rather than making `HoroFoundation` depend on gameplay,
renderer, GUI, platform, or transport types.

## Verification

Regression coverage must prove deterministic ordering, missing requirements,
version incompatibility, duplicate identities, cycles, optional absence, paired
callbacks, and the invariant that validation never invokes lifecycle callbacks.

## Related Documents

- [System Design](./system-design.md)
- [Ownership And Resource Lifetime](./ownership-and-resource-lifetime.md)
- [Error And Diagnostics](./error-and-diagnostics.md)
- [Runtime Lifecycle](../runtime/runtime-lifecycle.md)
