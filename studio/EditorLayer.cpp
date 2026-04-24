#include "EditorLayer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <ctime>
#include <fstream>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "core/LogBuffer.h"
#include "core/Logger.h"
#include "core/ProjectPath.h"
#include "AssetIdentity.h"
#include "AssetMetadata.h"
#include "SceneProjectBridge.h"
#include "SceneSerializer.h"
#include "math/MathUtils.h"
#include "math/Quaternion.h"
#include "math/Transform.h"
#include "scene/Entity.h"
#include "scene/Registry.h"
#include "scene/SceneRuntimeConversion.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/PlayerTagComponent.h"
#include "scene/components/TransformComponent.h"

namespace Monolith::Editor
{

    namespace
    {
        constexpr size_t kMaxEditorHistorySnapshots = 128;

        // ---------------------------------------------------------------------------
        // Issue helpers (used by BuildMcpBuildSnapshot)
        // ---------------------------------------------------------------------------

        std::string BuildIssueKey(const std::string &severity,
                                  const std::string &path,
                                  const std::string &message)
        {
            return severity + "\n" + path + "\n" + message;
        }

        std::string BuildIssueSeverityText(SceneProjectValidationIssue::Severity severity)
        {
            return severity == SceneProjectValidationIssue::Severity::Error ? "error" : "warning";
        }

        std::string BuildIssueSeverityText(RuntimeSceneBuildIssue::Severity severity)
        {
            return severity == RuntimeSceneBuildIssue::Severity::Error ? "error" : "warning";
        }

        Mcp::McpBuildSnapshot BuildMcpBuildSnapshot(const SceneDocument &document)
        {
            Mcp::McpBuildSnapshot snapshot;
            snapshot.available = true;

            const SceneProjectModel model = BuildSceneProjectModel(document);
            snapshot.assetCount = model.scene.assets.size();
            snapshot.nodeCount = model.scene.nodes.size();

            const SceneProjectValidationResult validation = ValidateSceneProjectModel(model);
            std::unordered_set<std::string> validationIssueKeys;
            for (const SceneProjectValidationIssue &issue : validation.issues)
            {
                const std::string severity = BuildIssueSeverityText(issue.severity);
                validationIssueKeys.insert(BuildIssueKey(severity, issue.path, issue.message));
                if (issue.severity == SceneProjectValidationIssue::Severity::Error)
                    ++snapshot.sceneValidationErrors;
                else
                    ++snapshot.sceneValidationWarnings;
                snapshot.issues.push_back(Mcp::McpBuildIssueSnapshot{"validation", severity, issue.path, issue.message});
            }

            const RuntimeSceneBuildResult runtimeBuild = Monolith::BuildRuntimeSceneDefinition(model);
            snapshot.roomCount = runtimeBuild.definition.rooms.size();
            snapshot.lightCount = runtimeBuild.definition.lights.size();
            snapshot.hasSceneCamera = runtimeBuild.definition.sceneCamera.has_value();
            for (const RuntimeSceneRoom &room : runtimeBuild.definition.rooms)
            {
                snapshot.panelCount += room.panels.size();
                snapshot.propCount += room.props.size();
            }

            for (const RuntimeSceneBuildIssue &issue : runtimeBuild.issues)
            {
                const std::string severity = BuildIssueSeverityText(issue.severity);
                if (validationIssueKeys.count(BuildIssueKey(severity, issue.path, issue.message)) != 0)
                    continue;
                if (issue.severity == RuntimeSceneBuildIssue::Severity::Error)
                    ++snapshot.runtimeBuildErrors;
                else
                    ++snapshot.runtimeBuildWarnings;
                snapshot.issues.push_back(Mcp::McpBuildIssueSnapshot{"runtime", severity, issue.path, issue.message});
            }

            const size_t totalErrors = snapshot.sceneValidationErrors + snapshot.runtimeBuildErrors;
            const size_t totalWarnings = snapshot.sceneValidationWarnings + snapshot.runtimeBuildWarnings;
            snapshot.status = totalErrors > 0 ? "error" : totalWarnings > 0 ? "warning"
                                                                             : "ok";
            return snapshot;
        }

        // ---------------------------------------------------------------------------
        // Schema snapshot helpers (used by PublishMcpSnapshot)
        // ---------------------------------------------------------------------------

        const char *FieldWidgetToString(FieldDef::Widget widget)
        {
            switch (widget)
            {
            case FieldDef::Widget::Float:
                return "float";
            case FieldDef::Widget::Bool:
                return "bool";
            case FieldDef::Widget::Enum:
                return "enum";
            case FieldDef::Widget::Color3:
                return "color3";
            case FieldDef::Widget::String:
            default:
                return "string";
            }
        }

