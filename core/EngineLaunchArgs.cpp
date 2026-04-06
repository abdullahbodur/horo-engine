#include "core/EngineLaunchArgs.h"

#include <string_view>

namespace Monolith {

EditorStartupCli ParseEditorStartupCli(int argc, char** argv) {
  EditorStartupCli out = EditorStartupCli::Default;
  for (int i = 1; i < argc; ++i) {
    if (!argv[i])
      continue;
    const std::string_view a(argv[i]);
    if (a == "--play")
      out = EditorStartupCli::ForcePlay;
    else if (a == "--editor")
      out = EditorStartupCli::ForceEditor;
  }
  return out;
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
