# Horo Engine

A high-performance, fully open-source 3D game engine written in modern C++20. Built as a static library with a clean, modular architecture — designed to be embedded directly into your project as a **git submodule**, giving AI coding assistants full visibility into the entire engine source.

## Why Submodule?

Most engines ship as opaque binaries or massive SDKs. Horo Engine is intentionally small and readable. Add it as a submodule and your AI assistant (Claude, Copilot, Cursor, etc.) can read, reason about, and integrate against the actual engine source — not just documentation.

```bash
git submodule add https://github.com/abdullahbodur/horo-engine engine
```

Then in your `CMakeLists.txt`:

```cmake
add_subdirectory(engine)
target_link_libraries(MyGame PRIVATE MonolithEngine)
```

That's it. No package manager, no prebuilt binaries.

---

## Features

### Math Library
- `Vec2`, `Vec3`, `Vec4` — arithmetic, dot/cross product, normalization, Lerp
- `Mat3`, `Mat4` — translation, rotation, scaling, matrix composition
- `Quaternion` — axis-angle, Euler angles, Slerp, matrix conversion
- `Transform` — composable local/world space transforms

### Physics Engine
- **Rigid body dynamics** — linear/angular motion, mass, inertia tensors
- **Broad-phase** — brute-force O(n²) collision pair detection
- **Narrow-phase** — GJK (Gilbert-Johnson-Keerthi) algorithm + SAT (Separating Axis Theorem)
- **Constraints** — contact solving with friction, restitution, damping
- **Integration** — semi-implicit Euler for stable simulation
- **Colliders** — Box and Sphere with automatic inertia computation

### Renderer (OpenGL 4.1)
- Procedural mesh generation: sphere, box, cylinder, pyramid, plane, quad
- OBJ asset loading with mesh caching
- GLSL shader pipeline (basic + debug wireframe)
- Per-material properties with shader parameter binding
- Multi-light support
- Perspective camera with view/projection matrix management
- Wireframe debug mode, debug HUD overlay
- In-engine screenshot capture

### Entity Component System (ECS)
- Type-indexed component registry with automatic pool management
- Lightweight entity ID system with lifecycle management
- Built-in components: `TransformComponent`, `MeshComponent`, `RigidBodyComponent`, `CameraComponent`, `BehaviorComponent`, `PlayerTagComponent`
- System-based update loops (variable timestep + fixed timestep)

### Scene Editor (ImGui)
- Real-time entity creation, deletion, duplication
- Transform inspector with drag controls
- Asset registry — import OBJ files, create renderables from assets
- Click-based entity picking in viewport
- Fly-camera mode (Tab key)
- Scene serialization to/from JSON
- Keyboard shortcuts with searchable help popup (F1)
- Quick-open search (Ctrl+P)
- Status bar, confirmation modals for destructive operations
- Built-in MCP server with Claude/Codex integration snippets
- Undo/redo foundation

### Input System
- Keyboard: key down, pressed (first frame only), released
- Mouse: button states, position, delta, scroll wheel
- Full key/mouse code enumerations

---

## Getting Started

### Prerequisites

| Tool | Version |
|------|---------|
| C++ Compiler | GCC / Clang / MSVC with C++20 support |
| CMake | 3.25+ |
| Build system | Ninja (Linux/macOS) or Visual Studio 2022 (Windows) |

**Linux additional dependencies:**
```bash
sudo apt install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libwayland-dev libxkbcommon-dev
```

### Standalone Build

```bash
git clone https://github.com/abdullahbodur/horo-engine
cd horo-engine

make          # debug build
make test     # run all 24 unit tests
make release  # optimized build
make coverage # HTML coverage report (requires lcov)
```

**CMake directly:**
```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

**Available presets:**

| Preset | Platform | Description |
|--------|----------|-------------|
| `debug` | Linux/macOS | Ninja, tests enabled |
| `release` | Linux/macOS | Ninja, optimized |
| `debug-msvc` | Windows | Visual Studio 2022 |
| `release-msvc` | Windows | Visual Studio 2022, optimized |
| `coverage` | Linux/macOS | GCC/Clang with coverage flags |

**Outputs:**
- Static library: `build/debug/lib/libMonolithEngine.a`
- Test binaries: `build/debug/bin/`
- Shaders (copied automatically): `build/debug/bin/shaders/`

---

## Embedding in Your Project

```
your-project/
├── engine/          ← git submodule (this repo)
├── src/
│   └── main.cpp
└── CMakeLists.txt
```

**CMakeLists.txt:**
```cmake
cmake_minimum_required(VERSION 3.25)
project(MyGame)

