#include "mcp/McpProtocol.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "core/ProjectPath.h"
#include "core/StringHash.h"
#include "mcp/McpSettings.h"

namespace Horo::Mcp {
    using json = nlohmann::json;

    namespace {
        const std::vector<McpCatalogEntry> &GetToolCatalog();

        json MakeVec3Schema() {
            return json{
                {"type", "array"},
                {"minItems", 3},
                {"maxItems", 3},
                {"items", {{"type", "number"}}}
            };
        }

        std::string ToLowerAscii(std::string value) {
            for (char &c: value)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return value;
        }

        std::string SanitizeToolName(std::string value) {
            std::ranges::replace(value, '.', '_');
            return value;
        }

        std::string CanonicalizeToolName(const std::string &value) {
            for (const McpCatalogEntry &entry: GetToolCatalog()) {
                if (value == entry.name || value == SanitizeToolName(entry.name))
                    return entry.name;
            }
            return value;
        }

        std::string JsonToCompactString(const json &value) {
            if (value.is_null())
                return "null";
            if (value.is_string())
                return value.get<std::string>();
            return value.dump();
        }

        std::string TruncatePreview(const std::string &text, size_t maxChars = 240) {
            if (text.size() <= maxChars)
                return text;
            if (maxChars < 4)
                return text.substr(0, maxChars);
            return text.substr(0, maxChars - 3) + "...";
        }

        McpHttpResponse MakeJsonResponse(int statusCode, std::string_view statusText,
                                         const json &body) {
            McpHttpResponse response;
            response.statusCode = statusCode;
            response.statusText = statusText;
            response.contentType = "application/json";
            response.body = body.dump();
            return response;
        }

        json MakeSuccess(const json &id, const json &result) {
            return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
        }

        json MakeError(const json &id, int code, const std::string &message) {
            return json{
                {"jsonrpc", "2.0"},
                {"id", id},
                {"error", {{"code", code}, {"message", message}}}
            };
        }

        const std::vector<McpCatalogEntry> &GetToolCatalog() {
            static const std::vector<McpCatalogEntry> catalog = {
                {"editor.search", "tool", "Search compact scene objects and assets."},
                {
                    "editor.get_object", "tool",
                    "Fetch one object by id with full MCP detail."
                },
                {
                    "editor.get_object_edges", "tool",
                    "Return one object's world-space corners and edge segments."
                },
                {
                    "editor.list_objects", "tool",
                    "List object summaries with optional filters and limits."
                },
                {"editor.get_objects", "tool", "Fetch multiple objects by id."},
                {
                    "editor.get_object_children", "tool",
                    "List direct children of one object."
                },
                {
                    "editor.get_object_parent", "tool",
                    "Fetch the parent summary of one object."
                },
                {
                    "editor.count_objects", "tool",
                    "Count objects with optional type filtering."
                },
                {"editor.select", "tool", "Update editor object selection."},
                {"editor.clear_selection", "tool", "Clear object and asset selection."},
                {"editor.create_object", "tool", "Create one scene object."},
                {
                    "editor.create_object_from_asset", "tool",
                    "Create a prop from an existing asset id."
                },
                {"editor.update_object", "tool", "Patch a scene object by id."},
                {"editor.transform", "tool", "Apply transform changes to one object."},
                {"editor.rename_object", "tool", "Rename a scene object id."},
                {"editor.reparent_object", "tool", "Set or clear an object's parent."},
                {"editor.duplicate", "tool", "Duplicate one object."},
                {"editor.delete", "tool", "Delete scene objects by id."},
                {
                    "editor.list_assets", "tool",
                    "List asset summaries with optional filters."
                },
                {"editor.get_asset", "tool", "Fetch one asset by id."},
                {"editor.search_assets", "tool", "Search assets by id, mesh or texture."},
                {
                    "editor.count_assets", "tool",
                    "Count assets with optional query filtering."
                },
                {"editor.select_asset", "tool", "Select an asset in the editor."},
                {"editor.update_asset", "tool", "Patch one asset definition."},
                {"editor.delete_asset", "tool", "Delete one asset definition."},
                {"editor.scene_status", "tool", "Return compact scene status."},
                {
                    "editor.get_scene_file", "tool",
                    "Return the active scene file path and save state."
                },
                {
                    "editor.list_schema_types", "tool",
                    "List object and component schemas exposed to MCP."
                },
                {
                    "editor.get_schema", "tool",
                    "Fetch one object or component schema by name."
                },
                {"editor.new_scene", "tool", "Create a new empty scene document."},
                {"editor.save_scene", "tool", "Save the active scene document."},
                {"editor.reload_scene", "tool", "Queue an in-editor scene reload."},
                {"editor.search_console", "tool", "Search recent console output."},
            };
            return catalog;
        }

        const std::vector<McpCatalogEntry> &GetResourceCatalog() {
            static const std::vector<McpCatalogEntry> catalog = {
                {
                    "scene.summary", "scene://summary",
                    "Compact scene overview with object sample."
                },
                {
                    "scene.selection", "scene://selection",
                    "Current selected objects and asset."
                },
                {"scene.assets", "scene://assets", "Compact asset summary sample."},
                {
                    "scene.hierarchy", "scene://hierarchy",
                    "Flat hierarchy preview with depth and child counts."
                },
                {"scene.objects", "scene://objects", "Filtered object list summary."},
                {
                    "scene.scene_status", "scene://scene_status",
                    "Scene status flags and counts."
                },
                {
                    "assets.selection", "assets://selection",
                    "Current selected asset summary."
                },
                {"assets.catalog", "assets://catalog", "Filtered asset catalog summary."},
                {"console.recent", "console://recent", "Recent console lines."},
                {
                    "console.summary", "console://summary",
                    "Console severity counts and latest lines."
                },
                {
                    "build.status", "build://status",
                    "Compact typed-scene and runtime build health."
                },
            };
            return catalog;
        }

        json BuildInitializeResult() {
            return json{
                {"protocolVersion", "2024-11-05"},
                {"serverInfo", {{"name", "horo-engine"}, {"version", "0.2.0"}}},
                {
                    "capabilities",
                    {
                        {"resources", {{"listChanged", false}}},
                        {"tools", {{"listChanged", false}}}
                    }
                }
            };
        }

        json BuildTextToolResult(const json &payload) {
            return json{
                {"content", json::array({{{"type", "text"}, {"text", payload.dump()}}})},
                {"structuredContent", payload}
            };
        }

        std::string GetParentId(const McpObjectSnapshot &object) {
            const auto it = object.props.find("parentId");
            if (it == object.props.end())
                return {};
            return it->second;
        }

        bool MatchesObjectQuery(const McpObjectSnapshot &object,
                                const std::string &query) {
            if (query.empty())
                return true;
            const std::string lowered = ToLowerAscii(query);
            return ToLowerAscii(object.id).find(lowered) != std::string::npos ||
                   ToLowerAscii(object.type).find(lowered) != std::string::npos ||
                   ToLowerAscii(object.assetId).find(lowered) != std::string::npos ||
                   ToLowerAscii(GetParentId(object)).find(lowered) != std::string::npos;
        }

        bool IsKnownResourceUri(const std::string &uri) {
            static const std::unordered_set<std::string, StringHash, std::equal_to<> >
                    uris = {
                        "scene://summary", "scene://selection", "scene://assets",
                        "scene://hierarchy", "scene://objects", "scene://scene_status",
                        "assets://selection", "assets://catalog", "console://recent",
                        "console://summary", "build://status",
                    };
            return uris.contains(uri);
        }

        bool IsWriteToolName(std::string_view name) {
            static const std::unordered_set<std::string, StringHash, std::equal_to<> >
                    kWriteTools = {
                        "editor.select", "editor.clear_selection",
                        "editor.create_object", "editor.create_object_from_asset",
                        "editor.update_object", "editor.transform",
                        "editor.rename_object", "editor.reparent_object",
                        "editor.duplicate", "editor.delete",
                        "editor.select_asset", "editor.update_asset",
                        "editor.delete_asset", "editor.new_scene",
                        "editor.save_scene", "editor.reload_scene",
                    };
            return kWriteTools.contains(name);
        }

        bool IsDestructiveToolName(std::string_view name) {
            static const std::unordered_set<std::string, StringHash, std::equal_to<> >
                    kDestructiveTools = {
                        "editor.delete",
                        "editor.delete_asset",
                        "editor.new_scene",
                        "editor.reload_scene",
                    };
            return kDestructiveTools.contains(name);
        }

        std::string FormatFloatText(double value) {
            return std::format("{:.4f}", value);
        }

        std::string FormatAuditTimestamp() {
            using clock = std::chrono::system_clock;
            const auto now = std::chrono::floor<std::chrono::seconds>(clock::now());
            return std::format("{:%Y-%m-%dT%H:%M:%SZ}", now);
        }

        std::filesystem::path ResolveMcpAuditPath() {
            namespace fs = std::filesystem;
            if (const fs::path projectRoot = Horo::ProjectPath::Root();
                !projectRoot.empty()) {
                if (std::error_code ec; fs::exists(projectRoot, ec) && !ec)
                    return projectRoot / ".horo" / "mcp-audit.jsonl";
            }
            return ResolveMcpSettingsDirectory() / "mcp-audit.jsonl";
        }

        // Extracts any "id" strings from a single summary value entry (object or array
        // of objects) and forwards them to the push callback.
        template<typename F>
        void CollectIdsFromSummaryValue(const json &value, F &&push) {
            if (value.is_object() && value.contains("id") && value["id"].is_string())
                push(value["id"].get<std::string>());
            if (value.is_array()) {
                for (const json &item: value) {
                    if (item.is_object() && item.contains("id") && item["id"].is_string())
                        push(item["id"].get<std::string>());
                }
            }
        }

        void PushUniqueId(json &changedIds, const std::string &value) {
            if (value.empty())
                return;
            for (const json &item: changedIds) {
                if (item.is_string() && item.get<std::string>() == value)
                    return;
            }
            changedIds.push_back(value);
        }

        json BuildChangedIds(std::string_view toolName, const json &arguments,
                             const json &summary) {
            json changedIds = json::array();
            auto pushUnique = [&changedIds](const std::string &value) {
                PushUniqueId(changedIds, value);
            };

            if (arguments.contains("id") && arguments["id"].is_string())
                pushUnique(arguments["id"].get<std::string>());
            if (arguments.contains("ids") && arguments["ids"].is_array()) {
                for (const json &item: arguments["ids"]) {
                    if (item.is_string())
                        pushUnique(item.get<std::string>());
                }
            }
            if (toolName == "editor.new_scene" && arguments.contains("sceneId") &&
                arguments["sceneId"].is_string())
                pushUnique(arguments["sceneId"].get<std::string>());

            static constexpr std::array<const char *, 5> kSummaryKeys = {
                "created", "updated", "asset", "renamed", "duplicates"
            };
            for (const char *key: kSummaryKeys) {
                if (summary.contains(key))
                    CollectIdsFromSummaryValue(summary[key], pushUnique);
            }
            for (const char *key: {"newId", "deletedAssetId", "sceneId"}) {
                if (summary.contains(key) && summary[key].is_string())
                    pushUnique(summary[key].get<std::string>());
            }

            return changedIds;
        }

