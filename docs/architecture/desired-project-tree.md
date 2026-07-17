# Desired Project Trees

## Purpose

This document captures the desired high-level tree structure for Horo Engine,
Horo game projects, official Horo packages, package archives, package caches,
and release outputs.

This is an architectural target, not a command to physically refactor the repo in
one step. New systems should move toward this structure through focused CMake
targets and isolated implementation directories.

During the transition, the previous source organization is moved under
`deprecated/` as a temporary migration reference. New work should target the
desired structure below; code may be moved out of `deprecated/` incrementally when
the owning module, CMake target, tests, and documentation are ready.

Mermaid is intentionally not used here. ASCII trees are easier to read in the
terminal and avoid renderer-specific centering/layout behavior.

## Horo Engine Repository

```text
horo-engine/
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── AGENTS.md
├── cmake/
│   ├── HoroCompilerOptions.cmake
│   ├── HoroTargets.cmake
│   ├── HoroDependencies.cmake
│   ├── HoroPackaging.cmake
│   ├── HoroSDK.cmake
│   └── modules/
├── docs/
│   ├── architecture/
│   │   ├── README.md
│   │   ├── desired-project-tree.md
│   │   ├── shared/
│   │   │   ├── horo-menu-bar.js
│   │   │   ├── ai-chat.js
│   │   │   └── ai-chat.css
│   │   ├── foundation/
│   │   │   ├── system-design.md
│   │   │   ├── glossary.md
│   │   │   ├── error-and-diagnostics.md
│   │   │   ├── scene-math.md
│   │   │   ├── configuration-system.md
│   │   │   ├── concurrency-and-jobs.md
│   │   │   ├── engine-data-bus.md
│   │   │   ├── ownership-and-resource-lifetime.md
│   │   │   └── platform-abstraction.md
│   │   ├── runtime/
│   │   │   ├── runtime-lifecycle.md
│   │   │   ├── scene-runtime.md
│   │   │   ├── rendering-architecture.md
│   │   │   ├── render-backend-parity-contract.md
│   │   │   ├── renderer-distribution-and-availability.md
│   │   │   ├── renderer-module-package-manifest.md
│   │   │   ├── material-and-shader-model.md
│   │   │   ├── shader-graph-editor.html
│   │   │   ├── advanced-rendering-architecture.md
│   │   │   ├── animation-architecture.md
│   │   │   ├── animation-editor.html
│   │   │   ├── vfx-and-particles-architecture.md
│   │   │   ├── particle-editor.html
│   │   │   ├── character-controller-architecture.md
│   │   │   ├── character-setup.html
│   │   │   ├── physics-architecture.md
│   │   │   ├── physics-debugger.html
│   │   │   ├── audio-architecture.md
│   │   │   ├── audio-mixer.html
│   │   │   ├── input-architecture.md
│   │   │   ├── input-mapping-editor.html
│   │   │   ├── game-ui-and-hud.md
│   │   │   ├── ui-canvas-editor.html
│   │   │   ├── networking-architecture.md
│   │   │   ├── network-debugger.html
│   │   │   ├── asset-pipeline.md
│   │   │   ├── prefab-architecture.md
│   │   │   ├── prefab-editor.html
│   │   │   ├── built-in-scene-primitives.md
│   │   │   ├── primitives-panel.html
│   │   │   ├── debug-console-and-overlays.md
│   │   │   ├── console-panel.html
│   │   │   ├── build-output.html
│   │   │   ├── platform-services-architecture.md
│   │   │   ├── platform-services-config.html
│   │   │   ├── terrain-and-foliage-architecture.md
│   │   │   ├── world-streaming-architecture.md
│   │   │   ├── save-game-and-persistence.md
│   │   │   ├── save-load-manager.html
│   │   │   ├── navigation-and-ai-architecture.md
│   │   │   ├── navigation-bake.html
│   │   │   ├── cinematic-sequencer-architecture.md
│   │   │   ├── cinematic-sequencer.html
│   │   │   ├── post-processing-and-effects-architecture.md
│   │   │   ├── post-processing-stack.html
│   │   │   ├── render-settings.html
│   │   │   ├── lod-and-culling-architecture.md
│   │   │   ├── lod-debugger.html
│   │   │   ├── accessibility-architecture.md
│   │   │   ├── decal-system-architecture.md
│   │   │   ├── decal-placement.html
│   │   │   ├── virtual-texturing-architecture.md
│   │   │   ├── virtual-texturing-debug.html
│   │   │   ├── destruction-and-fracture-architecture.md
│   │   │   ├── destruction-setup.html
│   │   │   ├── procedural-generation-architecture.md
│   │   │   ├── pcg-graph-editor.html
│   │   │   ├── multiplayer-replication-architecture.md
│   │   │   ├── vr-ar-architecture.md
│   │   │   ├── xr-setup.html
│   │   │   └── material-editor.html
│   │   ├── editor/
│   │   │   ├── gui-screen-host.md
│   │   │   ├── ui-design-system.md
│   │   │   ├── localization.md
│   │   │   ├── localization-editor.html
│   │   │   ├── editor-document-model.md
│   │   │   ├── editor-data-bus.md
│   │   │   ├── editor-panel-host.md
│   │   │   ├── editor-workspace.html
│   │   │   ├── editor-modal-host.md
│   │   │   ├── editor-modal-host-example.html
│   │   │   ├── editor-ai-agent-architecture.md
│   │   │   ├── project-model.md
│   │   │   └── project-settings.html
│   │   ├── extensions/
│   │   │   ├── gameplay-module.md
│   │   │   ├── module-config.html
│   │   │   ├── gameplay-module-boundary.md
│   │   │   ├── gameplay-behavior-authoring.md
│   │   │   ├── gameplay-runtime-integration.md
│   │   │   ├── gameplay-module-verification.md
│   │   │   └── plugin-system.md
│   │   ├── packages/
│   │   │   ├── README.md
│   │   │   ├── package-system.md
│   │   │   ├── package-restore.md
│   │   │   ├── package-lifecycle.md
│   │   │   └── package-release-integration.md
│   │   ├── interfaces/
│   │   │   ├── cli-architecture.md
│   │   │   └── mcp-architecture.md
│   │   ├── security/
│   │   │   └── application-security.md
│   │   ├── observability/
│   │   │   ├── observability.md
│   │   │   ├── observability-logging.md
│   │   │   └── observability-performance.md
│   │   ├── delivery/
│   │   │   ├── developer-environment.md
│   │   │   ├── build-system.md
│   │   │   ├── build-cache.md
│   │   │   ├── testing-architecture.md
│   │   │   ├── game-project-testing.md
│   │   │   └── quality-and-ci.md
│   │   └── release/
│   │       ├── release.md
│   │       ├── release-security.md
│   │       └── distribution-and-update.md
│   ├── guides/
│   ├── adr/
│   ├── merged/
│   └── screenshots/
├── include/
│   └── Horo/
│       ├── Foundation/
│       │   ├── Result.h
│       │   ├── ErrorCode.h
│       │   ├── Diagnostics.h
│       │   ├── Configuration.h
│       │   ├── JobSystem.h
│       │   ├── CancellationToken.h
│       │   ├── Progress.h
│       │   ├── DataBus.h
│       │   ├── Platform.h
│       │   ├── Paths.h
│       │   ├── Time.h
│       │   └── Handles.h
│       ├── Math/
│       │   └── SceneMath.h
│       ├── Runtime/
│       │   ├── Runtime.h
│       │   ├── RuntimeLifecycle.h
│       │   ├── FrameScheduler.h
│       │   ├── TimerQueue.h
│       │   ├── Scene.h
│       │   ├── Entity.h
│       │   ├── Component.h
│       │   ├── System.h
│       │   ├── Scene/
│       │   │   ├── PrimitiveCatalog.h
│       │   │   ├── PrimitiveMesh.h
│       │   │   └── SceneComponents.h
│       │   ├── AssetHandle.h
│       │   ├── AssetProvider.h
│       │   ├── Input.h
│       │   ├── Physics.h
│       │   ├── Renderer.h
│       │   ├── Render/
│       │   │   ├── Mesh.h
│       │   │   ├── RenderScene.h
│       │   │   ├── RenderBackend.h
│       │   │   ├── RenderBackendRegistry.h
│       │   │   ├── RenderFrontend.h
│       │   │   └── NullBackendModule.h
│       │   ├── Networking.h
│       │   ├── GameUI.h
│       │   ├── DebugConsole.h
│       │   ├── PlatformServices.h
│       │   └── Audio/
│       │       ├── Audio.h
│       │       ├── AudioMixer.h
│       │       ├── AudioVoice.h
│       │       ├── AudioCommandQueue.h
│       │       ├── AudioDSPNode.h
│       │       ├── AudioSpatializer.h
│       │       ├── AudioOcclusionProvider.h
│       │       ├── AudioMiddlewareBridge.h
│       │       └── AudioDecoder.h
│       ├── Gameplay/
│       │   ├── GameModule.h
│       │   ├── GameRegistrationContext.h
│       │   ├── GameRuntimeContext.h
│       │   ├── BehaviorDescriptor.h
│       │   ├── ServiceDescriptor.h
│       │   ├── ScriptModuleDescriptor.h
│       │   └── ImportedLibraryModule.h
│       ├── Assets/
│       │   ├── AssetImporter.h
│       │   ├── AssetCooker.h
│       │   ├── AssetRegistry.h
│       │   ├── AssetId.h
│       │   ├── PackageAssetReference.h
│       │   └── RuntimeArchive.h
│       ├── Packages/
│       │   ├── PackageId.h
│       │   ├── PackageManifest.h
│       │   ├── PackageContribution.h
│       │   ├── PackageSource.h
│       │   ├── PackageResolver.h
│       │   ├── PackageLockfile.h
│       │   ├── PackageCache.h
│       │   ├── PackageRestore.h
│       │   ├── PackageLifecycle.h
│       │   ├── PackageTrust.h
│       │   └── PackageValidation.h
│       ├── Extensions/
│       │   ├── ExtensionManifest.h
│       │   ├── ExtensionPoint.h
│       │   ├── ExtensionHost.h
│       │   ├── ExtensionRegistry.h
│       │   └── ExtensionCapability.h
│       ├── Observability/
│       │   ├── Logger.h
│       │   ├── LogCategory.h
│       │   ├── Metrics.h
│       │   ├── Profiler.h
│       │   ├── DiagnosticBundle.h
│       │   └── ObservabilityContribution.h
│       └── Release/
│           ├── ReleaseProfile.h
│           ├── ReleasePipeline.h
│           ├── ReleaseManifest.h
│           ├── ReleaseCandidate.h
│           ├── Signing.h
│           └── DistributionManifest.h
├── src/
│   ├── foundation/
│   │   ├── error/
│   │   ├── diagnostics/
│   │   ├── config/
│   │   ├── jobs/
│   │   ├── data_bus/
│   │   ├── math/
│   │   ├── memory/
│   │   ├── platform/
│   │   │   ├── common/
│   │   │   ├── macos/
│   │   │   ├── linux/
│   │   │   └── windows/
│   │   └── time/
│   ├── runtime/
│   │   ├── lifecycle/
│   │   ├── frame/
│   │   │   ├── frame_scheduler/
│   │   │   └── timer_queue/
│   │   ├── scene/
│   │   │   ├── entity/
│   │   │   ├── component/
│   │   │   ├── primitive/
│   │   │   │   ├── PrimitiveCatalog.cpp
│   │   │   │   └── PrimitiveMesh.cpp
│   │   │   ├── system/
│   │   │   ├── serialization/
│   │   │   └── transitions/
│   │   ├── assets/
│   │   │   ├── importer/
│   │   │   ├── cooker/
│   │   │   ├── registry/
│   │   │   ├── cache/
│   │   │   ├── runtime_provider/
│   │   │   ├── hot_reload/
│   │   │   └── archive/
│   │   ├── renderer/
│   │   │   ├── frontend/
│   │   │   ├── render_graph/
│   │   │   ├── rhi/
│   │   │   ├── backend_registry/
│   │   │   ├── module_abi/
│   │   │   ├── module_host/
│   │   │   ├── modules/
│   │   │   │   ├── null/
│   │   │   │   ├── opengl/
│   │   │   │   ├── vulkan/
│   │   │   │   ├── metal/
│   │   │   │   └── d3d12/
│   │   │   └── shaders/
│   │   ├── physics/
│   │   │   ├── broadphase/
│   │   │   ├── narrowphase/
│   │   │   ├── constraints/
│   │   │   ├── integration/
│   │   │   └── queries/
│   │   ├── audio/
│   │   │   ├── frontend/
│   │   │   ├── mixer/
│   │   │   ├── voice_registry/
│   │   │   ├── command_queue/
│   │   │   ├── decoders/
│   │   │   ├── dsp_nodes/
│   │   │   ├── spatializers/
│   │   │   ├── middleware_bridge/
│   │   │   ├── streaming/
│   │   │   └── null_backend/
│   │   ├── game_ui/
│   │   ├── input/
│   │   │   ├── Input.cpp
│   │   │   └── sdl/
│   │   │       ├── SdlInputBackend.h
│   │   │       └── SdlInputBackend.cpp
│   │   ├── networking/
│   │   ├── debug/
│   │   └── platform_services/
│   │       ├── frontend/
│   │       ├── backend/
│   │       ├── null_backend/
│   │       ├── request_queue/
│   │       ├── offline_queue/
│   │       ├── achievement_service/
│   │       ├── leaderboard_service/
│   │       ├── cloud_save_service/
│   │       ├── presence_service/
│   │       └── friends_service/
│   ├── gameplay/
│   │   ├── module_boundary/
│   │   ├── behavior/
│   │   ├── service_registry/
│   │   ├── script_runtime/
│   │   ├── imported_library_module/
│   │   └── verification/
│   ├── packages/
│   │   ├── manifest/
│   │   ├── source/
│   │   │   ├── file_source/
│   │   │   ├── url_source/
│   │   │   ├── static_index_source/
│   │   │   ├── registry_source/
│   │   │   └── git_source_future/
│   │   ├── resolver/
│   │   ├── lockfile/
│   │   ├── archive/
│   │   │   ├── reader/
│   │   │   ├── writer/
│   │   │   ├── files_manifest/
│   │   │   └── validator/
│   │   ├── cache/
│   │   │   ├── content_addressed_store/
│   │   │   ├── leases/
│   │   │   ├── gc/
│   │   │   └── quarantine/
│   │   ├── trust/
│   │   │   ├── publisher_identity/
│   │   │   ├── signing_roots/
│   │   │   ├── trust_policy/
│   │   │   └── audit/
│   │   ├── restore/
│   │   ├── lifecycle/
│   │   │   ├── install/
│   │   │   ├── enable/
│   │   │   ├── activate/
│   │   │   ├── update/
│   │   │   ├── uninstall/
│   │   │   └── transaction_journal/
│   │   ├── contribution/
│   │   │   ├── assets/
│   │   │   ├── scripts/
│   │   │   ├── behaviors/
│   │   │   ├── runtime_services/
│   │   │   ├── runtime_plugins/
│   │   │   │   ├── audio_decoders/
│   │   │   │   ├── audio_dsp_nodes/
│   │   │   │   ├── audio_spatializers/
│   │   │   │   └── audio_middleware_backends/
│   │   │   ├── platform_services_backends/
│   │   │   │   ├── steam/
│   │   │   │   ├── psn/
│   │   │   │   ├── xbox_live/
│   │   │   │   └── nintendo_online/
│   │   │   ├── editor_extensions/
│   │   │   ├── samples/
│   │   │   ├── templates/
│   │   │   └── documentation/
│   │   ├── ui_plan/
│   │   └── diagnostics/
│   ├── extensions/
│   │   ├── abi/
│   │   ├── manifest/
│   │   ├── host/
│   │   ├── registry/
│   │   └── capabilities/
│   │       ├── audio/
│   │       │   ├── decoder_capability.md
│   │       │   ├── dsp_node_capability.md
│   │       │   ├── spatializer_capability.md
│   │       │   └── middleware_backend_capability.md
│   │       ├── editor_points/
│   │       ├── asset_pipeline_points/
│   │       ├── cli_points/
│   │       └── mcp_points/
│   ├── editor/
│   │   ├── app/
│   │   ├── screens/
│   │   ├── design_system/
│   │   ├── localization/
│   │   ├── document/
│   │   ├── data_bus/
│   │   ├── panels/
│   │   ├── modals/
│   │   ├── source_editor/
│   │   ├── graph_editor/
│   │   ├── project_model/
│   │   └── mcp_bridge/
│   ├── interfaces/
│   │   ├── cli/
│   │   └── mcp/
│   ├── observability/
│   │   ├── runtime/
│   │   ├── logging/
│   │   ├── metrics/
│   │   ├── profiler/
│   │   ├── diagnostic_bundle/
│   │   └── schemas/
│   ├── security/
│   │   ├── trust/
│   │   ├── credentials/
│   │   ├── path_policy/
│   │   ├── process_policy/
│   │   ├── parser_limits/
│   │   ├── network_policy/
│   │   └── audit/
│   └── release/
│       ├── service/
│       ├── pipeline/
│       ├── profiles/
│       ├── manifest/
│       ├── candidate/
│       ├── promotion/
│       ├── signing/
│       ├── notarization/
│       ├── sbom/
│       ├── provenance/
│       ├── distribution/
│       ├── update/
│       └── dlc/
├── apps/
│   ├── HoroEditor/
│   │   ├── main.cpp
│   │   ├── app/
│   │   └── resources/
│   ├── horo-engine/
│   │   ├── main.cpp
│   │   └── cli_host/
│   └── horopak/
│       ├── main.cpp
│       └── package_tool/
├── sdk/
│   ├── cmake/
│   ├── include/
│   ├── templates/
│   │   ├── game-project/
│   │   ├── game-library-source/
│   │   ├── game-library-prebuilt/
│   │   ├── asset-package/
│   │   ├── hybrid-package/
│   │   └── editor-extension/
│   └── schemas/
│       ├── project.schema.json
│       ├── package-manifest.schema.json
│       ├── package-lock.schema.json
│       ├── release-manifest.schema.json
│       ├── observability-log.schema.json
│       └── mcp-tools.schema.json
├── examples/
│   ├── projects/
│   │   ├── minimal-2d/
│   │   ├── minimal-3d/
│   │   ├── package-restore-demo/
│   │   └── release-profile-demo/
│   ├── packages/
│   │   ├── asset-pack-basic/
│   │   ├── game-library-scheduler-minimal/
│   │   ├── hybrid-gun-pack-minimal/
│   │   └── template-package-basic/
│   └── extensions/
│       ├── editor-panel-basic/
│       ├── asset-importer-basic/
│       └── mcp-tool-basic/
├── tests/
│   ├── unit/
│   │   ├── foundation/
│   │   ├── runtime/
│   │   │   ├── frame/
│   │   │   ├── scene/
│   │   │   ├── assets/
│   │   │   ├── renderer/
│   │   │   ├── physics/
│   │   │   ├── audio/
│   │   │   │   ├── command_queue/
│   │   │   │   ├── mixer/
│   │   │   │   ├── voice_registry/
│   │   │   │   ├── decoders/
│   │   │   │   ├── dsp_nodes/
│   │   │   │   ├── spatializers/
│   │   │   │   └── middleware_bridge/
│   │   │   ├── input/
│   │   │   ├── networking/
│   │   │   ├── game_ui/
│   │   │   ├── debug/
│   │   │   └── platform_services/
│   │   │       ├── frontend/
│   │   │       ├── request_queue/
│   │   │       ├── offline_queue/
│   │   │       ├── achievement_service/
│   │   │       ├── leaderboard_service/
│   │   │       ├── cloud_save_service/
│   │   │       ├── presence_service/
│   │   │       └── friends_service/
│   │   ├── editor/
│   │   ├── packages/
│   │   ├── extensions/
│   │   ├── observability/
│   │   ├── release/
│   │   └── security/
│   ├── integration/
│   │   ├── project_open/
│   │   ├── asset_pipeline/
│   │   ├── package_restore/
│   │   ├── package_lifecycle/
│   │   ├── release_pipeline/
│   │   ├── cli/
│   │   └── mcp/
│   ├── fixtures/
│   │   ├── projects/
│   │   ├── packages/
│   │   ├── release/
│   │   └── observability/
│   ├── mocks/
│   ├── ui_scenarios/
│   └── python/
├── scripts/
│   ├── dev.py
│   ├── ci.py
│   ├── package.py
│   ├── release.py
│   ├── dependency-state.py
│   ├── validate-docs.py
│   └── generate-schemas.py
├── tools/
│   ├── package-index-server/
│   ├── schema-validator/
│   ├── asset-cooker/
│   ├── release-verifier/
│   ├── observability-viewer/
│   └── mcp-devtools/
├── vendor/
└── deprecated/
```

