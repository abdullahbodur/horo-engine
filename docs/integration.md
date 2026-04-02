# Integrating Horo Engine into a Project

This repository builds a static library target named `MonolithEngine`.

## 1. Add the engine to your source tree

Example layout:

```text
my-game/
  CMakeLists.txt
  src/
  external/
    horo-engine/
  assets/
  engine.config.toml
```

## 2. Add the engine via CMake

In your game's `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.25)
project(MyGame LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(external/horo-engine)

add_executable(MyGame
  src/main.cpp
  src/MyGameApp.cpp
)

target_link_libraries(MyGame PRIVATE MonolithEngine)

target_include_directories(MyGame PRIVATE
  ${CMAKE_SOURCE_DIR}/external/horo-engine
)
```

## 3. Create a project-local engine config

Create `engine.config.toml` in your host project:

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

## 4. Load config into your application spec

```cpp
#include "core/Application.h"

class MyGameApp : public Monolith::Application {
 public:
  MyGameApp()
      : Monolith::Application(Monolith::AppSpec::FromConfig("engine.config.toml", "My Game")) {}
};
```

What this gives you:

- window width / height / vsync applied automatically
- runtime budget metadata available on `GetSpec()`
- default scene path and asset directories stored in one place

## 5. Access config values in game code

```cpp
const Monolith::AppSpec& spec = GetSpec();
int ramBudgetMb = spec.maxRamMb;
int cpuBudgetPercent = spec.maxCpuPercent;
std::string defaultScene = spec.defaultScenePath;
```

## 6. Editor / scene integration

The editor scene document already supports:

- `assets`: reusable asset registry
- `objects`: placed map instances
- `settings`: scene-level metadata

Recommended workflow:

1. Register reusable assets in the editor
2. Place them into the world as scene objects
3. Save the scene JSON into your project's assets folder
4. Keep engine runtime defaults in `engine.config.toml`

## Notes on CPU / RAM limits

The engine currently **reads and exposes** CPU/RAM budget values.

That means:

- they are available to the host game/application
- they can drive project-specific throttling, telemetry, streaming, or quality presets
- they are **not yet OS-enforced hard resource caps**

This is still useful because the host project can centralize engine settings in a single config file and make policy decisions from there.
