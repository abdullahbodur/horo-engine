// MCP command handler methods for EditorLayer.
// Method definitions are in this file; declarations remain in EditorLayer.h.

// clang-format off
#include <glad/glad.h>
#include <GLFW/glfw3.h>
// clang-format on

#include <format>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/LogBuffer.h"
#include "core/Logger.h"
#include "editor/AssetIdentity.h"
#include "editor/AssetMetadata.h"
#include "editor/EditorLayer.h"
#include "editor/EditorLayerInternal.h"
#include "editor/EditorSceneGraph.h"
#include "editor/SceneProjectBridge.h"
#include "editor/SceneSerializer.h"
#include "math/MathUtils.h"
#include "math/Vec3.h"
#include "scene/SceneRuntimeConversion.h"

using json = nlohmann::json;

namespace Monolith::Editor {
    namespace {

        // ---- Issue key helpers (used only by BuildMcpBuildSnapshot) ----

        std::string BuildIssueKey(std::string_view severity, std::string_view path,
                                  std::string_view message) {
            return std::format("{}\n{}\n{}", severity, path, message);
        }

        const char *BuildIssueSeverityText(SceneProjectValidationIssue::Severity severity) {
            return severity == SceneProjectValidationIssue::Severity::Error ? "error" : "warning";
        }

        const char *BuildIssueSeverityText(RuntimeSceneBuildIssue::Severity severity) {
            return severity == RuntimeSceneBuildIssue::Severity::Error ? "error" : "warning";
        }

        // ---- MCP snapshot builders ----

        Mcp::McpBuildSnapshot BuildMcpBuildSnapshot(const SceneDocument &document) {
            Mcp::McpBuildSnapshot snapshot;
            snapshot.available = true;

            const SceneProjectModel model = BuildSceneProjectModel(document);
            snapshot.assetCount = model.scene.assets.size();
            snapshot.nodeCount = model.scene.nodes.size();

            const SceneProjectValidationResult validation = ValidateSceneProjectModel(model);
            std::unordered_set<std::string, StringHash, std::equal_to<> > validationIssueKeys;
            for (const SceneProjectValidationIssue &issue: validation.issues) {
                const std::string severity = BuildIssueSeverityText(issue.severity);
                validationIssueKeys.insert(BuildIssueKey(severity, issue.path, issue.message));
                if (issue.severity == SceneProjectValidationIssue::Severity::Error)
                    ++snapshot.sceneValidationErrors;
                else
                    ++snapshot.sceneValidationWarnings;
                snapshot.issues.emplace_back("validation", severity, issue.path, issue.message);
            }

            const RuntimeSceneBuildResult runtimeBuild = Monolith::BuildRuntimeSceneDefinition(model);
            snapshot.roomCount = runtimeBuild.definition.rooms.size();
            snapshot.lightCount = runtimeBuild.definition.lights.size();
            snapshot.hasSceneCamera = runtimeBuild.definition.sceneCamera.has_value();
            for (const RuntimeSceneRoom &room: runtimeBuild.definition.rooms) {
                snapshot.panelCount += room.panels.size();
                snapshot.propCount += room.props.size();
            }

            for (const RuntimeSceneBuildIssue &issue: runtimeBuild.issues) {
                const std::string severity = BuildIssueSeverityText(issue.severity);
                if (validationIssueKeys.contains(BuildIssueKey(severity, issue.path, issue.message)))
                    continue;
                if (issue.severity == RuntimeSceneBuildIssue::Severity::Error)
                    ++snapshot.runtimeBuildErrors;
                else
                    ++snapshot.runtimeBuildWarnings;
                snapshot.issues.emplace_back("runtime", severity, issue.path, issue.message);
            }

            const size_t totalErrors = snapshot.sceneValidationErrors + snapshot.runtimeBuildErrors;
            const size_t totalWarnings = snapshot.sceneValidationWarnings + snapshot.runtimeBuildWarnings;
            if (totalErrors > 0)       snapshot.status = "error";
            else if (totalWarnings > 0) snapshot.status = "warning";
            else                        snapshot.status = "ok";
            return snapshot;
        }

        const char *FieldWidgetToString(FieldDef::Widget widget) {
            using enum FieldDef::Widget;
            switch (widget) {
                case Float:   return "float";
                case Bool:    return "bool";
                case Enum:    return "enum";
                case Color3:  return "color3";
                case String:
                default:      return "string";
            }
        }