## Structural Notes

### Packages vs Extensions

- **Packages** are distribution units: versioned archives, assets, scripts,
  templates, and configuration. They are installed, enabled, and activated by the
  package lifecycle.
- **Extensions** are ABI-level plugin points registered with the engine host.
  They extend the editor, asset pipeline, CLI, MCP, or runtime behavior through
  stable interfaces.

A real-world integration usually uses both. For example, a Wwise middleware
integration ships as a **package** containing bank assets, project settings, and
cook rules, plus an **extension** providing `IAudioMiddlewareBackend` and
`IAudioMiddlewareEventBridge` implementations. The package contributes the
extension through `packages/contribution/runtime_plugins/audio_middleware_backends/`.

An editor panel package contributes through a similar split: the **package**
carries the panel UI definition, icons, and layout templates, while the
**extension** implements `IEditorPanel` and registers menu entry points through
`packages/contribution/editor_extensions/`.

### `vendor/` Naming

- `horo-engine/vendor/` holds third-party C/C++ dependencies built with the
  engine (source or prebuilt).
- Game projects do not use a top-level `vendor/` directory for Horo packages.
  Offline or vendored packages live under `packages/vendored/` inside the game
  project tree. `packages/source/git_source_future/` is a future package source
  type for remote Horo packages, not a replacement for `vendor/` or
  `packages/vendored/`.

