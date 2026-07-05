# Extension Module Development Guide

## Purpose

This guide shows how to design an installable Horo add-on package that extends
engine/backend capabilities and, when needed, the engine IDE without depending
on editor internals. The example is a hybrid package that contributes:

- a headless shader compile-preview capability
- a dockable editor tab
- a Settings page
- an MCP tool
- a command-palette/menu action
- editor data-bus subscriptions
- extension-owned notifications

The guide is intentionally API/ABI focused. Exact header names may evolve, but
these boundaries are the contract extension authors should design against.

## Example: Shader Tools Extension

The example package adds a backend shader compile-preview capability and a
Shader Inspector tab over that capability. The tab reads the selected asset,
queries compiler diagnostics, listens for asset/build/log changes, and requests
compile previews through the approved backend capability.

```text
com.vendor.shader-tools
  extension.json
  bin/
    macos-arm64/libhoro_shader_tools.dylib
    linux-x64/libhoro_shader_tools.so
    windows-x64/horo_shader_tools.dll
  schemas/
    compile-preview.schema.json
  resources/
    icons/shader-tools.svg
```

The package does not patch `EditorLayer`, does not subscribe directly to
`EngineDataBus` from its GUI tab, and does not write project files directly. It
contributes descriptors. The host validates them, grants narrow backend and
frontend capabilities, and owns placement, focus, persistence, scheduling,
permission checks, result storage, and teardown.

## Development Flow

1. Choose whether the package is backend-only, frontend-only, or hybrid.
2. Choose the extension points.
3. Declare package, module, permissions, settings, errors, events, and UI
   contributions in `extension.json`.
4. Implement the native module behind the Horo extension C ABI.
5. Register factories for the declared contributions.
6. Read engine and IDE data through approved query capabilities.
7. Mutate engine/editor state only through typed commands or application use
   cases.
8. Observe changes through `EditorDataBus` or approved process-event imports.
9. Store only bounded presentation state under the contribution ID.
10. Test registration, permission denial, event handling, workspace persistence,
   and teardown.

## Manifest

A minimal manifest for the Shader Tools package:

```json
{
  "id": "com.vendor.shader-tools",
  "displayName": "Shader Tools",
  "version": "1.0.0",
  "apiVersion": 1,
  "engineVersion": ">=0.8 <0.9",
  "publisher": "Vendor",

  "modules": [
    {
      "id": "com.vendor.shader-tools.native",
      "kind": "native",
      "entry": "bin/${platform}-${arch}/horo_shader_tools"
    }
  ],

  "contributions": [
    {
      "type": "application.capability",
      "id": "com.vendor.shader-tools.compile-service",
      "module": "com.vendor.shader-tools.native",
      "capability": "shader.compile.preview",
      "threadAffinity": "worker",
      "operationStore": true,
      "permissions": ["asset.read", "shader.compile.preview"]
    },
    {
      "type": "editor.tab",
      "id": "com.vendor.shader-tools.shader-inspector",
      "module": "com.vendor.shader-tools.native",
      "label": "Shader Inspector",
      "icon": "resources/icons/shader-tools.svg",
      "fallbackPlacement": "bottom.tools",
      "openByDefault": false,
      "events": [
        "horo.editor.selection.changed",
        "horo.editor.asset.changed",
        "horo.editor.operation.changed"
      ],
      "capabilities": [
        "selection.query",
        "asset.query",
        "shader.compile.preview",
        "log.query"
      ],
      "workspaceState": {
        "schemaVersion": 1,
        "maxBytes": 8192
      }
    },
    {
      "type": "editor.settings_page",
      "id": "com.vendor.shader-tools.settings",
      "module": "com.vendor.shader-tools.native",
      "placement": "ProjectSettings/Tools",
      "settingsPrefix": "vendor.shader_tools",
      "capabilities": ["configuration.draft_page"]
    },
    {
      "type": "command",
      "id": "com.vendor.shader-tools.compile-selected",
      "module": "com.vendor.shader-tools.native",
      "label": "Compile Selected Shader",
      "placement": "CommandPalette/Assets",
      "capabilities": [
        "selection.query",
        "asset.query",
        "shader.compile.preview"
      ]
    },
    {
      "type": "mcp.tool",
      "id": "com.vendor.shader-tools.compile-preview",
      "module": "com.vendor.shader-tools.native",
      "schema": "schemas/compile-preview.schema.json",
      "capabilities": [
        "asset.query",
        "shader.compile.preview"
      ]
    }
  ],

  "settings": [
    {
      "key": "vendor.shader_tools.default_profile",
      "type": "string",
      "scope": "project",
      "default": "debug",
      "reloadPolicy": "NextOperation"
    }
  ],

  "events": [
    {
      "name": "com.vendor.shader-tools.preview.changed",
      "scope": "editor-session",
      "payload": "revision-only"
    }
  ],

  "errors": [
    {
      "domain": "com.vendor.shader-tools",
      "code": "shader.preview.unsupported_asset",
      "defaultSeverity": "error",
      "summary": "Selected asset is not a shader",
      "userActionable": true,
      "retryable": false
    }
  ],

  "permissions": [
    "project.read",
    "asset.read",
    "shader.compile.preview",
    "mcp.register_tool"
  ]
}
```

