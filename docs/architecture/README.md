# Architecture

This directory defines the required architecture of Horo Engine. Documents are
normative: they describe the system to build and the rules all implementations
must follow.

## How To Read

For a first architecture review, read:

1. [System Design](./foundation/system-design.md)
2. [Architecture Glossary](./foundation/glossary.md)
3. [Error And Diagnostics](./foundation/error-and-diagnostics.md)
4. [Configuration System](./foundation/configuration-system.md)
5. [Concurrency And Job System](./foundation/concurrency-and-jobs.md)
6. [Runtime Lifecycle](./runtime/runtime-lifecycle.md)
7. [Scene Runtime](./runtime/scene-runtime.md)
8. the host or subsystem documents relevant to the change

`system-design.md` defines the map and dependency direction. Detailed documents
own their specific contracts; the overview does not override them.

## Directory Layout

```text
architecture/
  README.md          reading order, index, core rules, review checklist
  desired-project-tree.md target repository, game project, package, cache, and release trees
  foundation/        system-wide contracts and dependency boundaries
  runtime/           engine runtime subsystems and asset flow
  editor/            GUI, documents, workspace, panels, tabs, and modals
  extensions/        gameplay modules and external plugins
  packages/          asset packs, game libraries, hybrid packages, templates
  interfaces/        CLI and MCP host adapters
  security/          running-application trust and capability policy
  delivery/          development, build, test, quality, and CI
  observability/     logs, metrics, profiling, and diagnostics
  release/           artifacts, signing, distribution, and updates
```

Folder placement indicates the primary owner, not permission to bypass the
dependency direction in [System Design](./foundation/system-design.md).

## Foundation

- [Desired Project Trees](./desired-project-tree.md): target Horo Engine repo,
  game project, package repository, `.horopkg`, cache, and release output tree
  structures.
- [System Design](./foundation/system-design.md): hosts, modules, dependency
  direction, application boundaries, and composition rules.
- [Architecture Glossary](./foundation/glossary.md): canonical cross-document
  terminology.
- [Error And Diagnostics](./foundation/error-and-diagnostics.md): results,
  stable errors, diagnostics, assertions, and host translation.
- [Configuration System](./foundation/configuration-system.md): typed settings,
  precedence, immutable snapshots, dynamic reload, and secret references.
- [Concurrency And Job System](./foundation/concurrency-and-jobs.md): workers,
  task groups, cancellation, progress, affinity, backpressure, and shutdown.
- [Ownership And Resource Lifetime](./foundation/ownership-and-resource-lifetime.md):
  RAII, handles, leases, allocation domains, caches, and cross-thread lifetime.
- [Platform Abstraction](./foundation/platform-abstraction.md): paths, files,
  windows, events, processes, clocks, native dialogs, credentials, and crash
  services.
- [Engine Data Bus](./foundation/engine-data-bus.md): process-scoped typed
  notifications.

## Runtime

- [Runtime Lifecycle](./runtime/runtime-lifecycle.md): startup, frame phases,
  fixed-step simulation, play state, suspension, and shutdown.
- [Scene Runtime](./runtime/scene-runtime.md): runtime scene definitions, ECS
  ownership, systems, structural changes, references, and scene transitions.
- [Rendering Architecture](./runtime/rendering-architecture.md): render
  extraction, frontend/backend boundaries, render graph, GPU resources, and
  null rendering.
- [Material And Shader Model](./runtime/material-and-shader-model.md): standard
  PBR material model, shader variants, material instances, feature tiers, and
  pipeline cache.
- [Advanced Rendering Architecture](./runtime/advanced-rendering-architecture.md):
  lighting, shadows, global illumination, reflections, post-processing, TAA,
  upscaling, and high-end feature tiers.
- [Animation Architecture](./runtime/animation-architecture.md): skeletal
  animation, clips, animation graphs, blend trees, IK, root motion, retargeting,
  and animation events.
- [VFX And Particles Architecture](./runtime/vfx-and-particles-architecture.md):
  CPU/GPU particle systems, VFX graphs, decals, and volumetric effects.
- [Character Controller Architecture](./runtime/character-controller-architecture.md):
  kinematic capsule controller, slopes, steps, moving platforms, surface
  materials, and surface events.
- [Physics Architecture](./runtime/physics-architecture.md): fixed-step world
  ownership, transform authority, collision events, queries, and determinism.
- [Audio Architecture](./runtime/audio-architecture.md): real-time mixer, voices,
  streaming, devices, scene integration, and null audio.
- [Input Architecture](./runtime/input-architecture.md): input snapshots, action
  maps, focus, capture, modal routing, and simulation input frames.
- [Game UI And HUD](./runtime/game-ui-and-hud.md): runtime game menus, HUDs,
  canvases, UI primitives, focus/navigation, and UI rendering.
