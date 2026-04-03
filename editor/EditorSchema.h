#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "editor/SceneDocument.h"

namespace Horo {
namespace Editor {

struct FieldDef {
  enum class Widget { String, Float, Bool, Enum, Color3 };

  std::string key;
  std::string label;
  Widget widget = Widget::String;
  float minVal = 0.0f;
  float maxVal = 1.0f;
  std::vector<std::string> options;  // for Enum widget
  std::string defaultValue;
};

struct TypeSchema {
  std::vector<FieldDef> fields;
};

// Parses editor_schema.json and provides per-type field definitions.
// The properties panel in EditorLayer iterates these to build its UI.
class EditorSchema {
 public:
  // Load from file; silently no-ops on failure (schema is optional).
  void LoadFromFile(const std::string& path);

  // Returns nullptr if no schema registered for this type.
  const TypeSchema* GetSchema(SceneObjectType t) const;

 private:
  std::unordered_map<std::string, TypeSchema> m_schemas;
};

}  // namespace Editor
}  // namespace Horo