### Audio Plugin Categories Across Layers

Audio plugin categories (`audio_decoders`, `audio_dsp_nodes`,
`audio_spatializers`, `audio_middleware_backends`) appear in three places:

- `src/packages/contribution/runtime_plugins/`: package-authored plugin
  binaries/assets.
- `src/extensions/capabilities/audio/`: extension capability contracts and
  registration interfaces.
- `tests/unit/runtime/audio/`: per-category unit tests.

This repetition is intentional: each place owns a different view (distribution,
ABI contract, tests). The capability contract in
`src/extensions/capabilities/audio/*.md` is the authoritative interface; the
other two mirror it for packaging and verification.

### Runtime Components As Scene Primitives

Runtime subsystems such as Audio, Game UI, and Input expose components that are
scene primitives owned by Runtime and Editor. `AudioSourceComponent`,
`AudioListenerComponent`, canvas components, and input action maps are
serialized with the scene and documented in their respective runtime subsystem
architecture documents, not duplicated inside `built-in-scene-primitives.md`.

### SDK Schemas

JSON schemas under `sdk/schemas/` are generated from the C++ data model and
canonical type definitions, not the other way around. `scripts/generate-schemas.py`
reads the engine/package/release types and emits the schema files; the schema
validator and external tooling consume them. If the model changes, regenerate
schemas rather than hand-editing the JSON files.

