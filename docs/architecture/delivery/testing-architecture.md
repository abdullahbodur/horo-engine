# Testing Architecture

## Purpose

This document defines how Horo Engine is tested across unit, integration,
contract, CLI, MCP, and GUI layers. The goal is to catch regressions early,
maintain confidence in refactorings, and keep the cost of adding new tests low.

Tests are treated as production code. They must be deterministic, fast, and
maintainable.

## Test Framework

The project uses **Catch2 v3** for C++ tests. Catch2 provides:

- `TEST_CASE` / `SECTION` macros for test structure
- built-in main via `Catch2::Catch2WithMain`
- tag-based test selection
- BDD-style macros when useful

Catch2 is a test-only dependency pinned to an immutable revision in
`cmake/Dependencies.cmake`. Native test targets link
`Catch2::Catch2WithMain`; production targets must not expose or transitively
link the framework. `tests/CMakeLists.txt` registers native executables through
`horo_register_catch_test()`, which uses `catch_discover_tests()` so every
`TEST_CASE` is independently visible to CTest, IDEs, and CI.

```cpp
#include <catch2/catch_test_macros.hpp>

TEST_CASE("ProjectPath resolves relative to project root", "[core][path]") {
    const ProjectPath path("assets/model.fbx");
    REQUIRE(path.Relative() == "assets/model.fbx");
}
```

Python tooling tests use the pinned pytest 8.4.2 dependency and remain a process-level CTest suite; C++
and Python test frameworks are not mixed inside one executable. The canonical
Python conventions are:

- files: `tests/python/test_<concern>.py`
- test functions: `test_<behavior>()`
- shared pytest fixtures only: `tests/python/conftest.py`
- private helpers: a leading underscore and no `test_` prefix

Class-based tests are reserved for cases that genuinely share behavior or
fixture scope; they are not used merely for naming. Every scenario remains
independently discoverable by pytest.

## Running Tests Locally

Configure and build the canonical test matrix from the repository root:

```bash
python3 -m pip install -r scripts/requirements.txt
cmake -S . -B build/skeleton -DBUILD_TESTING=ON
cmake --build build/skeleton --parallel
```

The default local verification excludes tests that require a display or GPU:

```bash
ctest --test-dir build/skeleton -LE gpu --output-on-failure
```

Run all tests present in the configured build, or select them by CTest regex:

```bash
ctest --test-dir build/skeleton --output-on-failure
ctest --test-dir build/skeleton -R HoroProjectMigrationTests --output-on-failure
ctest --test-dir build/skeleton -N
```

Every discovered Catch2 case is a separate CTest test. A Catch2 executable may
also be invoked directly by exact case name or tag expression:

```bash
./build/skeleton/tests/HoroProjectMigrationTests \
  "Pipeline Requires Terminal Validation"
./build/skeleton/tests/HoroProjectMigrationTests "[unit][application]"
./build/skeleton/tests/HoroProjectMigrationTests --list-tests
```

The backend migration integration target runs the frozen `0.0.1` fixture through
the production catalog, planner, durable transaction, and `ProjectOpenService`
without ImGui, SDL, or a GPU:

```bash
cmake --build build/skeleton --target HoroProjectMigrationIntegrationTests --parallel
./build/skeleton/tests/HoroProjectMigrationIntegrationTests \
  "Legacy 0.0.1 project migrates to 0.1.0 through backend project open" \
  --reporter console --success
```

The target verifies the `0.1.0` root, compression defaults, unknown JSON
preservation, migration receipt/history binding, second-open idempotence,
failure-without-publication, and the required structured log categories. It uses
an isolated temporary copy of the frozen fixture and never mutates repository
fixtures.

CTest invokes the Python suite through the same interpreter selected by CMake.
For direct pytest selection and failure output:

```bash
python3 -m pytest tests/python
python3 -m pytest tests/python/test_project_migration_generator.py -k factory -vv
```

`BUILD_TESTING=ON` fails during configure with the required installation
command when pytest is unavailable. CMake never installs Python packages or
mutates the developer environment implicitly.

GPU smoke and editor first-frame checks are opt-in. They require a compatible
display and graphics device and must not be reported as run merely because the
targets compiled:

```bash
cmake -S . -B build/skeleton \
  -DBUILD_TESTING=ON \
  -DHORO_ENABLE_GPU_SMOKE_TESTS=ON
cmake --build build/skeleton --parallel
ctest --test-dir build/skeleton -L gpu --output-on-failure
```