        struct MutationAuditRecordInput {
            const json &requestId;
            const McpEditorSnapshot &snapshot;
            const std::string &toolName;
            const std::string &mode;
            const json &arguments;
            bool ok = false;
            const json &summary;
            const std::string &errorText;
        };

        void AppendMutationAuditRecord(const MutationAuditRecordInput &input) {
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
                {
                    "requestId", input.requestId.is_null()
                                     ? std::string()
                                     : JsonToCompactString(input.requestId)
                },
                {"tool", input.toolName},
                {"mode", input.mode},
                {"previewToken", input.arguments.value("previewToken", "")},
                {"sceneId", input.snapshot.sceneId},
                {"sceneFilePath", input.snapshot.sceneFilePath},
                {"result", input.ok ? "success" : "error"},
                {"error", input.ok ? std::string() : input.errorText},
                {
                    "changedIds",
                    BuildChangedIds(input.toolName, input.arguments, input.summary)
                },
                {"summary", input.summary},
            };

            out << record.dump() << "\n";
        }

        bool TryParseFloatText(const std::string &text, double *out) {
            if (!out)
                return false;
            char *end = nullptr;
            const double value = std::strtod(text.c_str(), &end);
            if (end == text.c_str() || (end && *end != '\0') || !std::isfinite(value))
                return false;
            *out = value;
            return true;
        }

