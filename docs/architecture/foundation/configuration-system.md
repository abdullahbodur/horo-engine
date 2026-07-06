# Configuration System Architecture

## Purpose

This document defines the typed configuration system shared by the editor,
games, CLI, MCP, Python tooling, build and release services, renderer backends,
and observability runtime.

Configuration changes behavior. It is not a general persistence store, command
bus, secret vault, or replacement for authoritative runtime models.

## Core Decisions

- Every setting has a typed schema, owner, default, validation rule, and reload
  policy.
- Configuration is resolved into immutable snapshots.
- Readers never query environment variables or JSON files directly.
- Precedence is explicit and shared across hosts.
- Secrets are represented by references to credential providers, not plain
  configuration values.
- Dynamic settings publish one committed-change notification after a validated
  snapshot becomes active.
- Unknown keys and invalid values produce diagnostics; they do not silently
  change behavior.

## Configuration Domains

Configuration is divided by ownership:

| Domain | Examples | Typical persistence |
|---|---|---|
| Engine defaults | fixed timestep, default backend | packaged resources |
| User preferences | theme, editor behavior, log filters | user config directory |
| Project settings | default scene, physics, target profiles | `.horo/project.json` |
| Project extension package requests | desired package IDs, versions, permission intent | `.horo/plugins.json` |
| Workspace settings | panel layout, local editor state | editor workspace file |
| Session overrides | temporary profiling or preview options | memory only |
| Invocation overrides | CLI and environment overrides | process launch |

Workspace presentation state remains governed by
[Project Model](../editor/project-model.md). It may use the same serialization and
validation infrastructure without becoming global configuration.

Project extension package requests are portable project configuration, but they
are not a trust grant and do not store resolved package paths. Extension package
resolution, trust decisions, and development overrides follow
[Extension System](../extensions/plugin-system.md).

## Schema

```cpp
struct SettingDescriptor {
    SettingKey key;
    SettingValueType type;
    SettingValue defaultValue;
    SettingScope scope;
    ReloadPolicy reloadPolicy;
    Sensitivity sensitivity;
    ValidationRule validation;
};
```

Descriptors are registered by the module that owns the setting. Registration is
complete before configuration resolution begins. Duplicate keys, incompatible
types, and conflicting ownership are startup errors.

Descriptor registration is performed by the composition root from validated
module descriptors. A module does not self-register settings through static
initializers or a process-global settings registry. This matters for the IDE:
Settings pages, command-line flags, MCP schemas, project validation, and Python
tooling must all see the same setting contract without each surface inventing
its own list of keys.

Keys use stable dotted names:

```text
runtime.fixed_step_hz
render.backend
render.fallbacks
editor.theme.active
editor.autosave.interval_seconds
observability.log.default_level
pipeline.max_parallel_jobs
```

## Resolution Precedence

From highest to lowest precedence:

1. explicit invocation arguments
2. environment variables
3. session overrides
4. project configuration
5. user configuration
6. packaged profile or preset
7. schema default

Not every key is legal at every layer. For example, workspace layout cannot be
set by a project committed to source control, and a release security policy
cannot be weakened by an arbitrary project value.

Environment variables are launch-time invocation inputs. Session overrides may
only override environment values for settings whose descriptor declares
`sessionOverride: true`. Otherwise the environment value remains locked until
process restart. This prevents a temporary editor profiling option or preview
switch from being silently overridden by a stale environment variable.

Each resolved value retains provenance:

```cpp
struct ResolvedSetting {
    SettingValue value;
    ConfigurationSource source;
    std::optional<SourceLocation> location;
};
```

The Settings modal may show where a value came from and why a lower-precedence
value is inactive.

## Environment Variables

Environment keys use the `HORO_` prefix and an explicit mapping declared by the
owning descriptor:

```text
HORO_RENDER_BACKEND=opengl
HORO_RENDER_FALLBACKS=opengl,null
HORO_LOG_LEVEL=debug
HORO_THEME_FILE=/path/to/theme.json
HORO_RUNTIME_FIXED_STEP_HZ=120
```

Modules do not invent undocumented aliases. Empty, malformed, or unsupported
values fail validation with a source-specific diagnostic.

The environment is read once by the host adapter and converted to a safe input
map. Arbitrary environment access is not spread throughout engine modules.

## Module Settings Contributions

Feature modules contribute settings through descriptors, not through UI code or
ad hoc JSON parsing:

```cpp
struct ModuleConfigurationContribution {
    ModuleId module;
    std::span<const SettingDescriptor> settings;
    std::span<const EnvironmentVariableBinding> environmentBindings;
};
```

The host validates contributions before snapshot resolution:

- setting keys must be prefixed by the owning module or a documented built-in
  namespace;
