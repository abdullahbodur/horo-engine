# Horo editor launcher (`HoroEditor`)

This directory is the **in-repo editor launcher**: SDK layout next to the binary, project scaffolding, recent projects, and the ImGui shell that hosts the editor when you run `HoroEditor`.

It is not a separate CMake project; it builds as part of the top-level `MonolithEngine` tree (`MONOLITH_ENGINE_BUILD_STANDALONE_EDITOR`).

- **Executable:** `HoroEditor` (`launcher/HoroEditorMain.cpp`)
- **Library sources:** other `.cpp` files here are compiled into `MonolithEngine` (entry `HoroEditorMain.cpp` is excluded from the static lib).

**Launcher unit tests** live under `tests/test_launcher_unit.cpp` (Catch2, no GLFW).

**Launcher UI automation** runs from the `HoroEditorUiTest` binary (`--run-ui-tests`). The scenario definitions live in `tests/ui_scenarios/` and are registered through `tests/UiTestRegistry.*`, so production launcher code stays test-agnostic.

- **Recording/capture enabled** (Windows/Linux CI): set `MONOLITH_UI_TEST_RECORDING=1`, `MONOLITH_UI_TEST_CAPTURE=1`, optionally `MONOLITH_UI_TEST_VIDEO=1` with `MONOLITH_UI_TEST_FFMPEG_PATH`.
- **No-recording mode** (macOS CI): set `MONOLITH_UI_TEST_RECORDING=0` to force capture/video off.
- **Scenario selection**: set `MONOLITH_UI_TEST_FILTER` (examples: `launcher/*`, `editor/*`, `launcher/open_project_from_recent_projects`).

For outputs, set `MONOLITH_UI_TEST_OUTPUT_DIR` to an absolute directory (CI uses the workspace `ui_test_output` folder).