- [Networking Architecture](./runtime/networking-architecture.md): optional
  transports, typed protocols, bounded I/O, runtime integration, and remote
  security.
- [Asset Pipeline](./runtime/asset-pipeline.md): import, cook, package, runtime
  loading, cache, and hot reload.
- [Prefab Architecture](./runtime/prefab-architecture.md): authored prefab
  assets, scene instance references, expansion, and cook-time inlining.
- [Built-In Scene Primitives](./runtime/built-in-scene-primitives.md): core
  procedural meshes, collider shapes, and scene object primitives available
  without external packages.
- [Debug Console And Overlays](./runtime/debug-console-and-overlays.md): runtime
  console commands, variables, in-game terminal, debug overlays, and diagnostics.
- [Platform Services Architecture](./runtime/platform-services-architecture.md):
  achievements, leaderboards, cloud saves, presence, friends, backend adapters,
  offline queues, and null services.
- [Terrain And Foliage Architecture](./runtime/terrain-and-foliage-architecture.md):
  heightfields, terrain layers, instanced foliage, wind, LOD, collision,
  streaming, and editor tools.
- [World Streaming Architecture](./runtime/world-streaming-architecture.md):
  streaming cells, volumes, priority, budgets, server authority, and editor
  world-composition tools.
- [Save Game And Persistence](./runtime/save-game-and-persistence.md): runtime
  save state, slot format, migration, cloud save, integrity, and secure archive
  loading.
- [Navigation And AI Architecture](./runtime/navigation-and-ai-architecture.md):
  NavMesh, pathfinding, dynamic obstacle overlays, perception, crowd, blackboard,
  and editor bake tooling.

- [Cinematic Sequencer Architecture](./runtime/cinematic-sequencer-architecture.md):
  timeline, tracks, typed bindings, playback, events, and external control
  security.
- [Post-Processing And Effects Architecture](./runtime/post-processing-and-effects-architecture.md):
  screen-space effects, HDR post chain, tonemapping, color grading, and
  accessibility pass ordering.
- [LOD And Culling Architecture](./runtime/lod-and-culling-architecture.md): mesh
  LOD, HLOD, impostors, occlusion, GPU-driven culling, and editor diagnostics.
- [Accessibility Architecture](./runtime/accessibility-architecture.md): captions,
  colorblind filters, input remapping, screen reader, assists, privacy, and
  persistence.
- [Decal System Architecture](./runtime/decal-system-architecture.md): deferred
  decals, forward fallbacks, material domain, pooling, ownership, and lifetime.
- [Virtual Texturing Architecture](./runtime/virtual-texturing-architecture.md):
  page tables, feedback, streaming, asset-provider cache, and backend-neutral
  resources.
- [Destruction And Fracture Architecture](./runtime/destruction-and-fracture-architecture.md):
  fracture assets, chunk physics, debris, authority, and network reconstruction.
- [Procedural Generation Architecture](./runtime/procedural-generation-architecture.md):
  PCG graphs, point clouds, validation, transactions, server authority, and
  streaming-cell ownership.
- [Multiplayer Replication Architecture](./runtime/multiplayer-replication-architecture.md):
  replication roles, property deltas, RPCs, prediction, interest management,
  dedicated servers, and security.
- [VR / AR Architecture](./runtime/vr-ar-architecture.md): OpenXR, stereo
  rendering, foveated capabilities, motion/hand tracking, AR passthrough, and
  privacy.

## Extensions

- [Gameplay Module](./extensions/gameplay-module.md): overview for
  project-owned gameplay modules, behavior authoring, runtime integration, and
  verification.
  - [Gameplay Module Boundary](./extensions/gameplay-module-boundary.md):
    native module ABI, registration, capability context, services, hot reload,
    and diagnostics.
  - [Gameplay Behavior Authoring](./extensions/gameplay-behavior-authoring.md):
    editor/IDE workflow, object-attached behaviors, script discovery, visual
    scripting, and iteration-speed goals.
  - [Gameplay Runtime Integration](./extensions/gameplay-runtime-integration.md):
    game-owned asset types, input actions, runtime systems, scene/play
    lifecycle, and component persistence.
  - [Gameplay Module Verification](./extensions/gameplay-module-verification.md):
    contract and regression coverage.
- [Extension System](./extensions/plugin-system.md): editor/tool extension packages, C ABI,
  manifests, permissions, registration, and lifecycle.

## Packages

- [Package System](./packages/package-system.md): core package kinds, contributions,
  manifest, sources, resolver, lockfile, cache, and trust model.
- [Package Restore](./packages/package-restore.md): clean-machine project restore,
  bootstrap, CI, offline restore, dev overrides, and non-interactive policy.