### Deprecated Legacy Source During Restructure

`deprecated/` is a temporary migration reference for the pre-restructure source
layout. The legacy source tree is moved there so the repository root can expose
the new desired structure without duplicate active source trees. Do not add new
systems there, do not make it part of CMake target discovery, and do not include
it in generated release artifacts. New implementation work belongs under
`include/Horo/`, `src/`, `apps/`, `sdk/`, `tools/`, or the matching
documentation area above.

When old code is migrated, move the owning module deliberately: public headers,
private implementation, CMake target wiring, tests, docs, and asset references
should move as one reviewed unit. After a module has been migrated and validated,
its backup copy may be removed in a later cleanup.

### Repository Release Policy

The desired tree assumes maintainer-driven releases. The repository should not
contain generated release-PR configuration or automation that decides when a
release exists. Version changes, changelog content, tags, and GitHub Releases are
manual maintainer actions. CI may validate and upload artifacts for an explicit
published tag, but it must not create the release decision itself.

### Package Overrides vs Local Dev Overrides

`assets/package_overrides/` contains committed, asset-level override files that
are layered over mounted package assets (see [Package Lifecycle](./packages/package-lifecycle.md)).
`.horo/local/packages.local.json` contains user-local source overrides that
replace a package source with a local path during development (see
[Package Restore](./packages/package-restore.md)). The two mechanisms operate at
different layers: project overrides patch assets, local overrides patch the
restore graph.

