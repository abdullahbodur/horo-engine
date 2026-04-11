# Backend Parity Validation Matrix

This matrix tracks the minimum validation surface for epic `#119`.

## Contract Tests

These tests protect backend-neutral engine contracts and should remain green regardless of backend implementation details.

- `test_renderer_foundation`
  - typed backend selection
  - frame/pass lifecycle invariants
  - capability reporting
  - unsupported backend request handling
  - Vulkan bootstrap / opaque-scene submission smoke path when enabled
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

- `MONOLITH_ENGINE_ENABLE_VULKAN=ON`
- configure/build succeeds without machine-global SDK import-lib assumptions
- `test_renderer_foundation` validates:
  - support reporting
  - deterministic failure without native window handle
  - real hidden-window bootstrap
  - swapchain init + clear + present
  - opaque-scene submission acceptance
- `test_editor` validates seam behavior under unsupported editor/debug/readback capabilities

## Required Validation Commands

### Default build

```bash
cmake --build --preset debug-msvc --target test_core test_renderer_foundation test_architecture_docs test_editor
ctest --test-dir build/debug-msvc -C Debug --output-on-failure -R "test_core|test_renderer_foundation|test_architecture_docs|test_editor"
```

### Vulkan-enabled build

```bash
cmake -S . -B build/debug-msvc-vulkan-tests -G "Visual Studio 17 2022" -A x64 -DMONOLITH_ENGINE_ENABLE_VULKAN=ON -DMONOLITH_ENGINE_BUILD_TESTS=ON
cmake --build build/debug-msvc-vulkan-tests --config Debug --target test_renderer_foundation test_architecture_docs test_editor
ctest --test-dir build/debug-msvc-vulkan-tests -C Debug --output-on-failure -R "test_renderer_foundation|test_architecture_docs|test_editor"
```

## CI Expectations

- default OpenGL contract coverage stays mandatory in hosted CI
- starter integration remains the downstream compatibility gate for renderer changes
- Vulkan-enabled validation remains branch/PR-local for now, but any Vulkan/parity PR must include the command results above in its validation notes

## Known Gaps

- Vulkan editor rendering is still seam-level, not full visual parity
- debug HUD/readback behavior on Vulkan is currently fallback-only through capabilities
- parity matrix coverage will expand again once editor rendering and readback implementations land