set(CMAKE_CXX_STANDARD 20)
add_subdirectory(engine)

add_executable(MyGame src/main.cpp)
target_link_libraries(MyGame PRIVATE MonolithEngine)
```

**main.cpp:**
```cpp
#include "core/Application.h"
#include "scene/Scene.h"
#include "input/Input.h"

class MyGame : public Application {
    Scene scene;

    void OnInit() override {
        Entity ball = scene.CreateEntity({0, 5, 0});
        // add components, systems...
    }

    void OnFixedUpdate(float dt) override {
        scene.physics.Step(dt);
    }

    void OnUpdate(float dt) override {
        scene.UpdateSystems(dt);
    }
};

int main() {
    MyGame game;
    game.Run();
}
```

---

## API Reference

### Application

```cpp
class Application {
    void Run();
    Window& GetWindow();

    virtual void OnInit() {}
    virtual void OnUpdate(float dt) {}        // variable timestep
    virtual void OnFixedUpdate(float dt) {}  // fixed timestep (physics)
    virtual void OnRender(float alpha) {}    // interpolation factor
    virtual void OnShutdown() {}
};
```

### ECS Registry

```cpp
Registry registry;

Entity e = registry.Create();
registry.Add<TransformComponent>(e, { .position = {0,1,0} });
registry.Add<MeshComponent>(e, { .mesh = &myMesh });

auto& transform = registry.Get<TransformComponent>(e);
bool has = registry.Has<MeshComponent>(e);

registry.Remove<MeshComponent>(e);
registry.Destroy(e);

// Iterate all entities with a component
for (Entity e : registry.GetEntities<TransformComponent>()) { ... }
```

---

## Architecture Docs

The engine architecture guidance lives under [docs/architecture](./docs/architecture/README.md).

That doc set defines:
- module boundaries and allowed dependencies
- ownership and shutdown rules
- the preferred error/result model
- threading and safe mutation guidance

Reviewer rule:
- new headers are internal by default
- every new public type should clearly identify its owning module

### Physics

```cpp
PhysicsWorld world;
world.gravity = {0, -9.81f, 0};

RigidBody sphere = RigidBody::MakeSphere(1.0f, 1.0f);   // radius, mass
RigidBody box    = RigidBody::MakeBox({1,1,1}, 2.0f);    // halfExtents, mass

RigidBody* body = world.AddBody(sphere);
body->AddForce({0, 100, 0});
body->AddTorque({0, 0, 10});

world.Step(1.0f / 60.0f);
```

### Renderer

```cpp
Renderer::BeginScene(camera);
Renderer::SetLights(lights);
Renderer::Submit(mesh, modelMatrix, material);
Renderer::EndScene();

// Procedural meshes
Mesh sphere = Mesh::CreateSphere(1.0f, 32, 32);
Mesh box    = Mesh::CreateBox(1.0f, 1.0f, 1.0f);
Mesh plane  = Mesh::CreatePlane(10.0f);
```

### Input

```cpp
// Keyboard
Input::IsKeyDown(Key::Space)     // held
Input::IsKeyPressed(Key::Space)  // first frame only
Input::IsKeyReleased(Key::Space)

// Mouse
Input::IsMouseButtonDown(MouseButton::Left)
Input::GetMousePosition()   // Vec2
Input::GetMouseDelta()      // Vec2
Input::GetScrollDelta()     // float
```

### Math

```cpp
Vec3 a = {1, 0, 0};
Vec3 b = {0, 1, 0};
float d = Vec3::Dot(a, b);
Vec3  c = Vec3::Cross(a, b);
Vec3  n = a.Normalized();
Vec3  l = Vec3::Lerp(a, b, 0.5f);

Mat4 model = Mat4::Translation({0, 1, 0})
           * Mat4::RotationY(Math::PI / 4.0f)
           * Mat4::Scale({2, 2, 2});

