#include "ProjectEntryFilter.h"

namespace Monolith {
namespace Editor {

bool IsHiddenDotEntry(std::string_view filenameUtf8) {
  return !filenameUtf8.empty() && filenameUtf8.front() == '.';
}

bool IsBlockedProjectDirName(std::string_view dirname,
                             const std::unordered_set<std::string>* extraBlocklist) {
  static const std::unordered_set<std::string> kDefaultBlock{
      "build", "engine", "out", "bin", "obj", ".vs", "cmake-build-debug", "cmake-build-release"};
  const std::string key{dirname};
  if (kDefaultBlock.count(key) != 0)
    return true;
  if (extraBlocklist && extraBlocklist->count(key) != 0)
    return true;
  // Prefix cmake-build-
  if (dirname.size() > 13 && dirname.compare(0, 13, "cmake-build-") == 0)
    return true;
  return false;
}

}  // namespace Editor
}  // namespace Monolith