        Mcp::McpSchemaFieldSnapshot BuildMcpSchemaFieldSnapshot(const FieldDef &field)
        {
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

        Mcp::McpSchemaEntrySnapshot BuildMcpSchemaEntrySnapshot(const TypeSchema &schema)
        {
            Mcp::McpSchemaEntrySnapshot snapshot;
            snapshot.kind = "object";
            snapshot.name = schema.name;
            snapshot.label = schema.label;
            snapshot.appliesTo = schema.appliesTo;
            snapshot.fields.reserve(schema.fields.size());
            for (const FieldDef &field : schema.fields)
                snapshot.fields.push_back(BuildMcpSchemaFieldSnapshot(field));
            return snapshot;
        }

        Mcp::McpSchemaEntrySnapshot BuildMcpSchemaEntrySnapshot(const ComponentSchema &schema)
        {
            Mcp::McpSchemaEntrySnapshot snapshot;
            snapshot.kind = "component";
            snapshot.name = schema.name;
            snapshot.label = schema.label;
            snapshot.appliesTo = schema.appliesTo;
            snapshot.fields.reserve(schema.fields.size());
            for (const FieldDef &field : schema.fields)
                snapshot.fields.push_back(BuildMcpSchemaFieldSnapshot(field));
            return snapshot;
        }

        Mcp::McpSchemaCatalogSnapshot BuildMcpSchemaCatalogSnapshot(const EditorSchema &schema)
        {
            Mcp::McpSchemaCatalogSnapshot snapshot;

            std::vector<std::string> typeNames;
            typeNames.reserve(schema.TypeSchemas().size());
            for (const auto &entry : schema.TypeSchemas())
                typeNames.push_back(entry.first);
            std::ranges::sort(typeNames);
            for (const std::string &typeName : typeNames)
                snapshot.objectTypes.push_back(BuildMcpSchemaEntrySnapshot(schema.TypeSchemas().at(typeName)));

            std::vector<std::string> componentTypes;
            componentTypes.reserve(schema.ComponentSchemas().size());
            for (const auto &entry : schema.ComponentSchemas())
                componentTypes.push_back(entry.first);
            std::ranges::sort(componentTypes);
            for (const std::string &componentType : componentTypes)
                snapshot.components.push_back(BuildMcpSchemaEntrySnapshot(schema.ComponentSchemas().at(componentType)));

            return snapshot;
        }

        // ---------------------------------------------------------------------------
        // Scene document equality helpers (used by history)
        // ---------------------------------------------------------------------------

        bool AssetDefsEqual(const AssetDef &lhs, const AssetDef &rhs)
        {
            return lhs.mesh == rhs.mesh &&
                   lhs.renderScale == rhs.renderScale &&
                   lhs.albedoMap == rhs.albedoMap &&
                   lhs.guid == rhs.guid &&
                   lhs.displayName == rhs.displayName;
        }

        bool ComponentDescsEqual(const ComponentDesc &lhs, const ComponentDesc &rhs)
        {
            return lhs.type == rhs.type && lhs.props == rhs.props;
        }

        bool SceneObjectsEqual(const SceneObject &lhs, const SceneObject &rhs)
        {
            const bool prefabEqual =
                (!lhs.prefabInstance.has_value() && !rhs.prefabInstance.has_value()) ||
                (lhs.prefabInstance.has_value() && rhs.prefabInstance.has_value() &&
                 lhs.prefabInstance->prefabId == rhs.prefabInstance->prefabId &&
                 lhs.prefabInstance->sourcePath == rhs.prefabInstance->sourcePath);
            if (lhs.id != rhs.id ||
                lhs.type != rhs.type ||
                lhs.position.x != rhs.position.x ||
                lhs.position.y != rhs.position.y ||
                lhs.position.z != rhs.position.z ||
                lhs.scale.x != rhs.scale.x ||
                lhs.scale.y != rhs.scale.y ||
                lhs.scale.z != rhs.scale.z ||
                lhs.yaw != rhs.yaw ||
                lhs.pitch != rhs.pitch ||
                lhs.roll != rhs.roll ||
                lhs.assetId != rhs.assetId ||
                !prefabEqual ||
                lhs.props != rhs.props ||
                lhs.components.size() != rhs.components.size())
            {
                return false;
            }

            for (size_t i = 0; i < lhs.components.size(); ++i)
            {
                if (!ComponentDescsEqual(lhs.components[i], rhs.components[i]))
                    return false;
            }
            return true;
        }

        bool SceneDocumentsEqual(const SceneDocument &lhs, const SceneDocument &rhs)
        {
            if (lhs.version != rhs.version ||
                lhs.sceneId != rhs.sceneId ||
                lhs.sceneName != rhs.sceneName ||
                lhs.filePath != rhs.filePath ||
                lhs.settings != rhs.settings ||
                lhs.assets.size() != rhs.assets.size() ||
                lhs.objects.size() != rhs.objects.size())
            {
                return false;
            }

            for (const auto &[assetId, asset] : lhs.assets)
            {
                const auto it = rhs.assets.find(assetId);
                if (it == rhs.assets.end() || !AssetDefsEqual(asset, it->second))
                    return false;
            }

            for (size_t i = 0; i < lhs.objects.size(); ++i)
            {
                if (!SceneObjectsEqual(lhs.objects[i], rhs.objects[i]))
                    return false;
            }

            return true;
        }

        // ---------------------------------------------------------------------------
        // Object-tree helpers
        // ---------------------------------------------------------------------------

        static const std::string kEmptyParentId;

        static const std::string &GetParentId(const SceneObject &obj)
        {
            const auto it = obj.props.find("parentId");
            return (it != obj.props.end()) ? it->second : kEmptyParentId;
        }

        static int FindObjectIndexById(const SceneDocument &doc, const std::string &id)
        {
            if (id.empty())
                return -1;
            for (int i = 0; i < static_cast<int>(doc.objects.size()); ++i)
            {
                if (doc.objects[static_cast<size_t>(i)].id == id)
                    return i;
            }
            return -1;
        }

        static bool IsDescendantOf(const SceneDocument &doc, int nodeIdx, int ancestorIdx)
        {
            if (nodeIdx < 0 || ancestorIdx < 0 || nodeIdx >= static_cast<int>(doc.objects.size()) ||
                ancestorIdx >= static_cast<int>(doc.objects.size()))
                return false;

            int cur = nodeIdx;
            for (int guard = 0; guard < static_cast<int>(doc.objects.size()); ++guard)
            {
                const int p = FindObjectIndexById(doc, GetParentId(doc.objects[static_cast<size_t>(cur)]));
                if (p < 0)
                    return false;
                if (p == ancestorIdx)
                    return true;
                cur = p;
            }
            return false;
        }

        static void PropagateHierarchyTransformDelta(SceneDocument &doc,
                                                     int parentIdx,
                                                     const Vec3 &oldParentPos,
                                                     const Quaternion &oldParentRot,
                                                     const Vec3 &newParentPos,
                                                     const Quaternion &newParentRot,
                                                     const std::function<void(const SceneObject &)> &transformCb,
                                                     const std::vector<int> &skipIndices = {})
        {
            if (parentIdx < 0 || parentIdx >= static_cast<int>(doc.objects.size()))
                return;

            const Quaternion deltaRot = newParentRot * oldParentRot.Inverse();
            for (int i = 0; i < static_cast<int>(doc.objects.size()); ++i)
            {
                if (i == parentIdx || !IsDescendantOf(doc, i, parentIdx))
                    continue;
                bool shouldSkip = false;
                for (int sk : skipIndices)
                {
                    if (sk == i)
                    {
                        shouldSkip = true;
                        break;
                    }
                }
                if (shouldSkip)
                    continue;

                SceneObject &child = doc.objects[static_cast<size_t>(i)];
                const Vec3 oldRel = child.position - oldParentPos;
                child.position = newParentPos + deltaRot * oldRel;

                const Quaternion childRot =
                    Quaternion::FromEuler(ToRadians(child.pitch), ToRadians(child.yaw), ToRadians(child.roll));
                const Quaternion rotatedChild = deltaRot * childRot;
                const Vec3 e = rotatedChild.ToEuler();
                child.pitch = ToDegrees(e.x);
                child.yaw = ToDegrees(e.y);
                child.roll = ToDegrees(e.z);

                if (transformCb)
                    transformCb(child);
            }
        }

        // ---------------------------------------------------------------------------
        // SceneObjectType <-> string
        // ---------------------------------------------------------------------------

        const char *SceneObjectTypeToString(SceneObjectType type)
        {
            switch (type)
            {
            case SceneObjectType::Panel:
                return "Panel";
            case SceneObjectType::Prop:
                return "Prop";
            case SceneObjectType::Light:
                return "Light";
            case SceneObjectType::Camera:
                return "Camera";
            }
            return "Panel";
        }

        bool ParseSceneObjectType(const std::string &raw, SceneObjectType *outType)
        {
            if (!outType)
                return false;
            std::string value = raw;
            std::ranges::transform(value, value.begin(), [](unsigned char c)
                                   { return static_cast<char>(std::tolower(c)); });
            if (value == "panel")
            {
                *outType = SceneObjectType::Panel;
                return true;
            }
            if (value == "prop")
            {
                *outType = SceneObjectType::Prop;
                return true;
            }
            if (value == "light")
            {
                *outType = SceneObjectType::Light;
                return true;
            }
            if (value == "camera")
            {
                *outType = SceneObjectType::Camera;
                return true;
            }
            return false;
        }

        // ---------------------------------------------------------------------------
        // Object-ID helpers
        // ---------------------------------------------------------------------------

        bool IsObjectReferencePropKey(const std::string &key)
        {
            return key == "parentId" || key == "followTargetId";
        }

        std::unordered_set<std::string> CollectReservedObjectIds(const SceneDocument &doc)
        {
            std::unordered_set<std::string> reservedIds;
            reservedIds.reserve(doc.objects.size() * 2);
            for (const SceneObject &obj : doc.objects)
            {
                if (!obj.id.empty())
                    reservedIds.insert(obj.id);
                for (const auto &entry : obj.props)
                {
                    if (IsObjectReferencePropKey(entry.first) && !entry.second.empty())
                        reservedIds.insert(entry.second);
                }
            }
            return reservedIds;
        }

        bool IsReservedObjectId(const SceneDocument &doc,
                                const std::string &id,
                                const std::string *ignoreConcreteObjectId = nullptr)
        {
            if (id.empty())
                return false;
            for (const SceneObject &obj : doc.objects)
            {
                if (!obj.id.empty() && (!ignoreConcreteObjectId || obj.id != *ignoreConcreteObjectId) && obj.id == id)
                    return true;
                for (const auto &entry : obj.props)
                {
                    if (!IsObjectReferencePropKey(entry.first) || entry.second.empty())
                        continue;
                    if (ignoreConcreteObjectId && entry.second == *ignoreConcreteObjectId)
                        continue;
                    if (entry.second == id)
                        return true;
                }
            }
            return false;
        }

        void RewriteObjectIdReferences(SceneDocument *doc, const std::string &oldId, const std::string &newId)
        {
            if (!doc || oldId.empty() || oldId == newId)
                return;
            for (SceneObject &object : doc->objects)
            {
                for (auto &entry : object.props)
                {
                    if (IsObjectReferencePropKey(entry.first) && entry.second == oldId)
                        entry.second = newId;
                }
            }
        }

        void LogDanglingObjectReferences(const SceneDocument &doc, const std::string &sourceLabel)
        {
            std::unordered_set<std::string> objectIds;
            objectIds.reserve(doc.objects.size() * 2);
            for (const SceneObject &object : doc.objects)
            {
                if (!object.id.empty())
                    objectIds.insert(object.id);
            }
            for (const SceneObject &object : doc.objects)
            {
                for (const auto &entry : object.props)
                {
                    if (!IsObjectReferencePropKey(entry.first) || entry.second.empty())
                        continue;
                    if (objectIds.find(entry.second) == objectIds.end())
                        LOG_WARN("[Editor] Dangling object reference in %s: %s.%s -> %s",
                                 sourceLabel.c_str(), object.id.c_str(), entry.first.c_str(), entry.second.c_str());
                }
            }
        }

        static void SyncAssetScaleMetadata(SceneDocument *doc)
        {
            if (!doc)
                return;

            for (SceneObject &obj : doc->objects)
            {
                if (obj.assetId.empty())
                {
                    obj.props.erase("_assetRenderScale");
                    continue;
                }

                const auto assetIt = doc->assets.find(obj.assetId);
                if (assetIt == doc->assets.end())
                {
                    obj.props.erase("_assetRenderScale");
                    continue;
                }

                obj.props["_assetRenderScale"] = assetIt->second.renderScale.empty()
                                                     ? "1.0000,1.0000,1.0000"
                                                     : assetIt->second.renderScale;
            }
        }

        // ---------------------------------------------------------------------------
        // Prefab helpers
        // ---------------------------------------------------------------------------

        std::string SanitizePrefabStem(std::string value)
        {
            for (char &ch : value)
            {
                const bool alphaNum = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                                      (ch >= '0' && ch <= '9');
                if (!alphaNum && ch != '_' && ch != '-')
                    ch = '_';
            }
            while (!value.empty() && value.front() == '_')
                value.erase(value.begin());
            while (!value.empty() && value.back() == '_')
                value.pop_back();
            return value.empty() ? "prefab" : value;
        }

        std::filesystem::path BuildUniquePrefabPath(const SceneDocument &doc, const SceneObject &object)
        {
            const std::filesystem::path prefabDir = ProjectPath::Resolve("assets/prefabs");
            const std::string stemBase = SanitizePrefabStem(object.id.empty() ? doc.sceneId + "_prefab"
                                                                              : object.id + "_prefab");
            std::filesystem::path candidate = prefabDir / (stemBase + ".horo");
            int suffix = 2;
            while (std::filesystem::exists(candidate))
            {
                candidate = prefabDir / (stemBase + "_" + std::to_string(suffix) + ".horo");
                ++suffix;
            }
            return candidate;
        }

        // ---------------------------------------------------------------------------
        // Asset directory helpers (used by DeleteAssetDefinition)
        // ---------------------------------------------------------------------------

        static bool IsPathWithinDirectory(const std::filesystem::path &path,
                                          const std::filesystem::path &directory)
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            const fs::path normPath = fs::weakly_canonical(path, ec);
            if (ec)
                return false;
            ec.clear();
            const fs::path normDir = fs::weakly_canonical(directory, ec);
            if (ec)
                return false;

            auto dirIt = normDir.begin();
            auto pathIt = normPath.begin();
            for (; dirIt != normDir.end() && pathIt != normPath.end(); ++dirIt, ++pathIt)
            {
                if (*dirIt != *pathIt)
                    return false;
            }
            return dirIt == normDir.end();
        }

