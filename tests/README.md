# Tests Module

`tests/` contains Catch2-based engine unit/integration tests.

## Structure

- Each test source builds into a separate executable (e.g. `test_math`, `test_physics`, `test_scene_systems`).
- CTest registers each binary independently.
- Test binaries are output under `build/.../bin/tests/`.

## Coverage Areas

- Math primitives and transforms
- Physics world, constraints, GJK/SAT paths, integration
- ECS registry and scene/system behavior
- Core/runtime utilities
- Editor logic and asset import flows
- Animation and gizmo-related functionality
- GI/reflection tier baseline + temporal instability regression gating

## Run Tests

From repository root:

```bash
make test
```

or with CMake/CTest:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

## Adding New Tests

1. Create `tests/test_<feature>.cpp`
2. Register executable in `tests/CMakeLists.txt`
3. Link against `MonolithEngine` and `Catch2::Catch2WithMain`
4. Add `add_test(...)` entry
