# ADR-005: Submodule Compatibility Constraints

- **Status**: Accepted
- **Date**: 2026-05-25
- **JIRA**: HORO-32
- **Supersedes**: None
- **Scope**: Engine consumption as a git submodule — guarantees, limitations, and test strategy

## Context

Horo Engine is designed for two consumption modes:

1. **Standalone development**: The engine repository is checked out directly.
   The developer builds `HoroEditor`, `horopak`, `horo-engine` CLI, and
   test binaries from the engine repo using CMake presets.

2. **Submodule consumption**: A game project vendors the engine as a git
   submodule, typically at `engine/`.  The game's `CMakeLists.txt` calls
   `add_subdirectory(engine)` and links against `HoroEngine::HoroEngine`.

Submodule consumption is central to the engine's value proposition — it
enables AI-assisted development (the engine source is visible to AI agents),
unified CI (engine and game tests run as one pipeline), and immediate engine
bug fixes (no waiting for a vendor release).

However, submodule consumption introduces constraints that standalone
development does not face:

- The engine's CMake configuration must work when included via
  `add_subdirectory()`, not just as a top-level project.
- The engine must not impose toolchain or dependency requirements that
  conflict with the consuming game project.
- The engine must not leak internal build options, test targets, or
  launcher-only features into the consuming project's build.
- AI agents reading the submodule'd engine source must see the real code,
  not generated or preprocessed intermediates.

## Decision

**The engine supports `add_subdirectory()` consumption with explicit
gating of launcher/editor/test features.  Submodule compatibility is
enforced by CI smoke tests on all three platforms.**

### Guaranteed submodule contract

When a game project includes the engine via:

```cmake
set(HORO_ENGINE_ROOT "engine" CACHE PATH "...")
add_subdirectory(${HORO_ENGINE_ROOT} engine)
target_link_libraries(MyGame PRIVATE HoroEngine::HoroEngine)
```

The following are guaranteed:

1. **Two CMake targets are available**:
   - `HoroEngine::HoroEngine` — the core engine library (math, core, scene,
     renderer, physics, input).
   - `HoroEngine::RendererNull` — a no-op renderer backend for headless/CI
     builds.

2. **Launcher and editor are gated behind CMake options**:
   - `HORO_ENGINE_BUILD_STANDALONE_EDITOR` — OFF in submodule mode. This
     prevents `HoroEditor` and its ImGui/GLFW dependencies from being built.
   - `HORO_ENGINE_BUILD_TESTS` — OFF in submodule mode. Prevents Catch2 test
     targets from appearing in the consuming project.
   - `HORO_ENGINE_ENABLE_SHADER_COOKER` — OFF in submodule mode.
   - `HORO_ENGINE_ENABLE_VULKAN` — OFF in submodule mode. Prevents Vulkan
     SDK dependency requirement.

3. **The engine does not set global CMake state that would affect the
   consuming project**:
   - No `CMAKE_CXX_STANDARD` forced globally (the engine requires C++20
     internally but sets it as a target property, not a global variable).
   - No `CMAKE_BUILD_TYPE` forced.
   - No global compiler flags forced.
   - No `CMAKE_INSTALL_PREFIX` modifications.

4. **The engine's vendor dependencies are isolated**: Third-party libraries
   (glad, GLFW, ufbx, bc7enc, nlohmann_json, Catch2) are brought in via
   CMake `FetchContent` and built as part of the engine target. They do not
   leak into the consuming project's include path or link interface.

5. **The submodule smoke test passes on all three CI platforms**: The
   fixture at `tests/fixtures/submodule-test/` is a minimal CMake project
   that includes the engine via `add_subdirectory()`, builds a trivial
   executable, and runs it to verify linkage and basic runtime behavior
   (`Vec3`, `Scene::CreateEntity`).

6. **AI agent visibility**: When the engine is a submodule, AI agents
   connected via MCP can read engine source files directly from the
   filesystem. The engine's source tree is fully present — no precompiled
   headers are required, no binary-only SDK path is normative.

### Limitations and non-guarantees

The following are NOT guaranteed and should not be relied upon by submodule
consumers:

1. **Internal header stability**: Headers in `core/`, `scene/`, `renderer/`,
   etc. are subject to change without notice. Only the public API surface
   (as documented in module READMEs) is intended for external consumption.
   There is no stable ABI guarantee.

2. **Build system internal details**: The engine's CMake internals (custom
   functions, variable names, target properties) are implementation details.
   Consumers should only use the documented `HoroEngine::HoroEngine` and
   `HoroEngine::RendererNull` targets.

3. **`vendor/` directory contents**: The specific versions and build
   configurations of third-party dependencies in `vendor/` may change
   between engine revisions. Consumers should not link against engine
   vendored libraries directly.

4. **Renderer backend selection**: The renderer backend (OpenGL, future
   Vulkan/Metal) is selected at engine build time. Submodule consumers
   should use the `HoroEngine::RendererNull` target for headless/CI builds
   and the OpenGL backend for runtime.

5. **Engine CLI/launcher tools in submodule mode**: `horopak` and
   `horo-engine` are built as part of the standalone engine build, not as
   part of a submodule'd game project. Game projects that need `horopak`
   for their own asset pipeline should either build the engine standalone
   and reference the binaries, or set `HORO_ENGINE_BUILD_STANDALONE_EDITOR`
   to ON (which is not recommended for CI).