Rules:

- IDs are stable contracts. Do not rename or repurpose them after release.
- Contributions are inert descriptors. Loading the package must not mutate host
  registries through static initialization.
- Permissions are reviewed before module activation.
- Settings, errors, and events are declared before the module can use them.
- Backend capabilities, UI surfaces, CLI/MCP tools, and commands are separate
  contributions even when implemented by the same module.

## Package Shapes

Horo add-ons are not necessarily GUI add-ons:

| Shape | Example | Rule |
|---|---|---|
| Backend-only | asset importer, cooker, project validator, pipeline step, toolchain provider | Must run without `HoroEditor` or ImGui. Exposes typed capabilities, diagnostics, settings, and observability. |
| Frontend-only | inspector tab over existing scene/asset/log stores, command-palette shortcut, Settings page for built-in service | Owns presentation only. Reads existing authorities and calls existing capabilities. |
| Hybrid | shader compile service plus inspector tab and MCP tool | Backend contribution is authoritative; GUI/MCP/command contributions are adapters over it. |

When a feature has real behavior, put that behavior behind a backend contribution
first. The GUI then becomes optional presentation. This keeps the same add-on
usable from the editor, CLI, MCP, CI, and tests.

```text
Backend contribution
  -> typed capability / operation / provider
  -> host-owned scheduling, cancellation, progress, result storage

Frontend contribution
  -> tab, panel, Settings page, command, MCP tool
  -> calls backend capability
  -> observes revisions and queries snapshots
```

## ABI Boundary

External native editor/tool modules cross a C ABI. C++ STL types, exceptions,
RTTI-dependent ownership, allocator ownership, and C++ object deletion do not
cross this boundary.

```c
typedef struct HoroExtensionHostApiV1 HoroExtensionHostApiV1;
typedef struct HoroExtensionModuleApiV1 HoroExtensionModuleApiV1;

typedef enum HoroExtensionStatusCode {
    HORO_EXTENSION_OK = 0,
    HORO_EXTENSION_REJECTED = 1,
    HORO_EXTENSION_INVALID_ARGUMENT = 2,
    HORO_EXTENSION_UNSUPPORTED_API = 3,
    HORO_EXTENSION_INTERNAL_ERROR = 4
} HoroExtensionStatusCode;

typedef struct HoroExtensionStatus {
    uint32_t size;
    HoroExtensionStatusCode code;
    const char* error_domain;
    const char* error_code;
    const char* message;
} HoroExtensionStatus;

HORO_EXTENSION_EXPORT HoroExtensionStatus
horo_extension_load_v1(const HoroExtensionHostApiV1* host,
                       HoroExtensionModuleApiV1* module);
```

ABI rules:

- Every ABI struct starts with `size` and `version` or is passed through a table
  that has them.
- The host owns host memory. The module owns module memory. Each side frees only
  memory it allocated.
- Callbacks never throw across the ABI. The module catches internal exceptions
  and returns `HoroExtensionStatus` or an operation result.
- Function tables are append-only within a major ABI version.
- Pointers received from the host are borrowed for the documented call or handle
  lifetime only.