- user, project, workspace, session, and invocation scopes must be legal for the
  setting's owner;
- reload policy must match the resource lifetime affected by the setting;
- environment bindings must be explicit and must not collide with another
  module's binding;
- Settings modal pages, CLI flags, MCP schemas, and Python adapters are derived
  from the same descriptor set where practical.

This keeps the editor modular without creating separate configuration systems
for GUI, CLI, MCP, game projects, and extensions.

## Immutable Snapshots

```cpp
class ConfigurationSnapshot {
public:
    template<typename T>
    const T& Get(SettingKey<T> key) const;

    ConfigurationRevision Revision() const noexcept;
};
```

`ConfigurationSnapshot` is an immutable, reference-counted snapshot handle.
Copying it is cheap and keeps the captured revision alive for the consumer's
lifetime. It does not copy the entire resolved configuration set. The snapshot
holds the validated value map internally; readers never observe partial updates.

`ConfigurationSnapshotRef` is the same handle type used when a snapshot must be
captured without implying authority over it, for example inside a `JobDescriptor`
or an operation context.

The configuration service builds and validates a complete candidate snapshot,
then atomically publishes it. Readers receive a stable snapshot for the duration
of their operation or frame.

No reader observes a partially applied multi-key update.

## Dynamic Reload

Every setting declares one reload policy:

| Policy | Behavior |
|---|---|
| `Immediate` | New snapshot may apply at the next owned synchronization point |
| `NextFrame` | GUI/runtime swaps the snapshot at a frame boundary |
| `NextOperation` | Existing jobs retain their captured configuration |
| `ProjectReopen` | Requires closing and reopening the project |
| `ProcessRestart` | Requires host restart |

The owner validates whether a transition is legal. A dynamic renderer setting,
for example, may require resource recreation and must not be marked `Immediate`.

After commit, the configuration service publishes a bounded notification:

```cpp
struct ConfigurationChangedEvent {
    ConfigurationRevision revision;
    ConfigurationDomain domain;
    std::vector<SettingKeyId> changedKeys;
};
```

Subscribers query the authoritative snapshot. The event does not carry the
entire configuration or secrets.

## Settings Modal

The Settings modal edits a draft transaction:

```cpp
struct ConfigurationDraft {
    ConfigurationRevision baseRevision;
    std::unordered_map<SettingKey, SettingValue> proposedValues;
};
```

```text
active snapshot
      |
      v
validated draft
      |
      +-- preview supported values
      +-- apply atomically
      +-- cancel and roll back previews
```

The modal is a publisher and subscriber through `EditorDataBus`, but the
configuration service remains the authority. Applying a draft commits through a
typed settings operation. The service publishes the resulting change event;
the modal does not impersonate it.

If the active configuration revision changed after the draft was created, the
service either rebases safe fields or rejects the apply with
`configuration.draft_stale`. The modal must reload the active snapshot and let
the user review conflicts before retrying. This protects against concurrent
changes from CLI, MCP, or another editor panel.

Theme settings follow the component and token rules in
[GUI Design System](../editor/ui-design-system.md).

## Secrets

Configuration may contain a credential reference such as a keychain entry ID or
CI secret name. It must not contain the credential value.

Secret resolution:

- occurs only inside the operation that needs the secret
- returns an owning secure value with a short lifetime
- is never included in snapshots, events, logs, diagnostics, or workspace files
- follows [Release Security](../release/release-security.md)

## File Format And Writes

Configuration files are versioned structured documents. Writes are:

- validated before persistence
- written to a temporary file in the same filesystem
- flushed as required by the durability contract
- atomically replaced
- protected against concurrent writer loss
- deterministic in key ordering where practical

Comments and unknown extension fields are preserved only when the selected
format and schema explicitly support them.

## Threading

Configuration resolution and file watching may occur on workers. Snapshot
activation occurs on the owning synchronization point. Callbacks never execute
while the configuration service holds an internal registry lock.

Long-running jobs capture the snapshot revision used at submission. A later
configuration change does not silently alter an in-progress build, cook, import,
or release job.

## Testing

Required tests cover:

- precedence at every source layer
- source restrictions by setting scope
- malformed environment and file values
- atomic multi-key updates
- snapshot immutability
- dynamic reload policies and rollback
- concurrent readers during activation
- deterministic writes and recovery from interrupted writes
- secret-reference redaction
- project extension package requests do not grant trust or store resolved
  package paths
- settings modal apply, preview, cancel, and external-change behavior

## Related Documents

- [Project Model](../editor/project-model.md)
- [GUI Design System](../editor/ui-design-system.md)
- [Editor Modal Host](../editor/editor-modal-host.md)
- [Error And Diagnostics](./error-and-diagnostics.md)
- [Release Security](../release/release-security.md)