        static std::filesystem::path ResolveProjectAssetPath(const std::string &rawPath)
        {
            namespace fs = std::filesystem;
            if (rawPath.empty())
                return {};

            const fs::path path(rawPath);
            if (path.is_absolute())
                return {};

            std::error_code ec;
            const fs::path root = fs::weakly_canonical(ProjectPath::Root(), ec);
            if (ec)
                return {};

            return fs::weakly_canonical(root / path, ec);
        }

        static std::filesystem::path GetManagedImportedAssetDirectory(const AssetDef &asset)
        {
            namespace fs = std::filesystem;
            if (!asset.guid.empty())
            {
                const fs::path guidDirectory = GetManagedAssetDirectory(asset.guid);
                std::error_code ec;
                if (fs::exists(guidDirectory, ec) && !ec)
                    return guidDirectory;
            }
            if (asset.mesh.empty())
                return {};

            const fs::path meshPath = ResolveProjectAssetPath(asset.mesh);
            if (meshPath.empty())
                return {};

            std::error_code ec;
            const fs::path modelsRoot = fs::weakly_canonical(ProjectPath::Root() / "assets" / "models", ec);
            if (ec || !IsPathWithinDirectory(meshPath, modelsRoot))
                return {};

            const fs::path relativeMesh = fs::relative(meshPath, modelsRoot, ec);
            if (ec || relativeMesh.empty())
                return {};

            auto relIt = relativeMesh.begin();
            if (relIt == relativeMesh.end())
                return {};
            const fs::path folder = *relIt;
            ++relIt;
            if (relIt == relativeMesh.end() || folder.empty() || folder == "." || folder == "..")
                return {};

            const fs::path managedDir = modelsRoot / folder;
            if (!asset.albedoMap.empty())
            {
                const fs::path albedoPath = ResolveProjectAssetPath(asset.albedoMap);
                if (albedoPath.empty() || !IsPathWithinDirectory(albedoPath, managedDir))
                    return {};
            }

            return managedDir;
        }

        // ---------------------------------------------------------------------------
        // Log-time formatting (used by PublishMcpSnapshot)
        // ---------------------------------------------------------------------------

        static void FormatLogTime(const LogLine &entry, char *buf, size_t bufSize)
        {
            using clock = std::chrono::system_clock;
            const std::time_t t = clock::to_time_t(entry.time);
            std::tm tmBuf{};
#ifdef _WIN32
            localtime_s(&tmBuf, &t);
#else
            localtime_r(&t, &tmBuf);
#endif
            std::strftime(buf, bufSize, "%H:%M:%S", &tmBuf);
        }

    } // namespace

    // ---- Lifecycle ---------------------------------------------------------------

    void EditorLayer::Init(GLFWwindow *window)
    {
        m_window = window;
        m_active = true;
        m_mcpController.Initialize();
        m_mcpController.SetEditorActive(true);
        if (m_mcpController.SettingsDocument().parseError)
            LOG_WARN("[MCP] Settings load fallback: %s", m_mcpController.SettingsDocument().error.c_str());

        const std::array<std::filesystem::path, 4> schemaCandidates = {
            ProjectPath::ResolveSdk("assets/editor_schema.json"),
            ProjectPath::Root() / "assets" / "editor_schema.json",
            ProjectPath::Root() / "engine" / "assets" / "editor_schema.json",
            ProjectPath::Root() / "horo-engine" / "assets" / "editor_schema.json",
        };
        for (const auto &candidate : schemaCandidates)
        {
            std::error_code ec;
            if (std::filesystem::is_regular_file(candidate, ec) && !ec)
            {
                m_schema.LoadFromFile(candidate.string());
                break;
            }
        }
    }

    void EditorLayer::Shutdown()
    {
        m_mcpController.Shutdown();
    }

    // ---- Per-frame update --------------------------------------------------------

    void EditorLayer::OnUpdate(float /*dt*/, Camera & /*cam*/, int /*screenW*/, int /*screenH*/)
    {
        ProcessMcpCommands();
        PublishMcpSnapshot();
    }

    // ---- Document management -----------------------------------------------------

    void EditorLayer::LoadDocument(SceneDocument doc)
    {
        ApplyLoadedDocument(std::move(doc), true);
    }

    void EditorLayer::ApplyLoadedDocument(SceneDocument doc, bool resetHistory)
    {
        if (doc.filePath.empty())
            doc.filePath = "assets/scenes/scene.json";

        EnsureAssetIdentity(&doc);

        for (auto &obj : doc.objects)
        {
            if (obj.type != SceneObjectType::Prop)
                continue;
            const auto behIt = obj.props.find("behavior");
            if (behIt == obj.props.end() || behIt->second.empty() || behIt->second == "none")
                continue;

            bool hasScript = false;
            for (const auto &comp : obj.components)
            {
                if (comp.type == "script")
                {
                    hasScript = true;
                    break;
                }
            }
            if (!hasScript)
            {
                ComponentDesc script;
                script.type = "script";
                script.props["behaviorTag"] = behIt->second;
                obj.components.push_back(std::move(script));
            }
            obj.props.erase("behavior");
        }

        SyncAssetScaleMetadata(&doc);
        LogDanglingObjectReferences(doc, doc.filePath);

        m_document = std::move(doc);
        m_lastSavedDocument = m_document;
        m_selectedIndices.clear();
        m_selectedAssetId.clear();
        if (resetHistory)
            ClearHistory();
    }

    void EditorLayer::SyncRuntimeEntityIds(Registry &registry)
    {
        std::vector<int> propIndices;
        propIndices.reserve(m_document.objects.size());
        for (int i = 0; i < static_cast<int>(m_document.objects.size()); ++i)
        {
            if (m_document.objects[static_cast<size_t>(i)].type == SceneObjectType::Prop)
                propIndices.push_back(i);
        }

        std::vector<Entity> meshEntities;
        for (Entity e : registry.GetEntities<MeshComponent>())
        {
            if (registry.Has<PlayerTagComponent>(e))
                continue;
            meshEntities.push_back(e);
        }
        std::ranges::sort(meshEntities);

        const size_t propN = propIndices.size();
        const size_t meshN = meshEntities.size();
        const size_t n = std::min(propN, meshN);
        if (propN != meshN)
        {
            LOG_WARN(
                "EditorLayer::SyncRuntimeEntityIds: %zu prop(s) vs %zu mesh entity(ies); mapping first %zu",
                propN, meshN, n);
        }

        for (size_t j = 0; j < n; ++j)
        {
            m_document.objects[static_cast<size_t>(propIndices[j])].props["_eid"] =
                std::to_string(meshEntities[j]);
        }
        for (size_t j = n; j < propN; ++j)
            m_document.objects[static_cast<size_t>(propIndices[j])].props.erase("_eid");
    }