- Dynamic library unload is not assumed safe. Disable/update/removal normally
  applies on restart unless the module API proves live unload safety.

## Module Registration

The module load function should only publish factories for descriptors the host
already validated from the manifest.

```c
HORO_EXTENSION_EXPORT HoroExtensionStatus
horo_extension_load_v1(const HoroExtensionHostApiV1* host,
                       HoroExtensionModuleApiV1* module) {
    if (host == NULL || module == NULL) {
        return HoroExtensionStatus_InvalidArgument();
    }

    if (host->version != HORO_EXTENSION_HOST_API_V1) {
        return HoroExtensionStatus_UnsupportedApi();
    }

    module->size = sizeof(HoroExtensionModuleApiV1);
    module->version = HORO_EXTENSION_MODULE_API_V1;
    module->on_activate = ShaderTools_OnActivate;
    module->on_deactivate = ShaderTools_OnDeactivate;
    module->create_application_capability = ShaderTools_CreateCompileService;
    module->create_editor_tab = ShaderTools_CreateEditorTab;
    module->create_settings_page = ShaderTools_CreateSettingsPage;
    module->execute_command = ShaderTools_ExecuteCommand;
    module->execute_mcp_tool = ShaderTools_ExecuteMcpTool;
    return HoroExtensionStatus_Ok();
}
```

Activation receives a module context containing only the capabilities approved by
manifest, trust, project policy, and host availability.

```cpp
struct ShaderToolsModule {
    HoroExtensionHostApiV1 host;
    HoroCapabilityTable capabilities;
    HoroSubscriptionGroup subscriptions;
};
```

The module must not keep arbitrary host pointers beyond their documented
lifetime. Long-lived access uses handles, leases, or capability interfaces whose
ownership is specified by the host.

## Backend Capability

The backend contribution implements behavior that must work without the editor
being open. For this example, `shader.compile.preview` is an application
capability that accepts typed requests, schedules worker work through the host,
and commits progress/result snapshots to the operation store.

```text
CLI / MCP / GUI command
  -> ShaderCompilePreview::Submit(request)
  -> host validates permission and asset identity
  -> host schedules worker operation
  -> extension backend compiles preview using approved file/asset handles
  -> operation store records progress, diagnostics, and final result
```

Backend capabilities must:

- validate all input from GUI, CLI, MCP, and automation callers;
- use host-owned scheduling, cancellation, diagnostics, and result storage;
- declare thread affinity and resource budget expectations;
- report stable errors from the manifest error domain;
- avoid depending on `EditorDataBus`, ImGui, selected tabs, or current widget
  focus;
- remain observable through logs, metrics, profiler zones, and operation IDs.

Backend capabilities must not:

- write project files directly when a host transaction should own the mutation;
- hold GUI pointers or editor-session references;
- publish GUI events as their source of truth;
- perform hidden process launches or network access outside declared
  permissions;
- require an editor surface to be open for correctness.

## Reading Data From The IDE

Read editor and engine state through query capabilities. Do not reach into tabs,
`EditorLayer`, renderer backend objects, or process-global services.

For the Shader Inspector tab, the normal read path is:

```text
ShaderInspectorTab
  -> SelectionQueries::CurrentAsset()
  -> AssetQueries::GetAsset(asset_id)
  -> LogQuery::ReadRange(log_revision, range)
  -> MetricsQuery::ReadLatest(group) when visible
```

A surface attaching to the editor should query current state immediately. It must
not rely on old events being replayed.

```cpp
void ShaderInspectorTab::OnAttach(EditorExtensionSurfaceContext& ctx) {
    m_ctx = &ctx;

    m_selectionChanged = ctx.editorEvents.Subscribe<SelectionChangedEvent>(
        [this](const SelectionChangedEvent&) {
            m_dirtySelection = true;
        });

    m_assetChanged = ctx.editorEvents.Subscribe<EditorAssetChangedEvent>(
        [this](const EditorAssetChangedEvent& event) {
            if (event.asset == m_currentAsset) {
                m_dirtyAsset = true;
            }
        });

    RefreshFromAuthorities();
}

void ShaderInspectorTab::RefreshFromAuthorities() {
    const auto selected = m_ctx->capabilities.Get<SelectionQueries>().CurrentAsset();
    if (!selected) {
        m_view = ShaderInspectorView::NoSelection();
        return;
    }

    const auto asset = m_ctx->capabilities.Get<AssetQueries>().GetAsset(*selected);
    m_view = BuildViewFromAsset(asset);
}
```