## Horo Game Project Repository

```text
MyGame/
├── README.md
├── CMakeLists.txt
├── .gitignore
├── .horo/
│   ├── project.json
│   ├── packages.json
│   ├── packages.lock
│   ├── plugins.json
│   ├── release-profiles.json
│   ├── editor_workspace.json
│   ├── asset_index.json
│   ├── scene_index.json
│   ├── generated_bindings.json
│   ├── local/
│   │   ├── packages.local.json
│   │   ├── trust.local.json
│   │   ├── credentials.local.json
│   │   ├── toolchains.local.json
│   │   └── editor_state.local.json
│   └── recovery/
│       ├── autosave/
│       └── transaction_journal/
├── assets/
│   ├── scenes/
│   │   ├── main.horo
│   │   ├── boot.horo
│   │   └── test/
│   ├── prefabs/
│   │   ├── player.prefab
│   │   ├── enemy.prefab
│   │   └── weapons/
│   ├── models/
│   │   ├── characters/
│   │   ├── props/
│   │   └── weapons/
│   ├── animations/
│   │   ├── humanoid/
│   │   ├── weapons/
│   │   └── cinematics/
│   ├── textures/
│   │   ├── characters/
│   │   ├── environment/
│   │   └── ui/
│   ├── materials/
│   │   ├── characters/
│   │   ├── environment/
│   │   └── weapons/
│   ├── audio/
│   │   ├── music/
│   │   ├── sfx/
│   │   ├── ambience/
│   │   └── voice/
│   ├── shaders/
│   ├── fonts/
│   ├── localization/
│   │   ├── en-US.json
│   │   └── tr-TR.json
│   └── package_overrides/
│       ├── com.vendor.weapon-pack/
│       └── com.vendor.humanoid-animation-pack/
├── packages/
│   ├── README.md
│   ├── vendored/
│   │   ├── index.json
│   │   ├── com.vendor.weapon-pack-2.0.0.horopkg
│   │   └── com.team.internal-ui-1.4.2.horopkg
│   └── imported_samples/
│       ├── com.horo.fps-starter/
│       └── com.horo.dialogue-sample/
├── src/
│   ├── GameModule.cpp
│   ├── GameModule.h
│   ├── components/
│   │   ├── PlayerComponent.h
│   │   ├── WeaponComponent.h
│   │   └── HealthComponent.h
│   ├── systems/
│   │   ├── PlayerMovementSystem.cpp
│   │   ├── WeaponSystem.cpp
│   │   └── AISystem.cpp
│   ├── services/
│   │   ├── SaveGameService.cpp
│   │   ├── DialogueService.cpp
│   │   └── GameStateService.cpp
│   ├── behaviors/
│   │   ├── PlayerController.cpp
│   │   ├── DoorBehavior.cpp
│   │   └── WeaponPickupBehavior.cpp
│   └── generated/
│       ├── behavior_descriptors.generated.cpp
│       ├── component_descriptors.generated.cpp
│       └── script_bindings.generated.cpp
├── scripts/
│   ├── game/
│   │   ├── boot.lua
│   │   ├── player.lua
│   │   ├── weapons.lua
│   │   └── ui.lua
│   ├── behaviors/
│   │   ├── rotating_platform.lua
│   │   ├── trigger_zone.lua
│   │   └── scripted_door.lua
│   └── tests/
│       ├── player_test.lua
│       └── weapon_test.lua
├── config/
│   ├── game.json
│   ├── input-actions.json
│   ├── render-profile.json
│   ├── physics-profile.json
│   ├── audio-profile.json
│   ├── audio-focus-policy.json
│   ├── package-policy.json
│   ├── trust-policy.json
│   └── release/
│       ├── windows-release.json
│       ├── macos-release.json
│       ├── linux-release.json
│       ├── steam-release.json
│       ├── itch-release.json
│       └── dedicated-server.json
├── content/
│   ├── chunks/
│   │   ├── core.chunk.json
│   │   ├── level_01.chunk.json
│   │   ├── level_02.chunk.json
│   │   └── cinematics.chunk.json
│   ├── dlc/
│   │   ├── dlc_weapons/
│   │   │   ├── dlc.json
│   │   │   └── chunk.json
│   │   └── dlc_cosmetics/
│   │       ├── dlc.json
│   │       └── chunk.json
│   └── packaging/
│       ├── asset-rules.json
│       ├── compression-rules.json
│       ├── encryption-rules.json
│       └── platform-overrides.json
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── fixtures/
│   └── smoke/
├── tools/
├── build/
├── logs/
└── dist/
```

