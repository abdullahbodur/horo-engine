# Horo Engine

Horo Engine (current CMake target: `MonolithEngine`) is a C++20 game engine/static library with:

- scene + ECS primitives
- renderer / camera / debug draw
- physics systems
- in-game editor overlay
- scene serialization
- project-local engine configuration via `engine.config.toml`

## What's new in this branch

- editor asset registry panel
- place registered assets directly into the map
- bind scene objects to registered assets
- richer object editing in the properties panel
- `engine.config.toml` support for project-local engine settings
- integration documentation for embedding the engine into another CMake project

## Build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Editor workflow

The editor now has three key panels:

- **Objects**: lists placed scene objects
- **Assets**: lists project assets registered in the current scene document
- **Properties**: edits transform, asset binding, and schema-driven properties

### Asset workflow

1. Open the **Assets** panel
2. Add an asset id, mesh path/tag, and render scale
3. Click **Add Asset**
4. Select the asset and click **Place Asset**
5. The engine creates a scene object bound to that asset
6. Use **Properties** to move, rotate, scale, or rebind the object

This gives you a simple content pipeline inside editor mode:

- register reusable assets once
- list them in the editor
- place them into the map repeatedly
- edit placed instances independently

## Project config: `engine.config.toml`

A host project can place an `engine.config.toml` file next to its app and load it through:

```cpp
#include "core/Application.h"

Monolith::AppSpec spec = Monolith::AppSpec::FromConfig("engine.config.toml", "My Game");
```

Supported sections today:

```toml
[window]
width = 1600
height = 900
vsync = true

[runtime]
max_ram_mb = 2048
max_cpu_percent = 70
default_scene = "assets/scenes/main.json"

[assets]
directories = ["assets", "mods/base"]
```

Notes:

- `width`, `height`, and `vsync` are applied directly to the app window
- `max_ram_mb` and `max_cpu_percent` are currently parsed and exposed as runtime metadata for the host project
- `default_scene` and `directories` give the project a central place to declare engine-facing content settings

## Embedding into another project

See:

- `docs/integration.md`
- `engine.config.toml.example`

## CMake embedding summary

Typical host usage:

```cmake
add_subdirectory(external/horo-engine)

target_link_libraries(MyGame PRIVATE MonolithEngine)
target_include_directories(MyGame PRIVATE ${CMAKE_SOURCE_DIR}/external/horo-engine)
```

The engine is designed to be embedded as a static library.
