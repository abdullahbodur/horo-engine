#include "core/ProjectPath.h"

#include <system_error>

namespace Monolith {

namespace fs = std::filesystem;

fs::path ProjectPath::s_root;

static bool IsProjectRoot(const fs::path& dir) {
  std::error_code ec;
  const bool hasPresets = fs::exists(dir / "CMakePresets.json", ec) && !ec;
  const bool hasAssets  = fs::is_directory(dir / "assets", ec) && !ec;
  return hasPresets && hasAssets;
}

void ProjectPath::Init(const fs::path& exeDir) {
  if (exeDir.empty()) {
    s_root.clear();
    return;
  }
  fs::path cur = fs::absolute(exeDir);
  while (!cur.empty()) {
    if (IsProjectRoot(cur))                { s_root = cur; return; }
    if (IsProjectRoot(cur / "monolith"))   { s_root = cur / "monolith"; return; }
    auto parent = cur.parent_path();
    if (parent == cur) break;
    cur = parent;
  }
  s_root = exeDir;  // fallback
}

const fs::path& ProjectPath::Root() {
  return s_root;
}

fs::path ProjectPath::Resolve(const std::string& relPath) {
  return s_root / relPath;
}

}  // namespace Monolith
