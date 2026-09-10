# ADR-177: MCP Application Capability and Host Boundary

- **Status**: Accepted
- **Date**: 2026-09-10
- **Scope**: MCP controller, application-capability, transport and in-process adapter ownership; tool effect categories; protocol-independent execution semantics
- **Issue**: [MCP-001.1](https://github.com/abdullahbodur/horo-engine/issues/232)
- **Jira**: [HORO-232](https://horo-engine.atlassian.net/browse/HORO-232)
- **Parent**: [MCP-001](https://github.com/abdullahbodur/horo-engine/issues/218)
- **Related**: [ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md), [ADR-019](019-cli-host-command-ownership-adapter-equivalence-and-horopak-boundary.md)
- **Normative documents**: [MCP Architecture](../architecture/interfaces/mcp-architecture.md), [System Design](../architecture/foundation/system-design.md)

## Context

Horo needs the same automation surface for an external local client and the
embedded AI runtime. The current MCP document describes future transports and
tools, but does not close the ownership seam strongly enough to prevent an MCP
handler from becoming a second scene, asset, build, or editor API. It also uses
"side effect" broadly enough that read-only queries, local presentation changes,
and durable mutations could be advertised or authorized as if they were equal.

The transport, session, descriptor registry, authorization policy, and concrete
tool packs are later delivery slices. This decision fixes the boundary they must
implement without prematurely selecting a transport library or public C++ API.

## Decision

### 1. MCP is an adapter, never a domain authority

The dependency and invocation direction is:

```text
external transport ----+
                       +-> McpController -> McpToolRegistry -> McpToolAdapter
in-process adapter -----+                                      |
                                                               v
                                                application capability/controller
                                                               |
                                                               v
                                                        domain service
```

Domain services and application capabilities contain all business validation,
transactions, scheduling, and state ownership. An MCP adapter validates and
translates the protocol envelope, binds caller/session context, invokes one
application capability, and translates its typed result. It cannot retain a
domain object to create a shorter mutation path.

Application, editor-model, editor-services, scene, runtime, asset, package,
renderer, audio, network, and other domain targets must not include MCP headers,
link MCP targets, accept MCP values, or return MCP result types. MCP targets may
depend on their narrow application capability contracts. Executable hosts are
composition roots and may own both sides without making either globally
discoverable.

### 2. One controller and registry serve every entry path

Transport and in-process adapters are framing/session adapters only. After
decoding, both submit the same immutable request envelope to the same
`McpController` instance and its host-owned registry. They use the same:

- descriptor snapshot and canonical tool identity;
- session authorization and project/trust revision;
- schema and resource limits;
- owner-thread dispatch and application capability;
- cancellation, deadline, operation, error, audit, and redaction policy; and
- shutdown admission barrier.

An in-process caller receives no privileged direct registry lookup, handler
pointer, application service locator, or approval bypass. Adapter equivalence
means equal admitted inputs at the same registry/policy revisions produce the
same typed application outcome; transport framing and presentation may differ.

### 3. Tool effect category is closed and mandatory

Every descriptor declares exactly one category:

| Category | Meaning | Required behavior |
|---|---|---|
| `Query` | Reads an authoritative snapshot without changing domain or presentation state. | No prompt, focus, selection, document dirtying, cache publication, network mutation, or lazy state repair. Pagination and revision semantics are explicit. |
| `PresentationSideEffect` | Changes ephemeral host presentation, such as reveal, focus, selection, or opening an existing document. | Requires an interactive host capability and owner-thread dispatch; never implies project mutation authority. |
| `Mutation` | Changes durable project/application state, starts externally observable work, or performs an external side effect. | Requires the exact mutation capability, authorization and approval policy; application transactions remain authoritative. |

Registration rejects a missing, unknown, or contradictory category. A handler
cannot downgrade its effective behavior. A query that discovers repair is needed
returns a typed condition or mutation proposal rather than repairing silently.
Starting a build, process, network request, import, save, install, or long-running
operation is a mutation even if its final output is not yet committed.

### 4. Protocol and application contracts remain distinct

The MCP protocol version governs framing, initialization, methods, schemas, and
result envelopes. Each tool descriptor separately identifies its stable tool
contract version and the application capability version it adapts. Compatibility
is checked before advertisement and dispatch. Unknown required fields or
unsupported major versions fail; additive optional fields require explicit
bounded decoding rules. Protocol version equality never proves application
capability compatibility.

Protocol failures use protocol error codes. Once a request reaches an application
capability, the canonical Horo error domain/code, safe context, causes, and
diagnostics remain intact inside the protocol error data. Adapters must not turn
denial, cancellation, timeout, conflict, unavailable capability, or stale revision
into an empty success. Internal paths, credentials, native handles, exception text,
and unbounded remote payloads are not diagnostic context.

### 5. Bounds and cancellation are admission contracts

Before application dispatch, the controller enforces finite frame, nesting,
string, collection, schema-validation-work, per-session in-flight, result, and
diagnostic limits. Query descriptors require stable ordering and explicit page or
cursor bounds. Oversized input is rejected before expensive allocation or
owner-thread work.

Every accepted request binds an immutable request identity, session generation,
deadline, cancellation token, registry revision, authorization revision, and
project/workspace generation where applicable. Cancellation is cooperative after
admission and cannot roll back a committed application transaction. Long-running
work is owned by the application operation/task scope; disconnect follows the
descriptor's explicit cancel-or-detach policy and never transfers ownership to
the transport.

### 6. Lifecycle has an explicit admission barrier

The composition root constructs application capabilities first, then the
controller/registry, then adapters/transports. Startup does not discover domain
services or register tools through static initialization. Shutdown proceeds in
reverse:

1. stop new sessions and request admission;
2. cancel or detach accepted requests according to policy;
3. drain bounded controller callbacks and audit publication;
4. destroy transport and in-process adapters;
5. revoke the registry snapshot and application-capability leases; and
6. destroy application/domain owners.

Requests racing a closed admission barrier fail with a stable unavailable or
shutdown result. No callback may outlive its registry generation, application
lease, or owning host.

### 7. Target enforcement is part of the contract

MCP implementation targets use the `HoroMcp` prefix. Only MCP implementation
targets and executable composition roots (`HoroEditor`, `horo-engine`) may declare
a direct dependency on such targets. `cmake/HoroDependencyDirection.cmake`
rejects a domain target that attempts to depend on an MCP target. A negative
configure test preserves the rejection and its actionable diagnostic before the
later MCP targets are introduced.

This check complements, rather than replaces, the ordinary per-target dependency
allowlist. Source review still rejects MCP types copied into a neutral application
contract or hidden behind a third-party dependency.

## Consequences

- External and embedded clients cannot drift into separate behavior or authority.
- Tool effect classification is available to discovery, authorization, approval,
  audit, and clients without inspecting handler code.
- Domain APIs remain reusable by GUI, CLI, tests, and future adapters.
- Later tickets must implement sessions, the tool registry, authorization, and
  concrete tool packs within this boundary.
- Transport-independent request context and application capability contracts are
  required before a concrete transport can dispatch work.

## Rejected Alternatives

- **Let each transport own a controller or registry.** This permits divergent
  schemas, authorization, lifecycle, and results.
- **Expose domain services directly to tool handlers.** This creates an ambient
  service locator and a parallel mutation API.
- **Treat every non-file-writing tool as read-only.** Presentation changes and
  operation starts are observable side effects requiring distinct policy.
- **Share the CLI or debug-console registry.** Their parsing, presentation,
  permissions, lifecycle, and invocation contracts are intentionally different.
- **Implement remote transport, sessions, or authentication in this decision.**
  Those are later tickets and would obscure the ownership boundary.
