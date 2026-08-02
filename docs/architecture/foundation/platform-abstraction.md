# Platform Abstraction Architecture

## Purpose

This document defines the boundary between portable engine code and operating
system services such as windows, event pumping, filesystem integration,
processes, native dialogs, clocks, credentials, crash handling, and dynamic
libraries.

Platform abstraction normalizes capabilities and ownership. It does not hide
meaningful operating system differences or force every host to create a window.

## Core Decisions

- Portable modules depend on narrow platform interfaces, not OS headers.
- Platform implementations are selected by the process composition root.
- Headless hosts omit presentation and other optional capabilities; baseline
  platform services remain available through real, deterministic, or restricted
  implementations.
- Paths use structured path types and explicit encoding rules.
- Process execution never relies on shell command concatenation.
- Window and native UI operations declare main-thread affinity.
- Capability absence is represented explicitly and can be queried.

## Capability Model

```cpp
struct PlatformServices {
    FileSystem& files;
    Clock& clock;
    ProcessService& processes;
    UserDirectories& directories;
    CredentialStore* credentials;
    NativeDialogs* dialogs;
    CrashService* crash;
};
```

`FileSystem`, `Clock`, `ProcessService`, and `UserDirectories` form the baseline
service set for supported engine hosts. Headless and test hosts provide suitable
headless, deterministic, or restricted implementations rather than making this
baseline nullable. A restricted implementation returns a typed `Unsupported`
error for an operation forbidden by host or platform policy.

Scenario-specific capabilities use nullable construction-time pointers, as in
the example, or typed capability queries. `CredentialStore`, `NativeDialogs`,
and `CrashService` are optional because availability depends on host role,
platform integration, and product policy. An operation requiring an unavailable
capability returns a typed error.

The platform layer includes:

- Windows, macOS, and Linux implementations
- deterministic or in-memory test implementations
- headless implementations where appropriate

## Filesystem And Paths

The engine uses structured path values:

- `ProjectPath`: normalized path relative to an open project
- `AbsolutePath`: validated absolute OS path
- `VirtualPath`: path inside an archive or package
- `UserDataPath`: resolved application support location

Rules:

- project files store portable forward-slash relative paths
- host adapters convert native input into structured paths
- normalization does not silently resolve outside an allowed root
- case sensitivity follows the storage contract, not the current machine alone
- symlink traversal is explicit for security-sensitive operations
- path comparisons are tested on Windows, macOS, and Linux semantics

Filesystem writes that protect user data use temporary same-filesystem writes
and atomic replacement when supported.

The implemented PRJ-001A baseline exposes `DurableFileSystem` and the move-only
`ExclusiveFileLock`. Native Windows, macOS, and Linux adapters provide durable
write/copy/remove, same-filesystem atomic replacement, parent-directory
synchronization according to host policy, capacity inspection, and immediate
OS-held exclusive locking. Lock-file text is diagnostic only; ownership lives
in the native handle and is released by RAII or process termination.

## User Directories

The platform service resolves logical directories:

```text
config
state
cache
logs
crash
temporary
```

Callers never build these paths from `$HOME`, `%APPDATA%`, or platform-specific
string literals. Locations follow platform conventions and the storage policies
in the owning architecture documents.

## Window And Event Pump

Window creation is optional. A window owns:

- native window lifetime
- event integration
- logical and framebuffer size
- DPI/content scale
- focus and minimize state
- graphics-surface compatibility

The host owns event-pump ordering. Input consumes a normalized event snapshot
after platform polling. Renderer resize occurs from committed framebuffer-size
state, not directly inside arbitrary native callbacks.

Native callbacks enqueue or update platform-owned state; they do not mutate
editor documents or application services.

## Time

The platform exposes:

- monotonic clock for durations, scheduling, and frame timing
- wall clock for records and user-facing timestamps
- high-resolution profiler timestamps

Runtime simulation never uses wall-clock jumps. Tests can inject deterministic
clocks.

## Process Execution

```cpp
struct ProcessRequest {
    AbsolutePath executable;
    std::vector<std::string> arguments;
    EnvironmentOverlay environment;
    std::optional<AbsolutePath> workingDirectory;
    ProcessIoPolicy io;
    Duration timeout;
};
```

`EnvironmentOverlay` has explicit base semantics:

