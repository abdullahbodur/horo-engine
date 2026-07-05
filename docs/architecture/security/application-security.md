# Application Security Architecture

## Purpose

This document defines application trust boundaries for projects, plugins,
gameplay code, assets, subprocesses, files, MCP access, credentials, diagnostics,
and update operations.

Release signing and credential details remain in
[Release Security](../release/release-security.md). This document governs the running
editor and CLI hosts.

## Core Decisions

- Projects and externally supplied packages are treated as untrusted input until
  the user grants the required trust.
- Opening a project does not automatically execute its code, tools, scripts, or
  plugins.
- File access is rooted and path normalized.
- Process execution uses explicit executable and argument arrays with policy
  checks.
- Native plugins and gameplay modules are trusted code, not falsely described
  as sandboxed.
- MCP binds locally by default and requires authenticated, capability-scoped
  access for remote exposure.
- Runtime console and remote-console access are profile-gated, permission-scoped,
  and denied remotely by default.
- Secrets remain in credential providers and short-lived secure memory.
- Security decisions are explicit, auditable, and revocable.

## Trust Domains

| Domain | Default trust |
|---|---|
| Packaged engine binaries/resources | Trusted after installation verification |
| User-created local project data | Data-trusted, code execution not implicit |
| Downloaded project/archive | Untrusted |
| Built-in plugin | Trusted with product |
| External native plugin | Untrusted until approved |
| Project gameplay binary | Untrusted until build/run approval |
| MCP local client | Authenticated local policy |
| MCP remote client | Denied unless explicitly configured |
| Runtime console local user | Product-profile and project-policy scoped |
| Runtime console remote client | Denied unless diagnostics policy enables authenticated access |
| Imported asset | Untrusted parser input |
| Toolchain executable | Allowed only through resolved trusted profile |

Trust is scoped by project identity, plugin identity/version, capability, and
where practical executable hash. A broad permanent "trust everything" switch is
not part of normal UI.

## Project Trust

Opening an untrusted project permits safe inspection and validation but blocks:

- project-defined executable code
- external native plugins
- build hooks and custom tools
- arbitrary subprocess execution
- credential access
- network-capable project operations

The editor presents the requested capabilities and their source before trust is
granted. Trust records are user-local and not stored in the project.

## Path Security

All project-relative paths are normalized and checked against their allowed
root after resolving `..`, symlinks where relevant, archive entries, and native
path semantics.

Sensitive operations define separate roots for:

- project read
- generated project write
- source asset write
- package extraction
- user config and state
- temporary files

Archive extraction rejects absolute paths, traversal, duplicate normalized
entries, unsafe links, and resource-expansion limits.

## Asset And File Parsing

Parsers treat sizes, counts, offsets, compression ratios, and recursion depth as
untrusted. They validate before allocation and use configured resource limits.

High-risk or historically unsafe third-party parsers may run in a restricted
helper process with bounded I/O.

## Process Execution

Process policy validates:

- executable identity and resolved absolute path
- owning toolchain or plugin capability
- argument boundaries
- working directory
- environment allowlist
- timeout and cancellation
- output bounds

Shell concatenation is forbidden by default. Project content cannot inject
extra arguments through untyped strings.

## Plugin And Code Security

Native code can access process memory and therefore requires trust. Plugin
permissions improve authority control but are not a security sandbox.

Untrusted extension execution requires process isolation with:

- a versioned IPC protocol
- restricted filesystem roots
- no inherited credentials
- bounded memory, CPU, and output where supported
- explicit network policy

## MCP Security

MCP defaults:

- loopback binding
- authenticated sessions
- per-tool capability declaration
- request size and rate limits
- no direct mutation from transport threads
- redacted audit records

Remote binding requires explicit configuration, secure transport, and a threat
review. MCP tools cannot bypass project trust, path, process, or credential
policy.

## Credentials

Credentials are:

- referenced by opaque ID
- resolved only for the authorized operation
- held in short-lived secure values
- excluded from configuration snapshots, event payloads, errors, logs, crash
  records, and diagnostic bundles
- wiped where the platform and memory contract permit

Credential prompts identify the requesting operation and capability.

## Network Access

Network clients declare destination policy, protocol, timeout, size limits, and
credential requirements. Plugins and projects require the `network.client`
capability.

Redirects, proxy behavior, certificate validation, and download integrity are
explicit for update and package operations.

## Diagnostic And Privacy Policy

Security-relevant events include:

- trust grant, denial, and revocation
- plugin load and permission denial
- blocked path traversal
- rejected executable or process request
- MCP authentication and rate-limit failures
- update signature failure

Records contain stable identities and safe reasons, not secrets or unnecessary
user content. Diagnostic bundle generation shows the user what categories will
be included.

## Security Updates

The application can revoke:

- plugin trust by identity/version
- project execution trust
- MCP tokens
- cached package/update trust
- credential references

Security-sensitive defaults may become stricter in a release without preserving
an unsafe previous default.

## Testing

Required tests cover:

- project trust capability gating
- path traversal and symlink escape
- archive extraction limits
- command argument injection resistance
- runtime console command permission, profile, allowlist, and remote-denial checks
- environment allowlisting
- plugin permission and trust checks
- MCP authentication, authorization, and rate limiting
- credential redaction across every signal
- parser resource limits and malformed input
- update signature rejection

## Related Documents

- [Release Security](../release/release-security.md)
- [Extension System](../extensions/plugin-system.md)
- [Platform Abstraction](../foundation/platform-abstraction.md)
- [MCP Architecture](../interfaces/mcp-architecture.md)
- [Runtime Debug Console And Development Overlays](../runtime/debug-console-and-overlays.md)
- [Horo Package System](../packages/package-system.md): package trust levels and code contribution policy
- [Error And Diagnostics](../foundation/error-and-diagnostics.md)
