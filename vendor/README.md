# Vendor Module

`vendor/` defines third-party dependencies used by the engine.

## Dependency Policy

- Prefer minimal, explicit integration.
- Use CMake target guards (`if(NOT TARGET ...)`) to avoid duplicate definitions when embedded.
- Keep vendor code isolated from engine logic.

## Included Dependencies

- **glfw** (FetchContent): windowing + input backend
- **glad** (committed source): OpenGL function loading
- **glm** (header-only): reference math utilities where needed
- **stb** (header-only): image decoding support
- **Dear ImGui** (FetchContent): editor UI
- **nlohmann/json** (FetchContent): JSON serialization
- **tinygltf** (FetchContent): glTF/GLB import

## Build Notes

- `vendor/CMakeLists.txt` exports reusable targets consumed by `MonolithEngine`.
- Warning suppression is applied to vendor targets to keep strict engine warning policies clean.
- tinygltf is configured to reuse project JSON/STB integration to avoid duplicate symbol/include issues.