```cpp
enum class EnvironmentBasePolicy {
    InheritParent,
    AllowlistedParent,
    Empty
};

struct EnvironmentOverlay {
    EnvironmentBasePolicy base;
    std::vector<std::string> inheritedNames;
    std::vector<EnvironmentAssignment> set;
    std::vector<std::string> unset;
};
```

Every process request selects a base policy; there is no implicit inheritance
default. `set` adds or replaces values after the base is formed, and `unset`
removes names after inheritance and assignment. `inheritedNames` is used only
with `AllowlistedParent`. Full parent inheritance is reserved for trusted local
tools that require it. Security-sensitive and release operations use an
allowlist or an empty base, and credentials are never inherited or injected
implicitly.

Arguments remain separate from the executable and are passed through native
process APIs. Shell execution requires a dedicated, security-reviewed operation
and is not the default.

The service supports:

- bounded stdout and stderr streaming
- cancellation and timeout
- exit status and termination reason
- redacted diagnostic representation
- process-tree termination where the OS supports it

Process cancellation results are normalized by the job system so that UI and
operation observers see stable codes. Graceful cancellation before work completes
maps to `job.cancelled`; timeout maps to `job.cancelled` with cause
`platform.process.timeout`; forced termination maps to
`platform.process.terminated`.

## Native Dialogs

File pickers, credential prompts, and other native dialogs temporarily enter
`NativeDialog` interaction scope. In an interactive Horo host, the GUI thread is
the process main thread; native dialogs are invoked only from that thread.
Headless hosts have no GUI thread or native-dialog capability. A future host
with a dedicated GUI thread must expose that affinity as a distinct contract
rather than treating `GUI thread` and `main thread` as interchangeable names.

Dialog results are normalized paths or typed cancellation. Native dialogs do not
perform project mutations directly.

## Credentials

Credential storage is a platform capability implementing the contract in
[Release Security](../release/release-security.md). Callers work with opaque credential
references and short-lived secure values.

Absence of a platform credential store is explicit. Falling back to plaintext
persistence is forbidden.

## Dynamic Libraries

If dynamic modules are supported, the platform layer owns loading and symbol
lookup primitives. ABI compatibility, extension package policy, and module lifecycle are
defined by the [Extension System](../extensions/plugin-system.md) and
[Gameplay Module Boundary](../extensions/gameplay-module-boundary.md). Raw
dynamic-library handles do not escape into ordinary engine modules.

## Crash And Emergency Services

The crash service installs the smallest safe platform handlers required to:

- preserve a crash marker or dump
- record process and build identity
- flush preallocated emergency logging state where safe
- avoid allocations and ordinary locks in signal-sensitive contexts

Where the platform and product support it, an out-of-process crash collector is
preferred for dump capture and durable metadata. The in-process handler performs
only the signal-safe notification or marker write needed to wake that collector.
When no collector is available, the handler falls back to a minimal preallocated
tombstone; it does not upload, symbolize, format a complex report, or attempt
general recovery in the compromised process. Abrupt process or operating-system
termination may prevent either path from completing, so the contract guarantees
best-effort evidence rather than a dump for every failure.

Crash upload, consent, retention, and symbol handling are separate application
and release policies.

## Threading

Capabilities declare affinity:

| Capability | Required affinity |
|---|---|
| Window and event pump | Interactive main thread |
| Native dialogs | Interactive main thread |
| Graphics surface creation | Render-capable thread |
| Filesystem and subprocess I/O | Any thread through owned synchronization |
| Credential UI | Interactive main thread |
| Monotonic clock | Any thread |

The platform layer does not dispatch arbitrary callbacks while holding native
or internal locks.

## Testing

Required tests cover:

- path normalization and root escape prevention
- Unicode and platform-specific path behavior
- atomic write and interrupted-write recovery
- subprocess arguments without shell interpretation
- subprocess environment inheritance, allowlisting, override, and removal
- sensitive variables do not reach restricted child processes
- bounded process output, cancellation, and timeout
- logical versus framebuffer resize behavior
- deterministic clock injection
- missing optional capabilities
- native dialog interaction-scope transitions
- crash marker creation through test-safe hooks
- collector notification and in-process tombstone fallback where supported

## Related Documents

- [System Design](./system-design.md)
- [Configuration System](./configuration-system.md)
- [Concurrency And Job System](./concurrency-and-jobs.md)
- [Input Architecture](../runtime/input-architecture.md)
- [Release Security](../release/release-security.md)
- [Extension System](../extensions/plugin-system.md)
- [Gameplay Module Boundary](../extensions/gameplay-module-boundary.md)
