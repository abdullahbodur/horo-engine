#include "core/ProjectPath.h"

#include <system_error>

namespace Horo {
    namespace fs = std::filesystem;

    fs::path ProjectPath::s_root;
    fs::path ProjectPath::s_sdkRoot;
    bool ProjectPath::s_hasExplicitProjectRoot = false;

    static bool IsProjectRoot(const fs::path &dir) {
        std::error_code ec;
        const bool hasPresets = fs::exists(dir / "CMakePresets.json", ec) && !ec;
        const bool hasAssets = fs::is_directory(dir / "assets", ec) && !ec;
        return hasPresets && hasAssets;
    }

    void ProjectPath::Init(const fs::path &exeDir) {
        s_hasExplicitProjectRoot = false;
        if (exeDir.empty()) {
            s_root.clear();
            if (s_sdkRoot.empty())
                s_sdkRoot.clear();
            return;
        }
        fs::path cur = fs::absolute(exeDir);
        while (!cur.empty()) {
            if (IsProjectRoot(cur)) {
                s_root = cur;
                if (s_sdkRoot.empty())
                    s_sdkRoot = s_root;
                return;
            }
            auto parent = cur.parent_path();
            if (parent == cur)
                break;
            cur = parent;
        }
        s_root = exeDir; // fallback
        if (s_sdkRoot.empty())
            s_sdkRoot = s_root;
    }

    void ProjectPath::SetProjectRoot(const fs::path &root) {
        if (root.empty()) {
            s_root.clear();
            s_hasExplicitProjectRoot = false;
            return;
        }

        std::error_code ec;
        fs::path canonical = fs::weakly_canonical(root, ec);
        if (ec)
            canonical = fs::absolute(root, ec);
        if (ec)
            canonical = root;
        s_root = canonical.lexically_normal();
        s_hasExplicitProjectRoot = true;
    }

    bool ProjectPath::HasExplicitProjectRoot() { return s_hasExplicitProjectRoot; }

    void ProjectPath::SetSdkRoot(const fs::path &sdkRoot) {
        if (sdkRoot.empty()) {
            s_sdkRoot.clear();
            return;
        }

        std::error_code ec;
        fs::path canonical = fs::weakly_canonical(sdkRoot, ec);
        if (ec)
            canonical = fs::absolute(sdkRoot, ec);
        if (ec)
            canonical = sdkRoot;
        s_sdkRoot = canonical.lexically_normal();
    }

    const fs::path &ProjectPath::SdkRoot() {
        return s_sdkRoot.empty() ? s_root : s_sdkRoot;
    }

    const fs::path &ProjectPath::Root() { return s_root; }

    fs::path ProjectPath::Resolve(const std::string &relPath) {
        return s_root / relPath;
    }

    fs::path ProjectPath::ResolveSdk(const std::string &relPath) {
        return SdkRoot() / relPath;
    }
} // namespace Horo