    void EditorLayer::SetProjectBrowserRoot(std::filesystem::path root)
    {
        m_projectBrowserRoot.clear();
        m_projectBrowserRootValid = false;
        m_projectBrowserCwd.clear();
        m_projectBrowserCwdValid = false;
        if (root.empty())
            return;
        std::error_code ec;
        std::filesystem::path canon = std::filesystem::weakly_canonical(root, ec);
        if (ec)
            canon = std::move(root);
        if (!std::filesystem::is_directory(canon, ec) || ec)
        {
            m_projectBrowserRoot = canon;
            return;
        }
        m_projectBrowserRoot = std::move(canon);
        m_projectBrowserRootValid = true;
        if (!m_savedProjectBrowserCwd.empty())
        {
            std::filesystem::path preferred = m_savedProjectBrowserCwd;
            if (preferred.is_relative())
                preferred = m_projectBrowserRoot / preferred;
            preferred = std::filesystem::weakly_canonical(preferred, ec);
            if (!ec && std::filesystem::is_directory(preferred) &&
                preferred.native().rfind(m_projectBrowserRoot.native(), 0) == 0)
            {
                m_projectBrowserCwd = std::move(preferred);
                m_projectBrowserCwdValid = true;
                return;
            }
        }
        m_projectBrowserCwd = m_projectBrowserRoot;
        m_projectBrowserCwdValid = true;
    }

    // ---- Reload state ------------------------------------------------------------

    bool EditorLayer::WantsSceneReload() const
    {
        return m_wantsSceneReload;
    }

    void EditorLayer::ClearPendingReload()
    {
        m_wantsSceneReload = false;
    }

    SceneDocument EditorLayer::GetPendingDocument() const
    {
        return m_pendingDocument;
    }

    // ---- MCP --------------------------------------------------------------------

    void EditorLayer::ProcessMcpCommands()
    {
        m_mcpController.DrainCommands([this](const std::string &toolName, const nlohmann::json &arguments)
                                      { return ExecuteMcpCommand(toolName, arguments); });
    }

    void EditorLayer::PublishMcpSnapshot()
    {
        Mcp::McpEditorSnapshot snapshot;
        snapshot.editorActive = m_active;
        snapshot.playMode = m_playMode;
        snapshot.dirty = m_document.dirty;
        snapshot.reloadPending = m_wantsSceneReload;
        snapshot.sceneId = m_document.sceneId;
        snapshot.sceneName = m_document.sceneName;
        snapshot.sceneFilePath = m_document.filePath;
        snapshot.selectedAssetId = m_selectedAssetId;
        snapshot.projectPath = m_launcherCallbacks.getProjectPath
            ? m_launcherCallbacks.getProjectPath() : "";
        snapshot.projectName = m_launcherCallbacks.getProjectName
            ? m_launcherCallbacks.getProjectName() : "";
        snapshot.editorActive = !snapshot.projectPath.empty();

        for (int idx : m_selectedIndices)
        {
            if (idx >= 0 && idx < static_cast<int>(m_document.objects.size()))
                snapshot.selectedObjectIds.push_back(m_document.objects[static_cast<size_t>(idx)].id);
        }

        snapshot.objects.reserve(m_document.objects.size());
        for (const SceneObject &object : m_document.objects)
        {
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
            for (const ComponentDesc &component : object.components)
            {
                Mcp::McpComponentSnapshot componentEntry;
                componentEntry.type = component.type;
                componentEntry.props = component.props;
                entry.components.push_back(std::move(componentEntry));
            }
            snapshot.objects.push_back(std::move(entry));
        }

        std::vector<std::string> assetIds;
        assetIds.reserve(m_document.assets.size());
        for (const auto &entry : m_document.assets)
            assetIds.push_back(entry.first);
        std::ranges::sort(assetIds);
        for (const std::string &assetId : assetIds)
        {
            const AssetDef &asset = m_document.assets.at(assetId);
            snapshot.assets.push_back(Mcp::McpAssetSnapshot{assetId, asset.mesh, asset.renderScale,
                                                             asset.albedoMap});
        }

        std::vector<LogLine> lines;
        LogBuffer::Instance().CopyLinesTo(&lines);
        const size_t start = lines.size() > 50 ? lines.size() - 50 : 0;
        for (size_t i = start; i < lines.size(); ++i)
        {
            char timeBuf[32];
            FormatLogTime(lines[i], timeBuf, sizeof(timeBuf));
            snapshot.consoleEntries.push_back(Mcp::McpConsoleEntry{
                timeBuf,
                lines[i].level == LogLevel::Info ? "INFO" : lines[i].level == LogLevel::Warn ? "WARN"
                                                                                              : "ERR",
                lines[i].message,
            });
        }

        snapshot.build = BuildMcpBuildSnapshot(m_document);
        snapshot.schema = BuildMcpSchemaCatalogSnapshot(m_schema);
        m_mcpController.PublishSnapshot(snapshot);
    }