        Mcp::McpSchemaFieldSnapshot BuildMcpSchemaFieldSnapshot(const FieldDef &field) {
            Mcp::McpSchemaFieldSnapshot snapshot;
            snapshot.key = field.key;
            snapshot.label = field.label;
            snapshot.description = field.description;
            snapshot.widget = FieldWidgetToString(field.widget);
            snapshot.hasDefault = field.hasDefault;
            snapshot.required = field.required;
            snapshot.allowEmpty = field.allowEmpty;
            snapshot.allowCustomValue = field.allowCustomValue;
            snapshot.hasMin = field.hasMin;
            snapshot.hasMax = field.hasMax;
            snapshot.minVal = field.minVal;
            snapshot.maxVal = field.maxVal;
            snapshot.options = field.options;
            snapshot.defaultValue = field.defaultValue;
            return snapshot;
        }

        Mcp::McpSchemaEntrySnapshot BuildMcpSchemaEntrySnapshot(const TypeSchema &schema) {
            Mcp::McpSchemaEntrySnapshot snapshot;
            snapshot.kind = "object";
            snapshot.name = schema.name;
            snapshot.label = schema.label;
            snapshot.appliesTo = schema.appliesTo;
            snapshot.fields.reserve(schema.fields.size());
            for (const FieldDef &field: schema.fields)
                snapshot.fields.push_back(BuildMcpSchemaFieldSnapshot(field));
            return snapshot;
        }

        Mcp::McpSchemaEntrySnapshot BuildMcpSchemaEntrySnapshot(const ComponentSchema &schema) {
            Mcp::McpSchemaEntrySnapshot snapshot;
            snapshot.kind = "component";
            snapshot.name = schema.name;
            snapshot.label = schema.label;
            snapshot.appliesTo = schema.appliesTo;
            snapshot.fields.reserve(schema.fields.size());
            for (const FieldDef &field: schema.fields)
                snapshot.fields.push_back(BuildMcpSchemaFieldSnapshot(field));
            return snapshot;
        }

        Mcp::McpSchemaCatalogSnapshot BuildMcpSchemaCatalogSnapshot(const EditorSchema &schema) {
            Mcp::McpSchemaCatalogSnapshot snapshot;

            std::vector<std::string> typeNames;
            typeNames.reserve(schema.TypeSchemas().size());
            for (const auto &[typeName, typeEntry]: schema.TypeSchemas())
                typeNames.push_back(typeName);
            std::ranges::sort(typeNames);
            for (const std::string &typeName: typeNames)
                snapshot.objectTypes.push_back(BuildMcpSchemaEntrySnapshot(schema.TypeSchemas().at(typeName)));

            std::vector<std::string> componentTypes;
            componentTypes.reserve(schema.ComponentSchemas().size());
            for (const auto &[compTypeName, compEntry]: schema.ComponentSchemas())
                componentTypes.push_back(compTypeName);
            std::ranges::sort(componentTypes);
            for (const std::string &componentType: componentTypes)
                snapshot.components.push_back(BuildMcpSchemaEntrySnapshot(schema.ComponentSchemas().at(componentType)));

            return snapshot;
        }

        // ---- MCP argument parsers ----

        bool McpParseVec3(const json &value, Vec3 *out) {
            if (!out || !value.is_array() || value.size() != 3)
                return false;
            if (!value[0].is_number() || !value[1].is_number() || !value[2].is_number())
                return false;
            out->x = value[0].get<float>();
            out->y = value[1].get<float>();
            out->z = value[2].get<float>();
            return true;
        }

        std::unordered_map<std::string, std::string, StringHash, std::equal_to<> >
        McpParseProps(const json &value) {
            std::unordered_map<std::string, std::string, StringHash, std::equal_to<> > props;
            if (!value.is_object())
                return props;
            for (auto it = value.begin(); it != value.end(); ++it) {
                props[it.key()] = it.value().is_string()
                                      ? it.value().get<std::string>()
                                      : it.value().dump();
            }
            return props;
        }

        bool McpParseComponents(const json &value, std::vector<ComponentDesc> *out) {
            if (!out || !value.is_array())
                return false;
            std::vector<ComponentDesc> components;
            for (const json &item: value) {
                if (!item.is_object() || !item.contains("type") || !item["type"].is_string())
                    return false;
                ComponentDesc component;
                component.type = item["type"].get<std::string>();
                component.props = McpParseProps(item.value("props", json::object()));
                components.push_back(std::move(component));
            }
            *out = std::move(components);
            return true;
        }

    } // namespace