For a GUI-off/headless matrix:

```bash
cmake -S . -B build/headless \
  -DBUILD_TESTING=ON \
  -DHORO_BUILD_EDITOR_GUI=OFF \
  -DHORO_BUILD_RENDER_OPENGL=OFF \
  -DHORO_BUILD_RENDER_METAL=OFF \
  -DHORO_ENABLE_GPU_SMOKE_TESTS=OFF
cmake --build build/headless --parallel
ctest --test-dir build/headless --output-on-failure
```

## Test Layers

Engine repository tests and downstream game project tests are separate concerns.
This document defines the engine test architecture. Game projects built with
Horo own additional tests for gameplay, content, scenes, play-mode behavior, and
packaged-player smoke validation as defined by
[Game Project Testing](./game-project-testing.md).

### Unit Tests

Unit tests validate isolated module behavior. They link against production
targets and must not compile production sources directly.

Location: `tests/unit/<owner>/<concern>Tests.cpp`

Rules:

- one logical concern per `TEST_CASE`
- place the scenario body directly in `TEST_CASE`; do not keep a second
  test-named function that is called only by that case
- retain helpers only for genuinely shared fixtures, setup/teardown, test data,
  or reusable domain actions
- mock or stub external dependencies
- avoid disk I/O when possible; use in-memory fixtures
- run in milliseconds

Observability unit tests additionally verify:

- compile-time and runtime-disabled call sites do not evaluate expensive
  arguments
- level/category filtering and structured record serialization
- nested and parallel diagnostic-context propagation
- redaction before records enter sinks or in-memory stores
- metric descriptors, units, cardinality limits, and series budgets
- CPU utilization and process-memory normalization
- histogram percentiles and bounded multi-resolution aggregation
- profiler compile-time/runtime gating and capture state transitions

### Integration Tests

Integration tests validate boundaries between modules:

- scene document to runtime conversion
- asset import and package output
- renderer frontend with null backend
- project build and release operations
- platform services behind testable interfaces

These tests may touch the filesystem and run in seconds, not milliseconds.

Observability integration tests use isolated temporary log roots and cover
asynchronous delivery, backpressure, rotation, retention, crash markers,
cross-process context, C++/Python schema parity, process CPU/memory sampling,
metrics persistence, profiler manifests, and unsupported-channel behavior.

### Contract Tests

Application use cases are exercised through every host adapter:

- GUI callback → use case
- CLI command → use case
- MCP tool → use case

Equivalent operations must produce equivalent typed outcomes. Contract tests
live in `tests/contract/`.

### CLI Tests

CLI tests validate:

- argument parsing and command dispatch
- exit codes
- structured and human-readable output
- filesystem side effects
- cancellation and signal handling

They invoke the built CLI binary as a subprocess.

### MCP Tests

MCP tests are divided by concern: low-level transport and protocol tests live in `tests/mcp/`, while typed-outcome equivalence tests live in `tests/contract/`.

MCP contract tests run against both hosts:

- MCP connected to the GUI host
- MCP connected to the CLI/headless host

The test harness starts the MCP transport, sends JSON-RPC requests, and
asserts on responses and side effects.

### GUI Tests

GUI tests use `EditorUiTestHarness`, with Catch2 owning discovery and final
reporting and Dear ImGui Test Engine owning widget lookup, input automation,
and popup/focus progression. Presentation is injected behind
`IEditorUiTestSurface`. The default headless surface runs an instrumented ImGui
context without initializing SDL or owning a window, display connection, or GPU
resource. Opt-in OpenGL and Metal surfaces present the same scenario in a real
SDL window and run the production render frontend, viewport adapter, GUI
bridge, and presentation lifecycle. Supported interactive backends execute the
same test and interaction path; renderer selection changes only the injected
host composition.

Types:

- component behavior matrices across size, variant, icon, loading, disabled, and
  focus states
- theme schema, inheritance, discovery, precedence, environment selection,
  live-reload, and last-known-good fallback tests
- panel workflow tests
- layout-tree persistence and invalid-layout recovery tests
- editor-session bus isolation and bridge allowlist tests
- data-bus dispatch tests for type-local lookup, reentrancy, coalescing,
  steady-state allocation, and slow-handler diagnostics
- Performance tab range/downsampling, metric availability, and capture-control
  tests
