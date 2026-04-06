# AGENTS.md

This file provides guidance to Codex (Codex.ai/code) when working with code in this repository.

## Build Commands

```bash
make              # Debug build (default)
make test         # Build and run all tests
make release      # Optimized release build
make configure    # Run CMake configuration only
make format       # Format all sources in-place (clang-format, Google style)
make format-check # Check formatting without changes (used in CI)
make coverage     # Generate HTML coverage report at build/coverage/html/index.html
make clean        # Remove debug build dir
make clean-all    # Remove all build dirs
```

**CMake directly:**
```bash
cmake --preset debug && cmake --build --preset debug
ctest --preset debug --output-on-failure
```

**Run a single test binary:**
```bash
./build/debug/bin/test_ecs         # or any test binary name
./build/debug/bin/test_editor
```

**Linux build deps:**
```bash
libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
libwayland-dev libxkbcommon-dev libgl1-mesa-dev libglu1-mesa-dev
```

## Testing

Framework: **Catch2** (BSL-1.0). All test sources are in `tests/`.

23 test executables covering: math, physics (broadphase, narrowphase/GJK, constraints, integration), ECS (registry, scene, systems), camera, transforms, editor, and OBJ import.

## Architecture

**Horo Engine** is a modular C++20 3D game engine shipped as a static library (`libMonolithEngine.a`), intended to be embedded as a git submodule.

### Module Overview

| Module | Path | Responsibility |
|--------|------|----------------|
| **core** | `core/` | `Application` base class (OnInit/OnUpdate/OnFixedUpdate/OnRender/OnShutdown), `Window` (GLFW), `Logger`, `Time` |
| **math** | `math/` | Vec2/3/4, Mat3/4, Quaternion, Transform — fully custom, GLM only used as reference |
| **physics** | `physics/` | `PhysicsWorld`: broadphase (brute-force), narrowphase (GJK + SAT), constraint solver (friction/restitution), semi-implicit Euler integration |
| **renderer** | `renderer/` | OpenGL 4.1 rendering: `Renderer`, `Camera`, `Mesh`, `Shader`, `Material`, `Light`, `Texture`, `ObjLoader` |
| **input** | `input/` | `Input` with key/mouse state tracking (down, pressed first-frame, released), mouse delta and scroll |
| **scene** | `scene/` | ECS: type-indexed `Registry` (automatic pool management, no manual Add<T> registrations), `Entity`, `Scene`, `System` |
| **scene/components** | `scene/components/` | `TransformComponent`, `MeshComponent`, `RigidBodyComponent`, `CameraComponent`, `BehaviorComponent` |
| **scene/systems** | `scene/systems/` | `RenderSystem`, `PhysicsSystem`, `CameraSystem`, `BehaviorSystem` |
| **editor** | `editor/` | ImGui-based scene editor: entity hierarchy, component inspector, asset import, raycasting (entity picking), JSON serialization |
| **vendor** | `vendor/` | glad, glm, stb; GLFW/ImGui/nlohmann-json/Catch2 via CMake FetchContent |

### Data Flow

```
Application
  ├── Window (GLFW)
  ├── Input (GLFW callbacks)
  ├── Scene
  │   ├── Registry (ECS)
  │   ├── PhysicsWorld
  │   │   ├── Broadphase → collision pairs
  │   │   ├── Narrowphase (GJK/SAT) → contact manifolds
  │   │   ├── ConstraintSolver → impulses
  │   │   └── SemiImplicitEuler → integration
  │   └── Systems (Render, Physics, Camera, Behavior)
  ├── Renderer (OpenGL 4.1)
  └── EditorLayer (ImGui)
      ├── SceneDocument + SceneSerializer (JSON)
      ├── Raycaster (entity picking)
      └── EditorAssetImport
```

### Editor Architecture

The editor (`editor/`) is structured as:
- `EditorLayer` — top-level ImGui layer, owns the editor lifecycle
- `EditorUiLogic` — UI state machine, handles panels, modals, toolbar
- `SceneDocument` — document model (selected entity, dirty state, undo stack foundation)
- `SceneSerializer` — reads/writes `.horo` JSON scene files via `nlohmann/json`
- `EditorSchema` — component schema definitions for the inspector (drives field rendering generically)
- `Raycaster` — mouse ray → entity picking using physics colliders

### ECS Design

`Registry` uses a type-indexed map of `IComponentPoolBase*` (erased) with concrete `ComponentPool<T>`. No explicit component registration—pools are created on first use. `Scene` owns a `Registry` and routes `Systems` through it.

## Code Style

Google C++ Style Guide, enforced by clang-format. CI fails on formatting violations (`make format-check`).

## CI

GitHub Actions runs on Linux (GCC), macOS (Clang), and Windows (MSVC 2022). Coverage is collected on Linux via lcov. See `.github/workflows/ci.yml`.