    void EditorLayer::ProcessMcpCommands() {
        m_mcpController.DrainCommands(
            [this](std::string_view toolName, const nlohmann::json &arguments) {
                return ExecuteMcpCommand(toolName, arguments);
            });
    }

    void EditorLayer::PublishMcpSnapshot() {
        Mcp::McpEditorSnapshot snapshot;
        snapshot.editorActive = m_active;
        snapshot.playMode = m_playMode;
        snapshot.dirty = m_document.dirty;
        snapshot.reloadPending = m_wantsReload;
        snapshot.sceneId = m_document.sceneId;
        snapshot.sceneName = m_document.sceneName;
        snapshot.sceneFilePath = m_document.filePath;
        snapshot.selectedAssetId = m_selectedAssetId;

        for (int idx: m_selectedIndices) {
            if (idx >= 0 && idx < static_cast<int>(m_document.objects.size()))
                snapshot.selectedObjectIds.push_back(
                    ObjectAt(m_document, idx).id);
        }

        snapshot.objects.reserve(m_document.objects.size());
        for (const SceneObject &object: m_document.objects) {
            Mcp::McpObjectSnapshot entry;
            entry.id = object.id;
            entry.type = SceneObjectTypeToString(object.type);
            entry.position = object.position;
            entry.scale = object.scale;
            entry.yaw = object.yaw;
            entry.pitch = object.pitch;
            entry.roll = object.roll;
            entry.assetId = object.assetId;
            entry.props = object.props;
            for (const ComponentDesc &component: object.components) {
                Mcp::McpComponentSnapshot componentEntry;
                componentEntry.type = component.type;
                componentEntry.props = component.props;
                entry.components.push_back(std::move(componentEntry));
            }
            snapshot.objects.push_back(std::move(entry));
        }

        std::vector<std::string> assetIds;
        assetIds.reserve(m_document.assets.size());
        for (const auto &[entryKey, entryVal]: m_document.assets)
            assetIds.emplace_back(entryKey);
        std::ranges::sort(assetIds);
        for (const std::string &assetId: assetIds) {
            const AssetDef &asset = m_document.assets.at(assetId);
            snapshot.assets.emplace_back(assetId, asset.mesh, asset.renderScale,
                                         asset.albedoMap);
        }

        std::vector<LogLine> lines;
        LogBuffer::Instance().CopyLinesTo(&lines);
        const size_t start = lines.size() > 50 ? lines.size() - 50 : 0;
        for (size_t i = start; i < lines.size(); ++i) {
            std::string timeBuf(32, '\0');
            FormatLogTime(lines[i], timeBuf.data(), timeBuf.size());
            const char *levelStr = "ERR";
            if (lines[i].level == LogLevel::Info)
                levelStr = "INFO";
            else if (lines[i].level == LogLevel::Warn)
                levelStr = "WARN";
            snapshot.consoleEntries.emplace_back(timeBuf.c_str(), levelStr,
                                                 lines[i].message);
        }

        snapshot.build = BuildMcpBuildSnapshot(m_document);
        snapshot.schema = BuildMcpSchemaCatalogSnapshot(m_schema);
        m_mcpController.PublishSnapshot(snapshot);
    }

    int EditorLayer::McpFindObjectIndex(std::string_view id) const {
        return FindObjectIndexById(m_document, id);
    }

    nlohmann::json EditorLayer::McpSummarizeObject(const SceneObject &object) const {
        json out = json::object();
        out["id"] = object.id;
        out["type"] = SceneObjectTypeToString(object.type);
        out["position"] =
                json::array({object.position.x, object.position.y, object.position.z});
        out["scale"] =
                json::array({object.scale.x, object.scale.y, object.scale.z});
        out["yaw"] = object.yaw;
        out["pitch"] = object.pitch;
        out["roll"] = object.roll;
        out["assetId"] = object.assetId;
        if (object.prefabInstance.has_value()) {
            out["prefab"] = json{
                {"id", object.prefabInstance->prefabId},
                {"path", object.prefabInstance->sourcePath}
            };
        }
        if (const auto parentIt = object.props.find("parentId");
            parentIt != object.props.end() && !parentIt->second.empty())
            out["parentId"] = parentIt->second;
        return out;
    }