Important constraints:

- Event payloads are invalidation hints, not authoritative data.
- Hidden surfaces may receive notifications but should defer expensive refreshes
  until visible.
- High-volume data is queried from bounded stores. It is not copied through the
  data bus.
- Query capabilities return typed errors; UI text is derived from error codes and
  diagnostics, not log parsing.

## Sending Data Or Mutating State

Mutations go through typed commands or application use cases. A tab does not
publish command events and does not write project files directly.

Examples:

```text
Compile preview button
  -> ShaderCompilePreview::Submit(request)
  -> OperationStore records operation state
  -> EngineDataBus publishes operation revision
  -> EditorEngineEventBridge imports editor-facing operation notification
  -> ShaderInspectorTab invalidates and queries operation result
```

```cpp
void ShaderInspectorTab::OnCompilePreviewClicked() {
    const auto selected = m_ctx->capabilities.Get<SelectionQueries>().CurrentAsset();
    if (!selected) {
        ShowError("com.vendor.shader-tools", "shader.preview.no_selection");
        return;
    }

    ShaderCompilePreviewRequest request;
    request.asset = *selected;
    request.profile = m_currentProfile;

    auto result = m_ctx->capabilities
        .Get<ShaderCompilePreview>()
        .Submit(request);

    if (!result) {
        PresentError(result.error());
        return;
    }

    m_observedOperation = result->operationId;
}
```

If the extension owns transient presentation state that other extension surfaces
need to observe, it may publish its own editor-session event declared in the
manifest:

```cpp
struct ShaderPreviewChangedEvent {
    static constexpr std::string_view HoroEventTypeName =
        "com.vendor.shader-tools.preview.changed";

    ShaderPreviewRevision revision;
};
```

The event should carry a revision or small identifier, not a copied compiler log,
shader blob, or full preview snapshot. Subscribers query the extension-owned or
host-owned store for details.

## Data Bus Rules

GUI surfaces use `EditorDataBus`. They do not directly subscribe to
`EngineDataBus` because their lifetime is one editor session, while
`EngineDataBus` is process-scoped.

Approved process events are imported through `EditorEngineEventBridge`:

```text
EngineDataBus
  -> EditorEngineEventBridge allowlist
  -> EditorDataBus
  -> extension tab/panel/modal page
```

If the package needs true process-level observation, it declares a separate
process-observer contribution or event-import request. The host validates the
consumer, permission, safe payload shape, and tests before granting that
capability.

Handlers must:

- run on the editor thread unless the extension point explicitly says otherwise;
- avoid blocking I/O, subprocess execution, compilation, network calls, and asset
  scanning;
- catch internal exceptions and report typed errors;
- release RAII subscription tokens during detach;
- avoid depending on subscriber order.

## Settings Page

Settings pages bind to typed configuration descriptors declared in the manifest.
They edit a draft owned by the Settings modal and never persist directly.

```text
Settings page field edit
  -> ConfigurationDraftPage::Set(key, value)
  -> Settings modal validates page and full draft
  -> Apply commits through ConfigurationService
  -> ConfigurationService publishes committed settings notification
```

A page can validate its local fields and return diagnostics:

```cpp
ValidationResult ShaderToolsSettingsPage::Validate() const {
    ValidationResult result;
    if (!IsKnownProfile(m_profile)) {
        result.diagnostics.push_back(Diagnostic{
            .code = "shader_tools.settings.unknown_profile",
            .severity = DiagnosticSeverity::Error,
            .message = "Unknown shader profile"
        });
    }
    return result;
}
```

The owning modal controls apply/cancel/preview/dirty-state policy. The extension
page cannot bypass close confirmation or publish committed settings events.

## MCP Tool

An MCP tool contribution exposes the same application capability as the GUI, but
through the MCP transport schema. The tool handler validates JSON input, calls a
use case, and returns a structured result or structured Horo error.