Quaternion q = Quaternion::FromAxisAngle({0,1,0}, Math::PI / 2.0f);
Quaternion r = Quaternion::Slerp(q1, q2, t);
```

---

## Project Structure

```
horo-engine/
├── core/           Application, Window, Time, Logger, Screenshot
├── mcp/            Built-in MCP settings, protocol, HTTP transport, controller
├── math/           Vec2/3/4, Mat3/4, Quaternion, Transform
├── physics/
│   ├── broadphase/     Collision pair generation
│   ├── narrowphase/    GJK, SAT algorithms
│   ├── constraints/    Contact constraint solver
│   └── integration/    Semi-implicit Euler integrator
├── renderer/
│   ├── shaders/        GLSL vertex/fragment shaders
│   └── ...             Mesh, Shader, Camera, Light, Texture, Material
├── scene/          ECS Registry, Scene, ComponentPool, Systems, SceneProjectModel, RuntimeSceneDefinition, SceneRuntimeCoordinator
├── input/          Input, KeyCodes, MouseCodes
├── editor/         EditorLayer, SceneSerializer, SceneDocument, SceneProjectBridge, SceneRuntimeBridge, SceneRuntimeCoordinatorBridge
├── docs/           Feature and integration guides
├── vendor/         GLFW, GLAD, GLM, ImGui, stb_image, nlohmann/json
└── tests/          24 unit test executables (Catch2)
```

## Built-in MCP

Horo Engine now ships with a built-in MCP server for the editor. Enable it from `File -> Settings...` in the editor and the server will auto-start on `http://127.0.0.1:39281/mcp` whenever the editor is open.

User settings live in `~/.horo/settings.json` (Windows: `%USERPROFILE%\\.horo\\settings.json`). The MCP tab in the editor shows runtime status, recent requests, and copy-ready Claude/Codex configuration snippets.

Integration details and token-minimal usage guidance live in [docs/mcp.md](/c:/Users/abdul/projects/fun/game/horo-engine/docs/mcp.md).

---

## Dependencies

All dependencies are managed automatically by CMake (FetchContent or vendored):

| Library | Purpose | License |
|---------|---------|---------|
| [GLFW 3.4](https://www.glfw.org/) | Window & input | Zlib |
| [GLAD](https://glad.dav1d.de/) | OpenGL 4.1 loader | MIT/Zlib |
| [GLM](https://github.com/g-truc/glm) | Reference math | MIT |
| [Dear ImGui](https://github.com/ocornut/imgui) | Editor UI | MIT |
| [stb_image](https://github.com/nothings/stb) | Image loading | MIT/Public Domain |
| [nlohmann/json](https://github.com/nlohmann/json) | Scene serialization | MIT |
| [Catch2](https://github.com/catchorg/Catch2) | Unit testing | BSL-1.0 |

---

## Testing

24 test executables covering all major subsystems:

```bash
make test
# or
ctest --preset debug --output-on-failure
```

| Suite | What it covers |
|-------|---------------|
| `test_math` / `test_math_extended` / `test_math_coverage` | Vec, Mat, Quat arithmetic |
| `test_physics` / `test_physics_extended` / `test_physics_deep` | Rigid body, forces, collision response |
| `test_gjk` / `test_gjk_extended` | Narrow-phase GJK algorithm |
| `test_constraints` | Contact constraint solver |
| `test_integration` | Euler integration accuracy |
| `test_camera` / `test_camera_extended` | View/projection matrices |
| `test_quaternion_extended` | Slerp, Euler conversions |
| `test_transform` | World/local space composition |
| `test_ecs` / `test_registry_extended` | Component add/remove/query |
| `test_scene` / `test_scene_systems` | Entity lifecycle, system updates |
| `test_core` | Application, window, time |
| `test_editor` | Serialization, picking, UI state |

**Coverage report:**
```bash
make coverage   # generates build/coverage/html/index.html
```

---

## Code Style

Google C++ Style Guide, enforced via `clang-format`:

```bash
make format        # format all sources in-place
make format-check  # CI check — exits non-zero if formatting needed
```

---

## Contributing

1. Fork and create a feature branch
2. Run `make test` — all tests must pass
3. Run `make format-check` — must be clean
4. Open a pull request

---

## License

Source code in this repository is open source. See individual vendor subdirectories for their respective licenses (all permissive: MIT, Zlib, BSL-1.0).