    nlohmann::json EditorLayer::McpSummarizeAsset(const std::string &assetId,
                                                  const AssetDef &asset) const {
        int referenceCount = 0;
        for (const SceneObject &object: m_document.objects) {
            if (object.assetId == assetId)
                ++referenceCount;
        }
        return json{
            {"id", assetId},
            {"guid", asset.guid},
            {"displayName", asset.displayName},
            {"mesh", asset.mesh},
            {"renderScale", asset.renderScale},
            {"albedoMap", asset.albedoMap},
            {"objectReferenceCount", referenceCount}
        };
    }

    Mcp::McpCommandResult
    EditorLayer::ExecuteMcpCommand(std::string_view toolName,
                                   const nlohmann::json &arguments) {

        if (toolName == "editor.select")
            return McpHandleSelect(arguments);
        if (toolName == "editor.clear_selection")
            return McpHandleClearSelection(arguments);
        if (toolName == "editor.undo")
            return McpHandleUndo(arguments);
        if (toolName == "editor.redo")
            return McpHandleRedo(arguments);
        if (toolName == "editor.create_object")
            return McpHandleCreateObject(arguments);
        if (toolName == "editor.create_object_from_asset")
            return McpHandleCreateObjectFromAsset(arguments);
        if (toolName == "editor.create_prefab")
            return McpHandleCreatePrefab(arguments);
        if (toolName == "editor.update_object" || toolName == "editor.transform")
            return McpHandleUpdateObject(arguments, toolName);
        if (toolName == "editor.rename_object")
            return McpHandleRenameObject(arguments);
        if (toolName == "editor.reparent_object")
            return McpHandleReparentObject(arguments);
        if (toolName == "editor.duplicate")
            return McpHandleDuplicate(arguments);
        if (toolName == "editor.delete")
            return McpHandleDelete(arguments);
        if (toolName == "editor.select_asset")
            return McpHandleSelectAsset(arguments);
        if (toolName == "editor.update_asset")
            return McpHandleUpdateAsset(arguments);
        if (toolName == "editor.delete_asset")
            return McpHandleDeleteAsset(arguments);
        if (toolName == "editor.new_scene")
            return McpHandleNewScene(arguments);
        if (toolName == "editor.save_scene")
            return McpHandleSaveScene(arguments);
        if (toolName == "editor.reload_scene")
            return McpHandleReloadScene(arguments);

        return Mcp::McpCommandResult{
            false, json::object(),
            "Unsupported MCP command."
        };
    }

    Mcp::McpCommandResult EditorLayer::McpHandleSelect(const nlohmann::json &arguments) {

        std::vector<std::string> ids;
        if (arguments.contains("id") && arguments["id"].is_string())
            ids.push_back(arguments["id"].get<std::string>());
        if (arguments.contains("ids") && arguments["ids"].is_array()) {
            for (const json &item: arguments["ids"]) {
                if (item.is_string())
                    ids.push_back(item.get<std::string>());
            }
        }
        m_selectedIndices.clear();
        for (const std::string &id: ids) {
            const int idx = McpFindObjectIndex(id);
            if (idx >= 0)
                m_selectedIndices.push_back(idx);
        }
        m_selectedAssetId.clear();
        return Mcp::McpCommandResult{
            true, json{{"selectedObjectIds", GetSelectedObjectIds()}},
            std::string()
        };
    }

    Mcp::McpCommandResult EditorLayer::McpHandleClearSelection(const nlohmann::json &) {

        m_selectedIndices.clear();
        m_selectedAssetId.clear();
        return Mcp::McpCommandResult{true, json{{"cleared", true}}, std::string()};
    }

    Mcp::McpCommandResult EditorLayer::McpHandleUndo(const nlohmann::json &) {

        const bool undone = UndoHistory();
        return Mcp::McpCommandResult{
            true,
            json{
                {"undone", undone},
                {"dirty", m_document.dirty},
                {"selectedObjectIds", GetSelectedObjectIds()},
                {"selectedAssetId", m_selectedAssetId}
            },
            std::string()
        };
    }

    Mcp::McpCommandResult EditorLayer::McpHandleRedo(const nlohmann::json &) {

        const bool redone = RedoHistory();
        return Mcp::McpCommandResult{
            true,
            json{
                {"redone", redone},
                {"dirty", m_document.dirty},
                {"selectedObjectIds", GetSelectedObjectIds()},
                {"selectedAssetId", m_selectedAssetId}
            },
            std::string()
        };
    }