- modal focus trapping, input blocking, stack, and close-policy tests
- keyboard and focus navigation tests
- screenshot comparison tests
- editor lifecycle and persistence tests

See [GUI Design System](../editor/ui-design-system.md) for component test conventions.

Business rules invoked by GUI features are tested through application/editor
capability interfaces without constructing ImGui. GUI tests verify the thin
adapter mapping, interaction behavior, and rendered result rather than
re-testing the full business rule only through pixels.

Performance-sensitive event paths have focused microbenchmarks. Benchmarks
report subscriber count, event type, allocation count, and dispatch duration;
release thresholds are maintained per supported platform instead of relying on
one machine-specific absolute number.

## Test Organization

```text
tests/
    CMakeLists.txt           native discovery and process-test registration
    unit/                    unit/component tests grouped by owning target
    integration/             cross-module integration tests
    contract/                GUI/CLI/MCP contract tests (typed-outcome equivalence)
    cli/                     CLI-specific tests
    mcp/                     MCP transport protocol and low-level tool tests
    gui/                     GUI automation tests
    game_project/            downstream game test harness coverage
    fixtures/                shared test data
    helpers/                 shared test utilities
```

Each native test executable corresponds to one owning CMake test target. A
target may contain multiple source files from the same dependency boundary;
unrelated owners, platform guards, and renderer compositions must not be folded
into a monolithic executable. Each behavior is a separately named and tagged
Catch2 `TEST_CASE`.

## Downstream Game Project Tests

Downstream games run their own tests through the `horo-engine test` command, not
through engine-private test binaries:

```bash
horo-engine test --project /path/to/MyGame --profile fast
horo-engine test --project /path/to/MyGame --profile playmode
horo-engine test --project /path/to/MyGame --profile release-smoke \
    --package build/release/MyGame
```

The engine test suite validates the harness, discovery, report schema, and
generated template behavior. It does not treat one sample game's test count as
engine coverage.

`scripts/dev.py test-game --project <path> --profile <name>` is only an
in-repository developer wrapper. It builds or locates the local `horo-engine`
executable and delegates to the same public `horo-engine test` command. External
project documentation and CI examples should use `horo-engine test` directly.

## Writing Tests

### Arrange-Act-Assert

```cpp
TEST_CASE("Asset archive can round-trip a mesh", "[asset][archive]") {
    // Arrange
    Mesh source = MakeCubeMesh();
    ArchiveWriter writer;

    // Act
    writer.Write(source);
    ArchiveReader reader(writer.Data());
    auto result = reader.ReadMesh();

    // Assert
    REQUIRE(result.has_value());
    REQUIRE(result->vertices.size() == source.vertices.size());
}
```

### Use Tags

Every `TEST_CASE` must have tags. Use the convention:

- layer tag: `[unit]` or `[integration]`
- module tag: `[foundation]`, `[application]`, `[runtime]`, `[scene]`, `[assets]`, `[editor]`
- concern tag: `[path]`, `[serialization]`, `[lifecycle]`
- capability tag when relevant: `[renderer]`, `[gpu]`, `[gui]`, `[slow]`

Test names are behavioral sentences and do not contain ticket IDs. Use
`REQUIRE` for prerequisites that make later assertions unsafe and `CHECK` when
independent observations should all be reported. Assertions must run on the
Catch2 test thread; worker tasks publish typed results or thread-safe probes
that the test thread inspects after join.

Public-header self-containment checks are compile targets, not Catch2 cases.
Python generator checks, editor first-frame processes, and other subprocess
contracts remain direct CTest entries. GPU smoke executables use Catch2 but are
registered only when `HORO_ENABLE_GPU_SMOKE_TESTS=ON` and retain `gpu`/display
labels.

### Avoid Shared Mutable State

Tests must not depend on execution order. Each test should set up its own
fixtures and clean up after itself. Use RAII for temporary directories.

### Temporary Files

Use the test helper `TempDir`:

```cpp
TEST_CASE("Project save creates expected files", "[project]") {
    TempDir temp;
    Project project(temp.Path());
    project.Save();

    REQUIRE(std::filesystem::exists(temp.Path() / ".horo" / "project.json"));
}
```

## Fixtures

Shared test data lives in `tests/fixtures/`:

```text
tests/fixtures/
    meshes/
        cube.obj
        sphere.fbx
    textures/
        checker.png
    scenes/
        empty_scene.json
    projects/
        minimal_project/
```