- [Package Lifecycle](./packages/package-lifecycle.md): install, trust, enable,
  activation, update, uninstall, ownership states, migration, and conflicts.
- [Package Release Integration](./packages/package-release-integration.md):
  lockfile freeze, `assets.horo`, chunks, DLC, editor-only exclusion, and license notices.

## Editor And GUI

- [GUI Screen Host](./editor/gui-screen-host.md): Welcome, Project Browser, creation
  flows, Editor Workspace, navigation, and leave guards.
- [GUI Design System](./editor/ui-design-system.md): reusable ImGui components, design
  tokens, themes, and accessibility.
- [Localization](./editor/localization.md): message resources, locale resolution,
  formatting, fallback, fonts, and layout verification.
- [Localization Implementation Plan](./editor/localization-implementation-plan.md):
  editor extractor, settings integration, runtime foundation, and migration phases.
- [Localization Extractor Report](./editor/localization-extractor-report.json):
  generated baseline of editor text candidates and technical exclusions.
- [Editor Document Model](./editor/editor-document-model.md): scene documents,
  commands, transactions, history, save, autosave, and recovery.
- [Editor Data Bus](./editor/editor-data-bus.md): session-local typed notifications,
  authoritative editor models, and the process-event bridge.
- [Editor Panel Host](./editor/editor-panel-host.md): layout tree, panel and tab
  lifetime, toolbars, and status bar.
- [Editor Modal Host](./editor/editor-modal-host.md): exclusive-focus screen-like
  workflow surfaces above the editor workspace.
- [Editor AI Agent Architecture](./editor/editor-ai-agent-architecture.md):
  editor-integrated conversational agent, viewport inline editing, MCP tool-calling,
  magic AI tools, conversation persistence, and privacy model.
- [Project Model](./editor/project-model.md): project directory, settings, workspace
  persistence, scene documents, and asset index.

## Hosts And Transport

- [CLI Architecture](./interfaces/cli-architecture.md): command registry, parsing, output,
  exit codes, progress, cancellation, and headless execution.
- [MCP Architecture](./interfaces/mcp-architecture.md): MCP transport, tool registry,
  request lifecycle, errors, and threading.
- [Application Security](./security/application-security.md): project and plugin trust,
  path/process policy, MCP access, credentials, and parser limits.

## Delivery And Operations

- [Developer Environment](./delivery/developer-environment.md): setup, toolchains, IDEs,
  and daily workflow.
- [Build System](./delivery/build-system.md): CMake targets, presets, dependencies, and
  module creation.
- [Build Cache](./delivery/build-cache.md): compiler and dependency caching.
- [Testing Architecture](./delivery/testing-architecture.md): test layers, fixtures,
  mocks, GUI/MCP harnesses, determinism, and performance budgets.
- [Quality And CI](./delivery/quality-and-ci.md): build matrix, coverage, gates, and CI
  artifacts.
- [Observability Architecture](./observability/observability.md): observability decisions and
  reading paths.
- [Logging, Context, And Diagnostics](./observability/observability-logging.md): log schema,
  MDC, sinks, storage, privacy, diagnostic bundles, and tests.
- [Metrics And Profiling](./observability/observability-performance.md): CPU, memory, frame,
  subsystem metrics, profiler captures, and performance views.
- [Release Architecture](./release/release.md): release jobs, artifacts, reproducibility,
  cancellation, and publishing.
- [Release Security](./release/release-security.md): trust boundaries, credentials,
  signing, archive protection, and CI controls.
- [Distribution And Update](./release/distribution-and-update.md): signed update
  manifests, staging, activation, compatibility, rollback, and offline packages.

## UI Reference Designs

HTML reference designs are static panel, modal, or screen mockups that live next
to their owning architecture documents. Panel/tab references do not include the
application menu bar; app-level screen references do.

- Runtime panels and screens: [Physics Debugger](./runtime/physics-debugger.html),
  [Animation Editor](./runtime/animation-editor.html), [Particle Editor](./runtime/particle-editor.html),
  [Audio Mixer](./runtime/audio-mixer.html), [Input Mapping Editor](./runtime/input-mapping-editor.html),
  [Prefab Editor](./runtime/prefab-editor.html), [Material Editor](./runtime/material-editor.html),
  [Network Debugger](./runtime/network-debugger.html), [Platform Services Config](./runtime/platform-services-config.html),
  [Render Settings](./runtime/render-settings.html), [Character Setup](./runtime/character-setup.html),
  [UI Canvas Editor](./runtime/ui-canvas-editor.html), [Scene Primitives](./runtime/primitives-panel.html),
  [Build Output](./runtime/build-output.html), [Cinematic Sequencer](./runtime/cinematic-sequencer.html),
  [Navigation Bake](./runtime/navigation-bake.html), [Save/Load Manager](./runtime/save-load-manager.html),
  [Post-Processing Stack](./runtime/post-processing-stack.html), [LOD Debugger](./runtime/lod-debugger.html),
  [PCG Graph Editor](./runtime/pcg-graph-editor.html), [Decal Placement](./runtime/decal-placement.html),
  [Destruction Setup](./runtime/destruction-setup.html), [Virtual Texturing Debug](./runtime/virtual-texturing-debug.html),
  [XR Setup](./runtime/xr-setup.html), and [Shader Graph](./runtime/shader-graph-editor.html).
