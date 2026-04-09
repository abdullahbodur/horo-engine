#include "mcp/McpProtocol.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>

#include "core/ProjectPath.h"
#include "mcp/McpSettings.h"

namespace Monolith {
namespace Mcp {

using json = nlohmann::json;

namespace {

const std::vector<McpCatalogEntry>& GetToolCatalog();

json MakeVec3Schema() {
  return json{{"type", "array"},
              {"minItems", 3},
              {"maxItems", 3},
              {"items", {{"type", "number"}}}};
}

std::string ToLowerAscii(std::string value) {
  for (char& c : value)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return value;
}

std::string SanitizeToolName(std::string value) {
  std::replace(value.begin(), value.end(), '.', '_');
  return value;
}

std::string CanonicalizeToolName(const std::string& value) {
  for (const McpCatalogEntry& entry : GetToolCatalog()) {
    if (value == entry.name || value == SanitizeToolName(entry.name))
      return entry.name;
  }
  return value;
}

std::string JsonToCompactString(const json& value) {
  if (value.is_null())
    return "null";
  if (value.is_string())
    return value.get<std::string>();
  return value.dump();
}

std::string TruncatePreview(const std::string& text, size_t maxChars = 240) {
  if (text.size() <= maxChars)
    return text;
  if (maxChars < 4)
    return text.substr(0, maxChars);
  return text.substr(0, maxChars - 3) + "...";
}

McpHttpResponse MakeJsonResponse(int statusCode, const std::string& statusText, const json& body) {
  McpHttpResponse response;
  response.statusCode = statusCode;
  response.statusText = statusText;
  response.contentType = "application/json";
  response.body = body.dump();
  return response;
}

json MakeSuccess(const json& id, const json& result) {
  return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

json MakeError(const json& id, int code, const std::string& message) {
  return json{{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

const std::vector<McpCatalogEntry>& GetToolCatalog() {
  static const std::vector<McpCatalogEntry> catalog = {
      {"editor.search", "tool", "Search compact scene objects and assets."},
      {"editor.get_object", "tool", "Fetch one object by id with full MCP detail."},
      {"editor.get_object_edges", "tool",
       "Return one object's world-space corners and edge segments."},
      {"editor.list_objects", "tool", "List object summaries with optional filters and limits."},
      {"editor.get_objects", "tool", "Fetch multiple objects by id."},
      {"editor.get_object_children", "tool", "List direct children of one object."},
      {"editor.get_object_parent", "tool", "Fetch the parent summary of one object."},
      {"editor.count_objects", "tool", "Count objects with optional type filtering."},
      {"editor.select", "tool", "Update editor object selection."},
      {"editor.clear_selection", "tool", "Clear object and asset selection."},
      {"editor.create_object", "tool", "Create one scene object."},
      {"editor.create_object_from_asset", "tool", "Create a prop from an existing asset id."},
      {"editor.update_object", "tool", "Patch a scene object by id."},
      {"editor.transform", "tool", "Apply transform changes to one object."},
      {"editor.rename_object", "tool", "Rename a scene object id."},
      {"editor.reparent_object", "tool", "Set or clear an object's parent."},
      {"editor.duplicate", "tool", "Duplicate one object."},
      {"editor.delete", "tool", "Delete scene objects by id."},
      {"editor.list_assets", "tool", "List asset summaries with optional filters."},
      {"editor.get_asset", "tool", "Fetch one asset by id."},
      {"editor.search_assets", "tool", "Search assets by id, mesh or texture."},
      {"editor.count_assets", "tool", "Count assets with optional query filtering."},
      {"editor.select_asset", "tool", "Select an asset in the editor."},
      {"editor.update_asset", "tool", "Patch one asset definition."},
      {"editor.delete_asset", "tool", "Delete one asset definition."},
      {"editor.scene_status", "tool", "Return compact scene status."},
      {"editor.get_scene_file", "tool", "Return the active scene file path and save state."},
      {"editor.list_schema_types", "tool", "List object and component schemas exposed to MCP."},
      {"editor.get_schema", "tool", "Fetch one object or component schema by name."},
      {"editor.new_scene", "tool", "Create a new empty scene document."},
      {"editor.save_scene", "tool", "Save the active scene document."},
      {"editor.reload_scene", "tool", "Queue an in-editor scene reload."},
      {"editor.search_console", "tool", "Search recent console output."},
  };
  return catalog;
}

const std::vector<McpCatalogEntry>& GetResourceCatalog() {
  static const std::vector<McpCatalogEntry> catalog = {
      {"scene.summary", "scene://summary", "Compact scene overview with object sample."},
      {"scene.selection", "scene://selection", "Current selected objects and asset."},
      {"scene.assets", "scene://assets", "Compact asset summary sample."},
      {"scene.hierarchy", "scene://hierarchy", "Flat hierarchy preview with depth and child counts."},
      {"scene.objects", "scene://objects", "Filtered object list summary."},
      {"scene.scene_status", "scene://scene_status", "Scene status flags and counts."},
      {"assets.selection", "assets://selection", "Current selected asset summary."},
      {"assets.catalog", "assets://catalog", "Filtered asset catalog summary."},
      {"console.recent", "console://recent", "Recent console lines."},
      {"console.summary", "console://summary", "Console severity counts and latest lines."},
      {"build.status", "build://status", "Compact typed-scene and runtime build health."},
  };
  return catalog;
}

json BuildInitializeResult() {
  return json{{"protocolVersion", "2024-11-05"},
              {"serverInfo", {{"name", "horo-engine"}, {"version", "0.2.0"}}},
              {"capabilities", {{"resources", {{"listChanged", false}}}, {"tools", {{"listChanged", false}}}}}};
}

json BuildTextToolResult(const json& payload) {
  return json{{"content", json::array({{{"type", "text"}, {"text", payload.dump()}}})},
              {"structuredContent", payload}};
}

std::string GetParentId(const McpObjectSnapshot& object) {
  const auto it = object.props.find("parentId");
  if (it == object.props.end())
    return {};
  return it->second;
}

bool MatchesObjectQuery(const McpObjectSnapshot& object, const std::string& query) {
  if (query.empty())
    return true;
  const std::string lowered = ToLowerAscii(query);
  return ToLowerAscii(object.id).find(lowered) != std::string::npos ||
         ToLowerAscii(object.type).find(lowered) != std::string::npos ||
         ToLowerAscii(object.assetId).find(lowered) != std::string::npos ||
         ToLowerAscii(GetParentId(object)).find(lowered) != std::string::npos;
}

bool IsKnownResourceUri(const std::string& uri) {
  static const std::unordered_set<std::string> uris = {
      "scene://summary",  "scene://selection",   "scene://assets",      "scene://hierarchy",
      "scene://objects",  "scene://scene_status","assets://selection",  "assets://catalog",
      "console://recent", "console://summary",   "build://status",
  };
  return uris.find(uri) != uris.end();
}

bool IsWriteToolName(const std::string& name) {
  return name == "editor.select" || name == "editor.clear_selection" ||
         name == "editor.create_object" || name == "editor.create_object_from_asset" ||
         name == "editor.update_object" || name == "editor.transform" ||
         name == "editor.rename_object" || name == "editor.reparent_object" ||
         name == "editor.duplicate" || name == "editor.delete" || name == "editor.select_asset" ||
         name == "editor.update_asset" || name == "editor.delete_asset" ||
         name == "editor.new_scene" || name == "editor.save_scene" ||
         name == "editor.reload_scene";
}

bool IsDestructiveToolName(const std::string& name) {
  return name == "editor.delete" || name == "editor.delete_asset" || name == "editor.new_scene" ||
         name == "editor.reload_scene";
}

std::string FormatFloatText(double value) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(4) << value;
  return out.str();
}

std::string FormatAuditTimestamp() {
  using clock = std::chrono::system_clock;
  const std::time_t now = clock::to_time_t(clock::now());
  std::tm tmBuf{};
#ifdef _WIN32
  gmtime_s(&tmBuf, &now);
#else
  gmtime_r(&now, &tmBuf);
#endif
  char buffer[48];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tmBuf);
  return buffer;
}

std::filesystem::path ResolveMcpAuditPath() {
  namespace fs = std::filesystem;
  const fs::path projectRoot = Monolith::ProjectPath::Root();
  if (!projectRoot.empty()) {
    std::error_code ec;
    if (fs::exists(projectRoot, ec) && !ec)
      return projectRoot / ".horo" / "mcp-audit.jsonl";
  }
  return ResolveMcpSettingsDirectory() / "mcp-audit.jsonl";
}

json BuildChangedIds(const std::string& toolName, const json& arguments, const json& summary) {
  json changedIds = json::array();
  auto pushUnique = [&](const std::string& value) {
    if (value.empty())
      return;
    for (const json& item : changedIds) {
      if (item.is_string() && item.get<std::string>() == value)
        return;
    }
    changedIds.push_back(value);
  };

  if (arguments.contains("id") && arguments["id"].is_string())
    pushUnique(arguments["id"].get<std::string>());
  if (arguments.contains("ids") && arguments["ids"].is_array()) {
    for (const json& item : arguments["ids"]) {
      if (item.is_string())
        pushUnique(item.get<std::string>());
    }
  }
  if (toolName == "editor.new_scene" && arguments.contains("sceneId") && arguments["sceneId"].is_string())
    pushUnique(arguments["sceneId"].get<std::string>());

  const std::vector<std::string> summaryKeys = {"created", "updated", "asset", "renamed", "duplicates"};
  for (const std::string& key : summaryKeys) {
    if (!summary.contains(key))
      continue;
    const json& value = summary[key];
    if (value.is_object() && value.contains("id") && value["id"].is_string())
      pushUnique(value["id"].get<std::string>());
    if (value.is_array()) {
      for (const json& item : value) {
        if (item.is_object() && item.contains("id") && item["id"].is_string())
          pushUnique(item["id"].get<std::string>());
      }
    }
  }
  for (const char* key : {"newId", "deletedAssetId", "sceneId"}) {
    if (summary.contains(key) && summary[key].is_string())
      pushUnique(summary[key].get<std::string>());
  }

  return changedIds;
}

void AppendMutationAuditRecord(const json& requestId,
                               const McpEditorSnapshot& snapshot,
                               const std::string& toolName,
                               const std::string& mode,
                               const json& arguments,
                               bool ok,
                               const json& summary,
                               const std::string& errorText) {
  namespace fs = std::filesystem;

  const fs::path auditPath = ResolveMcpAuditPath();
  std::error_code ec;
  fs::create_directories(auditPath.parent_path(), ec);
  if (ec)
    return;

  std::ofstream out(auditPath, std::ios::app);
  if (!out.is_open())
    return;

  json record = {
      {"timestamp", FormatAuditTimestamp()},
      {"requestId", requestId.is_null() ? std::string() : JsonToCompactString(requestId)},
      {"tool", toolName},
      {"mode", mode},
      {"previewToken", arguments.value("previewToken", std::string())},
      {"sceneId", snapshot.sceneId},
      {"sceneFilePath", snapshot.sceneFilePath},
      {"result", ok ? "success" : "error"},
      {"error", ok ? std::string() : errorText},
      {"changedIds", BuildChangedIds(toolName, arguments, summary)},
      {"summary", summary},
  };

  out << record.dump() << "\n";
}

bool TryParseFloatText(const std::string& text, double* out) {
  if (!out)
    return false;
  char* end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  if (end == text.c_str() || (end && *end != '\0') || !std::isfinite(value))
    return false;
  *out = value;
  return true;
}

bool TryParseBoolText(const std::string& text, bool* out) {
  if (!out)
    return false;
  const std::string lowered = ToLowerAscii(text);
  if (lowered == "true" || lowered == "1") {
    *out = true;
    return true;
  }
  if (lowered == "false" || lowered == "0") {
    *out = false;
    return true;
  }
  return false;
}

bool NormalizeFreeformScalar(const json& value, std::string* out) {
  if (!out)
    return false;
  if (value.is_string()) {
    *out = value.get<std::string>();
    return true;
  }
  if (value.is_number_integer() || value.is_number_unsigned() || value.is_number_float()) {
    *out = value.dump();
    return true;
  }
  if (value.is_boolean()) {
    *out = value.get<bool>() ? "true" : "false";
    return true;
  }
  if (value.is_null()) {
    out->clear();
    return true;
  }
  return false;
}

bool NormalizeVec3Argument(const json& value,
                           const std::string& argumentName,
                           json* out,
                           std::string* error) {
  if (!out)
    return false;
  if (!value.is_array() || value.size() != 3 || !value[0].is_number() || !value[1].is_number() ||
      !value[2].is_number()) {
    if (error)
      *error = argumentName + " must be [x,y,z].";
    return false;
  }
  *out = json::array({value[0].get<double>(), value[1].get<double>(), value[2].get<double>()});
  return true;
}

bool NormalizeColor3Value(const json& value, std::string* out) {
  if (!out)
    return false;
  if (value.is_string()) {
    std::stringstream stream(value.get<std::string>());
    std::string item;
    std::vector<double> parts;
    while (std::getline(stream, item, ',')) {
      item.erase(std::remove_if(item.begin(), item.end(), [](unsigned char c) { return std::isspace(c); }),
                 item.end());
      double parsed = 0.0;
      if (!TryParseFloatText(item, &parsed))
        return false;
      parts.push_back(parsed);
    }
    if (parts.size() != 3)
      return false;
    *out = FormatFloatText(parts[0]) + "," + FormatFloatText(parts[1]) + "," + FormatFloatText(parts[2]);
    return true;
  }
  if (!value.is_array() || value.size() != 3 || !value[0].is_number() || !value[1].is_number() ||
      !value[2].is_number()) {
    return false;
  }
  *out = FormatFloatText(value[0].get<double>()) + "," + FormatFloatText(value[1].get<double>()) +
         "," + FormatFloatText(value[2].get<double>());
  return true;
}

const McpSchemaEntrySnapshot* FindSchemaEntryByName(const std::vector<McpSchemaEntrySnapshot>& entries,
                                                    const std::string& name) {
  const std::string loweredName = ToLowerAscii(name);
  for (const McpSchemaEntrySnapshot& entry : entries) {
    if (ToLowerAscii(entry.name) == loweredName)
      return &entry;
  }
  return nullptr;
}

const McpSchemaFieldSnapshot* FindSchemaFieldByKey(const McpSchemaEntrySnapshot& schema,
                                                   const std::string& key) {
  const std::string loweredKey = ToLowerAscii(key);
  for (const McpSchemaFieldSnapshot& field : schema.fields) {
    if (ToLowerAscii(field.key) == loweredKey)
      return &field;
  }
  return nullptr;
}

const McpSchemaEntrySnapshot* FindObjectSchema(const McpEditorSnapshot& snapshot, const std::string& typeName) {
  return FindSchemaEntryByName(snapshot.schema.objectTypes, typeName);
}

const McpSchemaEntrySnapshot* FindComponentSchema(const McpEditorSnapshot& snapshot,
                                                  const std::string& componentType) {
  return FindSchemaEntryByName(snapshot.schema.components, componentType);
}

bool NormalizeFieldValue(const McpSchemaFieldSnapshot& field,
                         const json& value,
                         std::string* out,
                         std::string* error) {
  if (!out)
    return false;

  if (field.widget == "float") {
    double numeric = 0.0;
    if (value.is_number()) {
      numeric = value.get<double>();
    } else if (value.is_string()) {
      if (!TryParseFloatText(value.get<std::string>(), &numeric)) {
        if (error)
          *error = "must be a number.";
        return false;
      }
    } else {
      if (error)
        *error = "must be a number.";
      return false;
    }
    if (!std::isfinite(numeric)) {
      if (error)
        *error = "must be finite.";
      return false;
    }
    if (field.hasMin && numeric < static_cast<double>(field.minVal)) {
      if (error)
        *error = "must be >= " + FormatFloatText(field.minVal) + ".";
      return false;
    }
    if (field.hasMax && numeric > static_cast<double>(field.maxVal)) {
      if (error)
        *error = "must be <= " + FormatFloatText(field.maxVal) + ".";
      return false;
    }
    *out = FormatFloatText(numeric);
    return true;
  }

  if (field.widget == "bool") {
    bool boolValue = false;
    if (value.is_boolean()) {
      boolValue = value.get<bool>();
    } else if (value.is_string()) {
      if (!TryParseBoolText(value.get<std::string>(), &boolValue)) {
        if (error)
          *error = "must be true or false.";
        return false;
      }
    } else {
      if (error)
        *error = "must be a boolean.";
      return false;
    }
    *out = boolValue ? "true" : "false";
    return true;
  }

  if (field.widget == "enum") {
    if (!value.is_string()) {
      if (error)
        *error = "must be a string.";
      return false;
    }
    const std::string text = value.get<std::string>();
    const bool knownValue =
        std::find(field.options.begin(), field.options.end(), text) != field.options.end();
    if (!knownValue && !field.allowCustomValue) {
      if (error) {
        *error = "must be one of: ";
        for (size_t i = 0; i < field.options.size(); ++i) {
          if (i > 0)
            *error += ", ";
          *error += field.options[i];
        }
        *error += ".";
      }
      return false;
    }
    *out = text;
    return true;
  }

  if (field.widget == "color3") {
    if (!NormalizeColor3Value(value, out)) {
      if (error)
        *error = "must be a color3 string or [r,g,b] array.";
      return false;
    }
    return true;
  }

  if (!value.is_string()) {
    if (error)
      *error = "must be a string.";
    return false;
  }
  *out = value.get<std::string>();
  if (!field.allowEmpty && out->empty()) {
    if (error)
      *error = "must not be empty.";
    return false;
  }
  return true;
}

bool NormalizePropsObject(const json& input,
                          const McpSchemaEntrySnapshot* schema,
                          bool allowUnknown,
                          bool applyDefaults,
                          json* out,
                          std::string* error,
                          const std::string& prefix) {
  if (!out)
    return false;
  if (!input.is_object()) {
    if (error)
      *error = prefix + " must be an object.";
    return false;
  }

  json normalized = json::object();
  if (applyDefaults && schema) {
    for (const McpSchemaFieldSnapshot& field : schema->fields) {
      if (field.hasDefault)
        normalized[field.key] = field.defaultValue;
    }
  }

  for (auto it = input.begin(); it != input.end(); ++it) {
    const std::string key = it.key();
    if (schema) {
      if (const McpSchemaFieldSnapshot* field = FindSchemaFieldByKey(*schema, key)) {
        std::string normalizedValue;
        std::string fieldError;
        if (!NormalizeFieldValue(*field, it.value(), &normalizedValue, &fieldError)) {
          if (error)
            *error = prefix + "." + key + " " + fieldError;
          return false;
        }
        normalized[field->key] = normalizedValue;
        continue;
      }
      if (!allowUnknown) {
        if (error)
          *error = "Unknown field in " + prefix + ": " + key + ".";
        return false;
      }
    }

    std::string scalar;
    if (!NormalizeFreeformScalar(it.value(), &scalar)) {
      if (error)
        *error = prefix + "." + key + " must be a scalar value.";
      return false;
    }
    normalized[key] = scalar;
  }

  if (schema) {
    for (const McpSchemaFieldSnapshot& field : schema->fields) {
      const bool hasField = normalized.contains(field.key);
      if (field.required && (!hasField || normalized[field.key].get<std::string>().empty())) {
        if (error)
          *error = prefix + "." + field.key + " is required.";
        return false;
      }
      if (hasField && !field.allowEmpty && normalized[field.key].get<std::string>().empty()) {
        if (error)
          *error = prefix + "." + field.key + " must not be empty.";
        return false;
      }
    }
  }

  *out = std::move(normalized);
  return true;
}

bool NormalizeComponentList(const json& input,
                            const McpEditorSnapshot& snapshot,
                            bool applyDefaults,
                            json* out,
                            std::string* error) {
  if (!out)
    return false;
  if (!input.is_array()) {
    if (error)
      *error = "components must be an array of objects.";
    return false;
  }

  json normalized = json::array();
  for (size_t index = 0; index < input.size(); ++index) {
    const json& item = input[index];
    if (!item.is_object() || !item.contains("type") || !item["type"].is_string()) {
      if (error)
        *error = "components[" + std::to_string(index) + "] must include a string type.";
      return false;
    }
    const std::string componentType = item["type"].get<std::string>();
    const McpSchemaEntrySnapshot* schema = FindComponentSchema(snapshot, componentType);
    if (!schema) {
      if (error)
        *error = "Unknown component type: " + componentType + ".";
      return false;
    }
    json props = json::object();
    if (!NormalizePropsObject(item.value("props", json::object()), schema, false, applyDefaults, &props,
                              error, "components[" + std::to_string(index) + "].props")) {
      return false;
    }
    normalized.push_back(json{{"type", schema->name}, {"props", std::move(props)}});
  }

  *out = std::move(normalized);
  return true;
}

bool ParseMutationMode(const json& arguments, std::string* outMode, std::string* error) {
  if (!outMode)
    return false;
  *outMode = "apply";
  if (!arguments.contains("mode"))
    return true;
  if (!arguments["mode"].is_string()) {
    if (error)
      *error = "mode must be \"preview\" or \"apply\".";
    return false;
  }
  const std::string mode = ToLowerAscii(arguments["mode"].get<std::string>());
  if (mode != "preview" && mode != "apply") {
    if (error)
      *error = "mode must be \"preview\" or \"apply\".";
    return false;
  }
  *outMode = mode;
  return true;
}

json StripMutationControls(const json& arguments) {
  json stripped = arguments.is_object() ? arguments : json::object();
  stripped.erase("mode");
  stripped.erase("previewToken");
  return stripped;
}

bool ObjectExists(const McpEditorSnapshot& snapshot, const std::string& id) {
  return FindObjectById(snapshot, id) != nullptr;
}

bool AssetExists(const McpEditorSnapshot& snapshot, const std::string& id) {
  return FindAssetById(snapshot, id) != nullptr;
}

bool ParentWouldCreateCycle(const McpEditorSnapshot& snapshot,
                            const std::string& childId,
                            const std::string& parentId) {
  std::string current = parentId;
  while (!current.empty()) {
    if (current == childId)
      return true;
    const McpObjectSnapshot* parent = FindObjectById(snapshot, current);
    if (!parent)
      return false;
    current = GetParentId(*parent);
  }
  return false;
}

bool NormalizeObjectTypeName(const std::string& raw, std::string* outType) {
  if (!outType)
    return false;
  const std::string lowered = ToLowerAscii(raw);
  if (lowered == "panel") {
    *outType = "Panel";
    return true;
  }
  if (lowered == "prop") {
    *outType = "Prop";
    return true;
  }
  if (lowered == "light") {
    *outType = "Light";
    return true;
  }
  if (lowered == "camera") {
    *outType = "Camera";
    return true;
  }
  return false;
}

json BuildToolList() {
  json tools = json::array();
  for (const McpCatalogEntry& entry : GetToolCatalog()) {
    json tool = {
        {"name", SanitizeToolName(entry.name)},
        {"description", entry.description},
        {"inputSchema", {{"type", "object"}}},
    };
    if (entry.name == "editor.search") {
      tool["inputSchema"]["properties"] = {{"query", {{"type", "string"}}},
                                           {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 25}}},
                                           {"scope", {{"type", "string"}, {"enum", json::array({"all", "objects", "assets"})}}}};
    } else if (entry.name == "editor.get_object" || entry.name == "editor.get_object_edges" ||
               entry.name == "editor.get_asset" ||
               entry.name == "editor.get_object_children" || entry.name == "editor.get_object_parent" ||
               entry.name == "editor.rename_object" || entry.name == "editor.reparent_object" ||
               entry.name == "editor.select_asset" || entry.name == "editor.update_asset" ||
               entry.name == "editor.delete_asset") {
      tool["inputSchema"]["required"] = json::array({"id"});
      tool["inputSchema"]["properties"] = {{"id", {{"type", "string"}}}};
      if (entry.name == "editor.rename_object")
        tool["inputSchema"]["properties"]["newId"] = {{"type", "string"}};
      if (entry.name == "editor.reparent_object")
        tool["inputSchema"]["properties"]["parentId"] = {{"type", "string"}};
      if (entry.name == "editor.get_object_children")
        tool["inputSchema"]["properties"]["limit"] = {{"type", "integer"}, {"minimum", 1}, {"maximum", 64}};
      if (entry.name == "editor.update_asset") {
        tool["inputSchema"]["properties"]["mesh"] = {{"type", "string"}};
        tool["inputSchema"]["properties"]["renderScale"] = {{"type", "string"}};
        tool["inputSchema"]["properties"]["albedoMap"] = {{"type", "string"}};
        tool["inputSchema"]["properties"]["displayName"] = {{"type", "string"}};
      }
      if (entry.name == "editor.rename_object")
        tool["inputSchema"]["required"] = json::array({"id", "newId"});
    } else if (entry.name == "editor.get_objects") {
      tool["inputSchema"]["required"] = json::array({"ids"});
      tool["inputSchema"]["properties"] = {{"ids", {{"type", "array"}, {"items", {{"type", "string"}}}}}};
    } else if (entry.name == "editor.list_objects") {
      tool["inputSchema"]["properties"] = {{"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 64}}},
                                           {"type", {{"type", "string"}}},
                                           {"query", {{"type", "string"}}},
                                           {"selectedOnly", {{"type", "boolean"}}}};
    } else if (entry.name == "editor.count_objects") {
      tool["inputSchema"]["properties"] = {{"type", {{"type", "string"}}}, {"query", {{"type", "string"}}}};
    } else if (entry.name == "editor.select" || entry.name == "editor.delete") {
      tool["inputSchema"]["properties"] = {{"id", {{"type", "string"}}},
                                           {"ids", {{"type", "array"}, {"items", {{"type", "string"}}}}}};
    } else if (entry.name == "editor.create_object") {
      tool["inputSchema"]["required"] = json::array({"type"});
      tool["inputSchema"]["properties"] = {{"type", {{"type", "string"}}},
                                           {"id", {{"type", "string"}}},
                                           {"assetId", {{"type", "string"}}},
                                           {"parentId", {{"type", "string"}}},
                                           {"position", MakeVec3Schema()},
                                           {"scale", MakeVec3Schema()},
                                           {"yaw", {{"type", "number"}}},
                                           {"pitch", {{"type", "number"}}},
                                           {"roll", {{"type", "number"}}},
                                           {"props", {{"type", "object"}}},
                                           {"components", {{"type", "array"}}}};
    } else if (entry.name == "editor.create_object_from_asset") {
      tool["inputSchema"]["required"] = json::array({"assetId"});
      tool["inputSchema"]["properties"] = {{"assetId", {{"type", "string"}}},
                                           {"parentId", {{"type", "string"}}},
                                           {"id", {{"type", "string"}}},
                                           {"position", MakeVec3Schema()},
                                           {"yaw", {{"type", "number"}}},
                                           {"pitch", {{"type", "number"}}},
                                           {"roll", {{"type", "number"}}}};
    } else if (entry.name == "editor.update_object") {
      tool["inputSchema"]["required"] = json::array({"id"});
      tool["inputSchema"]["properties"] = {{"id", {{"type", "string"}}},
                                           {"assetId", {{"type", "string"}}},
                                           {"position", MakeVec3Schema()},
                                           {"scale", MakeVec3Schema()},
                                           {"yaw", {{"type", "number"}}},
                                           {"pitch", {{"type", "number"}}},
                                           {"roll", {{"type", "number"}}},
                                           {"props", {{"type", "object"}}},
                                           {"components", {{"type", "array"}}}};
    } else if (entry.name == "editor.transform") {
      tool["inputSchema"]["required"] = json::array({"id"});
      tool["inputSchema"]["properties"] = {{"id", {{"type", "string"}}},
                                           {"position", MakeVec3Schema()},
                                           {"scale", MakeVec3Schema()},
                                           {"yaw", {{"type", "number"}}},
                                           {"pitch", {{"type", "number"}}},
                                           {"roll", {{"type", "number"}}}};
    } else if (entry.name == "editor.duplicate") {
      tool["inputSchema"]["required"] = json::array({"id"});
      tool["inputSchema"]["properties"] = {{"id", {{"type", "string"}}},
                                           {"count", {{"type", "integer"}, {"minimum", 1}, {"maximum", 8}}}};
    } else if (entry.name == "editor.list_assets" || entry.name == "editor.search_assets" ||
               entry.name == "editor.search_console") {
      tool["inputSchema"]["properties"] = {{"query", {{"type", "string"}}},
                                           {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 64}}}};
    } else if (entry.name == "editor.count_assets") {
      tool["inputSchema"]["properties"] = {{"query", {{"type", "string"}}}};
    } else if (entry.name == "editor.list_schema_types") {
      tool["inputSchema"]["properties"] = {
          {"kind", {{"type", "string"}, {"enum", json::array({"all", "object", "component"})}}}};
    } else if (entry.name == "editor.get_schema") {
      tool["inputSchema"]["required"] = json::array({"name"});
      tool["inputSchema"]["properties"] = {
          {"name", {{"type", "string"}}},
          {"kind", {{"type", "string"}, {"enum", json::array({"all", "object", "component"})}}}};
    } else if (entry.name == "editor.new_scene") {
      tool["inputSchema"]["properties"] = {{"sceneName", {{"type", "string"}}},
                                           {"sceneId", {{"type", "string"}}}};
    }
    if (IsWriteToolName(entry.name)) {
      tool["inputSchema"]["properties"]["mode"] = {
          {"type", "string"},
          {"enum", json::array({"preview", "apply"})},
      };
      if (IsDestructiveToolName(entry.name))
        tool["inputSchema"]["properties"]["previewToken"] = {{"type", "string"}};
    }
    tools.push_back(std::move(tool));
  }
  return json{{"tools", std::move(tools)}};
}

json BuildResourceList() {
  json resources = json::array();
  for (const McpCatalogEntry& entry : GetResourceCatalog())
    resources.push_back({{"uri", entry.target}, {"name", entry.name}, {"mimeType", "application/json"}});
  return json{{"resources", std::move(resources)}};
}

json BuildResourcePayload(const McpEditorSnapshot& snapshot, const std::string& uri, const json& params) {
  if (uri == "scene://summary")
    return BuildSceneSummaryJson(snapshot, params.value("limit", 12));
  if (uri == "scene://selection")
    return BuildSelectionJson(snapshot);
  if (uri == "scene://assets")
    return BuildAssetsJson(snapshot, params.value("limit", 12));
  if (uri == "scene://hierarchy")
    return BuildHierarchyJson(snapshot, params.value("limit", 32), params.value("offset", 0));
  if (uri == "scene://objects")
    return BuildObjectListJson(snapshot, params.value("limit", 12), params.value("type", std::string()),
                               params.value("query", std::string()), params.value("selectedOnly", false),
                               params.value("offset", 0));
  if (uri == "scene://scene_status")
    return BuildSceneStatusJson(snapshot);
  if (uri == "assets://selection")
    return BuildAssetsSelectionJson(snapshot);
  if (uri == "assets://catalog")
    return BuildAssetsCatalogJson(snapshot, params.value("limit", 12), params.value("query", std::string()),
                                  params.value("offset", 0));
  if (uri == "console://recent")
    return BuildConsoleJson(snapshot, params.value("limit", 20), params.value("offset", 0));
  if (uri == "console://summary")
    return BuildConsoleSummaryJson(snapshot, params.value("limit", 5));
  if (uri == "build://status")
    return BuildBuildStatusJson(snapshot, params.value("limit", 5));
  return json::object();
}

json BuildResourceReadResult(const McpEditorSnapshot& snapshot, const std::string& uri, const json& params) {
  const json payload = BuildResourcePayload(snapshot, uri, params);
  return json{{"contents", json::array({{{"uri", uri}, {"mimeType", "application/json"}, {"text", payload.dump()}}})}};
}

Vec3 JsonArrayToVec3(const json& value) {
  return Vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
}

bool NormalizeWriteArguments(const McpEditorSnapshot& snapshot,
                             const std::string& toolName,
                             const json& arguments,
                             json* out,
                             std::string* error) {
  if (!out)
    return false;
  if (!arguments.is_object()) {
    if (error)
      *error = "arguments must be an object.";
    return false;
  }

  json normalized = json::object();

  if (toolName == "editor.select" || toolName == "editor.delete" || toolName == "editor.duplicate") {
    if (arguments.contains("id")) {
      if (!arguments["id"].is_string()) {
        if (error)
          *error = "id must be a string.";
        return false;
      }
      normalized["id"] = arguments["id"].get<std::string>();
    }
    if (arguments.contains("ids")) {
      if (!arguments["ids"].is_array()) {
        if (error)
          *error = "ids must be an array of strings.";
        return false;
      }
      json ids = json::array();
      for (const json& item : arguments["ids"]) {
        if (!item.is_string()) {
          if (error)
            *error = "ids must be an array of strings.";
          return false;
        }
        ids.push_back(item.get<std::string>());
      }
      normalized["ids"] = std::move(ids);
    }
    if (toolName == "editor.duplicate" && arguments.contains("count")) {
      if (!arguments["count"].is_number_integer()) {
        if (error)
          *error = "count must be an integer.";
        return false;
      }
      const int count = arguments["count"].get<int>();
      if (count < 1 || count > 8) {
        if (error)
          *error = "count must be between 1 and 8.";
        return false;
      }
      normalized["count"] = count;
    }
    *out = std::move(normalized);
    return true;
  }

  if (toolName == "editor.clear_selection" || toolName == "editor.save_scene") {
    *out = std::move(normalized);
    return true;
  }

  if (toolName == "editor.select_asset" || toolName == "editor.delete_asset") {
    if (!arguments.contains("id") || !arguments["id"].is_string()) {
      if (error)
        *error = "id is required.";
      return false;
    }
    const std::string assetId = arguments["id"].get<std::string>();
    if (!assetId.empty() && !AssetExists(snapshot, assetId)) {
      if (error)
        *error = "Asset not found.";
      return false;
    }
    normalized["id"] = assetId;
    *out = std::move(normalized);
    return true;
  }

  if (toolName == "editor.update_asset") {
    if (!arguments.contains("id") || !arguments["id"].is_string()) {
      if (error)
        *error = "id is required.";
      return false;
    }
    const std::string assetId = arguments["id"].get<std::string>();
    if (!AssetExists(snapshot, assetId)) {
      if (error)
        *error = "Asset not found.";
      return false;
    }
    normalized["id"] = assetId;
    for (const char* key : {"mesh", "renderScale", "albedoMap", "displayName"}) {
      if (!arguments.contains(key))
        continue;
      if (!arguments[key].is_string() && !arguments[key].is_null()) {
        if (error)
          *error = std::string(key) + " must be a string or null.";
        return false;
      }
      normalized[key] = arguments[key];
    }
    *out = std::move(normalized);
    return true;
  }

  if (toolName == "editor.rename_object") {
    if (!arguments.contains("id") || !arguments["id"].is_string() || !arguments.contains("newId") ||
        !arguments["newId"].is_string()) {
      if (error)
        *error = "id and newId are required.";
      return false;
    }
    const std::string id = arguments["id"].get<std::string>();
    const std::string newId = arguments["newId"].get<std::string>();
    if (!ObjectExists(snapshot, id)) {
      if (error)
        *error = "Object not found.";
      return false;
    }
    if (newId.empty()) {
      if (error)
        *error = "newId is required.";
      return false;
    }
    if (const McpObjectSnapshot* existing = FindObjectById(snapshot, newId)) {
      if (existing->id != id) {
        if (error)
          *error = "Object id already exists.";
        return false;
      }
    }
    normalized["id"] = id;
    normalized["newId"] = newId;
    *out = std::move(normalized);
    return true;
  }

  if (toolName == "editor.reparent_object") {
    if (!arguments.contains("id") || !arguments["id"].is_string()) {
      if (error)
        *error = "id is required.";
      return false;
    }
    const std::string id = arguments["id"].get<std::string>();
    if (!ObjectExists(snapshot, id)) {
      if (error)
        *error = "Object not found.";
      return false;
    }
    normalized["id"] = id;
    if (arguments.contains("parentId")) {
      if (!arguments["parentId"].is_string() && !arguments["parentId"].is_null()) {
        if (error)
          *error = "parentId must be a string or null.";
        return false;
      }
      const std::string parentId =
          arguments["parentId"].is_null() ? std::string() : arguments["parentId"].get<std::string>();
      if (!parentId.empty()) {
        if (!ObjectExists(snapshot, parentId)) {
          if (error)
            *error = "Parent object not found.";
          return false;
        }
        if (parentId == id) {
          if (error)
            *error = "Object cannot parent itself.";
          return false;
        }
        if (ParentWouldCreateCycle(snapshot, id, parentId)) {
          if (error)
            *error = "Parent would create a cycle.";
          return false;
        }
        normalized["parentId"] = parentId;
      } else {
        normalized["parentId"] = nullptr;
      }
    }
    *out = std::move(normalized);
    return true;
  }

  if (toolName == "editor.new_scene") {
    if (arguments.contains("sceneId") && !arguments["sceneId"].is_string()) {
      if (error)
        *error = "sceneId must be a string.";
      return false;
    }
    if (arguments.contains("sceneName") && !arguments["sceneName"].is_string()) {
      if (error)
        *error = "sceneName must be a string.";
      return false;
    }
    if (arguments.contains("sceneId"))
      normalized["sceneId"] = arguments["sceneId"].get<std::string>();
    if (arguments.contains("sceneName"))
      normalized["sceneName"] = arguments["sceneName"].get<std::string>();
    *out = std::move(normalized);
    return true;
  }

  if (toolName == "editor.reload_scene") {
    *out = std::move(normalized);
    return true;
  }

  if (toolName == "editor.create_object" || toolName == "editor.create_object_from_asset" ||
      toolName == "editor.update_object" || toolName == "editor.transform") {
    const McpObjectSnapshot* existingObject = nullptr;
    std::string objectTypeName;

    if (toolName == "editor.create_object") {
      if (!arguments.contains("type") || !arguments["type"].is_string()) {
        if (error)
          *error = "type is required.";
        return false;
      }
      if (!NormalizeObjectTypeName(arguments["type"].get<std::string>(), &objectTypeName)) {
        if (error)
          *error = "Invalid object type.";
        return false;
      }
      normalized["type"] = objectTypeName;
    } else if (toolName == "editor.update_object" || toolName == "editor.transform") {
      if (!arguments.contains("id") || !arguments["id"].is_string()) {
        if (error)
          *error = "id is required.";
        return false;
      }
      const std::string objectId = arguments["id"].get<std::string>();
      existingObject = FindObjectById(snapshot, objectId);
      if (!existingObject) {
        if (error)
          *error = "Object not found.";
        return false;
      }
      normalized["id"] = objectId;
      objectTypeName = existingObject->type;
    }

    if (toolName == "editor.create_object_from_asset") {
      if (!arguments.contains("assetId") || !arguments["assetId"].is_string()) {
        if (error)
          *error = "assetId is required.";
        return false;
      }
      const std::string assetId = arguments["assetId"].get<std::string>();
      if (!AssetExists(snapshot, assetId)) {
        if (error)
          *error = "Asset not found.";
        return false;
      }
      normalized["assetId"] = assetId;
      objectTypeName = "Prop";
      if (arguments.contains("scale") || arguments.contains("props") || arguments.contains("components")) {
        if (error)
          *error = "create_object_from_asset only supports id, parentId, position, yaw, pitch, and roll.";
        return false;
      }
    }

    const McpSchemaEntrySnapshot* objectSchema =
        objectTypeName.empty() ? nullptr : FindObjectSchema(snapshot, objectTypeName);

    if (arguments.contains("id")) {
      if (!arguments["id"].is_string()) {
        if (error)
          *error = "id must be a string.";
        return false;
      }
      const std::string objectId = arguments["id"].get<std::string>();
      if ((toolName == "editor.create_object" || toolName == "editor.create_object_from_asset") &&
          ObjectExists(snapshot, objectId)) {
        if (error)
          *error = "Object id already exists.";
        return false;
      }
      normalized["id"] = objectId;
    }
    if (arguments.contains("assetId")) {
      if (toolName == "editor.transform") {
        if (error)
          *error = "transform only accepts id, position, scale, yaw, pitch, and roll.";
        return false;
      }
      if (!arguments["assetId"].is_string() && !arguments["assetId"].is_null()) {
        if (error)
          *error = "assetId must be a string or null.";
        return false;
      }
      const std::string assetId =
          arguments["assetId"].is_null() ? std::string() : arguments["assetId"].get<std::string>();
      if (!assetId.empty() && !AssetExists(snapshot, assetId)) {
        if (error)
          *error = "Asset not found.";
        return false;
      }
      normalized["assetId"] = arguments["assetId"];
    }
    if (arguments.contains("parentId")) {
      if (toolName == "editor.update_object" || toolName == "editor.transform") {
        if (error)
          *error = "parentId must be changed with editor.reparent_object.";
        return false;
      }
      if (!arguments["parentId"].is_string()) {
        if (error)
          *error = "parentId must be a string.";
        return false;
      }
      const std::string parentId = arguments["parentId"].get<std::string>();
      if (!ObjectExists(snapshot, parentId)) {
        if (error)
          *error = "Parent object not found.";
        return false;
      }
      normalized["parentId"] = parentId;
    }
    if (arguments.contains("position") &&
        !NormalizeVec3Argument(arguments["position"], "position", &normalized["position"], error)) {
      return false;
    }
    if (arguments.contains("scale") &&
        !NormalizeVec3Argument(arguments["scale"], "scale", &normalized["scale"], error)) {
      return false;
    }
    for (const char* key : {"yaw", "pitch", "roll"}) {
      if (!arguments.contains(key))
        continue;
      if (!arguments[key].is_number()) {
        if (error)
          *error = std::string(key) + " must be a number.";
        return false;
      }
      normalized[key] = arguments[key].get<double>();
    }
    if (arguments.contains("props")) {
      if (toolName == "editor.transform" || toolName == "editor.create_object_from_asset") {
        if (error)
          *error = toolName == "editor.transform"
                       ? "transform only accepts id, position, scale, yaw, pitch, and roll."
                       : "create_object_from_asset only supports id, parentId, position, yaw, pitch, and roll.";
        return false;
      }
      if (!NormalizePropsObject(arguments["props"], objectSchema, true,
                                toolName == "editor.create_object", &normalized["props"], error, "props")) {
        return false;
      }
    } else if (toolName == "editor.create_object" && objectSchema) {
      if (!NormalizePropsObject(json::object(), objectSchema, true, true, &normalized["props"], error, "props"))
        return false;
    }
    if (arguments.contains("components")) {
      if (toolName == "editor.transform" || toolName == "editor.create_object_from_asset") {
        if (error)
          *error = toolName == "editor.transform"
                       ? "transform only accepts id, position, scale, yaw, pitch, and roll."
                       : "create_object_from_asset only supports id, parentId, position, yaw, pitch, and roll.";
        return false;
      }
      if (!NormalizeComponentList(arguments["components"], snapshot, true, &normalized["components"], error))
        return false;
    }
    *out = std::move(normalized);
    return true;
  }

  return false;
}

json BuildMutationPreviewPayload(const McpEditorSnapshot& snapshot,
                                 const std::string& toolName,
                                 const json& arguments) {
  if (toolName == "editor.select") {
    json ids = arguments.value("ids", json::array());
    if (arguments.contains("id") && arguments["id"].is_string())
      ids.push_back(arguments["id"].get<std::string>());
    return json{{"tool", toolName}, {"selectedObjectIds", std::move(ids)}};
  }
  if (toolName == "editor.clear_selection") {
    return json{{"tool", toolName}, {"cleared", true}};
  }
  if (toolName == "editor.create_object") {
    McpObjectSnapshot object;
    object.id = arguments.value("id", std::string("(auto-generated)"));
    object.type = arguments.value("type", std::string("Panel"));
    if (arguments.contains("position"))
      object.position = JsonArrayToVec3(arguments["position"]);
    if (arguments.contains("scale"))
      object.scale = JsonArrayToVec3(arguments["scale"]);
    object.yaw = arguments.value("yaw", 0.0f);
    object.pitch = arguments.value("pitch", 0.0f);
    object.roll = arguments.value("roll", 0.0f);
    object.assetId = arguments.value("assetId", std::string());
    if (arguments.contains("props")) {
      for (auto it = arguments["props"].begin(); it != arguments["props"].end(); ++it)
        object.props[it.key()] = it.value().get<std::string>();
    }
    if (arguments.contains("parentId"))
      object.props["parentId"] = arguments["parentId"].get<std::string>();
    if (arguments.contains("components")) {
      for (const json& item : arguments["components"]) {
        McpComponentSnapshot component;
        component.type = item["type"].get<std::string>();
        for (auto it = item["props"].begin(); it != item["props"].end(); ++it)
          component.props[it.key()] = it.value().get<std::string>();
        object.components.push_back(std::move(component));
      }
    }
    return json{{"tool", toolName}, {"created", BuildObjectJson(object)}};
  }
  if (toolName == "editor.create_object_from_asset") {
    json preview{{"tool", toolName},
                 {"assetId", arguments.value("assetId", std::string())},
                 {"id", arguments.value("id", std::string("(auto-generated)"))}};
    if (const McpAssetSnapshot* asset =
            FindAssetById(snapshot, arguments.value("assetId", std::string()))) {
      preview["asset"] = BuildAssetJson(*asset);
    }
    if (arguments.contains("parentId"))
      preview["parentId"] = arguments["parentId"];
    if (arguments.contains("position"))
      preview["position"] = arguments["position"];
    preview["yaw"] = arguments.value("yaw", 0.0f);
    preview["pitch"] = arguments.value("pitch", 0.0f);
    preview["roll"] = arguments.value("roll", 0.0f);
    return preview;
  }
  if (toolName == "editor.update_object" || toolName == "editor.transform") {
    const McpObjectSnapshot* current = FindObjectById(snapshot, arguments.value("id", std::string()));
    if (!current)
      return json{{"tool", toolName}, {"id", arguments.value("id", std::string())}};
    McpObjectSnapshot updated = *current;
    if (arguments.contains("position"))
      updated.position = JsonArrayToVec3(arguments["position"]);
    if (arguments.contains("scale"))
      updated.scale = JsonArrayToVec3(arguments["scale"]);
    if (arguments.contains("yaw"))
      updated.yaw = arguments["yaw"].get<float>();
    if (arguments.contains("pitch"))
      updated.pitch = arguments["pitch"].get<float>();
    if (arguments.contains("roll"))
      updated.roll = arguments["roll"].get<float>();
    if (arguments.contains("assetId"))
      updated.assetId = arguments["assetId"].is_null() ? std::string() : arguments["assetId"].get<std::string>();
    if (arguments.contains("props")) {
      for (auto it = arguments["props"].begin(); it != arguments["props"].end(); ++it)
        updated.props[it.key()] = it.value().get<std::string>();
    }
    if (arguments.contains("components")) {
      updated.components.clear();
      for (const json& item : arguments["components"]) {
        McpComponentSnapshot component;
        component.type = item["type"].get<std::string>();
        for (auto it = item["props"].begin(); it != item["props"].end(); ++it)
          component.props[it.key()] = it.value().get<std::string>();
        updated.components.push_back(std::move(component));
      }
    }
    return json{{"tool", toolName}, {"before", BuildObjectJson(*current)}, {"after", BuildObjectJson(updated)}};
  }
  if (toolName == "editor.rename_object") {
    return json{{"tool", toolName},
                {"id", arguments.value("id", std::string())},
                {"newId", arguments.value("newId", std::string())}};
  }
  if (toolName == "editor.reparent_object") {
    return json{{"tool", toolName},
                {"id", arguments.value("id", std::string())},
                {"parentId", arguments.contains("parentId") ? arguments["parentId"] : json(nullptr)}};
  }
  if (toolName == "editor.duplicate") {
    return json{{"tool", toolName},
                {"id", arguments.value("id", std::string())},
                {"ids", arguments.value("ids", json::array())},
                {"count", arguments.value("count", 1)}};
  }
  if (toolName == "editor.delete") {
    json deleted = json::array();
    std::vector<std::string> ids;
    if (arguments.contains("id") && arguments["id"].is_string())
      ids.push_back(arguments["id"].get<std::string>());
    if (arguments.contains("ids")) {
      for (const json& item : arguments["ids"])
        ids.push_back(item.get<std::string>());
    }
    for (const std::string& id : ids) {
      if (const McpObjectSnapshot* object = FindObjectById(snapshot, id))
        deleted.push_back(BuildObjectJson(*object));
    }
    return json{{"tool", toolName},
                {"deletedCount", deleted.size()},
                {"objects", std::move(deleted)}};
  }
  if (toolName == "editor.select_asset") {
    return json{{"tool", toolName}, {"selectedAssetId", arguments.value("id", std::string())}};
  }
  if (toolName == "editor.update_asset") {
    const std::string assetId = arguments.value("id", std::string());
    json preview{{"tool", toolName}, {"id", assetId}};
    if (const McpAssetSnapshot* asset = FindAssetById(snapshot, assetId))
      preview["before"] = BuildAssetJson(*asset);
    json after = preview.value("before", json::object());
    after["id"] = assetId;
    for (const char* key : {"mesh", "renderScale", "albedoMap", "displayName"}) {
      if (arguments.contains(key))
        after[key] = arguments[key];
    }
    preview["after"] = std::move(after);
    return preview;
  }
  if (toolName == "editor.delete_asset") {
    const std::string assetId = arguments.value("id", std::string());
    json preview{{"tool", toolName}, {"deletedAssetId", assetId}};
    if (const McpAssetSnapshot* asset = FindAssetById(snapshot, assetId)) {
      preview["asset"] = BuildAssetJson(*asset);
      size_t referenceCount = 0;
      for (const McpObjectSnapshot& object : snapshot.objects) {
        if (object.assetId == assetId)
          ++referenceCount;
      }
      preview["clearedObjectReferences"] = referenceCount;
    }
    return preview;
  }
  if (toolName == "editor.new_scene") {
    return json{{"tool", toolName},
                {"currentSceneId", snapshot.sceneId},
                {"currentSceneName", snapshot.sceneName},
                {"sceneId", arguments.value("sceneId", std::string("scene"))},
                {"sceneName", arguments.value("sceneName", std::string("Scene"))}};
  }
  if (toolName == "editor.save_scene") {
    return json{{"tool", toolName},
                {"filePath", snapshot.sceneFilePath.empty() ? "assets/scenes/scene.json"
                                                             : snapshot.sceneFilePath},
                {"dirty", snapshot.dirty}};
  }
  if (toolName == "editor.reload_scene") {
    return json{{"tool", toolName},
                {"filePath", snapshot.sceneFilePath},
                {"dirty", snapshot.dirty},
                {"reloadPending", snapshot.reloadPending}};
  }
  return json{{"tool", toolName}};
}

}  // namespace

McpProtocol::McpProtocol(McpProtocolContext context) : m_context(std::move(context)) {}

size_t McpProtocol::ToolCount() const {
  return GetToolCatalog().size();
}

size_t McpProtocol::ResourceCount() const {
  return GetResourceCatalog().size();
}

const std::vector<McpCatalogEntry>& McpProtocol::ToolCatalog() const {
  return GetToolCatalog();
}

const std::vector<McpCatalogEntry>& McpProtocol::ResourceCatalog() const {
  return GetResourceCatalog();
}

McpHttpResponse McpProtocol::HandleHttp(const McpHttpRequest& request) const {
  const auto startedAt = std::chrono::steady_clock::now();
  McpActivityRecord activity;
  activity.transportMethod = request.method;
  activity.requestPreview = TruncatePreview(request.body.empty() ? "{}" : request.body);

  auto finish = [&](const std::string& operation,
                    const std::string& target,
                    const std::string& mcpMethod,
                    const json& id,
                    bool ok,
                    int httpStatus,
                    const std::string& error,
                    const json* responsePayload,
                    const std::string& responseText) {
    activity.operation = operation;
    activity.target = target;
    activity.mcpMethod = mcpMethod;
    activity.requestId = id.is_null() ? std::string() : TruncatePreview(JsonToCompactString(id), 64);
    activity.ok = ok;
    activity.httpStatus = httpStatus;
    activity.error = error;
    activity.durationMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count();
    activity.responsePreview =
        responsePayload ? TruncatePreview(responsePayload->dump()) : TruncatePreview(responseText);
    if (m_context.activitySink)
      m_context.activitySink(activity);
  };

  if (request.path != "/mcp") {
    const json body = json{{"error", "Unknown MCP endpoint."}};
    finish("http", request.path, std::string(), nullptr, false, 404, "Unknown MCP endpoint.", &body, {});
    return MakeJsonResponse(404, "Not Found", body);
  }
  if (request.method == "GET") {
    const json body = json{{"name", "horo-engine"}, {"transport", "http"}};
    finish("http", "transport.info", std::string(), nullptr, true, 200, std::string(), &body, {});
    return MakeJsonResponse(200, "OK", body);
  }
  if (request.method != "POST") {
    const json body = json{{"error", "Use POST /mcp."}};
    finish("http", request.method, std::string(), nullptr, false, 405, "Use POST /mcp.", &body, {});
    return MakeJsonResponse(405, "Method Not Allowed", body);
  }

  json payload = json::object();
  try {
    payload = json::parse(request.body.empty() ? "{}" : request.body);
  } catch (const json::exception& e) {
    const json body = json{{"error", e.what()}};
    finish("parse", "parse", std::string(), nullptr, false, 400, e.what(), &body, {});
    return MakeJsonResponse(400, "Bad Request", body);
  }

  const json id = payload.contains("id") ? payload["id"] : nullptr;
  const std::string method = payload.value("method", std::string());
  const json params = payload.value("params", json::object());
  activity.requestId = id.is_null() ? std::string() : TruncatePreview(JsonToCompactString(id), 64);
  activity.mcpMethod = method;
  activity.requestPreview = TruncatePreview(params.dump());

  if (method.rfind("notifications/", 0) == 0) {
    finish("notification", method, method, id, true, 202, std::string(), nullptr, "accepted");
    McpHttpResponse response;
    response.statusCode = 202;
    response.statusText = "Accepted";
    return response;
  }
  if (method == "initialize") {
    const json result = MakeSuccess(id, BuildInitializeResult());
    finish("initialize", "initialize", method, id, true, 200, std::string(), &result, {});
    return MakeJsonResponse(200, "OK", result);
  }
  if (method == "ping") {
    const json result = MakeSuccess(id, json::object());
    finish("ping", "ping", method, id, true, 200, std::string(), &result, {});
    return MakeJsonResponse(200, "OK", result);
  }
  if (method == "resources/list") {
    const json result = MakeSuccess(id, BuildResourceList());
    finish("resource.list", "resources/list", method, id, true, 200, std::string(), &result, {});
    return MakeJsonResponse(200, "OK", result);
  }
  if (method == "tools/list") {
    const json result = MakeSuccess(id, BuildToolList());
    finish("tool.list", "tools/list", method, id, true, 200, std::string(), &result, {});
    return MakeJsonResponse(200, "OK", result);
  }

  const std::shared_ptr<const McpEditorSnapshot> snapshot =
      m_context.snapshotProvider ? m_context.snapshotProvider() : nullptr;
  if (!snapshot) {
    const json result = MakeError(id, -32001, "Editor snapshot unavailable.");
    finish("snapshot", method, method, id, false, 503, "Editor snapshot unavailable.", &result, {});
    return MakeJsonResponse(503, "Service Unavailable", result);
  }

  if (method == "resources/read") {
    const std::string uri = params.value("uri", std::string());
    if (!IsKnownResourceUri(uri)) {
      const json result = MakeError(id, -32602, "Unknown resource URI.");
      finish("resource", uri, method, id, false, 200, "Unknown resource URI.", &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    const json result = MakeSuccess(id, BuildResourceReadResult(*snapshot, uri, params));
    finish("resource", uri, method, id, true, 200, std::string(), &result, {});
    return MakeJsonResponse(200, "OK", result);
  }

  if (method == "tools/call") {
    const std::string requestedName = params.value("name", std::string());
    const std::string name = CanonicalizeToolName(requestedName);
    const json arguments = params.value("arguments", json::object());
    activity.requestPreview = TruncatePreview(arguments.dump());
    if (name == "editor.search") {
      const json payloadOut = SearchSnapshot(*snapshot, arguments.value("query", std::string()),
                                            arguments.value("limit", 8),
                                            arguments.value("scope", std::string("all")));
      const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.get_object") {
      const McpObjectSnapshot* object = FindObjectById(*snapshot, arguments.value("id", std::string()));
      if (!object) {
        const json result = MakeError(id, -32602, "Object not found.");
        finish("tool", name, method, id, false, 200, "Object not found.", &result, {});
        return MakeJsonResponse(200, "OK", result);
      }
      const json result = MakeSuccess(id, BuildTextToolResult(BuildObjectJson(*object)));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.get_object_edges") {
      const McpObjectSnapshot* object = FindObjectById(*snapshot, arguments.value("id", std::string()));
      if (!object) {
        const json result = MakeError(id, -32602, "Object not found.");
        finish("tool", name, method, id, false, 200, "Object not found.", &result, {});
        return MakeJsonResponse(200, "OK", result);
      }
      const json result = MakeSuccess(id, BuildTextToolResult(BuildObjectEdgesJson(*object)));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.list_objects") {
      const json payloadOut = BuildObjectListJson(*snapshot, arguments.value("limit", 12),
                                                  arguments.value("type", std::string()),
                                                  arguments.value("query", std::string()),
                                                  arguments.value("selectedOnly", false));
      const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.get_objects") {
      json objects = json::array();
      for (const json& item : arguments.value("ids", json::array())) {
        if (!item.is_string())
          continue;
        if (const McpObjectSnapshot* object = FindObjectById(*snapshot, item.get<std::string>()))
          objects.push_back(BuildObjectJson(*object));
      }
      const json result = MakeSuccess(id, BuildTextToolResult(json{{"objects", std::move(objects)}}));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.get_object_children") {
      const std::string objectId = arguments.value("id", std::string());
      const size_t limit = std::max<size_t>(1, std::min<size_t>(arguments.value("limit", 12), 64));
      json children = json::array();
      size_t totalChildren = 0;
      for (const McpObjectSnapshot& object : snapshot->objects) {
        if (GetParentId(object) != objectId)
          continue;
        ++totalChildren;
        if (children.size() < limit)
          children.push_back(BuildObjectJson(object));
      }
      const json payloadOut = {{"id", objectId},
                               {"children", std::move(children)},
                               {"childCount", totalChildren},
                               {"moreChildren", totalChildren > limit ? totalChildren - limit : 0}};
      const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.get_object_parent") {
      const McpObjectSnapshot* object = FindObjectById(*snapshot, arguments.value("id", std::string()));
      if (!object) {
        const json result = MakeError(id, -32602, "Object not found.");
        finish("tool", name, method, id, false, 200, "Object not found.", &result, {});
        return MakeJsonResponse(200, "OK", result);
      }
      const std::string parentId = GetParentId(*object);
      json payloadOut = {{"id", object->id}, {"parentId", parentId}};
      if (!parentId.empty()) {
        if (const McpObjectSnapshot* parent = FindObjectById(*snapshot, parentId))
          payloadOut["parent"] = BuildObjectJson(*parent);
      }
      const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.count_objects") {
      const std::string typeFilter = ToLowerAscii(arguments.value("type", std::string()));
      const std::string query = arguments.value("query", std::string());
      size_t count = 0;
      for (const McpObjectSnapshot& object : snapshot->objects) {
        if (!typeFilter.empty() && ToLowerAscii(object.type) != typeFilter)
          continue;
        if (!MatchesObjectQuery(object, query))
          continue;
        ++count;
      }
      const json payloadOut = {{"count", count},
                               {"type", arguments.value("type", std::string())},
                               {"query", query}};
      const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.list_assets") {
      const json payloadOut = BuildAssetsCatalogJson(*snapshot, arguments.value("limit", 12),
                                                     arguments.value("query", std::string()));
      const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.get_asset") {
      const std::string assetId = arguments.value("id", std::string());
      const McpAssetSnapshot* asset = FindAssetById(*snapshot, assetId);
      if (!asset) {
        const json result = MakeError(id, -32602, "Asset not found.");
        finish("tool", name, method, id, false, 200, "Asset not found.", &result, {});
        return MakeJsonResponse(200, "OK", result);
      }
      size_t objectReferenceCount = 0;
      for (const McpObjectSnapshot& object : snapshot->objects) {
        if (object.assetId == assetId)
          ++objectReferenceCount;
      }
      json payloadOut = BuildAssetJson(*asset);
      payloadOut["objectReferenceCount"] = objectReferenceCount;
      const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.search_assets") {
      const json payloadOut = SearchAssetsSnapshot(*snapshot, arguments.value("query", std::string()),
                                                   arguments.value("limit", 8));
      const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.count_assets") {
      const std::string query = arguments.value("query", std::string());
      const json payloadOut = BuildAssetsCatalogJson(*snapshot, snapshot->assets.size(), query);
      const json result = MakeSuccess(
          id, BuildTextToolResult(json{{"count", payloadOut.value("matchedAssets", 0U)}, {"query", query}}));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.scene_status") {
      const json result = MakeSuccess(id, BuildTextToolResult(BuildSceneStatusJson(*snapshot)));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.get_scene_file") {
      const json payloadOut = {{"filePath", snapshot->sceneFilePath},
                               {"sceneId", snapshot->sceneId},
                               {"sceneName", snapshot->sceneName},
                               {"dirty", snapshot->dirty}};
      const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.search_console") {
      const json payloadOut = SearchConsoleSnapshot(*snapshot, arguments.value("query", std::string()),
                                                    arguments.value("limit", 8));
      const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.list_schema_types") {
      const json payloadOut = BuildSchemaCatalogJson(*snapshot, arguments.value("kind", std::string()));
      const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    if (name == "editor.get_schema") {
      const std::string schemaName = arguments.value("name", std::string());
      const json payloadOut =
          BuildSchemaJson(*snapshot, schemaName, arguments.value("kind", std::string()));
      if (payloadOut.empty()) {
        const json result = MakeError(id, -32602, "Schema not found.");
        finish("tool", name, method, id, false, 200, "Schema not found.", &result, {});
        return MakeJsonResponse(200, "OK", result);
      }
      const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }

    if (!IsWriteToolName(name)) {
      const json result = MakeError(id, -32601, "Unknown tool.");
      finish("tool", name, method, id, false, 200, "Unknown tool.", &result, {});
      return MakeJsonResponse(200, "OK", result);
    }

    std::string mode;
    std::string validationError;
    if (!ParseMutationMode(arguments, &mode, &validationError)) {
      const json result = MakeError(id, -32602, validationError);
      finish("tool", name, method, id, false, 200, validationError, &result, {});
      return MakeJsonResponse(200, "OK", result);
    }

    json normalizedArguments = json::object();
    if (!NormalizeWriteArguments(*snapshot, name, StripMutationControls(arguments), &normalizedArguments,
                                 &validationError)) {
      if (mode == "apply") {
        AppendMutationAuditRecord(id, *snapshot, name, mode, StripMutationControls(arguments), false,
                                  json{{"arguments", StripMutationControls(arguments)}}, validationError);
      }
      const json result = MakeError(id, -32602, validationError);
      finish("tool", name, method, id, false, 200, validationError, &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    normalizedArguments["mode"] = mode;

    auto appendApplyAudit = [&](bool ok, const json& summary, const std::string& errorText) {
      if (mode != "apply")
        return;
      AppendMutationAuditRecord(id, *snapshot, name, mode, normalizedArguments, ok, summary, errorText);
    };

    auto storePreviewToken = [&](const json& canonicalArguments) {
      std::scoped_lock lock(m_previewMutex);
      if (m_previewRecords.size() >= 128)
        m_previewRecords.clear();
      std::ostringstream token;
      token << "preview_" << ++m_nextPreviewToken;
      m_previewRecords[token.str()] = PreviewRecord{name, canonicalArguments, snapshot->sceneId,
                                                    snapshot->sceneFilePath};
      return token.str();
    };

    auto consumePreviewToken = [&](const std::string& previewToken,
                                   const json& canonicalArguments,
                                   std::string* outError) {
      std::scoped_lock lock(m_previewMutex);
      const auto it = m_previewRecords.find(previewToken);
      if (it == m_previewRecords.end()) {
        if (outError)
          *outError = "previewToken is invalid or expired.";
        return false;
      }
      const PreviewRecord& record = it->second;
      if (record.toolName != name || record.arguments != canonicalArguments ||
          record.sceneId != snapshot->sceneId || record.sceneFilePath != snapshot->sceneFilePath) {
        if (outError)
          *outError = "previewToken does not match the current scene state or arguments.";
        return false;
      }
      m_previewRecords.erase(it);
      return true;
    };

    if (mode == "preview") {
      const json canonicalArguments = StripMutationControls(normalizedArguments);
      const std::string previewToken = storePreviewToken(canonicalArguments);
      json previewPayload = {{"tool", name},
                             {"mode", "preview"},
                             {"previewToken", previewToken},
                             {"preview", BuildMutationPreviewPayload(*snapshot, name, canonicalArguments)}};
      const json result = MakeSuccess(id, BuildTextToolResult(previewPayload));
      finish("tool", name, method, id, true, 200, std::string(), &result, {});
      return MakeJsonResponse(200, "OK", result);
    }

    if (IsDestructiveToolName(name)) {
      if (!arguments.contains("previewToken") || !arguments["previewToken"].is_string()) {
        appendApplyAudit(false, BuildMutationPreviewPayload(*snapshot, name, StripMutationControls(normalizedArguments)),
                         "previewToken is required for apply mode.");
        const json result = MakeError(id, -32602, "previewToken is required for apply mode.");
        finish("tool", name, method, id, false, 200, "previewToken is required for apply mode.",
               &result, {});
        return MakeJsonResponse(200, "OK", result);
      }
      const std::string previewToken = arguments["previewToken"].get<std::string>();
      std::string previewError;
      const json canonicalArguments = StripMutationControls(normalizedArguments);
      if (!consumePreviewToken(previewToken, canonicalArguments, &previewError)) {
        appendApplyAudit(false, BuildMutationPreviewPayload(*snapshot, name, canonicalArguments), previewError);
        const json result = MakeError(id, -32602, previewError);
        finish("tool", name, method, id, false, 200, previewError, &result, {});
        return MakeJsonResponse(200, "OK", result);
      }
      normalizedArguments["previewToken"] = previewToken;
    }

    if (!m_context.commandInvoker) {
      appendApplyAudit(false, BuildMutationPreviewPayload(*snapshot, name, StripMutationControls(normalizedArguments)),
                       "Command queue unavailable.");
      const json result = MakeError(id, -32002, "Command queue unavailable.");
      finish("tool", name, method, id, false, 503, "Command queue unavailable.", &result, {});
      return MakeJsonResponse(503, "Service Unavailable", result);
    }

    const McpCommandResult commandResult = m_context.commandInvoker(name, normalizedArguments);
    if (!commandResult.ok) {
      appendApplyAudit(false,
                       commandResult.data.empty()
                           ? BuildMutationPreviewPayload(*snapshot, name, StripMutationControls(normalizedArguments))
                           : commandResult.data,
                       commandResult.error);
      const json result = MakeError(id, -32010, commandResult.error);
      finish("tool", name, method, id, false, 200, commandResult.error, &result, {});
      return MakeJsonResponse(200, "OK", result);
    }
    appendApplyAudit(true, commandResult.data, std::string());
    const json result = MakeSuccess(id, BuildTextToolResult(commandResult.data));
    finish("tool", name, method, id, true, 200, std::string(), &result, {});
    return MakeJsonResponse(200, "OK", result);
  }

  const json result = MakeError(id, -32601, "Method not found.");
  finish("method", method, method, id, false, 200, "Method not found.", &result, {});
  return MakeJsonResponse(200, "OK", result);
}

}  // namespace Mcp
}  // namespace Monolith