Fixtures are versioned with the code that consumes them. A change that breaks
fixture compatibility must update the fixture and the loader.

## Mocks And Stubs

Use interface-based mocks for external dependencies:

```cpp
class IFilesystem {
public:
    virtual ~IFilesystem() = default;
    virtual bool Exists(std::filesystem::path path) const = 0;
};

class MockFilesystem : public IFilesystem {
public:
    MOCK_METHOD(bool, Exists, (std::filesystem::path), (const, override));
};
```

Platform services are designed behind testable interfaces so that tests can run
without real OS resources.

## GUI Test Harness

The dependency is fetched only in an explicit test-enabled GUI composition:

```bash
cmake -S . -B build/ui-automation \
  -DBUILD_TESTING=ON \
  -DHORO_BUILD_EDITOR_GUI=ON \
  -DHORO_ENABLE_IMGUI_UI_TESTS=ON
cmake --build build/ui-automation \
  --target HoroEditorUiAutomationTests --parallel
```

That composition instruments the existing `HoroThirdPartyImGui` target with
`IMGUI_ENABLE_TEST_ENGINE`; it never links a second ImGui implementation into
the same process. With the option disabled, or with GUI disabled, Test Engine
is absent from the target graph.

Each workflow is a Catch2 `TEST_CASE` tagged `[ui][imgui][editor]`. It registers
one parent Test Engine coroutine. `UiScenarioPipe` registers each named
operation as a native Test Engine child test and executes it with
`RunChildTest()`. The harness pumps deterministic frames through the injected
surface, stops and joins the coroutine, and only then translates parent status,
child-step results, logs, or captured exceptions into Catch2 assertions. Catch2
assertions must never run on the Test Engine coroutine thread.

```cpp
TEST_CASE("Full editor project journey creates content and configures the viewport",
          "[ui][imgui][editor][e2e]") {
    EditorUiTestHarness harness;
    FullEditorUiTestHost editor;
    const auto result = harness.RunScenario(
        "full_editor", "project_journey",
        [&](ImGuiTestContext* context) { editor.DrawFrame(context); },
        [&](UiScenarioPipe& scenario) {
            FullEditorSetups::CreateProjectAndOpenWorkspace(
                scenario, editor, {.name = "PipelineProject", .templateId = "empty"});
            FullEditorActions::CreateRootBox(scenario);
            FullEditorActions::SelectOrthographicProjection(scenario, editor);
        }, EditorUiScenarioBudget::Extended(1800), &editor.Input());

    INFO(result.testEngineLog);
    for (const UiScenarioStepResult& step : result.steps) {
        INFO(step.name);
        CHECK(step.status == UiScenarioStepStatus::Passed);
    }
    REQUIRE(result.Succeeded());
}
```

Child tests share the parent test context and application state. `Setup()` and
`Step()` both use native child tests, but setup children run at the fast Test
Engine speed and are reported separately from the behavior under test. A step is one
atomic user operation, not necessarily one low-level click. Transient ImGui
state such as an open popup or menu must be opened and consumed in the same
step; a later step may verify the resulting persistent editor command or model
state. Failure marks the current step failed and records later steps as
skipped. Every step records its name and frame interval for Catch2 diagnostics.

A basic harness-only contract scenario covers Button, Checkbox, Slider,
InputText, nested items, menu actions, and application-side state verification.
It validates the adapter and is not an editor workflow. Product UI scenarios
use `FullEditorUiTestHost`, which composes the real screen registry, modal host,
workspace panel registry, input router, project creation/open services, runtime
scene preview, and isolated editor configuration. They never replace the editor
with a hand-built component window.

Reusable setup pipelines live in `FullEditorUiTestSetups`. For example,
`CreateProjectAndOpenWorkspace()` composes four named setup children: open the
creation route, choose a template, enter and submit identity, and wait through
the real asynchronous `ProjectLoading` route. The following behavior steps
continue against the same live editor session. A scenario may compose the
smaller `OpenProjectCreation()`, `SubmitProjectCreation()`, and
`AwaitWorkspace()` setup functions when it needs an intermediate route.
Reusable behavior blocks live in `FullEditorUiTestActions`. A connected user
journey prepares the editor once, then composes several action blocks against
that same project/session instead of recreating the project for every feature.
Setup children assert only the route/barrier invariants required to make the
following behavior meaningful; feature assertions belong to `Step()` children.