```text
MCP client
  -> mcp.tool com.vendor.shader-tools.compile-preview
  -> schema validation
  -> ShaderCompilePreview::Submit(request)
  -> structured result / Error payload
```

The MCP handler must not call GUI objects or editor tabs. If it runs inside
`HoroEditor`, mutations are marshalled onto the owning editor/application thread
through the approved use case.

## Workspace State

Extension surfaces may persist bounded presentation state:

```json
{
  "schemaVersion": 1,
  "selectedProfile": "debug",
  "showGeneratedSource": false,
  "expandedSections": ["inputs", "diagnostics"]
}
```

Rules:

- State is stored under the contribution ID.
- State is presentation only. It contains no capabilities, raw handles, pointers,
  secrets, unbounded logs, or generated blobs.
- The surface owns migration of its own state schema.
- If the provider is missing, the host keeps the state opaque within size limits
  and restores it when the provider returns.

## Observability

A module should declare log categories, metric instruments, profiler zones, and
error codes. The host validates descriptors before activation.

Recommended signals for the Shader Tools example:

```text
log category: com.vendor.shader-tools
metric: shader_tools.preview.request_count
metric: shader_tools.preview.failure_count
metric: shader_tools.preview.duration_ms
profiler zone: shader_tools.preview.compile
error domain: com.vendor.shader-tools
```

Do not emit one event per log line, compiler output chunk, profiler sample, or
thumbnail. Append high-volume data to a bounded store and publish a revision.

## Teardown

On detach, disable, update, shutdown, or future live unload:

1. Stop accepting new UI interaction for the contributed surface.
2. Release `EditorDataBus` subscriptions.
3. Cancel or detach extension-owned jobs.
4. Flush or discard transient presentation state according to the host policy.
5. Unregister contributed surfaces transactionally.
6. Drain queued callbacks into module code.
7. Call module deactivate.
8. Keep the dynamic library loaded until process exit unless live unload has been
   explicitly proven safe for the API.

No callback may execute against unloaded module code or destroyed editor-session
state.

## Testing Checklist

Each extension package should have contract tests for:

- manifest schema validation and path containment;
- duplicate contribution ID rejection;
- permission denial and trust-required flows;
- unsupported ABI version rejection;
- missing required function rejection;
- backend capability runs from headless host without constructing GUI state;
- GUI, MCP, command, and CLI adapters call the same backend capability path;
- operation progress/result storage, cancellation, and diagnostics are bounded
  and queryable;
- editor tab registration and fallback placement;
- Settings page validation and apply/cancel behavior;
- MCP schema validation and structured error payloads;
- `EditorDataBus` subscriptions detach on surface destruction;
- requested process-event imports require bridge allowlist approval;
- event handlers do not perform blocking work inline;
- workspace state size limits and schema migration;
- package disable/update requiring restart unless safe live unload is proven;
- shutdown leaves no live callbacks into module code.

## Common Mistakes

- Adding a tab by editing `EditorLayer` instead of contributing `editor.tab`.
- Reading another tab's state directly instead of querying an authority.
- Subscribing a GUI surface directly to `EngineDataBus`.
- Sending commands through the data bus.
- Publishing full logs, compiler output, or asset snapshots as event payloads.
- Holding borrowed host pointers after the callback that supplied them.
- Throwing exceptions across the extension ABI.
- Using STL containers or C++ object ownership across the C ABI.
- Persisting secrets, raw handles, or capability-bearing data in workspace state.
- Assuming package disable can unload native code at runtime.

## Related Architecture

- [Extension System](../architecture/extensions/plugin-system.md)
- [Editor Panel Host](../architecture/editor/editor-panel-host.md)
- [Editor Modal Host](../architecture/editor/editor-modal-host.md)
- [Editor Data Bus](../architecture/editor/editor-data-bus.md)
- [Engine Data Bus](../architecture/foundation/engine-data-bus.md)
- [Configuration System](../architecture/foundation/configuration-system.md)
- [Error And Diagnostics](../architecture/foundation/error-and-diagnostics.md)
- [Observability Architecture](../architecture/observability/observability.md)
