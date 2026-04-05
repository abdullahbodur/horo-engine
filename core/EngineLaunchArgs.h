#pragma once

namespace Monolith {

// Parsed from argv before Application construction side effects.
// Used by Application::ParseArgs and unit tests (with explicit release-default flag).
enum class EditorStartupCli { Default, ForceEditor, ForcePlay };

EditorStartupCli ParseEditorStartupCli(int argc, char** argv);

// If isReleaseBuildDefault is true (NDEBUG), Default means start in game (no editor).
// If false (Debug), Default means start with editor.
bool ShouldStartWithEditor(EditorStartupCli cli, bool isReleaseBuildDefault);

}  // namespace Monolith