Interactive controls use localization-independent ImGui identities such as
`Visible label###stable_id`. Test code addresses stable IDs or ID paths, never
translated text. Full-editor workflows currently cover project creation/open,
hierarchy creation with cross-panel observation, and viewport projection in
both `en-US` and `tr-TR`. Settings modal, splitter, and Welcome routing retain
their lower-level model/render coverage until equivalent full-editor workflows
are added; isolated component windows are not accepted as replacements in this
automation suite.

The Test Engine override is the single input source in this harness. After
`ImGui::NewFrame`, `TestInputBridge` converts the current ImGui IO state into a
`RawInputCollector` snapshot and starts the same `InputRouter` frame consumed by
editor code. The harness does not inject an independent second event stream.

The default deterministic budget is 600 simulated frames. A scenario may opt
into 601–1800 frames through an explicit extended budget. Each discovered CTest
case also has a 120-second process timeout. Cases are process-isolated by
`catch_discover_tests()` and may run under `ctest -j`; a process-local admission
guard rejects a second live harness/ImGui context.
Interactive input animation expands the effective frame ceiling by 10x at
normal speed and 30x at cinematic speed; the 120-second process timeout remains
the terminal bound. Fast/headless execution retains the declared frame budget.

Run the suite by label, target regex, or Catch2 tags:

```bash
ctest --test-dir build/ui-automation -L gui --output-on-failure -j 8
ctest --test-dir build/ui-automation \
  -R '^HoroEditorUiAutomationTests' --output-on-failure -j 8
./build/ui-automation/tests/HoroEditorUiAutomationTests \
  '[ui][imgui][editor]'
```

GUI tests run against the null renderer by default. To exercise one scenario
through a real renderer, configure the desired backend and run the Catch2 case
directly:

```bash
cmake -S . -B build/ui-automation \
  -DBUILD_TESTING=ON \
  -DHORO_BUILD_EDITOR_GUI=ON \
  -DHORO_BUILD_RENDER_OPENGL=ON \
  -DHORO_ENABLE_IMGUI_UI_TESTS=ON
cmake --build build/ui-automation \
  --target HoroEditorUiAutomationTests --parallel
HORO_UI_TEST_RENDERER=opengl \
  ./build/ui-automation/tests/HoroEditorUiAutomationTests \
  "Full editor project journey creates content and configures the viewport" \
  --reporter console --success
```

`HORO_UI_TEST_RENDERER` accepts `null`, `opengl`, or `metal`. The null renderer
is the default deterministic CI composition. OpenGL and Metal use their
production SDL presentation port, render frontend, editor viewport adapter,
GUI bridge, offscreen target, and frame/present ordering. A requested backend
that is absent from the build is rejected with a clear error. Metal requires
`HORO_BUILD_RENDER_METAL=ON` on Apple platforms.

Interactive compositions use normal Test Engine speed by default so pointer,
widget, and rendered viewport actions can be followed; set
`HORO_UI_TEST_SPEED=cinematic` for slower observation. Closing the window
cancels the case cleanly. Run one directly selected case at a time because a
parallel CTest run would open one renderer window per process. These E2E runs
verify renderer integration and readiness, but are not screenshot-golden
coverage. Pixel baselines and backend-specific visual assertions remain in the
renderer GPU test layer.

Select a product scenario when inspecting Horo's UI. The
`Basic Dear ImGui widgets execute as named child test steps` case intentionally
renders a minimal Button/Checkbox/Slider fixture and validates only the harness.
Harness-contract cases explicitly force the headless surface even when
`HORO_UI_TEST_RENDERER=opengl` is set, so running the complete executable
never opens this fixture as a visible product window.
Every editor behavior scenario, whether headless or visible, pumps the complete
`FullEditorUiTestHost` composition. This is required because an interaction in
one panel may alter another panel, modal, route, input context, or project
service. Lower-level panel render tests remain useful outside this automation
suite, but do not substitute for a full-editor workflow scenario.

Selection tests update `EditorSelectionModel` through the same interaction path
as production UI. They also verify that a tab attached after the selection can
query current state without relying on event replay.

