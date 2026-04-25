#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "editor/SceneDocument.h"

namespace Monolith::Editor {
struct FieldDef {
  enum class Widget { String, Float, Bool, Enum, Color3 };

  std::string key;
  std::string label;
  std::string description;
  Widget widget = Widget::String;
  bool hasDefault = false;
  bool required = false;
  bool allowEmpty = true;
  bool allowCustomValue = false;
  bool hasMin = false;
  bool hasMax = false;
  float minVal = 0.0f;
  float maxVal = 1.0f;
  std::vector<std::string> options; // for Enum widget
  std::string defaultValue;
};

struct TypeSchema {
  std::string name;
  std::string label;
  std::vector<std::string> appliesTo;
  std::vector<FieldDef> fields;
};

struct ComponentSchema {
  std::string name;
  std::string label;
  std::vector<std::string> appliesTo;
  std::vector<FieldDef> fields;
};

// Parses editor_schema.json and provides per-type field definitions.
// The properties panel in EditorLayer iterates these to build its UI.
class EditorSchema {
public:
  // Load from file; silently no-ops on failure (schema is optional).
  void LoadFromFile(const std::string &path);

  // Returns nullptr if no schema registered for this type.
  const TypeSchema *GetSchema(SceneObjectType t) const;

  const TypeSchema *GetSchemaByName(const std::string &typeName) const;

  const ComponentSchema *
  GetComponentSchema(const std::string &componentType) const;

  const std::unordered_map<std::string, TypeSchema, StringHash,
                           std::equal_to<>> &
  TypeSchemas() const {
    return m_schemas;
  }

  const std::unordered_map<std::string, ComponentSchema, StringHash,
                           std::equal_to<>> &
  ComponentSchemas() const {
    return m_componentSchemas;
  }

private:
  std::unordered_map<std::string, TypeSchema, StringHash, std::equal_to<>>
      m_schemas;
  std::unordered_map<std::string, ComponentSchema, StringHash, std::equal_to<>>
      m_componentSchemas;
};
} // namespace Monolith::Editor