    Mcp::McpCommandResult EditorLayer::ExecuteMcpCommand(const std::string &toolName,
                                                         const nlohmann::json &arguments)
    {
        using json = nlohmann::json;

        auto parseVec3 = [](const json &value, Vec3 *out) -> bool
        {
            if (!out || !value.is_array() || value.size() != 3)
                return false;
            if (!value[0].is_number() || !value[1].is_number() || !value[2].is_number())
                return false;
            out->x = value[0].get<float>();
            out->y = value[1].get<float>();
            out->z = value[2].get<float>();
            return true;
        };

        auto parseProps = [](const json &value)
        {
            std::unordered_map<std::string, std::string> props;
            if (!value.is_object())
                return props;
            for (auto it = value.begin(); it != value.end(); ++it)
            {
                props[it.key()] = it.value().is_string() ? it.value().get<std::string>() : it.value().dump();
            }
            return props;
        };

        auto parseComponents = [&](const json &value, std::vector<ComponentDesc> *out) -> bool
        {
            if (!out)
                return false;
            if (!value.is_array())
                return false;
            std::vector<ComponentDesc> components;
            for (const json &item : value)
            {
                if (!item.is_object() || !item.contains("type") || !item["type"].is_string())
                    return false;
                ComponentDesc component;
                component.type = item["type"].get<std::string>();
                component.props = parseProps(item.value("props", json::object()));
                components.push_back(std::move(component));
            }
            *out = std::move(components);
            return true;
        };

        auto findIndexById = [&](const std::string &id) -> int
        {
            for (int i = 0; i < static_cast<int>(m_document.objects.size()); ++i)
            {
                if (m_document.objects[static_cast<size_t>(i)].id == id)
                    return i;
            }
            return -1;
        };

        auto summarizeObject = [&](const SceneObject &object)
        {
            json out = json::object();
            out["id"] = object.id;
            out["type"] = SceneObjectTypeToString(object.type);
            out["position"] = json::array({object.position.x, object.position.y, object.position.z});
            out["scale"] = json::array({object.scale.x, object.scale.y, object.scale.z});
            out["yaw"] = object.yaw;
            out["pitch"] = object.pitch;
            out["roll"] = object.roll;
            out["assetId"] = object.assetId;
            if (object.prefabInstance.has_value())
            {
                out["prefab"] = json{{"id", object.prefabInstance->prefabId},
                                     {"path", object.prefabInstance->sourcePath}};
            }
            const auto parentIt = object.props.find("parentId");
            if (parentIt != object.props.end() && !parentIt->second.empty())
                out["parentId"] = parentIt->second;
            return out;
        };

        auto summarizeAsset = [&](const std::string &assetId, const AssetDef &asset)
        {
            int referenceCount = 0;
            for (const SceneObject &object : m_document.objects)
            {
                if (object.assetId == assetId)
                    ++referenceCount;
            }
            return json{{"id", assetId},
                        {"guid", asset.guid},
                        {"displayName", asset.displayName},
                        {"mesh", asset.mesh},
                        {"renderScale", asset.renderScale},
                        {"albedoMap", asset.albedoMap},
                        {"objectReferenceCount", referenceCount}};
        };

        if (toolName == "editor.select")
        {
            std::vector<std::string> ids;
            if (arguments.contains("id") && arguments["id"].is_string())
                ids.push_back(arguments["id"].get<std::string>());
            if (arguments.contains("ids") && arguments["ids"].is_array())
            {
                for (const json &item : arguments["ids"])
                {
                    if (item.is_string())
                        ids.push_back(item.get<std::string>());
                }
            }
            m_selectedIndices.clear();
            for (const std::string &id : ids)
            {
                const int idx = findIndexById(id);
                if (idx >= 0)
                    m_selectedIndices.push_back(idx);
            }
            m_selectedAssetId.clear();
            return Mcp::McpCommandResult{true,
                                         json{{"selectedObjectIds", GetSelectedObjectIds()}},
                                         std::string()};
        }

        if (toolName == "editor.clear_selection")
        {
            m_selectedIndices.clear();
            m_selectedAssetId.clear();
            return Mcp::McpCommandResult{true, json{{"cleared", true}}, std::string()};
        }

        if (toolName == "editor.undo")
        {
            const bool undone = UndoHistory();
            return Mcp::McpCommandResult{true,
                                         json{{"undone", undone},
                                              {"dirty", m_document.dirty},
                                              {"selectedObjectIds", GetSelectedObjectIds()},
                                              {"selectedAssetId", m_selectedAssetId}},
                                         std::string()};
        }

        if (toolName == "editor.redo")
        {
            const bool redone = RedoHistory();
            return Mcp::McpCommandResult{true,
                                         json{{"redone", redone},
                                              {"dirty", m_document.dirty},
                                              {"selectedObjectIds", GetSelectedObjectIds()},
                                              {"selectedAssetId", m_selectedAssetId}},
                                         std::string()};
        }

        if (toolName == "editor.create_object")
        {
            const EditorHistorySnapshot before = CaptureHistorySnapshot();
            SceneObjectType type = SceneObjectType::Panel;
            if (!ParseSceneObjectType(arguments.value("type", std::string()), &type))
                return Mcp::McpCommandResult{false, json::object(), "Invalid object type."};

            SceneObject object;
            object.type = type;
            ApplySchemaDefaults(object);
            object.id = arguments.value("id", std::string());
            if (object.id.empty())
                object.id = type == SceneObjectType::Camera ? GenerateCameraId(m_document) : GenerateId(m_document);
            if (IsReservedObjectId(m_document, object.id))
                return Mcp::McpCommandResult{false, json::object(), "Object id already exists."};

            if (arguments.contains("position") && !parseVec3(arguments["position"], &object.position))
                return Mcp::McpCommandResult{false, json::object(), "position must be [x,y,z]."};
            if (arguments.contains("scale") && !parseVec3(arguments["scale"], &object.scale))
                return Mcp::McpCommandResult{false, json::object(), "scale must be [x,y,z]."};
            object.yaw = arguments.value("yaw", object.yaw);
            object.pitch = arguments.value("pitch", object.pitch);
            object.roll = arguments.value("roll", object.roll);
            object.assetId = arguments.value("assetId", std::string());
            if (arguments.contains("props"))
                object.props = parseProps(arguments["props"]);
            if (arguments.contains("components") && !parseComponents(arguments["components"], &object.components))
                return Mcp::McpCommandResult{false, json::object(), "components must be an array of objects."};
            const std::string parentId = arguments.value("parentId", std::string());
            if (!parentId.empty())
            {
                if (findIndexById(parentId) < 0)
                    return Mcp::McpCommandResult{false, json::object(), "Parent object not found."};
                object.props["parentId"] = parentId;
            }

            m_document.objects.push_back(std::move(object));
            m_selectedIndices = {static_cast<int>(m_document.objects.size()) - 1};
            m_document.dirty = true;
            TriggerReload();
            CommitHistoryChange(before);
            return Mcp::McpCommandResult{true,
                                         json{{"created", summarizeObject(m_document.objects.back())}},
                                         std::string()};
        }

        if (toolName == "editor.create_object_from_asset")
        {
            const std::string assetId = arguments.value("assetId", std::string());
            Vec3 position = Vec3::Zero();
            const Vec3 *positionPtr = nullptr;
            if (arguments.contains("position") && !parseVec3(arguments["position"], &position))
                return Mcp::McpCommandResult{false, json::object(), "position must be [x,y,z]."};
            if (arguments.contains("position"))
                positionPtr = &position;

            const std::string requestedId = arguments.value("id", std::string());
            const std::string parentId = arguments.value("parentId", std::string());
            SceneObject createdObject;
            std::string createError;
            if (!CreateObjectFromAsset(assetId,
                                       parentId,
                                       positionPtr,
                                       requestedId.empty() ? nullptr : &requestedId,
                                       &createdObject,
                                       &createError))
            {
                return Mcp::McpCommandResult{false, json::object(), createError};
            }
            SceneObject &object = m_document.objects.back();
            object.yaw = arguments.value("yaw", object.yaw);
            object.pitch = arguments.value("pitch", object.pitch);
            object.roll = arguments.value("roll", object.roll);
            if (m_transformCallback)
                m_transformCallback(object);
            TriggerReload();

            return Mcp::McpCommandResult{true,
                                         json{{"created", summarizeObject(object)}},
                                         std::string()};
        }

        if (toolName == "editor.create_prefab")
        {
            const std::string id = arguments.value("id", std::string());
            if (!id.empty())
                SetSelectedObjectIds({id});

            std::string prefabPath;
            std::string prefabError;
            if (!CreatePrefabFromSelection(&prefabError, &prefabPath))
                return Mcp::McpCommandResult{false, json::object(), prefabError};

            const int idx = PrimaryIdx();
            const SceneObject *object =
                (idx >= 0 && idx < static_cast<int>(m_document.objects.size()))
                    ? &m_document.objects[static_cast<size_t>(idx)]
                    : nullptr;
            return Mcp::McpCommandResult{
                true,
                json{{"prefabPath", prefabPath},
                     {"prefabId", object && object->prefabInstance.has_value() ? object->prefabInstance->prefabId
                                                                               : std::string()},
                     {"instance", object ? summarizeObject(*object) : json::object()}},
                std::string()};
        }

        if (toolName == "editor.update_object" || toolName == "editor.transform")
        {
            const EditorHistorySnapshot before = CaptureHistorySnapshot();
            const std::string id = arguments.value("id", std::string());
            const int idx = findIndexById(id);
            if (idx < 0)
                return Mcp::McpCommandResult{false, json::object(), "Object not found."};

            SceneObject &object = m_document.objects[static_cast<size_t>(idx)];
            if (arguments.contains("position") && !parseVec3(arguments["position"], &object.position))
                return Mcp::McpCommandResult{false, json::object(), "position must be [x,y,z]."};
            if (arguments.contains("scale") && !parseVec3(arguments["scale"], &object.scale))
                return Mcp::McpCommandResult{false, json::object(), "scale must be [x,y,z]."};
            if (arguments.contains("yaw"))
                object.yaw = arguments["yaw"].get<float>();
            if (arguments.contains("pitch"))
                object.pitch = arguments["pitch"].get<float>();
            if (arguments.contains("roll"))
                object.roll = arguments["roll"].get<float>();
            if (toolName == "editor.update_object")
            {
                if (arguments.contains("assetId"))
                    object.assetId = arguments["assetId"].is_null() ? std::string() : arguments["assetId"].get<std::string>();
                if (arguments.contains("props"))
                {
                    const auto props = parseProps(arguments["props"]);
                    for (const auto &entry : props)
                        object.props[entry.first] = entry.second;
                }
                if (arguments.contains("components") &&
                    !parseComponents(arguments["components"], &object.components))
                {
                    return Mcp::McpCommandResult{false, json::object(), "components must be an array of objects."};
                }
            }
            m_document.dirty = true;
            if (m_transformCallback)
                m_transformCallback(object);
            TriggerReload();
            CommitHistoryChange(before);
            return Mcp::McpCommandResult{true, json{{"updated", summarizeObject(object)}}, std::string()};
        }

        if (toolName == "editor.rename_object")
        {
            const EditorHistorySnapshot before = CaptureHistorySnapshot();
            const std::string id = arguments.value("id", std::string());
            const std::string newId = arguments.value("newId", std::string());
            const int idx = findIndexById(id);
            if (idx < 0)
                return Mcp::McpCommandResult{false, json::object(), "Object not found."};
            if (newId.empty())
                return Mcp::McpCommandResult{false, json::object(), "newId is required."};
            if (id != newId && IsReservedObjectId(m_document, newId, &id))
                return Mcp::McpCommandResult{false, json::object(), "Object id already exists."};

            SceneObject &object = m_document.objects[static_cast<size_t>(idx)];
            const std::string oldId = object.id;
            object.id = newId;
            RewriteObjectIdReferences(&m_document, oldId, newId);
            m_document.dirty = true;
            TriggerReload();
            CommitHistoryChange(before);
            return Mcp::McpCommandResult{true,
                                         json{{"renamed", true}, {"oldId", oldId}, {"newId", newId}},
                                         std::string()};
        }

        if (toolName == "editor.reparent_object")
        {
            const EditorHistorySnapshot before = CaptureHistorySnapshot();
            const std::string id = arguments.value("id", std::string());
            const int idx = findIndexById(id);
            if (idx < 0)
                return Mcp::McpCommandResult{false, json::object(), "Object not found."};

            const std::string parentId = arguments.value("parentId", std::string());
            if (!parentId.empty())
            {
                const int parentIdx = findIndexById(parentId);
                if (parentIdx < 0)
                    return Mcp::McpCommandResult{false, json::object(), "Parent object not found."};
                if (parentIdx == idx)
                    return Mcp::McpCommandResult{false, json::object(), "Object cannot parent itself."};
                if (IsDescendantOf(m_document, parentIdx, idx))
                    return Mcp::McpCommandResult{false, json::object(), "Parent would create a cycle."};
                m_document.objects[static_cast<size_t>(idx)].props["parentId"] = parentId;
            }
            else
            {
                m_document.objects[static_cast<size_t>(idx)].props.erase("parentId");
            }
            m_document.dirty = true;
            TriggerReload();
            CommitHistoryChange(before);
            return Mcp::McpCommandResult{true,
                                         json{{"id", id}, {"parentId", parentId}},
                                         std::string()};
        }

        if (toolName == "editor.duplicate")
        {
            const EditorHistorySnapshot before = CaptureHistorySnapshot();
            std::vector<int> sourceIndices;
            if (arguments.contains("id") && arguments["id"].is_string())
            {
                const int idx = findIndexById(arguments["id"].get<std::string>());
                if (idx >= 0)
                    sourceIndices.push_back(idx);
            }
            if (arguments.contains("ids") && arguments["ids"].is_array())
            {
                for (const json &item : arguments["ids"])
                {
                    if (!item.is_string())
                        continue;
                    const int idx = findIndexById(item.get<std::string>());
                    if (idx >= 0)
                        sourceIndices.push_back(idx);
                }
            }
            if (sourceIndices.empty())
                return Mcp::McpCommandResult{false, json::object(), "Object not found."};

            std::ranges::sort(sourceIndices);
            const auto sourceIndicesUniqueTail = std::ranges::unique(sourceIndices);
            sourceIndices.erase(sourceIndicesUniqueTail.begin(), sourceIndicesUniqueTail.end());

            const int count =
                (sourceIndices.size() == 1) ? std::max(1, std::min(8, arguments.value("count", 1))) : 1;
            json created = json::array();
            std::vector<int> duplicatedIndices;
            for (int idx : sourceIndices)
            {
                for (int i = 0; i < count; ++i)
                {
                    SceneObject clone = DuplicateObject(m_document, m_document.objects[static_cast<size_t>(idx)]);
                    clone.position.x += static_cast<float>(i + 1);
                    clone.position.z += static_cast<float>(i + 1);
                    m_document.objects.push_back(std::move(clone));
                    duplicatedIndices.push_back(static_cast<int>(m_document.objects.size()) - 1);
                    created.push_back(summarizeObject(m_document.objects.back()));
                }
            }
            m_selectedIndices = std::move(duplicatedIndices);
            m_document.dirty = true;
            TriggerReload();
            CommitHistoryChange(before);
            return Mcp::McpCommandResult{true, json{{"duplicates", std::move(created)}}, std::string()};
        }

        if (toolName == "editor.delete")
        {
            const EditorHistorySnapshot before = CaptureHistorySnapshot();
            std::vector<int> indices;
            if (arguments.contains("id") && arguments["id"].is_string())
            {
                const int idx = findIndexById(arguments["id"].get<std::string>());
                if (idx >= 0)
                    indices.push_back(idx);
            }
            if (arguments.contains("ids") && arguments["ids"].is_array())
            {
                for (const json &item : arguments["ids"])
                {
                    if (!item.is_string())
                        continue;
                    const int idx = findIndexById(item.get<std::string>());
                    if (idx >= 0)
                        indices.push_back(idx);
                }
            }
            if (indices.empty())
                return Mcp::McpCommandResult{false, json::object(), "No matching objects to delete."};

            std::ranges::sort(indices);
            const auto indicesUniqueTail = std::ranges::unique(indices);
            indices.erase(indicesUniqueTail.begin(), indicesUniqueTail.end());
            std::sort(indices.rbegin(), indices.rend());
            for (int idx : indices)
                m_document.objects.erase(m_document.objects.begin() + idx);
            m_selectedIndices.clear();
            m_document.dirty = true;
            TriggerReload();
            CommitHistoryChange(before);
            return Mcp::McpCommandResult{true,
                                         json{{"deletedCount", static_cast<int>(indices.size())}},
                                         std::string()};
        }

        if (toolName == "editor.select_asset")
        {
            const std::string assetId = arguments.value("id", std::string());
            if (!assetId.empty() && m_document.assets.find(assetId) == m_document.assets.end())
                return Mcp::McpCommandResult{false, json::object(), "Asset not found."};
            m_selectedAssetId = assetId;
            return Mcp::McpCommandResult{true, json{{"selectedAssetId", m_selectedAssetId}}, std::string()};
        }

        if (toolName == "editor.update_asset")
        {
            const EditorHistorySnapshot before = CaptureHistorySnapshot();
            const std::string assetId = arguments.value("id", std::string());
            auto it = m_document.assets.find(assetId);
            if (it == m_document.assets.end())
                return Mcp::McpCommandResult{false, json::object(), "Asset not found."};
            if (arguments.contains("mesh"))
                it->second.mesh = arguments["mesh"].is_null() ? std::string() : arguments["mesh"].get<std::string>();
            if (arguments.contains("renderScale"))
                it->second.renderScale =
                    arguments["renderScale"].is_null() ? std::string() : arguments["renderScale"].get<std::string>();
            if (arguments.contains("albedoMap"))
                it->second.albedoMap =
                    arguments["albedoMap"].is_null() ? std::string() : arguments["albedoMap"].get<std::string>();
            if (arguments.contains("displayName"))
                it->second.displayName =
                    arguments["displayName"].is_null() ? std::string() : arguments["displayName"].get<std::string>();
            EnsureAssetIdentity(assetId, &it->second);
            m_document.dirty = true;
            TriggerReload();
            CommitHistoryChange(before);
            return Mcp::McpCommandResult{true,
                                         json{{"asset", summarizeAsset(assetId, it->second)}},
                                         std::string()};
        }

        if (toolName == "editor.delete_asset")
        {
            const std::string assetId = arguments.value("id", std::string());
            const AssetDeleteResult deleteResult = DeleteAssetDefinition(assetId);
            if (!deleteResult.ok)
                return Mcp::McpCommandResult{false, json::object(), deleteResult.error};

            json payload{{"deletedAssetId", assetId},
                         {"clearedObjectReferences", deleteResult.clearedReferences},
                         {"deletedManagedFiles", deleteResult.deletedManagedFiles}};
            if (!deleteResult.deletedAssetDirectory.empty())
                payload["deletedAssetDirectory"] = deleteResult.deletedAssetDirectory;
            return Mcp::McpCommandResult{true,
                                         std::move(payload),
                                         std::string()};
        }

        if (toolName == "editor.new_scene")
        {
            const EditorHistorySnapshot before = CaptureHistorySnapshot();
            AddNewScene();
            if (arguments.contains("sceneId") && arguments["sceneId"].is_string() &&
                !arguments["sceneId"].get<std::string>().empty())
            {
                m_document.sceneId = arguments["sceneId"].get<std::string>();
            }
            if (arguments.contains("sceneName") && arguments["sceneName"].is_string() &&
                !arguments["sceneName"].get<std::string>().empty())
            {
                m_document.sceneName = arguments["sceneName"].get<std::string>();
            }
            m_lastSavedDocument = m_document;
            TriggerReload();
            CommitHistoryChange(before);
            return Mcp::McpCommandResult{true,
                                         json{{"sceneId", m_document.sceneId},
                                              {"sceneName", m_document.sceneName},
                                              {"dirty", m_document.dirty}},
                                         std::string()};
        }

        if (toolName == "editor.save_scene")
        {
            std::string saveError;
            if (!SaveDocument(&saveError))
                return Mcp::McpCommandResult{false, json::object(), saveError};
            return Mcp::McpCommandResult{true, json{{"saved", true}, {"filePath", m_document.filePath}},
                                         std::string()};
        }

        if (toolName == "editor.reload_scene")
        {
            const EditorHistorySnapshot before = CaptureHistorySnapshot();
            const std::vector<std::string> previousSelectionIds = GetSelectedObjectIds();
            const std::string previousAssetId = m_selectedAssetId;
            std::string reloadError;
            if (!ReloadDocumentFromDisk(&reloadError, &previousSelectionIds, &previousAssetId))
                return Mcp::McpCommandResult{false, json::object(), reloadError};
            CommitHistoryChange(before);
            return Mcp::McpCommandResult{true,
                                         json{{"reloadPending", true},
                                              {"filePath", m_document.filePath},
                                              {"dirty", m_document.dirty}},
                                         std::string()};
        }

        if (toolName == "launcher.open_project")
        {
            const std::string projectPath = arguments.value("path", "");
            if (projectPath.empty())
                return Mcp::McpCommandResult{false, json::object(), "path is required"};
            if (!m_launcherCallbacks.openProject)
                return Mcp::McpCommandResult{false, json::object(), "launcher not attached"};
            std::string error;
            const bool ok = m_launcherCallbacks.openProject(std::filesystem::path(projectPath), &error);
            return Mcp::McpCommandResult{ok,
                                          json{{"projectPath", projectPath},
                                               {"ok", ok}},
                                          ok ? std::string() : error};
        }

        if (toolName == "launcher.create_project")
        {
            const std::string name = arguments.value("name", "");
            const std::string projPath = arguments.value("path", "");
            if (name.empty() || projPath.empty())
                return Mcp::McpCommandResult{false, json::object(), "name and path are required"};
            if (!m_launcherCallbacks.createProject)
                return Mcp::McpCommandResult{false, json::object(), "launcher not attached"};
            std::string error;
            const bool ok = m_launcherCallbacks.createProject(name, std::filesystem::path(projPath), &error);
            return Mcp::McpCommandResult{ok,
                                          json{{"projectPath", projPath},
                                               {"projectName", name},
                                               {"ok", ok}},
                                          ok ? std::string() : error};
        }

        if (toolName == "launcher.close_project")
        {
            if (!m_launcherCallbacks.closeProject)
                return Mcp::McpCommandResult{false, json::object(), "launcher not attached"};
            m_launcherCallbacks.closeProject();
            return Mcp::McpCommandResult{true, json{{"ok", true}}, std::string()};
        }

        if (toolName == "launcher.get_project_status")
        {
            const bool has = m_launcherCallbacks.hasProject && m_launcherCallbacks.hasProject();
            const std::string projPath = (has && m_launcherCallbacks.getProjectPath)
                ? m_launcherCallbacks.getProjectPath() : "";
            const std::string projName = (has && m_launcherCallbacks.getProjectName)
                ? m_launcherCallbacks.getProjectName() : "";
            return Mcp::McpCommandResult{true,
                                          json{{"hasProject", has},
                                               {"projectPath", projPath},
                                               {"projectName", projName}},
                                          std::string()};
        }

        return Mcp::McpCommandResult{false, json::object(), "Unsupported MCP command."};
    }