        bool TryParseBoolText(const std::string &text, bool *out) {
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

        bool NormalizeFreeformScalar(const json &value, std::string *out) {
            if (!out)
                return false;
            if (value.is_string()) {
                *out = value.get<std::string>();
                return true;
            }
            if (value.is_number()) {
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

        bool NormalizeVec3Argument(const json &value, const std::string &argumentName,
                                   json *out, std::string *error) {
            if (!out)
                return false;
            if (!value.is_array() || value.size() != 3 || !value[0].is_number() ||
                !value[1].is_number() || !value[2].is_number()) {
                if (error)
                    *error = argumentName + " must be [x,y,z].";
                return false;
            }
            *out = json::array(
                {value[0].get<double>(), value[1].get<double>(), value[2].get<double>()});
            return true;
        }

        bool NormalizeColor3Value(const json &value, std::string *out) {
            if (!out)
                return false;
            if (value.is_string()) {
                std::stringstream stream(value.get<std::string>());
                std::string item;
                std::vector<double> parts;
                while (std::getline(stream, item, ',')) {
                    std::erase_if(item, [](unsigned char c) { return std::isspace(c); });
                    double parsed = 0.0;
                    if (!TryParseFloatText(item, &parsed))
                        return false;
                    parts.push_back(parsed);
                }
                if (parts.size() != 3)
                    return false;
                *out = FormatFloatText(parts[0]) + "," + FormatFloatText(parts[1]) + "," +
                       FormatFloatText(parts[2]);
                return true;
            }
            if (!value.is_array() || value.size() != 3 || !value[0].is_number() ||
                !value[1].is_number() || !value[2].is_number()) {
                return false;
            }
            *out = FormatFloatText(value[0].get<double>()) + "," +
                   FormatFloatText(value[1].get<double>()) + "," +
                   FormatFloatText(value[2].get<double>());
            return true;
        }

        const McpSchemaEntrySnapshot *
        FindSchemaEntryByName(const std::vector<McpSchemaEntrySnapshot> &entries,
                              const std::string &name) {
            const std::string loweredName = ToLowerAscii(name);
            for (const McpSchemaEntrySnapshot &entry: entries) {
                if (ToLowerAscii(entry.name) == loweredName)
                    return &entry;
            }
            return nullptr;
        }

        const McpSchemaFieldSnapshot *
        FindSchemaFieldByKey(const McpSchemaEntrySnapshot &schema,
                             const std::string &key) {
            const std::string loweredKey = ToLowerAscii(key);
            for (const McpSchemaFieldSnapshot &field: schema.fields) {
                if (ToLowerAscii(field.key) == loweredKey)
                    return &field;
            }
            return nullptr;
        }

        const McpSchemaEntrySnapshot *
        FindObjectSchema(const McpEditorSnapshot &snapshot,
                         const std::string &typeName) {
            return FindSchemaEntryByName(snapshot.schema.objectTypes, typeName);
        }

        const McpSchemaEntrySnapshot *
        FindComponentSchema(const McpEditorSnapshot &snapshot,
                            const std::string &componentType) {
            return FindSchemaEntryByName(snapshot.schema.components, componentType);
        }

        bool NormalizeFloatField(const McpSchemaFieldSnapshot &field, const json &value,
                                 std::string *out, std::string *error) {
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

        bool NormalizeBoolField(const json &value, std::string *out,
                                std::string *error) {
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

        bool NormalizeEnumField(const McpSchemaFieldSnapshot &field, const json &value,
                                std::string *out, std::string *error) {
            if (!value.is_string()) {
                if (error)
                    *error = "must be a string.";
                return false;
            }
            if (const std::string text = value.get<std::string>();
                std::ranges::find(field.options, text) != field.options.end() ||
                field.allowCustomValue) {
                *out = text;
                return true;
            }
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

        bool NormalizeFieldValue(const McpSchemaFieldSnapshot &field, const json &value,
                                 std::string *out, std::string *error) {
            if (!out)
                return false;

            if (field.widget == "float")
                return NormalizeFloatField(field, value, out, error);

            if (field.widget == "bool")
                return NormalizeBoolField(value, out, error);

            if (field.widget == "enum")
                return NormalizeEnumField(field, value, out, error);

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

        bool NormalizeFreeformProp(const std::string &key, const json &value,
                                   const std::string &prefix, json *normalized,
                                   std::string *error) {
            if (!normalized)
                return false;
            std::string scalar;
            if (!NormalizeFreeformScalar(value, &scalar)) {
                if (error)
                    *error = prefix + "." + key + " must be a scalar value.";
                return false;
            }
            (*normalized)[key] = scalar;
            return true;
        }

        bool NormalizeSchemaProp(const McpSchemaEntrySnapshot &schema,
                                 const std::string &key, const json &value,
                                 bool allowUnknown, const std::string &prefix,
                                 json *normalized, std::string *error) {
            if (!normalized)
                return false;
            if (const McpSchemaFieldSnapshot *field = FindSchemaFieldByKey(schema, key)) {
                std::string normalizedValue;
                if (std::string fieldError;
                    !NormalizeFieldValue(*field, value, &normalizedValue, &fieldError)) {
                    if (error)
                        *error = prefix + "." + key + " " + fieldError;
                    return false;
                }
                (*normalized)[field->key] = normalizedValue;
                return true;
            }
            if (!allowUnknown) {
                if (error)
                    *error = "Unknown field in " + prefix + ": " + key + ".";
                return false;
            }
            return NormalizeFreeformProp(key, value, prefix, normalized, error);
        }

        bool ValidateNormalizedSchemaProps(const McpSchemaEntrySnapshot &schema,
                                           const std::string &prefix,
                                           const json &normalized, std::string *error) {
            for (const McpSchemaFieldSnapshot &field: schema.fields) {
                const bool hasField = normalized.contains(field.key);
                if (field.required &&
                    (!hasField || normalized[field.key].get<std::string>().empty())) {
                    if (error)
                        *error = prefix + "." + field.key + " is required.";
                    return false;
                }
                if (hasField && !field.allowEmpty &&
                    normalized[field.key].get<std::string>().empty()) {
                    if (error)
                        *error = prefix + "." + field.key + " must not be empty.";
                    return false;
                }
            }
            return true;
        }

        bool NormalizePropsObject(const json &input,
                                  const McpSchemaEntrySnapshot *schema,
                                  bool allowUnknown, bool applyDefaults, json *out,
                                  std::string *error, const std::string &prefix) {
            if (!out)
                return false;
            if (!input.is_object()) {
                if (error)
                    *error = prefix + " must be an object.";
                return false;
            }

            json normalized = json::object();
            if (applyDefaults && schema) {
                for (const McpSchemaFieldSnapshot &field: schema->fields) {
                    if (field.hasDefault)
                        normalized[field.key] = field.defaultValue;
                }
            }

            for (auto it = input.begin(); it != input.end(); ++it) {
                const std::string key = it.key();
                if (!schema) {
                    if (!NormalizeFreeformProp(key, it.value(), prefix, &normalized, error))
                        return false;
                    continue;
                }
                if (!NormalizeSchemaProp(*schema, key, it.value(), allowUnknown, prefix,
                                         &normalized, error))
                    return false;
            }

            if (schema &&
                !ValidateNormalizedSchemaProps(*schema, prefix, normalized, error))
                return false;

            *out = std::move(normalized);
            return true;
        }

        bool NormalizeComponentList(const json &input,
                                    const McpEditorSnapshot &snapshot,
                                    bool applyDefaults, json *out, std::string *error) {
            if (!out)
                return false;
            if (!input.is_array()) {
                if (error)
                    *error = "components must be an array of objects.";
                return false;
            }

            json normalized = json::array();
            for (size_t index = 0; index < input.size(); ++index) {
                const json &item = input[index];
                if (!item.is_object() || !item.contains("type") ||
                    !item["type"].is_string()) {
                    if (error)
                        *error =
                                std::format("components[{}] must include a string type.", index);
                    return false;
                }
                const std::string componentType = item["type"].get<std::string>();
                const McpSchemaEntrySnapshot *schema =
                        FindComponentSchema(snapshot, componentType);
                if (!schema) {
                    if (error)
                        *error = "Unknown component type: " + componentType + ".";
                    return false;
                }
                json props = json::object();
                if (!NormalizePropsObject(item.value("props", json::object()), schema,
                                          false, applyDefaults, &props, error,
                                          std::format("components[{}].props", index))) {
                    return false;
                }
                normalized.push_back(
                    json{{"type", schema->name}, {"props", std::move(props)}});
            }

            *out = std::move(normalized);
            return true;
        }

        bool ParseMutationMode(const json &arguments, std::string *outMode,
                               std::string *error) {
            if (!outMode)
                return false;
            *outMode = "apply";
            if (!arguments.contains("mode"))
                return true;
            if (!arguments["mode"].is_string()) {
                if (error)
                    *error = R"(mode must be "preview" or "apply".)";
                return false;
            }
            const std::string mode = ToLowerAscii(arguments["mode"].get<std::string>());
            if (mode != "preview" && mode != "apply") {
                if (error)
                    *error = R"(mode must be "preview" or "apply".)";
                return false;
            }
            *outMode = mode;
            return true;
        }

        json StripMutationControls(const json &arguments) {
            json stripped = arguments.is_object() ? arguments : json::object();
            stripped.erase("mode");
            stripped.erase("previewToken");
            return stripped;
        }

        bool ObjectExists(const McpEditorSnapshot &snapshot, const std::string &id) {
            return FindObjectById(snapshot, id) != nullptr;
        }

        bool AssetExists(const McpEditorSnapshot &snapshot, const std::string &id) {
            return FindAssetById(snapshot, id) != nullptr;
        }

        bool ParentWouldCreateCycle(const McpEditorSnapshot &snapshot,
                                    std::string_view childId,
                                    std::string_view parentId) {
            std::string current(parentId);
            while (!current.empty()) {
                if (current == childId)
                    return true;
                const McpObjectSnapshot *parent = FindObjectById(snapshot, current);
                if (!parent)
                    return false;
                current = GetParentId(*parent);
            }
            return false;
        }

        bool NormalizeObjectTypeName(const std::string &raw, std::string *outType) {
            if (!outType)
                return false;
            static const std::unordered_map<std::string, std::string, StringHash,
                        std::equal_to<> >
                    kTypes = {
                        {"panel", "Panel"},
                        {"prop", "Prop"},
                        {"light", "Light"},
                        {"camera", "Camera"},
                    };
            if (const auto it = kTypes.find(ToLowerAscii(raw)); it != kTypes.end()) {
                *outType = it->second;
                return true;
            }
            return false;
        }

        bool ConfigureLookupToolSchema(std::string_view toolName, json *inputSchema) {
            if (static const std::unordered_set<std::string, StringHash, std::equal_to<> >
                kIdTools =
                {
                    "editor.get_object",
                    "editor.get_object_edges",
                    "editor.get_asset",
                    "editor.get_object_children",
                    "editor.get_object_parent",
                    "editor.rename_object",
                    "editor.reparent_object",
                    "editor.select_asset",
                    "editor.update_asset",
                    "editor.delete_asset",
                };
                !kIdTools.contains(toolName))
                return false;

            (*inputSchema)["properties"] = {{"id", {{"type", "string"}}}};
            (*inputSchema)["required"] = json::array({"id"});

            if (toolName == "editor.rename_object") {
                (*inputSchema)["properties"]["newId"] = {{"type", "string"}};
                (*inputSchema)["required"] = json::array({"id", "newId"});
            }
            if (toolName == "editor.reparent_object")
                (*inputSchema)["properties"]["parentId"] = {{"type", "string"}};
            if (toolName == "editor.get_object_children")
                (*inputSchema)["properties"]["limit"] = {
                    {"type", "integer"}, {"minimum", 1}, {"maximum", 64}
                };
            if (toolName == "editor.update_asset") {
                (*inputSchema)["properties"]["mesh"] = {{"type", "string"}};
                (*inputSchema)["properties"]["renderScale"] = {{"type", "string"}};
                (*inputSchema)["properties"]["albedoMap"] = {{"type", "string"}};
                (*inputSchema)["properties"]["normalMap"] = {{"type", "string"}};
                (*inputSchema)["properties"]["metallicRoughnessMap"] = {{"type", "string"}};
                (*inputSchema)["properties"]["emissiveMap"] = {{"type", "string"}};
                (*inputSchema)["properties"]["occlusionMap"] = {{"type", "string"}};
                (*inputSchema)["properties"]["displayName"] = {{"type", "string"}};
            }
            return true;
        }

        bool ConfigureObjectMutationToolSchema(std::string_view toolName,
                                               json *inputSchema) {
            if (toolName == "editor.create_object") {
                (*inputSchema)["required"] = json::array({"type"});
                (*inputSchema)["properties"] = {
                    {"type", {{"type", "string"}}}, {"id", {{"type", "string"}}},
                    {"assetId", {{"type", "string"}}}, {"parentId", {{"type", "string"}}},
                    {"position", MakeVec3Schema()}, {"scale", MakeVec3Schema()},
                    {"yaw", {{"type", "number"}}}, {"pitch", {{"type", "number"}}},
                    {"roll", {{"type", "number"}}}, {"props", {{"type", "object"}}},
                    {"components", {{"type", "array"}}}
                };
                return true;
            }
            if (toolName == "editor.create_object_from_asset") {
                (*inputSchema)["required"] = json::array({"assetId"});
                (*inputSchema)["properties"] = {
                    {"assetId", {{"type", "string"}}}, {"parentId", {{"type", "string"}}},
                    {"id", {{"type", "string"}}}, {"position", MakeVec3Schema()},
                    {"yaw", {{"type", "number"}}}, {"pitch", {{"type", "number"}}},
                    {"roll", {{"type", "number"}}}
                };
                return true;
            }
            if (toolName == "editor.update_object") {
                (*inputSchema)["required"] = json::array({"id"});
                (*inputSchema)["properties"] = {
                    {"id", {{"type", "string"}}}, {"assetId", {{"type", "string"}}},
                    {"position", MakeVec3Schema()}, {"scale", MakeVec3Schema()},
                    {"yaw", {{"type", "number"}}}, {"pitch", {{"type", "number"}}},
                    {"roll", {{"type", "number"}}}, {"props", {{"type", "object"}}},
                    {"components", {{"type", "array"}}}
                };
                return true;
            }
            if (toolName == "editor.transform") {
                (*inputSchema)["required"] = json::array({"id"});
                (*inputSchema)["properties"] = {
                    {"id", {{"type", "string"}}}, {"position", MakeVec3Schema()},
                    {"scale", MakeVec3Schema()}, {"yaw", {{"type", "number"}}},
                    {"pitch", {{"type", "number"}}}, {"roll", {{"type", "number"}}}
                };
                return true;
            }
            return false;
        }

        void ConfigureToolInputSchema(std::string_view toolName, json *inputSchema) {
            if (toolName == "editor.search") {
                (*inputSchema)["properties"] = {
                    {"query", {{"type", "string"}}},
                    {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 25}}},
                    {
                        "scope",
                        {
                            {"type", "string"},
                            {"enum", json::array({"all", "objects", "assets"})}
                        }
                    }
                };
                return;
            }
            if (ConfigureLookupToolSchema(toolName, inputSchema)) {
                return;
            }
            if (toolName == "editor.get_objects") {
                (*inputSchema)["required"] = json::array({"ids"});
                (*inputSchema)["properties"] = {
                    {"ids", {{"type", "array"}, {"items", {{"type", "string"}}}}}
                };
                return;
            }
            if (toolName == "editor.list_objects") {
                (*inputSchema)["properties"] = {
                    {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 64}}},
                    {"type", {{"type", "string"}}},
                    {"query", {{"type", "string"}}},
                    {"selectedOnly", {{"type", "boolean"}}}
                };
                return;
            }
            if (toolName == "editor.count_objects") {
                (*inputSchema)["properties"] = {
                    {"type", {{"type", "string"}}},
                    {"query", {{"type", "string"}}}
                };
                return;
            }
            if (toolName == "editor.select" || toolName == "editor.delete") {
                (*inputSchema)["properties"] = {
                    {"id", {{"type", "string"}}},
                    {"ids", {{"type", "array"}, {"items", {{"type", "string"}}}}}
                };
                return;
            }
            if (ConfigureObjectMutationToolSchema(toolName, inputSchema)) {
                return;
            }
            if (toolName == "editor.duplicate") {
                (*inputSchema)["required"] = json::array({"id"});
                (*inputSchema)["properties"] = {
                    {"id", {{"type", "string"}}},
                    {"count", {{"type", "integer"}, {"minimum", 1}, {"maximum", 8}}}
                };
                return;
            }
            if (toolName == "editor.list_assets" || toolName == "editor.search_assets" ||
                toolName == "editor.search_console") {
                (*inputSchema)["properties"] = {
                    {"query", {{"type", "string"}}},
                    {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 64}}}
                };
                return;
            }
            if (toolName == "editor.count_assets") {
                (*inputSchema)["properties"] = {{"query", {{"type", "string"}}}};
                return;
            }
            if (toolName == "editor.list_schema_types") {
                (*inputSchema)["properties"] = {
                    {
                        "kind",
                        {
                            {"type", "string"},
                            {"enum", json::array({"all", "object", "component"})}
                        }
                    }
                };
                return;
            }
            if (toolName == "editor.get_schema") {
                (*inputSchema)["required"] = json::array({"name"});
                (*inputSchema)["properties"] = {
                    {"name", {{"type", "string"}}},
                    {
                        "kind",
                        {
                            {"type", "string"},
                            {"enum", json::array({"all", "object", "component"})}
                        }
                    }
                };
                return;
            }
            if (toolName == "editor.new_scene") {
                (*inputSchema)["properties"] = {
                    {"sceneName", {{"type", "string"}}},
                    {"sceneId", {{"type", "string"}}}
                };
            }
        }

        json BuildToolList() {
            json tools = json::array();
            for (const McpCatalogEntry &entry: GetToolCatalog()) {
                json tool = {
                    {"name", SanitizeToolName(entry.name)},
                    {"description", entry.description},
                    {"inputSchema", {{"type", "object"}}},
                };
                ConfigureToolInputSchema(entry.name, &tool["inputSchema"]);
                if (IsWriteToolName(entry.name)) {
                    tool["inputSchema"]["properties"]["mode"] = {
                        {"type", "string"},
                        {"enum", json::array({"preview", "apply"})},
                    };
                    if (IsDestructiveToolName(entry.name))
                        tool["inputSchema"]["properties"]["previewToken"] = {
                            {"type", "string"}
                        };
                }
                tools.push_back(std::move(tool));
            }
            return json{{"tools", std::move(tools)}};
        }

        json BuildResourceList() {
            json resources = json::array();
            for (const McpCatalogEntry &entry: GetResourceCatalog())
                resources.push_back({
                    {"uri", entry.target},
                    {"name", entry.name},
                    {"mimeType", "application/json"}
                });
            return json{{"resources", std::move(resources)}};
        }

        json BuildResourcePayload(const McpEditorSnapshot &snapshot,
                                  std::string_view uri, const json &params) {
            if (uri == "scene://summary")
                return BuildSceneSummaryJson(snapshot, params.value("limit", 12));
            if (uri == "scene://selection")
                return BuildSelectionJson(snapshot);
            if (uri == "scene://assets")
                return BuildAssetsJson(snapshot, params.value("limit", 12));
            if (uri == "scene://hierarchy")
                return BuildHierarchyJson(snapshot, params.value("limit", 32),
                                          params.value("offset", 0));
            if (uri == "scene://objects")
                return BuildObjectListJson(
                    snapshot, params.value("limit", 12), params.value("type", ""),
                    params.value("query", ""), params.value("selectedOnly", false),
                    params.value("offset", 0));
            if (uri == "scene://scene_status")
                return BuildSceneStatusJson(snapshot);
            if (uri == "assets://selection")
                return BuildAssetsSelectionJson(snapshot);
            if (uri == "assets://catalog")
                return BuildAssetsCatalogJson(snapshot, params.value("limit", 12),
                                              params.value("query", ""),
                                              params.value("offset", 0));
            if (uri == "console://recent")
                return BuildConsoleJson(snapshot, params.value("limit", 20),
                                        params.value("offset", 0));
            if (uri == "console://summary")
                return BuildConsoleSummaryJson(snapshot, params.value("limit", 5));
            if (uri == "build://status")
                return BuildBuildStatusJson(snapshot, params.value("limit", 5));
            return json::object();
        }

        json BuildResourceReadResult(const McpEditorSnapshot &snapshot,
                                     const std::string &uri, const json &params) {
            const json payload = BuildResourcePayload(snapshot, uri, params);
            return json{
                {
                    "contents", json::array({
                        {
                            {"uri", uri},
                            {"mimeType", "application/json"},
                            {"text", payload.dump()}
                        }
                    })
                }
            };
        }

        Vec3 JsonArrayToVec3(const json &value) {
            return Vec3(value[0].get<float>(), value[1].get<float>(),
                        value[2].get<float>());
        }

        bool FailNormalization(std::string *error, std::string_view message) {
            if (error)
                *error = std::string(message);
            return false;
        }

        bool NormalizeSelectDeleteDuplicateArgs(std::string_view toolName,
                                                const json &arguments, json *out,
                                                std::string *error) {
            if (!out)
                return false;
            json normalized = json::object();
            if (arguments.contains("id")) {
                if (!arguments["id"].is_string())
                    return FailNormalization(error, "id must be a string.");
                normalized["id"] = arguments["id"].get<std::string>();
            }
            if (arguments.contains("ids")) {
                if (!arguments["ids"].is_array())
                    return FailNormalization(error, "ids must be an array of strings.");
                json ids = json::array();
                for (const json &item: arguments["ids"]) {
                    if (!item.is_string())
                        return FailNormalization(error, "ids must be an array of strings.");
                    ids.push_back(item.get<std::string>());
                }
                normalized["ids"] = std::move(ids);
            }
            if (toolName == "editor.duplicate" && arguments.contains("count")) {
                if (!arguments["count"].is_number_integer())
                    return FailNormalization(error, "count must be an integer.");
                const int count = arguments["count"].get<int>();
                if (count < 1 || count > 8)
                    return FailNormalization(error, "count must be between 1 and 8.");
                normalized["count"] = count;
            }
            *out = std::move(normalized);
            return true;
        }

        bool NormalizeAssetSelectDeleteArgs(const McpEditorSnapshot &snapshot,
                                            const json &arguments, json *out,
                                            std::string *error) {
            if (!out)
                return false;
            if (!arguments.contains("id") || !arguments["id"].is_string())
                return FailNormalization(error, "id is required.");
            const std::string assetId = arguments["id"].get<std::string>();
            if (!assetId.empty() && !AssetExists(snapshot, assetId))
                return FailNormalization(error, "Asset not found.");
            *out = json{{"id", assetId}};
            return true;
        }

        bool NormalizeUpdateAssetArgs(const McpEditorSnapshot &snapshot,
                                      const json &arguments, json *out,
                                      std::string *error) {
            if (!out)
                return false;
            if (!arguments.contains("id") || !arguments["id"].is_string())
                return FailNormalization(error, "id is required.");
            const std::string assetId = arguments["id"].get<std::string>();
            if (!AssetExists(snapshot, assetId))
                return FailNormalization(error, "Asset not found.");
            auto normalized = json{{"id", assetId}};
            for (const char *key: {"mesh", "renderScale", "albedoMap", "normalMap",
                                    "metallicRoughnessMap", "emissiveMap",
                                    "occlusionMap", "displayName"}) {
                if (!arguments.contains(key))
                    continue;
                if (!arguments[key].is_string() && !arguments[key].is_null())
                    return FailNormalization(error,
                                             std::string(key) + " must be a string or null.");
                normalized[key] = arguments[key];
            }
            *out = std::move(normalized);
            return true;
        }

        bool NormalizeRenameObjectArgs(const McpEditorSnapshot &snapshot,
                                       const json &arguments, json *out,
                                       std::string *error) {
            if (!out)
                return false;
            if (!arguments.contains("id") || !arguments["id"].is_string() ||
                !arguments.contains("newId") || !arguments["newId"].is_string())
                return FailNormalization(error, "id and newId are required.");
            const std::string id = arguments["id"].get<std::string>();
            const std::string newId = arguments["newId"].get<std::string>();
            if (!ObjectExists(snapshot, id))
                return FailNormalization(error, "Object not found.");
            if (newId.empty())
                return FailNormalization(error, "newId is required.");
            if (const McpObjectSnapshot *existing = FindObjectById(snapshot, newId)) {
                if (existing->id != id)
                    return FailNormalization(error, "Object id already exists.");
            }
            *out = json{{"id", id}, {"newId", newId}};
            return true;
        }

        bool NormalizeReparentObjectArgs(const McpEditorSnapshot &snapshot,
                                         const json &arguments, json *out,
                                         std::string *error) {
            if (!out)
                return false;
            if (!arguments.contains("id") || !arguments["id"].is_string())
                return FailNormalization(error, "id is required.");
            const std::string id = arguments["id"].get<std::string>();
            if (!ObjectExists(snapshot, id))
                return FailNormalization(error, "Object not found.");

            auto normalized = json{{"id", id}};
            if (!arguments.contains("parentId")) {
                *out = std::move(normalized);
                return true;
            }
            if (!arguments["parentId"].is_string() && !arguments["parentId"].is_null())
                return FailNormalization(error, "parentId must be a string or null.");

            const std::string parentId = arguments["parentId"].is_null()
                                             ? std::string()
                                             : arguments["parentId"].get<std::string>();
            if (parentId.empty()) {
                normalized["parentId"] = nullptr;
                *out = std::move(normalized);
                return true;
            }
            if (!ObjectExists(snapshot, parentId))
                return FailNormalization(error, "Parent object not found.");
            if (parentId == id)
                return FailNormalization(error, "Object cannot parent itself.");
            if (ParentWouldCreateCycle(snapshot, id, parentId))
                return FailNormalization(error, "Parent would create a cycle.");
            normalized["parentId"] = parentId;
            *out = std::move(normalized);
            return true;
        }

        bool NormalizeNewSceneArgs(const json &arguments, json *out,
                                   std::string *error) {
            if (!out)
                return false;
            json normalized = json::object();
            if (arguments.contains("sceneId") && !arguments["sceneId"].is_string())
                return FailNormalization(error, "sceneId must be a string.");
            if (arguments.contains("sceneName") && !arguments["sceneName"].is_string())
                return FailNormalization(error, "sceneName must be a string.");
            if (arguments.contains("sceneId"))
                normalized["sceneId"] = arguments["sceneId"].get<std::string>();
            if (arguments.contains("sceneName"))
                normalized["sceneName"] = arguments["sceneName"].get<std::string>();
            *out = std::move(normalized);
            return true;
        }

        struct ObjectMutationFlags {
            bool isCreate = false;
            bool isCreateFromAsset = false;
            bool isUpdate = false;
            bool isTransform = false;
        };

        struct ObjectMutationContext {
            const McpEditorSnapshot &snapshot;
            const json &arguments;
            ObjectMutationFlags flags;
            std::string *error = nullptr;
        };

        ObjectMutationFlags GetObjectMutationFlags(std::string_view toolName) {
            return ObjectMutationFlags{
                .isCreate = toolName == "editor.create_object",
                .isCreateFromAsset =
                toolName == "editor.create_object_from_asset",
                .isUpdate = toolName == "editor.update_object",
                .isTransform = toolName == "editor.transform"
            };
        }

        bool RejectTransformAndAssetCreateOnly(const ObjectMutationContext &ctx) {
            if (ctx.flags.isTransform) {
                return FailNormalization(
                    ctx.error,
                    "transform only accepts id, position, scale, yaw, pitch, and roll.");
            }
            if (ctx.flags.isCreateFromAsset) {
                return FailNormalization(
                    ctx.error, "create_object_from_asset only supports id, parentId, "
                    "position, yaw, pitch, and roll.");
            }
            return true;
        }

        bool NormalizeMutationRequiredFields(const ObjectMutationContext &ctx,
                                             json *normalized,
                                             std::string *objectTypeName) {
            if (ctx.flags.isCreate) {
                if (!ctx.arguments.contains("type") || !ctx.arguments["type"].is_string())
                    return FailNormalization(ctx.error, "type is required.");
                if (!NormalizeObjectTypeName(ctx.arguments["type"].get<std::string>(),
                                             objectTypeName)) {
                    return FailNormalization(ctx.error, "Invalid object type.");
                }
                (*normalized)["type"] = *objectTypeName;
                return true;
            }
            if (!ctx.flags.isUpdate && !ctx.flags.isTransform)
                return true;
            if (!ctx.arguments.contains("id") || !ctx.arguments["id"].is_string())
                return FailNormalization(ctx.error, "id is required.");
            const std::string objectId = ctx.arguments["id"].get<std::string>();
            const auto *existingObject = FindObjectById(ctx.snapshot, objectId);
            if (!existingObject)
                return FailNormalization(ctx.error, "Object not found.");
            (*normalized)["id"] = objectId;
            *objectTypeName = existingObject->type;
            return true;
        }

        bool NormalizeMutationCreateFromAssetFields(const ObjectMutationContext &ctx,
                                                    json *normalized,
                                                    std::string *objectTypeName) {
            if (!ctx.flags.isCreateFromAsset)
                return true;
            if (!ctx.arguments.contains("assetId") ||
                !ctx.arguments["assetId"].is_string())
                return FailNormalization(ctx.error, "assetId is required.");
            const std::string assetId = ctx.arguments["assetId"].get<std::string>();
            if (!AssetExists(ctx.snapshot, assetId))
                return FailNormalization(ctx.error, "Asset not found.");
            (*normalized)["assetId"] = assetId;
            *objectTypeName = "Prop";
            if (ctx.arguments.contains("scale") || ctx.arguments.contains("props") ||
                ctx.arguments.contains("components")) {
                return FailNormalization(
                    ctx.error, "create_object_from_asset only supports id, parentId, "
                    "position, yaw, pitch, and roll.");
            }
            return true;
        }

        bool NormalizeMutationIdentityFields(const ObjectMutationContext &ctx,
                                             json *normalized) {
            if (ctx.arguments.contains("id")) {
                if (!ctx.arguments["id"].is_string())
                    return FailNormalization(ctx.error, "id must be a string.");
                const std::string objectId = ctx.arguments["id"].get<std::string>();
                if ((ctx.flags.isCreate || ctx.flags.isCreateFromAsset) &&
                    ObjectExists(ctx.snapshot, objectId)) {
                    return FailNormalization(ctx.error, "Object id already exists.");
                }
                (*normalized)["id"] = objectId;
            }

            if (ctx.arguments.contains("assetId")) {
                if (ctx.flags.isTransform) {
                    return FailNormalization(
                        ctx.error,
                        "transform only accepts id, position, scale, yaw, pitch, and roll.");
                }
                if (!ctx.arguments["assetId"].is_string() &&
                    !ctx.arguments["assetId"].is_null())
                    return FailNormalization(ctx.error, "assetId must be a string or null.");
                if (const auto assetId = ctx.arguments["assetId"].is_null()
                                             ? std::string()
                                             : ctx.arguments["assetId"].get<std::string>();
                    !assetId.empty() && !AssetExists(ctx.snapshot, assetId)) {
                    return FailNormalization(ctx.error, "Asset not found.");
                }
                (*normalized)["assetId"] = ctx.arguments["assetId"];
            }

            if (!ctx.arguments.contains("parentId"))
                return true;
            if (ctx.flags.isUpdate || ctx.flags.isTransform) {
                return FailNormalization(ctx.error, "parentId must be changed with "
                                         "editor.reparent_object.");
            }
            if (!ctx.arguments["parentId"].is_string())
                return FailNormalization(ctx.error, "parentId must be a string.");
            const std::string parentId = ctx.arguments["parentId"].get<std::string>();
            if (!ObjectExists(ctx.snapshot, parentId))
                return FailNormalization(ctx.error, "Parent object not found.");
            (*normalized)["parentId"] = parentId;
            return true;
        }

        bool NormalizeMutationTransformFields(const ObjectMutationContext &ctx,
                                              json *normalized) {
            if (ctx.arguments.contains("position") &&
                !NormalizeVec3Argument(ctx.arguments["position"], "position",
                                       &(*normalized)["position"], ctx.error)) {
                return false;
            }
            if (ctx.arguments.contains("scale") &&
                !NormalizeVec3Argument(ctx.arguments["scale"], "scale",
                                       &(*normalized)["scale"], ctx.error)) {
                return false;
            }
            for (const char *key: {"yaw", "pitch", "roll"}) {
                if (!ctx.arguments.contains(key))
                    continue;
                if (!ctx.arguments[key].is_number())
                    return FailNormalization(ctx.error,
                                             std::string(key) + " must be a number.");
                (*normalized)[key] = ctx.arguments[key].get<double>();
            }
            return true;
        }

        bool NormalizeMutationDetailFields(const ObjectMutationContext &ctx,
                                           std::string_view objectTypeName,
                                           json *normalized) {
            const auto *objectSchema =
                    objectTypeName.empty()
                        ? nullptr
                        : FindObjectSchema(ctx.snapshot, std::string(objectTypeName));
            if (ctx.arguments.contains("props")) {
                if (!RejectTransformAndAssetCreateOnly(ctx))
                    return false;
                if (!NormalizePropsObject(ctx.arguments["props"], objectSchema, true,
                                          ctx.flags.isCreate, &(*normalized)["props"],
                                          ctx.error, "props")) {
                    return false;
                }
            } else if (ctx.flags.isCreate && objectSchema &&
                       !NormalizePropsObject(json::object(), objectSchema, true, true,
                                             &(*normalized)["props"], ctx.error,
                                             "props")) {
                return false;
            }
            if (!ctx.arguments.contains("components"))
                return true;
            if (!RejectTransformAndAssetCreateOnly(ctx))
                return false;
            return NormalizeComponentList(ctx.arguments["components"], ctx.snapshot, true,
                                          &(*normalized)["components"], ctx.error);
        }

        bool NormalizeObjectMutationArgs(const McpEditorSnapshot &snapshot,
                                         std::string_view toolName,
                                         const json &arguments, json *out,
                                         std::string *error) {
            if (!out)
                return false;

            const ObjectMutationContext ctx{
                .snapshot = snapshot,
                .arguments = arguments,
                .flags = GetObjectMutationFlags(toolName),
                .error = error
            };
            auto normalized = json::object();
            std::string objectTypeName;
            if (!NormalizeMutationRequiredFields(ctx, &normalized, &objectTypeName))
                return false;
            if (!NormalizeMutationCreateFromAssetFields(ctx, &normalized,
                                                        &objectTypeName))
                return false;
            if (!NormalizeMutationIdentityFields(ctx, &normalized))
                return false;
            if (!NormalizeMutationTransformFields(ctx, &normalized))
                return false;
            if (!NormalizeMutationDetailFields(ctx, objectTypeName, &normalized))
                return false;

            *out = std::move(normalized);
            return true;
        }

        bool NormalizeWriteArguments(const McpEditorSnapshot &snapshot,
                                     std::string_view toolName, const json &arguments,
                                     json *out, std::string *error) {
            if (!out)
                return false;
            if (!arguments.is_object()) {
                if (error)
                    *error = "arguments must be an object.";
                return false;
            }

            if (toolName == "editor.select" || toolName == "editor.delete" ||
                toolName == "editor.duplicate")
                return NormalizeSelectDeleteDuplicateArgs(toolName, arguments, out, error);

            if (toolName == "editor.clear_selection" || toolName == "editor.save_scene" ||
                toolName == "editor.reload_scene") {
                *out = json::object();
                return true;
            }

            if (toolName == "editor.select_asset" || toolName == "editor.delete_asset")
                return NormalizeAssetSelectDeleteArgs(snapshot, arguments, out, error);

            if (toolName == "editor.update_asset")
                return NormalizeUpdateAssetArgs(snapshot, arguments, out, error);

            if (toolName == "editor.rename_object")
                return NormalizeRenameObjectArgs(snapshot, arguments, out, error);

            if (toolName == "editor.reparent_object")
                return NormalizeReparentObjectArgs(snapshot, arguments, out, error);

            if (toolName == "editor.new_scene")
                return NormalizeNewSceneArgs(arguments, out, error);

            if (toolName == "editor.create_object" ||
                toolName == "editor.create_object_from_asset" ||
                toolName == "editor.update_object" || toolName == "editor.transform") {
                return NormalizeObjectMutationArgs(snapshot, toolName, arguments, out,
                                                   error);
            }

            return false;
        }

        json BuildCreateObjectPreview(const std::string &toolName,
                                      const json &arguments) {
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
            object.assetId = arguments.value("assetId", "");
            if (arguments.contains("props")) {
                for (auto it = arguments["props"].begin(); it != arguments["props"].end();
                     ++it) {
                    object.props[it.key()] = it.value().get<std::string>();
                }
            }
            if (arguments.contains("parentId"))
                object.props["parentId"] = arguments["parentId"].get<std::string>();
            if (arguments.contains("components")) {
                for (const json &item: arguments["components"]) {
                    McpComponentSnapshot component;
                    component.type = item["type"].get<std::string>();
                    for (auto it = item["props"].begin(); it != item["props"].end(); ++it) {
                        component.props[it.key()] = it.value().get<std::string>();
                    }
                    object.components.push_back(std::move(component));
                }
            }
            return json{{"tool", toolName}, {"created", BuildObjectJson(object)}};
        }

        json BuildCreateObjectFromAssetPreview(const McpEditorSnapshot &snapshot,
                                               const std::string &toolName,
                                               const json &arguments) {
            json preview{
                {"tool", toolName},
                {"assetId", arguments.value("assetId", "")},
                {"id", arguments.value("id", std::string("(auto-generated)"))}
            };
            if (const McpAssetSnapshot *asset =
                    FindAssetById(snapshot, arguments.value("assetId", ""))) {
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

        json BuildUpdateObjectPreview(const McpEditorSnapshot &snapshot,
                                      const std::string &toolName,
                                      const json &arguments) {
            const McpObjectSnapshot *current =
                    FindObjectById(snapshot, arguments.value("id", ""));
            if (!current)
                return json{{"tool", toolName}, {"id", arguments.value("id", "")}};

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
            if (arguments.contains("assetId")) {
                updated.assetId = arguments["assetId"].is_null()
                                      ? std::string()
                                      : arguments["assetId"].get<std::string>();
            }
            if (arguments.contains("props")) {
                for (auto it = arguments["props"].begin(); it != arguments["props"].end();
                     ++it) {
                    updated.props[it.key()] = it.value().get<std::string>();
                }
            }
            if (arguments.contains("components")) {
                updated.components.clear();
                for (const json &item: arguments["components"]) {
                    McpComponentSnapshot component;
                    component.type = item["type"].get<std::string>();
                    for (auto it = item["props"].begin(); it != item["props"].end(); ++it) {
                        component.props[it.key()] = it.value().get<std::string>();
                    }
                    updated.components.push_back(std::move(component));
                }
            }
            return json{
                {"tool", toolName},
                {"before", BuildObjectJson(*current)},
                {"after", BuildObjectJson(updated)}
            };
        }

        json BuildDeletePreview(const McpEditorSnapshot &snapshot,
                                const std::string &toolName, const json &arguments) {
            json deleted = json::array();
            std::vector<std::string> ids;
            if (arguments.contains("id") && arguments["id"].is_string())
                ids.push_back(arguments["id"].get<std::string>());
            if (arguments.contains("ids")) {
                for (const json &item: arguments["ids"])
                    ids.push_back(item.get<std::string>());
            }
            for (const std::string &id: ids) {
                if (const McpObjectSnapshot *object = FindObjectById(snapshot, id))
                    deleted.push_back(BuildObjectJson(*object));
            }
            return json{
                {"tool", toolName},
                {"deletedCount", deleted.size()},
                {"objects", std::move(deleted)}
            };
        }

        json BuildUpdateAssetPreview(const McpEditorSnapshot &snapshot,
                                     const std::string &toolName,
                                     const json &arguments) {
            const std::string assetId = arguments.value("id", "");
            json preview{{"tool", toolName}, {"id", assetId}};
            if (const McpAssetSnapshot *asset = FindAssetById(snapshot, assetId))
                preview["before"] = BuildAssetJson(*asset);
            json after = preview.value("before", json::object());
            after["id"] = assetId;
            for (const char *key: {"mesh", "renderScale", "albedoMap", "normalMap",
                                    "metallicRoughnessMap", "emissiveMap",
                                    "occlusionMap", "displayName"}) {
                if (arguments.contains(key))
                    after[key] = arguments[key];
            }
            preview["after"] = std::move(after);
            return preview;
        }

        json BuildDeleteAssetPreview(const McpEditorSnapshot &snapshot,
                                     const std::string &toolName,
                                     const json &arguments) {
            const std::string assetId = arguments.value("id", "");
            json preview{{"tool", toolName}, {"deletedAssetId", assetId}};
            if (const McpAssetSnapshot *asset = FindAssetById(snapshot, assetId)) {
                preview["asset"] = BuildAssetJson(*asset);
                preview["clearedObjectReferences"] =
                        static_cast<size_t>(std::ranges::count_if(
                            snapshot.objects, [&](const McpObjectSnapshot &object) {
                                return object.assetId == assetId;
                            }));
            }
            return preview;
        }

        json BuildMutationPreviewPayload(const McpEditorSnapshot &snapshot,
                                         const std::string &toolName,
                                         const json &arguments) {
            if (toolName == "editor.select") {
                json ids = arguments.value("ids", json::array());
                if (arguments.contains("id") && arguments["id"].is_string())
                    ids.push_back(arguments["id"].get<std::string>());
                return json{{"tool", toolName}, {"selectedObjectIds", std::move(ids)}};
            }
            if (toolName == "editor.clear_selection") {
                return json{{"tool", toolName}, {"cleared", true}};
            }
            if (toolName == "editor.create_object")
                return BuildCreateObjectPreview(toolName, arguments);
            if (toolName == "editor.create_object_from_asset")
                return BuildCreateObjectFromAssetPreview(snapshot, toolName, arguments);
            if (toolName == "editor.update_object" || toolName == "editor.transform")
                return BuildUpdateObjectPreview(snapshot, toolName, arguments);
            if (toolName == "editor.rename_object") {
                return json{
                    {"tool", toolName},
                    {"id", arguments.value("id", "")},
                    {"newId", arguments.value("newId", "")}
                };
            }
            if (toolName == "editor.reparent_object") {
                return json{
                    {"tool", toolName},
                    {"id", arguments.value("id", "")},
                    {
                        "parentId", arguments.contains("parentId")
                                        ? arguments["parentId"]
                                        : json(nullptr)
                    }
                };
            }
            if (toolName == "editor.duplicate") {
                return json{
                    {"tool", toolName},
                    {"id", arguments.value("id", "")},
                    {"ids", arguments.value("ids", json::array())},
                    {"count", arguments.value("count", 1)}
                };
            }
            if (toolName == "editor.delete")
                return BuildDeletePreview(snapshot, toolName, arguments);
            if (toolName == "editor.select_asset") {
                return json{
                    {"tool", toolName},
                    {"selectedAssetId", arguments.value("id", "")}
                };
            }
            if (toolName == "editor.update_asset")
                return BuildUpdateAssetPreview(snapshot, toolName, arguments);
            if (toolName == "editor.delete_asset")
                return BuildDeleteAssetPreview(snapshot, toolName, arguments);
            if (toolName == "editor.new_scene") {
                return json{
                    {"tool", toolName},
                    {"currentSceneId", snapshot.sceneId},
                    {"currentSceneName", snapshot.sceneName},
                    {"sceneId", arguments.value("sceneId", std::string("scene"))},
                    {"sceneName", arguments.value("sceneName", std::string("Scene"))}
                };
            }
            if (toolName == "editor.save_scene") {
                return json{
                    {"tool", toolName},
                    {
                        "filePath", snapshot.sceneFilePath.empty()
                                        ? "assets/scenes/scene.json"
                                        : snapshot.sceneFilePath
                    },
                    {"dirty", snapshot.dirty}
                };
            }
            if (toolName == "editor.reload_scene") {
                return json{
                    {"tool", toolName},
                    {"filePath", snapshot.sceneFilePath},
                    {"dirty", snapshot.dirty},
                    {"reloadPending", snapshot.reloadPending}
                };
            }
            return json{{"tool", toolName}};
        }

        struct McpActivityLabel {
            std::string_view operation;
            std::string_view target;
            std::string_view mcpMethod;
        };

        struct McpResponseInfo {
            bool ok = false;
            int httpStatus = 200;
            std::string_view error;
            const json *payload = nullptr;
            std::string_view responseText;
        };

        // Fills an activity record from per-request fields and fires the activity sink.
        void FinishActivity(McpActivityRecord &activity,
                            const std::chrono::steady_clock::time_point &startedAt,
                            const std::function<void(const McpActivityRecord &)> &sink,
                            const McpActivityLabel &label, const json &id,
                            const McpResponseInfo &response) {
            activity.operation = label.operation;
            activity.target = label.target;
            activity.mcpMethod = label.mcpMethod;
            activity.requestId = id.is_null()
                                     ? std::string()
                                     : TruncatePreview(JsonToCompactString(id), 64);
            activity.ok = response.ok;
            activity.httpStatus = response.httpStatus;
            activity.error = response.error;
            activity.durationMs = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - startedAt)
                    .count();
            activity.responsePreview =
                    response.payload
                        ? TruncatePreview(response.payload->dump())
                        : TruncatePreview(std::string(response.responseText));
            if (sink)
                sink(activity);
        }

        size_t CountAssetObjectReferences(const McpEditorSnapshot &snapshot,
                                          const std::string &assetId) {
            return static_cast<size_t>(std::ranges::count_if(
                snapshot.objects, [&](const McpObjectSnapshot &object) {
                    return object.assetId == assetId;
                }));
        }

        json BuildObjectChildrenPayload(const McpEditorSnapshot &snapshot,
                                        const std::string &objectId, size_t limit) {
            json children = json::array();
            size_t totalChildren = 0;
            for (const McpObjectSnapshot &object: snapshot.objects) {
                if (GetParentId(object) != objectId)
                    continue;
                ++totalChildren;
                if (children.size() < limit)
                    children.push_back(BuildObjectJson(object));
            }
            return json{
                {"id", objectId},
                {"children", std::move(children)},
                {"childCount", totalChildren},
                {"moreChildren", totalChildren > limit ? totalChildren - limit : 0}
            };
        }

        size_t CountMatchingObjects(const McpEditorSnapshot &snapshot,
                                    std::string_view typeFilter,
                                    const std::string &query) {
            return static_cast<size_t>(std::ranges::count_if(
                snapshot.objects, [&](const McpObjectSnapshot &object) {
                    return (typeFilter.empty() ||
                            ToLowerAscii(object.type) == typeFilter) &&
                           MatchesObjectQuery(object, query);
                }));
        }

        template<typename FinishFn>
        bool TryHandleObjectReadOnlyToolCall(const McpEditorSnapshot &snapshot,
                                             const std::string &name,
                                             const json &arguments, const json &id,
                                             std::string_view method,
                                             const FinishFn &finish,
                                             McpHttpResponse *response) {
            if (!response)
                return false;
            if (name == "editor.search") {
                const json payloadOut = SearchSnapshot(
                    snapshot, arguments.value("query", ""), arguments.value("limit", 8),
                    arguments.value("scope", std::string("all")));
                const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
                finish({"tool", name, method}, id, {true, 200, "", &result, {}});
                *response = MakeJsonResponse(200, "OK", result);
                return true;
            }
            if (name == "editor.get_object") {
                const McpObjectSnapshot *object =
                        FindObjectById(snapshot, arguments.value("id", ""));
                if (!object) {
                    const json result = MakeError(id, -32602, "Object not found.");
                    finish({"tool", name, method}, id,
                           {false, 200, "Object not found.", &result, {}});
                    *response = MakeJsonResponse(200, "OK", result);
                    return true;
                }
                const json result =
                        MakeSuccess(id, BuildTextToolResult(BuildObjectJson(*object)));
                finish({"tool", name, method}, id, {true, 200, "", &result, {}});
                *response = MakeJsonResponse(200, "OK", result);
                return true;
            }
            if (name == "editor.get_object_edges") {
                const McpObjectSnapshot *object =
                        FindObjectById(snapshot, arguments.value("id", ""));
                if (!object) {
                    const json result = MakeError(id, -32602, "Object not found.");
                    finish({"tool", name, method}, id,
                           {false, 200, "Object not found.", &result, {}});
                    *response = MakeJsonResponse(200, "OK", result);
                    return true;
                }
                const json result =
                        MakeSuccess(id, BuildTextToolResult(BuildObjectEdgesJson(*object)));
                finish({"tool", name, method}, id, {true, 200, "", &result, {}});
                *response = MakeJsonResponse(200, "OK", result);
                return true;
            }
            if (name == "editor.list_objects") {
                const json payloadOut = BuildObjectListJson(
                    snapshot, arguments.value("limit", 12), arguments.value("type", ""),
                    arguments.value("query", ""), arguments.value("selectedOnly", false));
                const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
                finish({"tool", name, method}, id, {true, 200, "", &result, {}});
                *response = MakeJsonResponse(200, "OK", result);
                return true;
            }
            if (name == "editor.get_objects") {
                json objects = json::array();
                for (const json &item: arguments.value("ids", json::array())) {
                    if (!item.is_string())
                        continue;
                    const McpObjectSnapshot *object =
                            FindObjectById(snapshot, item.get<std::string>());
                    if (object)
                        objects.push_back(BuildObjectJson(*object));
                }
                const json result = MakeSuccess(
                    id, BuildTextToolResult(json{{"objects", std::move(objects)}}));
                finish({"tool", name, method}, id, {true, 200, "", &result, {}});
                *response = MakeJsonResponse(200, "OK", result);
                return true;
            }
            if (name == "editor.get_object_children") {
                const std::string objectId = arguments.value("id", "");
                const size_t limit =
                        std::max<size_t>(1, std::min<size_t>(arguments.value("limit", 12), 64));
                const json payloadOut =
                        BuildObjectChildrenPayload(snapshot, objectId, limit);
                const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
                finish({"tool", name, method}, id, {true, 200, "", &result, {}});
                *response = MakeJsonResponse(200, "OK", result);
                return true;
            }
            if (name == "editor.get_object_parent") {
                const McpObjectSnapshot *object =
                        FindObjectById(snapshot, arguments.value("id", ""));
                if (!object) {
                    const json result = MakeError(id, -32602, "Object not found.");
                    finish({"tool", name, method}, id,
                           {false, 200, "Object not found.", &result, {}});
                    *response = MakeJsonResponse(200, "OK", result);
                    return true;
                }
                const std::string parentId = GetParentId(*object);
                json payloadOut = {{"id", object->id}, {"parentId", parentId}};
                if (const McpObjectSnapshot *parent =
                        parentId.empty() ? nullptr : FindObjectById(snapshot, parentId)) {
                    payloadOut["parent"] = BuildObjectJson(*parent);
                }
                const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
                finish({"tool", name, method}, id, {true, 200, "", &result, {}});
                *response = MakeJsonResponse(200, "OK", result);
                return true;
            }
            if (name == "editor.count_objects") {
                const std::string typeFilter = ToLowerAscii(arguments.value("type", ""));
                const std::string query = arguments.value("query", "");
                const size_t count = CountMatchingObjects(snapshot, typeFilter, query);
                auto payloadOut = json{
                    {"count", count},
                    {"type", arguments.value("type", "")},
                    {"query", query}
                };
                const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
                finish({"tool", name, method}, id, {true, 200, "", &result, {}});
                *response = MakeJsonResponse(200, "OK", result);
                return true;
            }
            return false;
        }

        template<typename FinishFn>
        bool TryHandleAssetReadOnlyToolCall(const McpEditorSnapshot &snapshot,
                                            const std::string &name,
                                            const json &arguments, const json &id,
                                            std::string_view method,
                                            const FinishFn &finish,
                                            McpHttpResponse *response) {
            if (!response)
                return false;
            if (name == "editor.list_assets") {
                const json payloadOut = BuildAssetsCatalogJson(
                    snapshot, arguments.value("limit", 12), arguments.value("query", ""));
                const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
                finish({"tool", name, method}, id, {true, 200, "", &result, {}});
                *response = MakeJsonResponse(200, "OK", result);
                return true;
            }
            if (name == "editor.get_asset") {
                const std::string assetId = arguments.value("id", "");
                const McpAssetSnapshot *asset = FindAssetById(snapshot, assetId);
                if (!asset) {
                    const json result = MakeError(id, -32602, "Asset not found.");
                    finish({"tool", name, method}, id,
                           {false, 200, "Asset not found.", &result, {}});
                    *response = MakeJsonResponse(200, "OK", result);
                    return true;
                }
                json payloadOut = BuildAssetJson(*asset);
                payloadOut["objectReferenceCount"] =
                        CountAssetObjectReferences(snapshot, assetId);
                const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
                finish({"tool", name, method}, id, {true, 200, "", &result, {}});
                *response = MakeJsonResponse(200, "OK", result);
                return true;
            }
            if (name == "editor.search_assets") {
                const json payloadOut = SearchAssetsSnapshot(
                    snapshot, arguments.value("query", ""), arguments.value("limit", 8));
                const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
                finish({"tool", name, method}, id, {true, 200, "", &result, {}});
                *response = MakeJsonResponse(200, "OK", result);
                return true;
            }
            if (name == "editor.count_assets") {
                const std::string query = arguments.value("query", "");
                const json payloadOut =
                        BuildAssetsCatalogJson(snapshot, snapshot.assets.size(), query);
                const json result =
                        MakeSuccess(id, BuildTextToolResult(json{
                                        {"count", payloadOut.value("matchedAssets", 0U)},
                                        {"query", query}
                                    }));
                finish({"tool", name, method}, id, {true, 200, "", &result, {}});
                *response = MakeJsonResponse(200, "OK", result);
                return true;
            }
            return false;
        }

        template<typename FinishFn>
        bool TryHandleSceneReadOnlyToolCall(const McpEditorSnapshot &snapshot,
                                            const std::string &name,
                                            const json &arguments, const json &id,
                                            std::string_view method,
                                            const FinishFn &finish,
                                            McpHttpResponse *response) {
            if (!response)
                return false;
            if (name == "editor.scene_status") {
                const json result =
                        MakeSuccess(id, BuildTextToolResult(BuildSceneStatusJson(snapshot)));
                finish({"tool", name, method}, id, {true, 200, "", &result, {}});
                *response = MakeJsonResponse(200, "OK", result);
                return true;
            }
            if (name == "editor.get_scene_file") {
                const json payloadOut = {
                    {"filePath", snapshot.sceneFilePath},
                    {"sceneId", snapshot.sceneId},
                    {"sceneName", snapshot.sceneName},
                    {"dirty", snapshot.dirty}
                };
                const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
                finish({"tool", name, method}, id, {true, 200, "", &result, {}});
                *response = MakeJsonResponse(200, "OK", result);
                return true;
            }
            if (name == "editor.search_console") {
                const json payloadOut = SearchConsoleSnapshot(
                    snapshot, arguments.value("query", ""), arguments.value("limit", 8));
                const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
                finish({"tool", name, method}, id, {true, 200, "", &result, {}});
                *response = MakeJsonResponse(200, "OK", result);
                return true;
            }
            if (name == "editor.list_schema_types") {
                const auto payloadOut =
                        BuildSchemaCatalogJson(snapshot, arguments.value("kind", ""));
                const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
                finish({"tool", name, method}, id, {true, 200, "", &result, {}});
                *response = MakeJsonResponse(200, "OK", result);
                return true;
            }
            if (name == "editor.get_schema") {
                const std::string schemaName = arguments.value("name", "");
                const json payloadOut =
                        BuildSchemaJson(snapshot, schemaName, arguments.value("kind", ""));
                if (payloadOut.empty()) {
                    const json result = MakeError(id, -32602, "Schema not found.");
                    finish({"tool", name, method}, id,
                           {false, 200, "Schema not found.", &result, {}});
                    *response = MakeJsonResponse(200, "OK", result);
                    return true;
                }
                const json result = MakeSuccess(id, BuildTextToolResult(payloadOut));
                finish({"tool", name, method}, id, {true, 200, "", &result, {}});
                *response = MakeJsonResponse(200, "OK", result);
                return true;
            }
            return false;
        }

        template<typename FinishFn>
        bool TryHandleReadOnlyToolCall(const McpEditorSnapshot &snapshot,
                                       const std::string &name, const json &arguments,
                                       const json &id, std::string_view method,
                                       const FinishFn &finish,
                                       McpHttpResponse *response) {
            return TryHandleObjectReadOnlyToolCall(snapshot, name, arguments, id, method,
                                                   finish, response) ||
                   TryHandleAssetReadOnlyToolCall(snapshot, name, arguments, id, method,
                                                  finish, response) ||
                   TryHandleSceneReadOnlyToolCall(snapshot, name, arguments, id, method,
                                                  finish, response);
        }

        struct WriteToolCallInput {
            const McpEditorSnapshot &snapshot;
            const std::string &name;
            const json &arguments;
            const json &id;
            std::string_view method;
        };

        template<typename CommandInvokerFn, typename StorePreviewTokenFn,
            typename ConsumePreviewTokenFn, typename FinishFn>
        McpHttpResponse HandleWriteToolCall(
            const WriteToolCallInput &input, const CommandInvokerFn &commandInvoker,
            const StorePreviewTokenFn &storePreviewToken,
            const ConsumePreviewTokenFn &consumePreviewToken, const FinishFn &finish) {
            std::string mode;
            std::string validationError;
            if (!ParseMutationMode(input.arguments, &mode, &validationError)) {
                const json result = MakeError(input.id, -32602, validationError);
                finish({"tool", input.name, input.method}, input.id,
                       {false, 200, validationError, &result, {}});
                return MakeJsonResponse(200, "OK", result);
            }

            json normalizedArguments = json::object();
            if (!NormalizeWriteArguments(input.snapshot, input.name,
                                         StripMutationControls(input.arguments),
                                         &normalizedArguments, &validationError)) {
                if (mode == "apply") {
                    const json strippedArguments = StripMutationControls(input.arguments);
                    AppendMutationAuditRecord(MutationAuditRecordInput{
                        .requestId = input.id,
                        .snapshot = input.snapshot,
                        .toolName = input.name,
                        .mode = mode,
                        .arguments = strippedArguments,
                        .ok = false,
                        .summary = json{{"arguments", strippedArguments}},
                        .errorText = validationError
                    });
                }
                const json result = MakeError(input.id, -32602, validationError);
                finish({"tool", input.name, input.method}, input.id,
                       {false, 200, validationError, &result, {}});
                return MakeJsonResponse(200, "OK", result);
            }
            normalizedArguments["mode"] = mode;

            const auto appendApplyAudit = [&](bool ok, const json &summary,
                                              const std::string &errorText) {
                if (mode != "apply")
                    return;
                AppendMutationAuditRecord(
                    MutationAuditRecordInput{
                        .requestId = input.id,
                        .snapshot = input.snapshot,
                        .toolName = input.name,
                        .mode = mode,
                        .arguments = normalizedArguments,
                        .ok = ok,
                        .summary = summary,
                        .errorText = errorText
                    });
            };

            if (mode == "preview") {
                const json canonicalArguments = StripMutationControls(normalizedArguments);
                const std::string previewToken = storePreviewToken(canonicalArguments);
                json previewPayload = {
                    {"tool", input.name},
                    {"mode", "preview"},
                    {"previewToken", previewToken},
                    {
                        "preview", BuildMutationPreviewPayload(input.snapshot, input.name,
                                                               canonicalArguments)
                    }
                };
                const json result =
                        MakeSuccess(input.id, BuildTextToolResult(previewPayload));
                finish({"tool", input.name, input.method}, input.id,
                       {true, 200, "", &result, {}});
                return MakeJsonResponse(200, "OK", result);
            }

            if (IsDestructiveToolName(input.name)) {
                if (!input.arguments.contains("previewToken") ||
                    !input.arguments["previewToken"].is_string()) {
                    appendApplyAudit(false,
                                     BuildMutationPreviewPayload(
                                         input.snapshot, input.name,
                                         StripMutationControls(normalizedArguments)),
                                     "previewToken is required for apply mode.");
                    const json result = MakeError(input.id, -32602,
                                                  "previewToken is required for apply mode.");
                    finish({"tool", input.name, input.method}, input.id,
                           {
                               false,
                               200,
                               "previewToken is required for apply mode.",
                               &result,
                               {}
                           });
                    return MakeJsonResponse(200, "OK", result);
                }
                const std::string previewToken =
                        input.arguments["previewToken"].get<std::string>();
                std::string previewError;
                if (const json canonicalArguments =
                            StripMutationControls(normalizedArguments);
                    !consumePreviewToken(previewToken, canonicalArguments, &previewError)) {
                    appendApplyAudit(false,
                                     BuildMutationPreviewPayload(input.snapshot, input.name,
                                                                 canonicalArguments),
                                     previewError);
                    const json result = MakeError(input.id, -32602, previewError);
                    finish({"tool", input.name, input.method}, input.id,
                           {false, 200, previewError, &result, {}});
                    return MakeJsonResponse(200, "OK", result);
                }
                normalizedArguments["previewToken"] = previewToken;
            }

            if (!commandInvoker) {
                appendApplyAudit(
                    false,
                    BuildMutationPreviewPayload(input.snapshot, input.name,
                                                StripMutationControls(normalizedArguments)),
                    "Command queue unavailable.");
                const json result =
                        MakeError(input.id, -32002, "Command queue unavailable.");
                finish({"tool", input.name, input.method}, input.id,
                       {false, 503, "Command queue unavailable.", &result, {}});
                return MakeJsonResponse(503, "Service Unavailable", result);
            }

            const McpCommandResult commandResult =
                    commandInvoker(input.name, normalizedArguments);
            if (!commandResult.ok) {
                appendApplyAudit(false,
                                 commandResult.data.empty()
                                     ? BuildMutationPreviewPayload(
                                         input.snapshot, input.name,
                                         StripMutationControls(normalizedArguments))
                                     : commandResult.data,
                                 commandResult.error);
                const json result = MakeError(input.id, -32010, commandResult.error);
                finish({"tool", input.name, input.method}, input.id,
                       {false, 200, commandResult.error, &result, {}});
                return MakeJsonResponse(200, "OK", result);
            }
            appendApplyAudit(true, commandResult.data, std::string());
            const json result =
                    MakeSuccess(input.id, BuildTextToolResult(commandResult.data));
            finish({"tool", input.name, input.method}, input.id,
                   {true, 200, "", &result, {}});
            return MakeJsonResponse(200, "OK", result);
        }

        struct PreviewTokenValidationInput {
            const std::string &previewToken;
            const std::string &toolName;
            const json &canonicalArguments;
            std::string_view sceneId;
            std::string_view sceneFilePath;
            std::string *outError = nullptr;
        };

        template<typename PreviewMap>
        bool ConsumePreviewTokenRecord(std::mutex *previewMutex,
                                       PreviewMap *previewRecords,
                                       const PreviewTokenValidationInput &input) {
            if (!previewMutex || !previewRecords)
                return false;
            std::scoped_lock lock(*previewMutex);
            if (const auto it = previewRecords->find(input.previewToken);
                it == previewRecords->end()) {
                if (input.outError)
                    *input.outError = "previewToken is invalid or expired.";
                return false;
            } else if (const auto &record = it->second;
                record.toolName != input.toolName ||
                record.arguments != input.canonicalArguments ||
                record.sceneId != input.sceneId ||
                record.sceneFilePath != input.sceneFilePath) {
                if (input.outError) {
                    *input.outError =
                            "previewToken does not match the current scene state or arguments.";
                }
                return false;
            } else {
                previewRecords->erase(it);
                return true;
            }
        }

        template<typename FinishFn>
        bool TryHandleTransportRequest(const McpHttpRequest &request,
                                       const FinishFn &finish,
                                       McpHttpResponse *response) {
            if (!response)
                return false;
            if (request.path != "/mcp") {
                const auto body = json{{"error", "Unknown MCP endpoint."}};
                finish({"http", request.path, ""}, nullptr,
                       {false, 404, "Unknown MCP endpoint.", &body, {}});
                *response = MakeJsonResponse(404, "Not Found", body);
                return true;
            }
            if (request.method == "GET") {
                const auto body = json{{"name", "horo-engine"}, {"transport", "http"}};
                finish({"http", "transport.info", ""}, nullptr, {true, 200, "", &body, {}});
                *response = MakeJsonResponse(200, "OK", body);
                return true;
            }
            if (request.method != "POST") {
                const auto body = json{{"error", "Use POST /mcp."}};
                finish({"http", request.method, ""}, nullptr,
                       {false, 405, "Use POST /mcp.", &body, {}});
                *response = MakeJsonResponse(405, "Method Not Allowed", body);
                return true;
            }
            return false;
        }

        template<typename FinishFn>
        bool TryHandleRpcMethodBeforeSnapshot(const std::string &method, const json &id,
                                              const FinishFn &finish,
                                              McpHttpResponse *response) {
            if (!response)
                return false;
            if (method.starts_with("notifications/")) {
                finish({"notification", method, method}, id,
                       {true, 202, "", nullptr, "accepted"});
                McpHttpResponse out;
                out.statusCode = 202;
                out.statusText = "Accepted";
                *response = std::move(out);
                return true;
            }
            if (method == "initialize") {
                const json result = MakeSuccess(id, BuildInitializeResult());
                finish({"initialize", "initialize", method}, id,
                       {true, 200, "", &result, {}});
                *response = MakeJsonResponse(200, "OK", result);
                return true;
            }
            if (method == "ping") {
                const json result = MakeSuccess(id, json::object());
                finish({"ping", "ping", method}, id, {true, 200, "", &result, {}});
                *response = MakeJsonResponse(200, "OK", result);
                return true;
            }
            if (method == "resources/list") {
                const json result = MakeSuccess(id, BuildResourceList());
                finish({"resource.list", "resources/list", method}, id,
                       {true, 200, "", &result, {}});
                *response = MakeJsonResponse(200, "OK", result);
                return true;
            }
            if (method == "tools/list") {
                const json result = MakeSuccess(id, BuildToolList());
                finish({"tool.list", "tools/list", method}, id,
                       {true, 200, "", &result, {}});
                *response = MakeJsonResponse(200, "OK", result);
                return true;
            }
            return false;
        }

        template<typename FinishFn>
        bool TryHandleResourceRead(const McpEditorSnapshot &snapshot,
                                   const std::string &method, const json &id,
                                   const json &params, const FinishFn &finish,
                                   McpHttpResponse *response) {
            if (!response || method != "resources/read")
                return false;
            const std::string uri = params.value("uri", "");
            if (!IsKnownResourceUri(uri)) {
                const json result = MakeError(id, -32602, "Unknown resource URI.");
                finish({"resource", uri, method}, id,
                       {false, 200, "Unknown resource URI.", &result, {}});
                *response = MakeJsonResponse(200, "OK", result);
                return true;
            }
            const json result =
                    MakeSuccess(id, BuildResourceReadResult(snapshot, uri, params));
            finish({"resource", uri, method}, id, {true, 200, "", &result, {}});
            *response = MakeJsonResponse(200, "OK", result);
            return true;
        }
    } // namespace

    McpProtocol::McpProtocol(McpProtocolContext context)
        : m_context(std::move(context)) {
    }

    size_t McpProtocol::ToolCount() const { return GetToolCatalog().size(); }

    size_t McpProtocol::ResourceCount() const {
        return GetResourceCatalog().size();
    }

    const std::vector<McpCatalogEntry> &McpProtocol::ToolCatalog() const {
        return GetToolCatalog();
    }

    const std::vector<McpCatalogEntry> &McpProtocol::ResourceCatalog() const {
        return GetResourceCatalog();
    }

    McpHttpResponse McpProtocol::HandleHttp(const McpHttpRequest &request) const {
        const auto startedAt = std::chrono::steady_clock::now();
        McpActivityRecord activity;
        activity.transportMethod = request.method;
        activity.requestPreview =
                TruncatePreview(request.body.empty() ? "{}" : request.body);

        auto finish = [&activity, &startedAt, this](const McpActivityLabel &label,
                                                    const json &id,
                                                    const McpResponseInfo &response) {
            FinishActivity(activity, startedAt, m_context.activitySink, label, id,
                           response);
        };

        if (McpHttpResponse transportResponse;
            TryHandleTransportRequest(request, finish, &transportResponse)) {
            return transportResponse;
        }

        json payload = json::object();
        try {
            payload = json::parse(request.body.empty() ? "{}" : request.body);
        } catch (const json::exception &e) {
            const auto body = json{{"error", e.what()}};
            finish({"parse", "parse", ""}, nullptr, {false, 400, e.what(), &body, {}});
            return MakeJsonResponse(400, "Bad Request", body);
        }

        const json id = payload.contains("id") ? payload["id"] : nullptr;
        const std::string method = payload.value("method", "");
        const json params = payload.value("params", json::object());
        activity.requestId = id.is_null()
                                 ? std::string()
                                 : TruncatePreview(JsonToCompactString(id), 64);
        activity.mcpMethod = method;
        activity.requestPreview = TruncatePreview(params.dump());

        if (McpHttpResponse preSnapshotResponse; TryHandleRpcMethodBeforeSnapshot(
            method, id, finish, &preSnapshotResponse)) {
            return preSnapshotResponse;
        }

        const std::shared_ptr<const McpEditorSnapshot> snapshot =
                m_context.snapshotProvider ? m_context.snapshotProvider() : nullptr;
        if (!snapshot) {
            const json result = MakeError(id, -32001, "Editor snapshot unavailable.");
            finish({"snapshot", method, method}, id,
                   {false, 503, "Editor snapshot unavailable.", &result, {}});
            return MakeJsonResponse(503, "Service Unavailable", result);
        }

        if (McpHttpResponse resourceResponse; TryHandleResourceRead(
            *snapshot, method, id, params, finish, &resourceResponse)) {
            return resourceResponse;
        }

        if (method == "tools/call") {
            const std::string requestedName = params.value("name", "");
            const std::string name = CanonicalizeToolName(requestedName);
            const json arguments = params.value("arguments", json::object());
            activity.requestPreview = TruncatePreview(arguments.dump());
            if (McpHttpResponse readOnlyResponse;
                TryHandleReadOnlyToolCall(*snapshot, name, arguments, id, method,
                                          finish, &readOnlyResponse)) {
                return readOnlyResponse;
            }
            if (!IsWriteToolName(name)) {
                const json result = MakeError(id, -32601, "Unknown tool.");
                finish({"tool", name, method}, id,
                       {false, 200, "Unknown tool.", &result, {}});
                return MakeJsonResponse(200, "OK", result);
            }

            const auto storePreviewToken = [this, &name,
                        &snapshot](const json &canonicalArguments) {
                std::scoped_lock lock(m_previewMutex);
                if (m_previewRecords.size() >= 128)
                    m_previewRecords.clear();
                const uint64_t nextToken = m_nextPreviewToken + 1;
                m_nextPreviewToken = nextToken;
                std::ostringstream token;
                token << "preview_" << nextToken;
                m_previewRecords[token.str()] = PreviewRecord{
                    name, canonicalArguments, snapshot->sceneId, snapshot->sceneFilePath
                };
                return token.str();
            };

            const auto consumePreviewToken = [this, &name, &snapshot](
                const std::string &previewToken,
                const json &canonicalArguments,
                std::string *outError) {
                return ConsumePreviewTokenRecord(
                    &m_previewMutex, &m_previewRecords,
                    PreviewTokenValidationInput{
                        .previewToken = previewToken,
                        .toolName = name,
                        .canonicalArguments = canonicalArguments,
                        .sceneId = snapshot->sceneId,
                        .sceneFilePath = snapshot->sceneFilePath,
                        .outError = outError
                    });
            };

            const WriteToolCallInput writeInput{
                .snapshot = *snapshot,
                .name = name,
                .arguments = arguments,
                .id = id,
                .method = method
            };
            return HandleWriteToolCall(writeInput, m_context.commandInvoker,
                                       storePreviewToken, consumePreviewToken, finish);
        }

        const json result = MakeError(id, -32601, "Method not found.");
        finish({"method", method, method}, id,
               {false, 200, "Method not found.", &result, {}});
        return MakeJsonResponse(200, "OK", result);
    }
} // namespace Horo::Mcp
