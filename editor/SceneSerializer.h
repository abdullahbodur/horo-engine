#pragma once
#include <string>

#include "editor/SceneDocument.h"

namespace Horo {
namespace Editor {

class SceneSerializer {
 public:
  // Throws std::runtime_error if path cannot be opened or JSON is invalid.
  static SceneDocument LoadFromFile(const std::string& path);

  // Throws std::runtime_error if path cannot be written.
  static void SaveToFile(const SceneDocument& doc, const std::string& path);
};

}  // namespace Editor
}  // namespace Horo