Committed project state:

```text
.horo/project.json
.horo/packages.json
.horo/packages.lock
source assets
package provenance metadata
package overrides if project-owned
vendored .horopkg files only when intentionally offline/vendor mode
```

User-local / generated state:

```text
.horo/local/
user trust decisions
credentials
global package cache
build/
cooked/
release intermediates
```

## Official Horo Packages Repository

```text
horo-packages/
├── README.md
├── package-index.json
├── publisher.json
├── signing/
│   ├── public-publisher-keys/
│   ├── key-rotation-policy.md
│   └── revocations.json
├── packages/
│   ├── com.horo.scheduler/
│   │   ├── horo-package.toml
│   │   ├── files.manifest.json
│   │   ├── src/
│   │   ├── scripts/
│   │   ├── behaviors/
│   │   ├── services/
│   │   ├── samples/
│   │   ├── docs/
│   │   ├── tests/
│   │   ├── NOTICE.md
│   │   └── CHANGELOG.md
│   ├── com.horo.dialogue/
│   ├── com.horo.inventory/
│   ├── com.horo.quest/
│   ├── com.horo.ai.behavior-tree/
│   ├── com.horo.localization-runtime/
│   ├── com.horo.save-system/
│   ├── com.horo.tween/
│   ├── com.horo.fps-controller/
│   └── com.horo.third-person-controller/
├── asset-packs/
│   ├── com.horo.sample.gun-pack/
│   │   ├── horo-package.toml
│   │   ├── files.manifest.json
│   │   ├── assets/
│   │   │   ├── models/
│   │   │   ├── animations/
│   │   │   ├── sounds/
│   │   │   ├── materials/
│   │   │   └── textures/
│   │   ├── samples/
│   │   ├── docs/
│   │   └── NOTICE.md
│   ├── com.horo.sample.humanoid-animation-pack/
│   └── com.horo.sample.ui-sounds/
├── templates/
│   ├── com.horo.template.empty-3d/
│   ├── com.horo.template.fps-starter/
│   ├── com.horo.template.third-person/
│   └── com.horo.template.dedicated-server/
├── hybrid/
│   ├── com.horo.fps-starter-kit/
│   └── com.horo.ai-debug-kit/
├── dist/
│   ├── index/
│   │   ├── static-index.json
│   │   ├── static-index.sig
│   │   └── publisher-metadata.json
│   ├── packages/
│   │   ├── com.horo.scheduler-0.1.0.horopkg
│   │   └── com.horo.scheduler-0.1.0.horopkg.sig
│   ├── sbom/
│   ├── provenance/
│   └── notices/
├── tests/
└── scripts/
```