    // ---- Selection ---------------------------------------------------------------

    bool EditorLayer::IsSelected(int i) const
    {
        return std::ranges::find(m_selectedIndices, i) != m_selectedIndices.end();
    }

    int EditorLayer::PrimaryIdx() const
    {
        return m_selectedIndices.empty() ? -1 : m_selectedIndices.back();
    }

    void EditorLayer::ToggleSelect(int i)
    {
        auto it = std::ranges::find(m_selectedIndices, i);
        if (it != m_selectedIndices.end())
            m_selectedIndices.erase(it);
        else
            m_selectedIndices.push_back(i);
    }

    void EditorLayer::TriggerReload()
    {
        m_pendingDocument = m_document;
        m_wantsSceneReload = true;
    }

    std::vector<std::string> EditorLayer::GetSelectedObjectIds() const
    {
        std::vector<std::string> ids;
        ids.reserve(m_selectedIndices.size());
        for (int idx : m_selectedIndices)
        {
            if (idx >= 0 && idx < static_cast<int>(m_document.objects.size()))
                ids.push_back(m_document.objects[static_cast<size_t>(idx)].id);
        }
        return ids;
    }

    void EditorLayer::SetSelectedObjectIds(const std::vector<std::string> &ids)
    {
        m_selectedIndices.clear();
        std::unordered_set<std::string> seen;
        seen.reserve(ids.size());
        for (const std::string &id : ids)
        {
            if (id.empty() || !seen.insert(id).second)
                continue;
            for (int i = 0; i < static_cast<int>(m_document.objects.size()); ++i)
            {
                if (m_document.objects[static_cast<size_t>(i)].id == id)
                {
                    m_selectedIndices.push_back(i);
                    break;
                }
            }
        }
    }

