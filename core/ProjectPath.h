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
  // Also checks one level deeper for monolith/ sub-repo layout.
  // Fallback: exeDir itself.
  static void Init(const std::filesystem::path& exeDir);

  // Absolute project root path.
  static const std::filesystem::path& Root();

  // Resolve a repo-relative path to absolute (Root() / relPath).
  static std::filesystem::path Resolve(const std::string& relPath);

 private:
  static std::filesystem::path s_root;
};

}  // namespace Monolith