- Editor/extension panels: [Localization Editor](./editor/localization-editor.html),
  [Project Settings](./editor/project-settings.html), and [Gameplay Integration Config](./extensions/module-config.html).

## Core Rules

- Horo Engine is provided through GUI and CLI hosts.
- Both hosts expose MCP through shared application services.
- GUI, CLI, and MCP do not duplicate engine business logic.
- Modules have explicit CMake targets and enforced dependency direction.
- The GUI is one editor application; welcome and project-browser flows are GUI
  screens, not a separate application or module.
- Runtime modules remain independent from GUI and transport concerns.
- Expected failures use typed results and stable error codes; logs are not
  control flow.
- Configuration is typed, validated, provenance-aware, and read through
  immutable snapshots.
- Jobs use structured ownership, cooperative cancellation, bounded queues, and
  explicit owner-thread handoff.
- Runtime simulation uses fixed ticks and render interpolation.
- Persistent identity, runtime handles, and memory ownership are distinct.
- ImGui is isolated to the GUI implementation.
- Reusable UI components expose typed size, variant, icon, and interaction
  contracts and consume semantic design tokens.
- Runtime Game UI/HUD is game content and remains separate from HoroEditor
  panels, tabs, modals, and ImGui design-system widgets.
- GUI rendering code contains no visual literals; packaged and custom theme
  resources are resolved dynamically into an immutable frame theme.
- GUI features receive narrow application capabilities rather than an omnibus
  application service.
- CI validates engine, GUI, CLI, and MCP behavior.
- Code coverage and GUI scenario coverage are separate complementary signals.
- Ownership, thread access, cancellation, and shutdown order are explicit.
- Commands and use cases perform operations; data buses publish notifications
  only after state commits.
- C++, Python, editor, CLI, release jobs, and games emit the same structured log
  schema and propagate diagnostic context across asynchronous work.
- Hosts expose bounded CPU, memory, frame, and subsystem metrics; detailed
  profiler tracing is explicit and build-profile gated.
- High-volume logs, profiler samples, and output streams live in bounded
  queryable stores; buses publish revision or availability notifications.
- Editor modals are transient GUI workflow surfaces; they are not tabs, layout
  nodes, application screens, or state authorities.
- Editor document mutations use typed commands and transactions; autosave is
  recovery state and never silently replaces the user's saved file.
- Raw device input is normalized into snapshots and routed by interaction scope;
  high-frequency input does not travel through data buses.
- Native gameplay and plugin extensions cross explicit registration boundaries;
  external plugins use a versioned C ABI rather than a cross-version C++ ABI.
- Real-time audio callbacks allocate nothing, block on nothing, and perform no
  ordinary logging.
- Remote communication uses explicit bounded protocols; process-local data-bus
  events are never serialized automatically.
- Projects, plugins, assets, subprocess requests, and network input are treated
  according to explicit trust and resource-limit policy.

## Review Checklist

- Does the change preserve the documented dependency direction?
- Is the correct authority responsible for the committed state?
- Are ownership, thread affinity, and shutdown behavior explicit?
- Does fallible behavior return a typed error with a stable code?
- Are settings read through the configuration snapshot rather than directly
  from files or environment variables?
- Is asynchronous work owned by a task group with cancellation and
  backpressure?
- Is reusable logic in the owning module instead of a host adapter?
- Can applicable operations run through GUI, CLI, and MCP consistently?
- Are ImGui and renderer-backend details contained within their modules?
- Does UI styling use shared component contracts and semantic theme tokens
  without source-level visual literals?
- Does each GUI feature receive only the application capabilities it needs?
- Is high-volume data owned by a bounded/queryable store instead of copied
  through event payloads?
- Are logs structured, redacted, correctly leveled, and correlated with the
  active operation context?
- Are metrics typed, bounded, low-cardinality, and explicit about units and
  availability?
- Is expensive profiler instrumentation opt-in and excluded from shipping where
  required?
- Are runtime mutations committed at the correct frame or subsystem
  synchronization point?
- Are stable IDs, handles, snapshots, and borrows used for their intended
  lifetimes?
- Are test type, coverage expectation, and CI impact clear?