    // ---- History -----------------------------------------------------------------

    bool EditorLayer::HistorySnapshotsEqual(const EditorHistorySnapshot &lhs,
                                            const EditorHistorySnapshot &rhs)
    {
        return SceneDocumentsEqual(lhs.document, rhs.document) &&
               SceneDocumentsEqual(lhs.savedDocument, rhs.savedDocument) &&
               lhs.selectedObjectIds == rhs.selectedObjectIds &&
               lhs.selectedAssetId == rhs.selectedAssetId;
    }

    void EditorLayer::TrimHistory(std::vector<EditorHistorySnapshot> *history)
    {
        if (!history)
            return;
        if (history->size() <= kMaxEditorHistorySnapshots)
            return;
        history->erase(history->begin(),
                       history->begin() + static_cast<std::ptrdiff_t>(history->size() - kMaxEditorHistorySnapshots));
    }

    EditorLayer::EditorHistorySnapshot EditorLayer::CaptureHistorySnapshot() const
    {
        EditorHistorySnapshot snapshot;
        snapshot.document = m_document;
        snapshot.savedDocument = m_lastSavedDocument;
        snapshot.selectedObjectIds = GetSelectedObjectIds();
        snapshot.selectedAssetId = m_selectedAssetId;
        return snapshot;
    }

    void EditorLayer::RestoreHistorySnapshot(const EditorHistorySnapshot &snapshot)
    {
        m_document = snapshot.document;
        m_lastSavedDocument = snapshot.savedDocument;
        SetSelectedObjectIds(snapshot.selectedObjectIds);
        if (!snapshot.selectedAssetId.empty() &&
            m_document.assets.find(snapshot.selectedAssetId) != m_document.assets.end())
        {
            m_selectedAssetId = snapshot.selectedAssetId;
        }
        else
        {
            m_selectedAssetId.clear();
        }
        TriggerReload();
    }

    void EditorLayer::CommitHistoryChange(const EditorHistorySnapshot &before)
    {
        const EditorHistorySnapshot after = CaptureHistorySnapshot();
        if (HistorySnapshotsEqual(before, after))
            return;

        m_undoHistory.push_back(before);
        TrimHistory(&m_undoHistory);
        m_redoHistory.clear();
    }

    void EditorLayer::BeginHistoryTransaction(const EditorHistorySnapshot &before)
    {
        if (m_historyTransactionOpen)
            return;
        m_historyTransactionBefore = before;
        m_historyTransactionOpen = true;
    }

    void EditorLayer::FinalizeHistoryTransaction()
    {
        if (!m_historyTransactionOpen)
            return;
        CommitHistoryChange(m_historyTransactionBefore);
        m_historyTransactionOpen = false;
    }

    void EditorLayer::ClearHistory()
    {
        m_undoHistory.clear();
        m_redoHistory.clear();
        m_historyTransactionOpen = false;
    }

    void EditorLayer::RefreshHistorySavedBaseline()
    {
        auto refreshSnapshot = [this](EditorHistorySnapshot *snapshot)
        {
            if (!snapshot)
                return;
            if (snapshot->document.filePath == m_document.filePath)
            {
                snapshot->savedDocument = m_lastSavedDocument;
                snapshot->document.dirty = !SceneDocumentsEqual(snapshot->document, snapshot->savedDocument);
            }
        };

        for (EditorHistorySnapshot &snapshot : m_undoHistory)
            refreshSnapshot(&snapshot);
        for (EditorHistorySnapshot &snapshot : m_redoHistory)
            refreshSnapshot(&snapshot);
        if (m_historyTransactionOpen)
            refreshSnapshot(&m_historyTransactionBefore);
    }

    bool EditorLayer::CanUndoHistory() const
    {
        return !m_undoHistory.empty();
    }

    bool EditorLayer::CanRedoHistory() const
    {
        return !m_redoHistory.empty();
    }

    bool EditorLayer::UndoHistory()
    {
        FinalizeHistoryTransaction();
        if (m_undoHistory.empty())
            return false;

        const EditorHistorySnapshot current = CaptureHistorySnapshot();
        const EditorHistorySnapshot target = m_undoHistory.back();
        m_undoHistory.pop_back();
        m_redoHistory.push_back(current);
        TrimHistory(&m_redoHistory);
        RestoreHistorySnapshot(target);
        return true;
    }

    bool EditorLayer::RedoHistory()
    {
        FinalizeHistoryTransaction();
        if (m_redoHistory.empty())
            return false;

        const EditorHistorySnapshot current = CaptureHistorySnapshot();
        const EditorHistorySnapshot target = m_redoHistory.back();
        m_redoHistory.pop_back();
        m_undoHistory.push_back(current);
        TrimHistory(&m_undoHistory);
        RestoreHistorySnapshot(target);
        return true;
    }

    // ---- Document I/O ------------------------------------------------------------

    bool EditorLayer::SaveDocument(std::string *outError)
    {
        if (outError)
            outError->clear();

        FinalizeHistoryTransaction();

        std::string path = m_document.filePath.empty() ? "assets/scenes/scene.json" : m_document.filePath;
        m_document.filePath = path;
        EnsureAssetIdentity(&m_document);

        LOG_INFO("[Editor] Saving scene to: %s", path.c_str());

        try
        {
            if (!EnsureAssetMetadataForDocument(&m_document, outError))
            {
                LOG_ERROR("[Editor] Asset metadata save failed: %s", outError ? outError->c_str() : "");
                return false;
            }
            SceneSerializer::SaveToFile(m_document, path);
            m_document.dirty = false;
            m_lastSavedDocument = m_document;
            RefreshHistorySavedBaseline();
            LOG_INFO("[Editor] Scene saved OK");
            TriggerReload();
            return true;
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("[Editor] Save failed: %s", e.what());
            if (outError)
                *outError = e.what();
            return false;
        }
    }