## `.horopkg` Archive Layout

```text
com.vendor.weapon-pack-2.0.0.horopkg
├── horo-package.toml
├── files.manifest.json
├── package-lock.json
├── publisher.cert.json
├── signature.sig
├── assets/
│   ├── models/
│   │   └── rifle.fbx
│   ├── animations/
│   │   ├── rifle_reload.anim
│   │   └── rifle_fire.anim
│   ├── audio/
│   │   ├── rifle_fire.wav
│   │   └── rifle_reload.wav
│   ├── textures/
│   └── materials/
├── scripts/
│   └── weapon_recoil.lua
├── behaviors/
│   ├── weapon_behavior.horo-behavior.json
│   └── recoil_behavior.horo-behavior.json
├── services/
│   └── recoil_service.horo-service.json
├── editor/
│   ├── weapon_stats_panel.horo-extension.json
│   └── inspectors/
├── samples/
│   ├── scenes/
│   │   └── weapon_demo.horo
│   └── prefabs/
│       └── rifle_demo.prefab
├── templates/
├── docs/
│   └── README.md
└── NOTICE.md
```

## Global Package Cache

```text
~/.cache/horo/
├── packages/
│   ├── by-hash/
│   │   └── sha256/
│   │       └── a1b2c3.../
│   │           ├── archive.horopkg
│   │           ├── extracted/
│   │           ├── files.manifest.json
│   │           ├── verification.json
│   │           └── leases.json
│   ├── by-id/
│   │   └── com.vendor.weapon-pack/
│   │       └── 2.0.0 -> ../../by-hash/sha256/a1b2c3...
│   ├── quarantine/
│   │   ├── hash-mismatch/
│   │   ├── invalid-signature/
│   │   ├── path-traversal/
│   │   └── archive-bomb/
│   ├── downloads/
│   │   └── partial/
│   ├── staging/
│   ├── pins.json
│   ├── gc-state.json
│   └── cache-index.json
├── trust/
│   ├── publisher-trust.json
│   ├── package-trust.local.json
│   ├── organization-policy.json
│   └── revocations.json
└── logs/
```

