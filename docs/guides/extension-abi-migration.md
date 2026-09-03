# Native extension ABI negotiation

ABI 1.1 keeps the v1 host prefix and load/unload symbols while adding an optional
inert `horo_extension_query` bootstrap. The host negotiates before calling load;
an incompatible query cannot receive a registration callback. Missing queries
select the legacy 1.0 contract, not an inferred newer ABI.

## Version and size rules

- `HoroExtensionHostApi::abiVersion` remains the major version, currently 1.
  The appended `abiMinorVersion` is 1; `reserved` is zero.
- A query fills `HoroExtensionRequirements` in host-owned storage. It must return
  the complete supported requirements size, matching major, a minimum minor no
  greater than the host's, and a host size the host can supply.
- Required function bits are explicit. Unknown bits and nonzero reserved fields
  fail negotiation; requested callbacks must be present.
- Input-table tails may be appended. Modules must test size before reading new
  fields, and must still validate their requirements in load when supporting an
  older host that does not call query. No field is reordered or repurposed.
- Output `structSize` starts as writable capacity. A module must not write past
  that capacity and returns its populated prefix size. Module results may end
  before `moduleId`, before `moduleVersion`, or after the complete current table.
  Absent legacy identities are cleared and remain manifest-owned. Partial fields
  and sizes beyond supplied capacity fail before contribution commit.
- Query is metadata-only: no allocations, lifecycle state, retained pointers,
  callbacks into host services, or registration. Query errors propagate and
  contract-violating exceptions are contained by the host.

## Source compatibility and ownership

The public header now compiles as C11 and C++20. C `typedef` is intentional; the
C++-only Sonar preference for `using` is inapplicable to this shared header.
Status and setting tags use `uint32_t` rather than compiler-sized enum wire
types. Constant names and values are unchanged. Code that explicitly spells the
old enum tags must use the public typedef names instead. Rebuild native modules
with the updated SDK; binaries built with nonstandard short-enum options are not
covered by compatibility guarantees.

The existing example, host adapter, C consumer, and C++ public-header consumer
are affected callers. The C consumer is a registered CTest target, not a C++ test
that merely includes the header.

Importer context ownership requires a module-provided destroy callback whenever
the context is non-null. A rejected registration does not transfer ownership;
the module must clean it up. Accepted contexts retain the module lease and are
destroyed through the originating module, including transaction rollback.

This bootstrap is not a sandbox or package trust decision. It cannot prevent a
malicious native module from ignoring buffer bounds or accessing process memory.