    Mcp::McpCommandResult EditorLayer::McpHandleCreateObject(const nlohmann::json &arguments) {
        using enum SceneObjectType;

        const EditorHistorySnapshot before = CaptureHistorySnapshot();
        SceneObjectType type = Panel;
        if (!ParseSceneObjectType(arguments.value("type", ""), &type))
            return Mcp::McpCommandResult{
                false, json::object(),
                "Invalid object type."
            };

        SceneObject object;
        object.type = type;
        ApplySchemaDefaults(object);
        object.id = arguments.value("id", "");
        if (object.id.empty())
            object.id = type == Camera
                            ? GenerateCameraId(m_document)
                            : GenerateId(m_document);
        if (IsReservedObjectId(m_document, object.id))
            return Mcp::McpCommandResult{
                false, json::object(),
                "Object id already exists."
            };

        if (arguments.contains("position") &&
            !McpParseVec3(arguments["position"], &object.position))
            return Mcp::McpCommandResult{
                false, json::object(),
                "position must be [x,y,z]."
            };
        if (arguments.contains("scale") &&
            !McpParseVec3(arguments["scale"], &object.scale))
            return Mcp::McpCommandResult{
                false, json::object(),
                "scale must be [x,y,z]."
            };
        object.yaw = arguments.value("yaw", object.yaw);
        object.pitch = arguments.value("pitch", object.pitch);
        object.roll = arguments.value("roll", object.roll);
        object.assetId = arguments.value("assetId", "");
        if (arguments.contains("props"))
            object.props = McpParseProps(arguments["props"]);
        if (arguments.contains("components") &&
            !McpParseComponents(arguments["components"], &object.components))
            return Mcp::McpCommandResult{
                false, json::object(),
                "components must be an array of objects."
            };
        if (const std::string parentId = arguments.value("parentId", ""); !parentId.empty()) {
            if (McpFindObjectIndex(parentId) < 0)
                return Mcp::McpCommandResult{
                    false, json::object(),
                    "Parent object not found."
                };
            object.props["parentId"] = parentId;
        }

        m_document.objects.push_back(std::move(object));
        m_selectedIndices = {static_cast<int>(m_document.objects.size()) - 1};
        MarkDirtyAndReload();
        CommitHistoryChange(before);
        return Mcp::McpCommandResult{
            true, json{{"created", McpSummarizeObject(m_document.objects.back())}},
            std::string()
        };
    }

    Mcp::McpCommandResult EditorLayer::McpHandleCreateObjectFromAsset(const nlohmann::json &arguments) {

        const std::string assetId = arguments.value("assetId", "");
        Vec3 position = Vec3::Zero();
        const Vec3 *positionPtr = nullptr;
        if (arguments.contains("position") &&
            !McpParseVec3(arguments["position"], &position))
            return Mcp::McpCommandResult{
                false, json::object(),
                "position must be [x,y,z]."
            };
        if (arguments.contains("position"))
            positionPtr = &position;

        const std::string requestedId = arguments.value("id", "");
        const std::string parentId = arguments.value("parentId", "");
        SceneObject createdObject;
        if (std::string createError; !CreateObjectFromAsset(assetId, parentId, positionPtr,
                                                            requestedId.empty() ? nullptr : &requestedId,
                                                            &createdObject, &createError)) {
            return Mcp::McpCommandResult{false, json::object(), createError};
        }
        SceneObject &object = m_document.objects.back();
        object.yaw = arguments.value("yaw", object.yaw);
        object.pitch = arguments.value("pitch", object.pitch);
        object.roll = arguments.value("roll", object.roll);
        if (m_transformCb)
            m_transformCb(object);
        TriggerReload();

        return Mcp::McpCommandResult{
            true, json{{"created", McpSummarizeObject(object)}}, std::string()
        };
    }

    Mcp::McpCommandResult EditorLayer::McpHandleCreatePrefab(const nlohmann::json &arguments) {

        if (const std::string id = arguments.value("id", ""); !id.empty())
            SetSelectedObjectIds({id});

        std::string prefabPath;
        if (std::string prefabError;
            !CreatePrefabFromSelection(&prefabError, &prefabPath))
            return Mcp::McpCommandResult{false, json::object(), prefabError};

        const int idx = PrimaryIdx();
        const SceneObject *object =
                (idx >= 0 && idx < static_cast<int>(m_document.objects.size()))
                    ? &ObjectAt(m_document, idx)
                    : nullptr;
        return Mcp::McpCommandResult{
            true,
            json{
                {"prefabPath", prefabPath},
                {
                    "prefabId", object && object->prefabInstance.has_value()
                                    ? object->prefabInstance->prefabId
                                    : std::string()
                },
                {"instance", object ? McpSummarizeObject(*object) : json::object()}
            },
            std::string()
        };
    }

