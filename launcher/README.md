# Horo editor launcher (`HoroEditor`)

This directory is the **in-repo editor launcher**: SDK layout next to the binary, project scaffolding, recent projects, and the ImGui shell that hosts the editor when you run `HoroEditor`.

It is not a separate CMake project; it builds as part of the top-level `MonolithEngine` tree (`MONOLITH_ENGINE_BUILD_STANDALONE_EDITOR`).

- **Executable:** `HoroEditor` (`launcher/HoroEditorMain.cpp`)
- **Library sources:** other `.cpp` files here are compiled into `MonolithEngine` (entry `HoroEditorMain.cpp` is excluded from the static lib).

**Launcher unit tests** live under `tests/test_launcher_unit.cpp` (Catch2, no GLFW). **End-to-end ImGui Test Engine** scenarios for the launcher run only in the `HoroEditorUiTest` binary (`--run-ui-tests`), implemented in `launcher/LauncherUiAutomation.cpp`. For screen/video capture in CI or locally, set `MONOLITH_UI_TEST_OUTPUT_DIR` to an absolute directory (CI uses the workspace `ui_test_output` folder).