6. **SDK/pre-built mode is not the submodule mode**: When the engine is
   consumed as a pre-built SDK (`cmake --fresh -S <game> -B <game>/build
   -DCMAKE_PREFIX_PATH=<sdk>`), the CMake export targets
   (`HoroEngineConfig.cmake`, `HoroEngineTargets.cmake`) are used instead of
   `add_subdirectory()`. This is a different consumption path with its own
   compatibility contract. See `docs/architecture/release/release.md` and the
   horo-engine-development skill's submodule/SDK build pattern reference.

### CI enforcement

The submodule smoke test runs in every CI job (macOS, Windows, Linux) as a
step within the main `ci.yml` workflow:

```yaml
- name: Smoke: submodule
  run: |
    fixture="tests/fixtures/submodule-test"
    cmake -S "$fixture" -B "$fixture/build" -G Ninja \
      -DHORO_ENGINE_ROOT="${{ github.workspace }}" \
      -DFETCHCONTENT_BASE_DIR="..."
    cmake --build "$fixture/build"
    "$fixture/build/smoke-submodule"
```

This verifies:
- The engine's CMake can be consumed via `add_subdirectory()`.
- The engine compiles and links against the platform's native toolchain.
- The `smoke-submodule` executable runs without crashing.
- `Vec3::Length()` and `Vec3::Dot()` produce correct results.
- `Scene::CreateEntity()` and `Scene::Clear()` work.

### When submodule compatibility breaks

Submodule compatibility can break when:

1. **A new file is added to the engine but not to the CMake target**. This
   causes linker errors in submodule consumers because the symbol is
   missing from `HoroEngine::HoroEngine`. Mitigation: the submodule smoke
   test catches this immediately (it links against `HoroEngine::HoroEngine`).

2. **A global CMake setting is introduced**. If a developer adds
   `set(CMAKE_CXX_STANDARD 20)` (global) instead of
   `target_compile_features(HoroEngine PUBLIC cxx_std_20)`, it may override
   the consuming project's C++ standard. Mitigation: code review policy;
   the `ci.yml` submodule smoke test would catch a compilation failure.

3. **A new dependency is added that conflicts with the consuming project**.
   If the engine adds a `FetchContent` dependency that the consuming project
   also uses at a different version, CMake may pick the wrong version.
   Mitigation: the engine's `FetchContent` calls are scoped with
   `FETCHCONTENT_BASE_DIR` and use `GIT_TAG` pinned to specific commits.

4. **A new build option leaks into the consuming project**. If a developer
   adds a `option()` call without gating it behind
   `HORO_ENGINE_BUILD_STANDALONE_EDITOR`, it appears in the submodule
   consumer's CMake cache. Mitigation: test for option leakage in the
   submodule smoke fixture.

## Consequences

### Positive
- Game projects get the full engine source for debugging, AI assistance,
  and customization.
- Engine and game tests run as one CI pipeline.
- Pinned engine commits mean reproducible builds.
- The submodule smoke test prevents silent breakage.

### Negative
- Submodule maintenance adds a compatibility burden: every engine change
  must consider whether it breaks `add_subdirectory()` consumers.
- The engine's CMake must be more careful than a standalone project's CMake
  — no global state leakage, no implicit assumptions about being the
  top-level project.
- `FetchContent` dependency version conflicts between engine and game are a
  known failure mode with no automatic resolution. The mitigation is
  "don't use the same dependency at conflicting versions" — which requires
  awareness from game developers.

### Open questions
- Should the engine provide a CMake package export target so submodule
  consumers can also use `find_package(HoroEngine)`? Currently only the
  SDK/pre-built path supports this.
- Should there be a compatibility test matrix for different game project
  CMake versions (3.25, 3.27, 3.28, etc.)?
- Should the engine adopt a `FetchContent` wrapper that isolates all
  third-party dependencies into a separate CMake scope to prevent version
  conflicts?

## Rejected Alternatives

### Binary-only SDK as the primary consumption mode

**Rejected because**: This contradicts the engine's core value proposition
of source-visible AI-assisted development.  A binary SDK makes the engine a
black box to AI agents, removing the ability to trace calls, read component
definitions, or debug engine internals.  The submodule approach is a
first-class feature, not a fallback.

### Header-only engine

**Rejected because**: The engine has significant implementation code
(renderer backends, physics stepping, scene serialization, archive I/O) that
cannot reasonably be header-only without causing compile-time explosion and
ODR violations.

### Monorepo with game code inside the engine repo

**Rejected because**: This would make the engine repository a multi-project
monorepo, blurring the boundary between engine and game.  Game developers
should own their project structure.  The submodule approach gives them the
engine without taking over their repository.

### FetchContent-based consumption (auto-download engine from GitHub)

**Rejected for now, not ruled out**: `FetchContent` could download the
engine from GitHub at configure time, avoiding the need for a git submodule.
This would simplify initial setup but would remove the ability to pin
commits, make local engine modifications, and have the engine source visible
in the same working tree for AI agents.  It is a potential future option
alongside submodule consumption, not a replacement.