    Mcp::McpCommandResult
    EditorLayer::McpHandleUpdateObject(const nlohmann::json &arguments, std::string_view toolName) {

        const EditorHistorySnapshot before = CaptureHistorySnapshot();
        const std::string id = arguments.value("id", "");
        const int idx = McpFindObjectIndex(id);
        if (idx < 0)
            return Mcp::McpCommandResult{false, json::object(), "Object not found."};

        SceneObject &object = ObjectAt(m_document, idx);
        if (arguments.contains("position") &&
            !McpParseVec3(arguments["position"], &object.position))
            return Mcp::McpCommandResult{
                false, json::object(),
                "position must be [x,y,z]."
            };
        if (arguments.contains("scale") &&
            !McpParseVec3(arguments["scale"], &object.scale))
            return Mcp::McpCommandResult{
                false, json::object(),
                "scale must be [x,y,z]."
            };
        if (arguments.contains("yaw"))
            object.yaw = arguments["yaw"].get<float>();
        if (arguments.contains("pitch"))
            object.pitch = arguments["pitch"].get<float>();
        if (arguments.contains("roll"))
            object.roll = arguments["roll"].get<float>();
        if (toolName == "editor.update_object") {
            if (arguments.contains("assetId"))
                object.assetId = arguments["assetId"].is_null()
                                     ? std::string()
                                     : arguments["assetId"].get<std::string>();
            if (arguments.contains("props")) {
                const auto props = McpParseProps(arguments["props"]);
                for (const auto &[entryKey, entryVal]: props)
                    object.props[entryKey] = entryVal;
            }
            if (arguments.contains("components") &&
                !McpParseComponents(arguments["components"], &object.components)) {
                return Mcp::McpCommandResult{
                    false, json::object(),
                    "components must be an array of objects."
                };
            }
        }
        m_document.dirty = true;
        if (m_transformCb)
            m_transformCb(object);
        TriggerReload();
        CommitHistoryChange(before);
        return Mcp::McpCommandResult{
            true, json{{"updated", McpSummarizeObject(object)}}, std::string()
        };
    }

    Mcp::McpCommandResult EditorLayer::McpHandleRenameObject(const nlohmann::json &arguments) {

        const EditorHistorySnapshot before = CaptureHistorySnapshot();
        const std::string id = arguments.value("id", "");
        const std::string newId = arguments.value("newId", "");
        const int idx = McpFindObjectIndex(id);
        if (idx < 0)
            return Mcp::McpCommandResult{false, json::object(), "Object not found."};
        if (newId.empty())
            return Mcp::McpCommandResult{false, json::object(), "newId is required."};
        if (id != newId && IsReservedObjectId(m_document, newId, &id))
            return Mcp::McpCommandResult{
                false, json::object(),
                "Object id already exists."
            };

        SceneObject &object = ObjectAt(m_document, idx);
        const std::string oldId = object.id;
        object.id = newId;
        RewriteObjectIdReferences(&m_document, oldId, newId);
        MarkDirtyAndReload();
        CommitHistoryChange(before);
        return Mcp::McpCommandResult{
            true, json{{"renamed", true}, {"oldId", oldId}, {"newId", newId}},
            std::string()
        };
    }

    Mcp::McpCommandResult EditorLayer::McpHandleReparentObject(const nlohmann::json &arguments) {

        const EditorHistorySnapshot before = CaptureHistorySnapshot();
        const std::string id = arguments.value("id", "");
        const int idx = McpFindObjectIndex(id);
        if (idx < 0)
            return Mcp::McpCommandResult{false, json::object(), "Object not found."};

        const std::string parentId = arguments.value("parentId", "");
        if (!parentId.empty()) {
            const int parentIdx = McpFindObjectIndex(parentId);
            if (parentIdx < 0)
                return Mcp::McpCommandResult{
                    false, json::object(),
                    "Parent object not found."
                };
            if (parentIdx == idx)
                return Mcp::McpCommandResult{
                    false, json::object(),
                    "Object cannot parent itself."
                };
            if (IsDescendantOf(m_document, parentIdx, idx))
                return Mcp::McpCommandResult{
                    false, json::object(),
                    "Parent would create a cycle."
                };
            ObjectAt(m_document, idx).props["parentId"] = parentId;
        } else {
            ObjectAt(m_document, idx).props.erase("parentId");
        }
        MarkDirtyAndReload();
        CommitHistoryChange(before);
        return Mcp::McpCommandResult{
            true, json{{"id", id}, {"parentId", parentId}},
            std::string()
        };
    }

