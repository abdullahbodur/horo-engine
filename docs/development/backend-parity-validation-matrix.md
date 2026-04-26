# Backend Parity Validation Matrix

This matrix tracks the minimum validation surface for epic `#119`.

## Contract Tests

These tests protect backend-neutral engine contracts and should remain green regardless of backend implementation details.

- `test_renderer_foundation`
  - typed backend selection
  - frame/pass lifecycle invariants
  - capability reporting
  - unsupported backend request handling
  - Vulkan bootstrap / opaque-scene real indexed draw execution-or-diagnostics path when enabled
  - Vulkan offscreen target lifecycle + handle metadata + resize generation validation
  - Vulkan editor viewport target provisioning + per-frame stability + resize generation validation
  - Vulkan color/depth readback capability-gated success/failure diagnostics
- `test_architecture_docs`
  - architecture document discoverability
  - backend-boundary guardrails in higher-level code
  - editor/debug/readback seam enforcement
- `test_editor`
  - preview handle plumbing
  - ImGui backend selection seam
  - editor fallback behavior on unsupported backend capabilities

## Backend-Specific Smoke Coverage

### OpenGL / default build

- standard CI matrix: Linux, Windows, macOS
- starter integration workflow
- offscreen preview handles available
- screenshot/readback available
- debug draw + debug HUD available

### Vulkan-enabled build

- `HORO_ENGINE_ENABLE_VULKAN=ON`
- configure/build succeeds without machine-global SDK import-lib assumptions
- `test_renderer_foundation` validates:
  - support reporting
  - deterministic failure without native window handle
  - real hidden-window bootstrap
  - swapchain init + clear + present
  - opaque-scene submission acceptance
  - offscreen render-target create/resize/destroy metadata stability
- `test_editor` validates seam behavior under unsupported editor/debug/readback capabilities
  and keeps OpenGL-oriented editor behavior stable

## Required Validation Commands

### Default build

```bash
cmake --build --preset debug-msvc --target test_core test_renderer_foundation test_architecture_docs test_editor
ctest --test-dir build/debug-msvc -C Debug --output-on-failure -R "test_core|test_renderer_foundation|test_architecture_docs|test_editor"
```

### Vulkan-enabled build

```bash
cmake -S . -B build/debug-msvc-vulkan-tests -G "Visual Studio 17 2022" -A x64 -DHORO_ENGINE_ENABLE_VULKAN=ON -DHORO_ENGINE_BUILD_TESTS=ON
cmake --build build/debug-msvc-vulkan-tests --config Debug --target test_renderer_foundation test_architecture_docs test_editor
ctest --test-dir build/debug-msvc-vulkan-tests -C Debug --output-on-failure -R "test_renderer_foundation|test_architecture_docs|test_editor"
```

## CI Expectations

- default OpenGL contract coverage stays mandatory in hosted CI
- starter integration remains the downstream compatibility gate for renderer changes
- Vulkan-enabled validation remains branch/PR-local for now, but any Vulkan/parity PR must include the command results above in its validation notes

## Known Gaps

- debug HUD parity on Vulkan remains capability-gated while richer tooling panels mature
- broader Vulkan-enabled CI coverage remains branch/PR-local until hosted-runner stability is proven