    bool EditorLayer::ReloadDocumentFromDisk(std::string *outError,
                                             const std::vector<std::string> *preferredSelectionIds,
                                             const std::string *preferredAssetId)
    {
        if (outError)
            outError->clear();

        const std::string path = m_document.filePath.empty() ? "assets/scenes/scene.json" : m_document.filePath;
        try
        {
            SceneDocument reloaded = SceneSerializer::LoadFromFile(path);
            reloaded.dirty = false;
            ApplyLoadedDocument(std::move(reloaded), false);
            m_document.filePath = path;
            m_lastSavedDocument = m_document;
            if (preferredSelectionIds)
                SetSelectedObjectIds(*preferredSelectionIds);
            if (preferredAssetId && m_document.assets.find(*preferredAssetId) != m_document.assets.end())
                m_selectedAssetId = *preferredAssetId;
            TriggerReload();
            return true;
        }
        catch (const std::exception &e)
        {
            if (outError)
                *outError = e.what();
            return false;
        }
    }

    // ---- Asset / object creation helpers -----------------------------------------

    EditorLayer::AssetDeleteResult EditorLayer::DeleteAssetDefinition(const std::string &assetId)
    {
        AssetDeleteResult result;
        auto it = m_document.assets.find(assetId);
        if (it == m_document.assets.end())
        {
            result.error = "Asset not found.";
            return result;
        }

        const EditorHistorySnapshot before = CaptureHistorySnapshot();

        const std::filesystem::path managedDirectory = GetManagedImportedAssetDirectory(it->second);
        if (!managedDirectory.empty())
        {
            std::error_code ec;
            const bool existedBeforeDelete = std::filesystem::exists(managedDirectory, ec) && !ec;
            ec.clear();
            std::filesystem::remove_all(managedDirectory, ec);
            if (ec)
            {
                result.error = "Failed to delete imported asset files: " + ec.message();
                return result;
            }
            result.deletedManagedFiles = existedBeforeDelete;
            result.deletedAssetDirectory = managedDirectory.generic_string();
        }

        for (SceneObject &object : m_document.objects)
        {
            if (object.assetId == assetId)
            {
                object.assetId.clear();
                ++result.clearedReferences;
            }
        }
        if (m_selectedAssetId == assetId)
            m_selectedAssetId.clear();

        m_document.assets.erase(it);
        m_document.dirty = true;
        TriggerReload();
        CommitHistoryChange(before);

        result.ok = true;
        return result;
    }

    bool EditorLayer::CreateObjectFromAsset(const std::string &assetId,
                                            const std::string &parentId,
                                            const Vec3 *worldPosition,
                                            const std::string *preferredId,
                                            SceneObject *outCreated,
                                            std::string *outError)
    {
        if (outError)
            outError->clear();

        if (assetId.empty() || m_document.assets.find(assetId) == m_document.assets.end())
        {
            if (outError)
                *outError = "Asset not found.";
            return false;
        }
        if (!parentId.empty() && FindObjectIndexById(m_document, parentId) < 0)
        {
            if (outError)
                *outError = "Parent object not found.";
            return false;
        }

        const EditorHistorySnapshot before = CaptureHistorySnapshot();

        SceneObject object = MakeObjectFromAsset(m_document, assetId, m_schema);
        if (preferredId && !preferredId->empty())
        {
            if (IsReservedObjectId(m_document, *preferredId))
            {
                if (outError)
                    *outError = "Object id already exists.";
                return false;
            }
            object.id = *preferredId;
        }
        if (!parentId.empty())
            object.props["parentId"] = parentId;
        if (worldPosition)
            object.position = *worldPosition;

        m_document.objects.push_back(object);
        m_selectedIndices = {static_cast<int>(m_document.objects.size()) - 1};
        m_selectedAssetId = assetId;
        m_document.dirty = true;
        TriggerReload();
        CommitHistoryChange(before);

        if (outCreated)
            *outCreated = m_document.objects.back();
        return true;
    }

    bool EditorLayer::CreatePrefabFromSelection(std::string *outError, std::string *outPrefabPath)
    {
        if (outError)
            outError->clear();
        if (outPrefabPath)
            outPrefabPath->clear();

        const int primaryIdx = PrimaryIdx();
        if (m_selectedIndices.size() != 1 ||
            primaryIdx < 0 ||
            primaryIdx >= static_cast<int>(m_document.objects.size()))
        {
            if (outError)
                *outError = "Select exactly one object to create a prefab.";
            return false;
        }

        const EditorHistorySnapshot before = CaptureHistorySnapshot();
        const SceneObject &sourceObject = m_document.objects[static_cast<size_t>(primaryIdx)];
        const std::filesystem::path prefabAbsPath = BuildUniquePrefabPath(m_document, sourceObject);
        std::error_code ec;
        std::filesystem::create_directories(prefabAbsPath.parent_path(), ec);
        if (ec)
        {
            if (outError)
                *outError = "Failed to create prefab directory: " + ec.message();
            return false;
        }

        SceneDocument prefabDoc;
        prefabDoc.version = m_document.version;
        prefabDoc.sceneId = prefabAbsPath.stem().string();
        prefabDoc.sceneName = sourceObject.id.empty() ? prefabDoc.sceneId : sourceObject.id;
        prefabDoc.filePath = prefabAbsPath.generic_string();

        SceneObject prefabObject = sourceObject;
        prefabObject.prefabInstance.reset();
        prefabObject.props.erase("parentId");
        prefabDoc.objects.push_back(std::move(prefabObject));
        if (!sourceObject.assetId.empty())
        {
            const auto assetIt = m_document.assets.find(sourceObject.assetId);
            if (assetIt != m_document.assets.end())
                prefabDoc.assets[sourceObject.assetId] = assetIt->second;
        }

        try
        {
            SceneSerializer::SaveToFile(prefabDoc, prefabAbsPath.generic_string());
        }
        catch (const std::exception &e)
        {
            if (outError)
                *outError = e.what();
            return false;
        }

        const std::filesystem::path relativePath =
            prefabAbsPath.lexically_relative(ProjectPath::Root()).lexically_normal();
        SceneObject &object = m_document.objects[static_cast<size_t>(primaryIdx)];
        object.prefabInstance = ScenePrefabInstance{prefabDoc.sceneId, relativePath.generic_string()};
        m_document.dirty = true;
        TriggerReload();
        CommitHistoryChange(before);

        if (outPrefabPath)
            *outPrefabPath = relativePath.generic_string();
        return true;
    }

    void EditorLayer::AddNewScene()
    {
        SceneDocument newDoc;
        newDoc.sceneId = "scene";
        newDoc.sceneName = "Scene";
        newDoc.dirty = true;
        m_document = std::move(newDoc);
        m_selectedIndices.clear();
        m_selectedAssetId.clear();
    }

    // ---- Schema / object factories -----------------------------------------------

    void EditorLayer::ApplySchemaDefaults(SceneObject &obj) const
    {
        const TypeSchema *schema = m_schema.GetSchema(obj.type);
        if (!schema)
            return;
        for (const auto &fd : schema->fields)
            if (fd.hasDefault && obj.props.find(fd.key) == obj.props.end())
                obj.props[fd.key] = fd.defaultValue;
    }

    void EditorLayer::ApplyComponentSchemaDefaults(ComponentDesc &component) const
    {
        const ComponentSchema *schema = m_schema.GetComponentSchema(component.type);
        if (!schema)
            return;
        for (const FieldDef &field : schema->fields)
        {
            if (field.hasDefault && component.props.find(field.key) == component.props.end())
                component.props[field.key] = field.defaultValue;
        }
    }

    SceneObject EditorLayer::MakeObjectFromAsset(const SceneDocument &doc,
                                                 const std::string &assetId,
                                                 const EditorSchema &schema)
    {
        SceneObject obj;
        obj.id = GenerateId(doc);
        obj.type = SceneObjectType::Prop;
        obj.assetId = assetId;
        const auto assetIt = doc.assets.find(assetId);
        if (assetIt != doc.assets.end())
            obj.props["_assetRenderScale"] = assetIt->second.renderScale.empty() ? "1.0000,1.0000,1.0000"
                                                                                 : assetIt->second.renderScale;

        const TypeSchema *typeSchema = schema.GetSchema(obj.type);
        if (typeSchema)
        {
            for (const auto &fd : typeSchema->fields)
                obj.props[fd.key] = fd.defaultValue;
        }

        return obj;
    }

    SceneObject EditorLayer::DuplicateObject(const SceneDocument &doc, const SceneObject &src)
    {
        SceneObject clone = src;
        clone.id = GenerateId(doc);
        clone.props.erase("_eid");
        return clone;
    }

    std::string EditorLayer::GenerateId(const SceneDocument &doc)
    {
        const std::unordered_set<std::string> existingIds = CollectReservedObjectIds(doc);

        for (int i = 0; i < 1000000; ++i)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "obj_%03d", i);
            if (existingIds.find(buf) == existingIds.end())
                return buf;
        }
        return "obj_new";
    }

    std::string EditorLayer::GenerateCameraId(const SceneDocument &doc)
    {
        const std::unordered_set<std::string> existingIds = CollectReservedObjectIds(doc);

        for (int i = 0; i < 1000000; ++i)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "cam_%03d", i);
            if (existingIds.find(buf) == existingIds.end())
                return buf;
        }
        return "cam_new";
    }

} // namespace Monolith::Editor
