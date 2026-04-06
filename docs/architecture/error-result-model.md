# Error and Result Model

This document is the baseline failure-handling contract for issue `#36`.

## Goals

- Make failures predictable for humans, tests, and AI clients.
- Avoid silent failure paths.
- Keep low-level modules free of UI-shaped error behavior.
- Avoid duplicate logging at every layer of the stack.

## Standard Patterns

Use one of these patterns for new and touched code:

### `bool + outError`

Use when:

- the operation has a simple success/failure result
- the caller already owns the output object
- a short actionable message is enough

Current examples in the codebase:

- settings save/load-adjacent flows
- document save/reload helper flows

Rules:

- return `false` on failure
- clear `outError` on success when provided
- write actionable failure text on error
- do not also invent a secondary hidden failure channel

### Structured result object

Use when:

- the operation naturally returns success/failure plus payload data
- machine-readable callers consume the result
- the caller needs both a status flag and structured output

Current example:

- MCP command execution returns `McpCommandResult`

Rules:

- include an explicit success flag
- include structured payload data on success
- include actionable error text on failure
- avoid encoding UI presentation details inside the result type

## Logging Policy

- Leaf layers should log only when they own the failure context or are the terminal place where the failure becomes actionable.
- If a lower layer already returned a structured or actionable error, higher layers should usually propagate it rather than log the same failure again with less context.
- UI/editor layers may translate returned errors into user-facing presentation, but should not redefine the underlying failure contract.

## Cross-Module Guidance

- `math` should prefer deterministic return behavior and assertions only where invariants are purely local.
- `core`, `scene`, `renderer`, `physics`, and `input` should avoid editor-shaped error types.
- `mcp` should return machine-readable failure information without leaking transport-specific internals into unrelated modules.
- `editor` may present failures to users, but should still prefer lower-level actionable messages instead of replacing them with vague UI-only text.

## Avoid

- silent `false` returns without context
- mixing exceptions, status flags, and logs for the same failure path without a clear rule
- returning UI-formatted text from lower-level modules
- logging the same failure at multiple layers unless each layer adds distinct context

## Style Rule for New Code

For new or touched public APIs:

- pick either `bool + outError` or a structured result type
- document which pattern is used
- keep error text stable enough for tests and automation where practical

## Seed Examples in This Repo

The following patterns are acceptable examples to preserve and extend:

- `bool + outError` helpers around settings/document persistence
- `McpCommandResult` for editor-facing structured mutation/read operations

This issue does not require rewriting all existing call sites. It establishes the contract that future cleanup should follow.
