#pragma once
#include <stdexcept>
#include <string>

#include "editor/SceneDocument.h"

namespace Monolith {
namespace Editor {

class SceneSerializerException : public std::runtime_error {
 public:
  explicit SceneSerializerException(const std::string& message) : std::runtime_error(message) {}
};

class SceneSerializer {
 public:
  // Throws SceneSerializerException if path cannot be opened or JSON is invalid.
  static SceneDocument LoadFromFile(const std::string& path);

  // Throws SceneSerializerException if path cannot be written.
  static void SaveToFile(const SceneDocument& doc, const std::string& path);
};

}  // namespace Editor
}  // namespace Monolith
