# Horo editor launcher (`HoroEditor`)

This directory is the **in-repo editor launcher**: SDK layout next to the binary, project scaffolding, recent projects, and the ImGui shell that hosts the editor when you run `HoroEditor`.

It is not a separate CMake project; it builds as part of the top-level `MonolithEngine` tree (`MONOLITH_ENGINE_BUILD_STANDALONE_EDITOR`).

- **Executable:** `HoroEditor` (`launcher/HoroEditorMain.cpp`)
- **Library sources:** other `.cpp` files here are compiled into `MonolithEngine` (entry `HoroEditorMain.cpp` is excluded from the static lib).

UI automation tests live under `tests/test_launcher_ui.cpp` and use [Dear ImGui Test Engine](https://github.com/ocornut/imgui_test_engine). Set `MONOLITH_UI_TEST_OUTPUT_DIR` to an absolute directory for captures (CI uses the workspace `ui_test_output` folder). If unset, captures go to `ui_test_output/` at the engine repo root (`RepoRootFromTestSource()`), not the build directory.