    Mcp::McpCommandResult EditorLayer::McpHandleDuplicate(const nlohmann::json &arguments) {

        const EditorHistorySnapshot before = CaptureHistorySnapshot();
        std::vector<int> sourceIndices;
        if (arguments.contains("id") && arguments["id"].is_string()) {
            const int idx = McpFindObjectIndex(arguments["id"].get<std::string>());
            if (idx >= 0)
                sourceIndices.push_back(idx);
        }
        if (arguments.contains("ids") && arguments["ids"].is_array()) {
            for (const json &item: arguments["ids"]) {
                if (!item.is_string())
                    continue;
                const int idx = McpFindObjectIndex(item.get<std::string>());
                if (idx >= 0)
                    sourceIndices.push_back(idx);
            }
        }
        if (sourceIndices.empty())
            return Mcp::McpCommandResult{false, json::object(), "Object not found."};

        std::ranges::sort(sourceIndices);
        const auto sourceIndicesUniqueTail = std::ranges::unique(sourceIndices);
        sourceIndices.erase(sourceIndicesUniqueTail.begin(),
                            sourceIndicesUniqueTail.end());

        const int count =
                (sourceIndices.size() == 1)
                    ? std::max(1, std::min(8, arguments.value("count", 1)))
                    : 1;
        json created = json::array();
        std::vector<int> duplicatedIndices;
        for (int idx: sourceIndices) {
            for (int i = 0; i < count; ++i) {
                SceneObject clone = DuplicateObject(
                    m_document, ObjectAt(m_document, idx));
                clone.position.x += static_cast<float>(i + 1);
                clone.position.z += static_cast<float>(i + 1);
                m_document.objects.push_back(std::move(clone));
                duplicatedIndices.push_back(
                    static_cast<int>(m_document.objects.size()) - 1);
                created.push_back(McpSummarizeObject(m_document.objects.back()));
            }
        }
        m_selectedIndices = std::move(duplicatedIndices);
        MarkDirtyAndReload();
        CommitHistoryChange(before);
        return Mcp::McpCommandResult{
            true, json{{"duplicates", std::move(created)}},
            std::string()
        };
    }

    Mcp::McpCommandResult EditorLayer::McpHandleDelete(const nlohmann::json &arguments) {

        const EditorHistorySnapshot before = CaptureHistorySnapshot();
        std::vector<int> indices;
        if (arguments.contains("id") && arguments["id"].is_string()) {
            const int idx = McpFindObjectIndex(arguments["id"].get<std::string>());
            if (idx >= 0)
                indices.push_back(idx);
        }
        if (arguments.contains("ids") && arguments["ids"].is_array()) {
            for (const json &item: arguments["ids"]) {
                if (!item.is_string())
                    continue;
                const int idx = McpFindObjectIndex(item.get<std::string>());
                if (idx >= 0)
                    indices.push_back(idx);
            }
        }
        if (indices.empty())
            return Mcp::McpCommandResult{
                false, json::object(),
                "No matching objects to delete."
            };

        std::ranges::sort(indices);
        const auto indicesUniqueTail = std::ranges::unique(indices);
        indices.erase(indicesUniqueTail.begin(), indicesUniqueTail.end());
        std::sort(indices.rbegin(), indices.rend());
        for (int idx: indices)
            m_document.objects.erase(m_document.objects.begin() + idx);
        m_selectedIndices.clear();
        MarkDirtyAndReload();
        CommitHistoryChange(before);
        return Mcp::McpCommandResult{
            true, json{{"deletedCount", static_cast<int>(indices.size())}},
            std::string()
        };
    }

    Mcp::McpCommandResult EditorLayer::McpHandleSelectAsset(const nlohmann::json &arguments) {

        const std::string assetId = arguments.value("id", "");
        if (!assetId.empty() &&
            !m_document.assets.contains(assetId))
            return Mcp::McpCommandResult{false, json::object(), "Asset not found."};
        m_selectedAssetId = assetId;
        return Mcp::McpCommandResult{
            true, json{{"selectedAssetId", m_selectedAssetId}}, std::string()
        };
    }