## Build And Release Output

```text
build/
├── debug/
├── tests/
├── generated/
├── asset-cache/
├── package-restore/
│   ├── restore-report.json
│   ├── package-graph.json
│   └── verification/
├── cooked/
│   ├── windows-x64/
│   ├── linux-x64/
│   └── macos-arm64/
└── release/
    ├── candidates/
    │   └── MyGame-1.0.0-rc.1/
    │       ├── release-manifest.json
    │       ├── packages-used.json
    │       ├── compatibility-report.json
    │       ├── license-notices/
    │       ├── sbom/
    │       ├── provenance/
    │       ├── symbols/
    │       ├── artifacts/
    │       │   ├── windows-x64/
    │       │   ├── linux-x64/
    │       │   └── macos-arm64/
    │       └── verification/
    │           ├── package-restore.json
    │           ├── hash-verification.json
    │           ├── signature-verification.json
    │           ├── smoke-tests.json
    │           └── notarization.json
    ├── chunks/
    │   ├── core.assets.horo
    │   ├── level_01.assets.horo
    │   └── dlc_weapons.assets.horo
    └── published/
```

## Dependency Direction

```text
foundation
├── runtime
│   └── gameplay
│       └── packages
│           ├── editor adapters
│           ├── cli adapters
│           ├── mcp adapters
│           └── release integration
└── observability
    ├── logs
    ├── metrics
    ├── profiler
    └── diagnostic bundles
```

Rules:

- Foundation owns cross-cutting system primitives (jobs, locks, data-bus
  message types, cancellation tokens, result types). Scene primitives
  (meshes, colliders, lights, cameras) are owned by Runtime and Editor as
  documented in [Built-In Scene Primitives](./runtime/built-in-scene-primitives.md).
- Runtime owns simulation, assets, rendering, audio, input, physics, and scene execution.
- Gameplay owns project/game code boundaries.
- Packages compose package primitives into restore/lifecycle/release use cases.
- Editor, CLI, and MCP are adapters over shared services.
- Data buses publish committed-state notifications only; they do not carry command requests.

## Package Primitive Composition

```text
Package primitives
├── PackageId
├── PackageSource
├── PackageManifest
├── PackageContributionDescriptor
├── PackageLockEntry
├── PackageGraph
├── PackageCacheEntry
├── PackageTrustRequirement
├── PackageRestorePlan
├── PackageLifecycleTransaction
└── PackageReleaseClosure

PackageRestoreService
├── PackageResolver
├── PackageCache
├── PackageVerifier
├── TrustCheck
└── MountPlan

PackageLifecycleService
├── RestorePlan
├── TransactionJournal
├── EnableState
└── ActivationRegistry

PackageReleaseIntegration
├── FrozenLockfile
├── RestoreVerifier
├── AssetCook
├── RuntimeClosure
└── LicenseNotices
```
