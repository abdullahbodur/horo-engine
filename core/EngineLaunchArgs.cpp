#include "core/EngineLaunchArgs.h"

#include <string_view>

namespace Monolith {

EngineLaunchOptions ParseEngineLaunchOptions(int argc, char** argv) {
  EngineLaunchOptions out;
  for (int i = 1; i < argc; ++i) {
    if (!argv[i])
      continue;
    const std::string_view a(argv[i]);
    if (a == "--play")
      out.editorStartup = EditorStartupCli::ForcePlay;
    else if (a == "--editor")
      out.editorStartup = EditorStartupCli::ForceEditor;
    else if (a == "--project") {
      if ((i + 1) < argc && argv[i + 1]) {
        out.projectPath = argv[i + 1];
        ++i;
      }
    } else if (a.starts_with("--project=")) {
      out.projectPath = std::string(a.substr(10));
    }
  }
  return out;
}

EditorStartupCli ParseEditorStartupCli(int argc, char** argv) {
  return ParseEngineLaunchOptions(argc, argv).editorStartup;
}

bool ShouldStartWithEditor(EditorStartupCli cli, bool isReleaseBuildDefault) {
  switch (cli) {
    case EditorStartupCli::ForcePlay:
      return false;
    case EditorStartupCli::ForceEditor:
      return true;
    case EditorStartupCli::Default:
      return !isReleaseBuildDefault;
  }
  return false;
}

}  // namespace Monolith