    Mcp::McpCommandResult EditorLayer::McpHandleUpdateAsset(const nlohmann::json &arguments) {

        const EditorHistorySnapshot before = CaptureHistorySnapshot();
        const std::string assetId = arguments.value("id", "");
        auto it = m_document.assets.find(assetId);
        if (it == m_document.assets.end())
            return Mcp::McpCommandResult{false, json::object(), "Asset not found."};
        if (arguments.contains("mesh"))
            it->second.mesh = arguments["mesh"].is_null()
                                  ? std::string()
                                  : arguments["mesh"].get<std::string>();
        if (arguments.contains("renderScale"))
            it->second.renderScale =
                    arguments["renderScale"].is_null()
                        ? std::string()
                        : arguments["renderScale"].get<std::string>();
        if (arguments.contains("albedoMap"))
            it->second.albedoMap = arguments["albedoMap"].is_null()
                                       ? std::string()
                                       : arguments["albedoMap"].get<std::string>();
        if (arguments.contains("displayName"))
            it->second.displayName =
                    arguments["displayName"].is_null()
                        ? std::string()
                        : arguments["displayName"].get<std::string>();
        EnsureAssetIdentity(assetId, &it->second);
        MarkDirtyAndReload();
        CommitHistoryChange(before);
        return Mcp::McpCommandResult{
            true, json{{"asset", McpSummarizeAsset(assetId, it->second)}},
            std::string()
        };
    }

    Mcp::McpCommandResult EditorLayer::McpHandleDeleteAsset(const nlohmann::json &arguments) {

        const std::string assetId = arguments.value("id", "");
        const AssetDeleteResult deleteResult = DeleteAssetDefinition(assetId);
        if (!deleteResult.ok)
            return Mcp::McpCommandResult{false, json::object(), deleteResult.error};

        json payload{
            {"deletedAssetId", assetId},
            {"clearedObjectReferences", deleteResult.clearedReferences},
            {"deletedManagedFiles", deleteResult.deletedManagedFiles}
        };
        if (!deleteResult.deletedAssetDirectory.empty())
            payload["deletedAssetDirectory"] = deleteResult.deletedAssetDirectory;
        return Mcp::McpCommandResult{true, std::move(payload), std::string()};
    }

    Mcp::McpCommandResult EditorLayer::McpHandleNewScene(const nlohmann::json &arguments) {

        const EditorHistorySnapshot before = CaptureHistorySnapshot();
        AddNewScene();
        if (arguments.contains("sceneId") && arguments["sceneId"].is_string() &&
            !arguments["sceneId"].get<std::string>().empty()) {
            m_document.sceneId = arguments["sceneId"].get<std::string>();
        }
        if (arguments.contains("sceneName") && arguments["sceneName"].is_string() &&
            !arguments["sceneName"].get<std::string>().empty()) {
            m_document.sceneName = arguments["sceneName"].get<std::string>();
        }
        m_lastSavedDocument = m_document;
        TriggerReload();
        CommitHistoryChange(before);
        return Mcp::McpCommandResult{
            true,
            json{
                {"sceneId", m_document.sceneId},
                {"sceneName", m_document.sceneName},
                {"dirty", m_document.dirty}
            },
            std::string()
        };
    }

    Mcp::McpCommandResult EditorLayer::McpHandleSaveScene(const nlohmann::json &) {

        if (std::string saveError; !SaveDocument(&saveError))
            return Mcp::McpCommandResult{false, json::object(), saveError};
        return Mcp::McpCommandResult{
            true, json{{"saved", true}, {"filePath", m_document.filePath}},
            std::string()
        };
    }

    Mcp::McpCommandResult EditorLayer::McpHandleReloadScene(const nlohmann::json &) {

        const EditorHistorySnapshot before = CaptureHistorySnapshot();
        const std::vector<std::string> previousSelectionIds =
                GetSelectedObjectIds();
        const std::string previousAssetId = m_selectedAssetId;
        if (std::string reloadError; !ReloadDocumentFromDisk(&reloadError, &previousSelectionIds,
                                                             &previousAssetId))
            return Mcp::McpCommandResult{false, json::object(), reloadError};
        CommitHistoryChange(before);
        return Mcp::McpCommandResult{
            true,
            json{
                {"reloadPending", true},
                {"filePath", m_document.filePath},
                {"dirty", m_document.dirty}
            },
            std::string()
        };
    }

} // namespace Monolith::Editor
