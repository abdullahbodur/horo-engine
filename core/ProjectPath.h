#pragma once
#include <filesystem>
#include <string>

namespace Monolith {
    // Locates the project root and resolves asset paths relative to it.
    // Initialized once by Application constructor; call Resolve() from anywhere.
    //
    // Project root = first ancestor dir containing BOTH:
    //   - CMakePresets.json   (build system sentinel)
    //   - assets/             (content directory)
    // Dual sentinel prevents false match on build output dirs that receive
    // a copied assets/ tree but no CMakePresets.json.
    class ProjectPath {
    public:
        // Called by Application constructor. Walks upward from exeDir.
        // Fallback: exeDir itself.
        static void Init(const std::filesystem::path &exeDir);

        // Binds the active project root explicitly. Launcher editor sessions use
        // this to point project-relative paths at the opened project rather than the
        // engine/source tree. Passing an empty path clears the active root.
        static void SetProjectRoot(const std::filesystem::path &root);

        // True when the project root was set explicitly rather than discovered from
        // the executable path.
        static bool HasExplicitProjectRoot();

        // Engine/SDK resource root used for bundled editor assets (schema, shaders).
        // Defaults to the discovered root unless overridden by launcher bootstrap.
        static void SetSdkRoot(const std::filesystem::path &sdkRoot);

        static const std::filesystem::path &SdkRoot();

        // Absolute project root path.
        static const std::filesystem::path &Root();

        // Resolve a repo-relative path to absolute (Root() / relPath).
        static std::filesystem::path Resolve(const std::string &relPath);

        // Resolve an engine/SDK-relative path to absolute (SdkRoot() / relPath).
        static std::filesystem::path ResolveSdk(const std::string &relPath);

    private:
        static std::filesystem::path s_root;
        static std::filesystem::path s_sdkRoot;
        static bool s_hasExplicitProjectRoot;
    };
} // namespace Monolith
