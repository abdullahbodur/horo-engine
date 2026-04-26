#include "core/EngineLaunchArgs.h"

#include <string_view>

namespace Horo {
    EngineLaunchOptions ParseEngineLaunchOptions(int argc, char **argv) {
        EngineLaunchOptions out;
        for (int i = 1; i < argc;) {
            if (!argv[i]) {
                ++i;
                continue;
            }
            if (const std::string_view a(argv[i]); a == "--play")
                out.editorStartup = EditorStartupCli::ForcePlay;
            else if (a == "--editor")
                out.editorStartup = EditorStartupCli::ForceEditor;
            else if (a == "--project") {
                if ((i + 1) < argc && argv[i + 1]) {
                    out.projectPath = argv[i + 1];
                    i += 2;
                    continue;
                }
            } else if (a.starts_with("--project=")) {
                out.projectPath = std::string(a.substr(10));
            }
            ++i;
        }
        return out;
    }

    EditorStartupCli ParseEditorStartupCli(int argc, char **argv) {
        return ParseEngineLaunchOptions(argc, argv).editorStartup;
    }

    bool ShouldStartWithEditor(EditorStartupCli cli, bool isReleaseBuildDefault) {
        using enum EditorStartupCli;
        switch (cli) {
            case ForcePlay:
                return false;
            case ForceEditor:
                return true;
            case Default:
                return !isReleaseBuildDefault;
        }
        return false;
    }
} // namespace Horo