Modal tests open workflows through `EditorModalHost` and verify that background
workspace controls cannot receive pointer, keyboard, shortcut, drag/drop, or
focus input. They also cover child modal focus restoration and close-frame
click-through prevention. Settings scenarios additionally verify that
interaction-blocked panels still receive preview, committed, and reverted
settings notifications and render from `ResolvedEditorSettings`. See
[Editor Modal Host](../editor/editor-modal-host.md).

## MCP Test Harness

The MCP test harness starts the engine in a subprocess with stdio transport:

```python
def test_mcp_create_object():
    with McpSession(["build/debug/bin/horo-engine", "--mcp"]) as session:
        result = session.call("tools/create_object", {
            "name": "Cube",
            "type": "static_mesh"
        })
        assert result["success"] is True
        assert result["objectId"] is not None
```

MCP tests validate both protocol correctness and engine side effects.

## Coverage

Coverage is collected from instrumented builds using the `coverage` preset.

```bash
cmake --preset coverage
python3 scripts/dev.py test --all --coverage
python3 scripts/dev.py coverage-report
```

Coverage reports are merged across compatible jobs. Coverage data is exclusively collected from **Unit**, **Integration**, and **Contract (CLI/MCP)** test layers. 

GUI test coverage is intentionally excluded from official metrics to prevent flaky line-hit variance (due to frame timings) and to avoid artificially inflating coverage numbers with UI layout execution.

Coverage thresholds are defined per module category. Overall project coverage
is not gated by one global number that can hide weak areas behind highly covered
utility code. Quality And CI additionally defines an aggregate New Code
regression floor for changed lines; both policies must pass.

See [Quality And CI](./quality-and-ci.md) for coverage policy details.

## Determinism

Tests must be deterministic:

- do not use `rand()` without a fixed seed
- do not depend on wall-clock time
- do not depend on file ordering unless explicitly sorted
- clean up temporary state in `TempDir` destructors
- use explicit locale-independent conversions

## Performance Budgets

Tests have implied performance budgets:

| Layer | Target Duration |
|---|---|
| Unit | < 10 ms each |
| Integration | < 1 s each |
| Contract | < 5 s each |
| GUI single workflow | < 30 s each |
| Full GUI suite | < 10 min total |

Slow tests must be tagged `[slow]` and excluded from the local fast suite.

*Note on CI routing: While individual integration tests (< 1 s) have a faster budget than contract tests (< 5 s), integration tests are deferred out of the CI PR Fast Lane. This is because their cross-module fan-out creates higher variance and non-local failure risks. Fast contract tests are kept in the PR Fast Lane to ensure the immediate module's boundaries remain strictly intact.*

## Debugging Failed Tests

### Reproduce locally

```bash
python3 scripts/dev.py test -- test_failing
HORO_LOG_LEVELS=scene.serialization=trace \
python3 scripts/dev.py test -- test_failing
```

### Run under debugger

```bash
lldb build/debug/bin/tests/test_failing
# or
gdb build/debug/bin/tests/test_failing
```

### Run with sanitizers

```bash
cmake --preset sanitizer
cmake --build --preset sanitizer
ctest --preset sanitizer --output-on-failure
```

### GUI test failure

The current headless interaction suite reports Test Engine logs through
Catch2. Screenshot golden comparison is a later TST-002 slice; when enabled,
visual tests write their artifacts under the configured build tree rather than
the source tree:

```text
build/debug/test_output/
    failed_<test_name>_expected.png
    failed_<test_name>_actual.png
    failed_<test_name>_diff.png
```

## Related Documents

- [Quality And CI](./quality-and-ci.md): CI gates and coverage policy.
- [Game Project Testing](./game-project-testing.md): tests authored by games built
  with Horo.
- [Build System](./build-system.md): how test targets are declared.
- [MCP Architecture](../interfaces/mcp-architecture.md): MCP transport and tool design.
- [Runtime Lifecycle](../runtime/runtime-lifecycle.md): fixed-step, mode, lifecycle, and
  shutdown invariants.
- [Concurrency And Job System](../foundation/concurrency-and-jobs.md): deterministic worker
  mode, cancellation, saturation, and context-isolation tests.
- [Error And Diagnostics](../foundation/error-and-diagnostics.md): stable error and
  diagnostic contract tests.
- [GUI Design System](../editor/ui-design-system.md): component and GUI test
  conventions.
- [Developer Environment](./developer-environment.md): running tests locally.
- [Observability Architecture](../observability/observability.md): logger behavior,
  performance, context, and retention tests.
