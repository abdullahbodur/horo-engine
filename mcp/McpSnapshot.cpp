#include "mcp/McpSnapshot.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>
#include <unordered_map>

#include "math/MathUtils.h"
#include "math/Quaternion.h"
#include "math/Transform.h"

namespace Horo::Mcp {
    using json = nlohmann::json;

    namespace {
        constexpr size_t kMaxSummaryObjects = 64;
        constexpr size_t kMaxSummaryAssets = 64;
        constexpr size_t kMaxSummaryConsoleLines = 100;

        // Transparent hasher for unordered_map – allows lookup by string_view without
        // constructing a std::string key.
        struct StringHash {
            using is_transparent = void;

            std::size_t operator()(std::string_view sv) const noexcept {
                return std::hash<std::string_view>{}(sv);
            }
        };

        std::string ToLowerAscii(std::string value) {
            for (char &c: value)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return value;
        }

        bool ContainsCaseInsensitive(const std::string &haystack,
                                     const std::string &needle) {
            if (needle.empty())
                return true;
            return ToLowerAscii(haystack).find(ToLowerAscii(needle)) != std::string::npos;
        }

        size_t ClampLimit(size_t requested, size_t fallback, size_t maximum) {
            if (requested == 0)
                return fallback;
            return std::max<size_t>(1, std::min(requested, maximum));
        }

        size_t ClampOffset(size_t requested, size_t total) {
            return std::min(requested, total);
        }

        json Vec3ToJson(const Vec3 &value) {
            return json::array({value.x, value.y, value.z});
        }

        Transform BuildObjectTransform(const McpObjectSnapshot &object) {
            return Transform(object.position,
                             Quaternion::FromEuler(ToRadians(object.pitch),
                                                   ToRadians(object.yaw),
                                                   ToRadians(object.roll)),
                             object.scale);
        }

        std::string GetParentId(const McpObjectSnapshot &object) {
            const auto it = object.props.find("parentId");
            if (it == object.props.end())
                return {};
            return it->second;
        }

        json BuildObjectSummaryJson(const McpObjectSnapshot &object) {
            json out = json::object();
            out["id"] = object.id;
            out["type"] = object.type;
            out["position"] = Vec3ToJson(object.position);
            out["scale"] = Vec3ToJson(object.scale);
            out["yaw"] = object.yaw;
            out["pitch"] = object.pitch;
            out["roll"] = object.roll;
            if (!object.assetId.empty())
                out["assetId"] = object.assetId;
            if (const std::string parentId = GetParentId(object); !parentId.empty())
                out["parentId"] = parentId;

            std::vector<std::string> componentTypes;
            for (const McpComponentSnapshot &component: object.components)
                componentTypes.push_back(component.type);
            std::ranges::sort(componentTypes);
            if (!componentTypes.empty())
                out["components"] = componentTypes;

            std::vector<std::string> propKeys;
            for (const auto &[key, value]: object.props) {
                if (key != "parentId")
                    propKeys.push_back(key);
            }
            std::ranges::sort(propKeys);
            if (propKeys.size() > 8)
                propKeys.resize(8);
            if (!propKeys.empty())
                out["propKeys"] = propKeys;

            return out;
        }

        json BuildAssetSummaryJson(const McpAssetSnapshot &asset,
                                   size_t objectReferenceCount = 0) {
            auto out = json{
                {"id", asset.id},
                {"mesh", asset.mesh},
                {"renderScale", asset.renderScale},
                {"albedoMap", asset.albedoMap},
            };
            if (objectReferenceCount > 0)
                out["objectReferenceCount"] = objectReferenceCount;
            return out;
        }

        json BuildConsoleEntryJson(const McpConsoleEntry &entry) {
            return json{
                {"time", entry.timeText},
                {"level", entry.level},
                {"message", entry.message},
            };
        }

        json BuildBuildIssueJson(const McpBuildIssueSnapshot &issue) {
            return json{
                {"stage", issue.stage},
                {"severity", issue.severity},
                {"path", issue.path},
                {"message", issue.message},
            };
        }

        std::string NormalizeSchemaKind(std::string value) {
            value = ToLowerAscii(std::move(value));
            if (value.empty() || value == "all")
                return "all";
            if (value == "object" || value == "objects" || value == "type" ||
                value == "types")
                return "object";
            if (value == "component" || value == "components")
                return "component";
            return value;
        }

        json BuildSchemaFieldJson(const McpSchemaFieldSnapshot &field) {
            auto out = json{
                {"key", field.key},
                {"label", field.label},
                {"description", field.description},
                {"widget", field.widget},
                {"hasDefault", field.hasDefault},
                {"default", field.defaultValue},
                {"required", field.required},
                {"allowEmpty", field.allowEmpty},
                {"allowCustomValue", field.allowCustomValue},
            };
            if (!field.options.empty())
                out["options"] = field.options;
            if (field.hasMin || field.hasMax) {
                json numeric = json::object();
                if (field.hasMin)
                    numeric["min"] = field.minVal;
                if (field.hasMax)
                    numeric["max"] = field.maxVal;
                out["numeric"] = std::move(numeric);
            }
            return out;
        }

        json BuildSchemaEntrySummaryJson(const McpSchemaEntrySnapshot &entry) {
            auto out = json{
                {"kind", entry.kind},
                {"name", entry.name},
                {"label", entry.label},
                {"fieldCount", entry.fields.size()},
            };
            if (!entry.appliesTo.empty())
                out["appliesTo"] = entry.appliesTo;
            return out;
        }

        json BuildSchemaEntryJson(const McpSchemaEntrySnapshot &entry) {
            json fields = json::array();
            for (const McpSchemaFieldSnapshot &field: entry.fields)
                fields.push_back(BuildSchemaFieldJson(field));

            json out = BuildSchemaEntrySummaryJson(entry);
            out["fields"] = std::move(fields);
            return out;
        }

        const McpSchemaEntrySnapshot *
        FindSchemaEntry(const std::vector<McpSchemaEntrySnapshot> &entries,
                        const std::string &name) {
            const std::string normalizedName = ToLowerAscii(name);
            for (const McpSchemaEntrySnapshot &entry: entries) {
                if (ToLowerAscii(entry.name) == normalizedName)
                    return &entry;
            }
            return nullptr;
        }

        bool ObjectMatchesQuery(const McpObjectSnapshot &object,
                                const std::string &query) {
            if (ContainsCaseInsensitive(object.id, query) ||
                ContainsCaseInsensitive(object.type, query) ||
                ContainsCaseInsensitive(object.assetId, query) ||
                ContainsCaseInsensitive(GetParentId(object), query)) {
                return true;
            }
            for (const auto &[key, value]: object.props) {
                if (ContainsCaseInsensitive(key, query) ||
                    ContainsCaseInsensitive(value, query))
                    return true;
            }
            for (const McpComponentSnapshot &component: object.components) {
                if (ContainsCaseInsensitive(component.type, query))
                    return true;
                for (const auto &[compKey, compValue]: component.props) {
                    if (ContainsCaseInsensitive(compKey, query) ||
                        ContainsCaseInsensitive(compValue, query))
                        return true;
                }
            }
            return false;
        }

        bool AssetMatchesQuery(const McpAssetSnapshot &asset,
                               const std::string &query) {
            return ContainsCaseInsensitive(asset.id, query) ||
                   ContainsCaseInsensitive(asset.mesh, query) ||
                   ContainsCaseInsensitive(asset.renderScale, query) ||
                   ContainsCaseInsensitive(asset.albedoMap, query);
        }

        bool ConsoleMatchesQuery(const McpConsoleEntry &entry,
                                 const std::string &query) {
            return ContainsCaseInsensitive(entry.timeText, query) ||
                   ContainsCaseInsensitive(entry.level, query) ||
                   ContainsCaseInsensitive(entry.message, query);
        }

        size_t CountAssetReferences(const McpEditorSnapshot &snapshot,
                                    std::string_view assetId) {
            size_t count = 0;
            for (const McpObjectSnapshot &object: snapshot.objects) {
                if (object.assetId == assetId)
                    ++count;
            }
            return count;
        }
    } // namespace

    std::shared_ptr<const McpEditorSnapshot>
    CloneSnapshot(const McpEditorSnapshot &snapshot) {
        return std::make_shared<const McpEditorSnapshot>(snapshot);
    }

    const McpObjectSnapshot *FindObjectById(const McpEditorSnapshot &snapshot,
                                            std::string_view id) {
        for (const McpObjectSnapshot &object: snapshot.objects) {
            if (object.id == id)
                return &object;
        }
        return nullptr;
    }

    const McpAssetSnapshot *FindAssetById(const McpEditorSnapshot &snapshot,
                                          std::string_view id) {
        for (const McpAssetSnapshot &asset: snapshot.assets) {
            if (asset.id == id)
                return &asset;
        }
        return nullptr;
    }

    json BuildSceneSummaryJson(const McpEditorSnapshot &snapshot,
                               size_t objectLimit) {
        json out = BuildSceneStatusJson(snapshot);
        json objects = json::array();
        const size_t count = std::min(ClampLimit(objectLimit, 12, kMaxSummaryObjects),
                                      snapshot.objects.size());
        for (size_t i = 0; i < count; ++i)
            objects.push_back(BuildObjectSummaryJson(snapshot.objects[i]));
        out["objects"] = std::move(objects);
        out["moreObjects"] =
                snapshot.objects.size() > count ? snapshot.objects.size() - count : 0;
        return out;
    }

    json BuildSceneStatusJson(const McpEditorSnapshot &snapshot) {
        return json{
            {"sceneId", snapshot.sceneId},
            {"sceneName", snapshot.sceneName},
            {"filePath", snapshot.sceneFilePath},
            {"editorActive", snapshot.editorActive},
            {"playMode", snapshot.playMode},
            {"dirty", snapshot.dirty},
            {"reloadPending", snapshot.reloadPending},
            {"objectCount", snapshot.objects.size()},
            {"assetCount", snapshot.assets.size()},
            {"selectedObjectIds", snapshot.selectedObjectIds},
            {"selectedAssetId", snapshot.selectedAssetId},
        };
    }

    json BuildSelectionJson(const McpEditorSnapshot &snapshot) {
        json out = json::object();
        out["selectedObjectIds"] = snapshot.selectedObjectIds;
        out["selectedAssetId"] = snapshot.selectedAssetId;
        json objects = json::array();
        for (const std::string &id: snapshot.selectedObjectIds) {
            const McpObjectSnapshot *object = FindObjectById(snapshot, id);
            if (object)
                objects.push_back(BuildObjectSummaryJson(*object));
        }
        out["objects"] = std::move(objects);
        if (const McpAssetSnapshot *asset =
                FindAssetById(snapshot, snapshot.selectedAssetId))
            out["asset"] = BuildAssetSummaryJson(
                *asset, CountAssetReferences(snapshot, asset->id));
        return out;
    }

    json BuildAssetsJson(const McpEditorSnapshot &snapshot, size_t assetLimit) {
        json out = json::object();
        out["assetCount"] = snapshot.assets.size();

        json assets = json::array();
        const size_t count = std::min(ClampLimit(assetLimit, 12, kMaxSummaryAssets),
                                      snapshot.assets.size());
        for (size_t i = 0; i < count; ++i) {
            const McpAssetSnapshot &asset = snapshot.assets[i];
            assets.push_back(
                BuildAssetSummaryJson(asset, CountAssetReferences(snapshot, asset.id)));
        }
        out["assets"] = std::move(assets);
        out["moreAssets"] =
                snapshot.assets.size() > count ? snapshot.assets.size() - count : 0;
        return out;
    }

    json BuildAssetsSelectionJson(const McpEditorSnapshot &snapshot) {
        auto out = json{
            {"selectedAssetId", snapshot.selectedAssetId},
            {"hasSelection", !snapshot.selectedAssetId.empty()},
        };
        if (const McpAssetSnapshot *asset =
                FindAssetById(snapshot, snapshot.selectedAssetId))
            out["asset"] = BuildAssetSummaryJson(
                *asset, CountAssetReferences(snapshot, asset->id));
        return out;
    }

    json BuildAssetsCatalogJson(const McpEditorSnapshot &snapshot,
                                size_t assetLimit, const std::string &query,
                                size_t offset) {
        const size_t safeLimit = ClampLimit(assetLimit, 12, kMaxSummaryAssets);
        std::vector<json> matchedAssets;
        matchedAssets.reserve(snapshot.assets.size());
        size_t totalMatches = 0;
        for (const McpAssetSnapshot &asset: snapshot.assets) {
            if (!AssetMatchesQuery(asset, query))
                continue;
            ++totalMatches;
            matchedAssets.push_back(
                BuildAssetSummaryJson(asset, CountAssetReferences(snapshot, asset.id)));
        }

        const size_t safeOffset = ClampOffset(offset, matchedAssets.size());
        json assets = json::array();
        const size_t end = std::min(safeOffset + safeLimit, matchedAssets.size());
        for (size_t i = safeOffset; i < end; ++i)
            assets.push_back(std::move(matchedAssets[i]));
        const size_t returned = assets.size();
        const bool hasMore = end < matchedAssets.size();

        return json{
            {"query", query},
            {"offset", safeOffset},
            {"limit", safeLimit},
            {"returned", returned},
            {"hasMore", hasMore},
            {"assetCount", snapshot.assets.size()},
            {"matchedAssets", totalMatches},
            {"assets", std::move(assets)},
            {
                "moreAssets", totalMatches > safeOffset + returned
                                  ? totalMatches - safeOffset - returned
                                  : 0
            },
        };
    }

    json BuildConsoleJson(const McpEditorSnapshot &snapshot, size_t lineLimit,
                          size_t offset) {
        const size_t safeLimit = ClampLimit(lineLimit, 20, kMaxSummaryConsoleLines);
        const size_t safeOffset = ClampOffset(offset, snapshot.consoleEntries.size());
        const size_t remaining = snapshot.consoleEntries.size() - safeOffset;
        const size_t count = std::min(safeLimit, remaining);

        json lines = json::array();
        const size_t startIndex = snapshot.consoleEntries.size() - safeOffset - count;
        for (size_t i = startIndex; i < startIndex + count; ++i)
            lines.push_back(BuildConsoleEntryJson(snapshot.consoleEntries[i]));
        const bool hasMore = safeOffset + count < snapshot.consoleEntries.size();

        json out = json::object();
        out["offset"] = safeOffset;
        out["limit"] = safeLimit;
        out["returned"] = count;
        out["hasMore"] = hasMore;
        out["lineCount"] = snapshot.consoleEntries.size();
        out["lines"] = std::move(lines);
        out["truncated"] = hasMore;
        return out;
    }

    json BuildConsoleSummaryJson(const McpEditorSnapshot &snapshot,
                                 size_t lineLimit) {
        size_t infoCount = 0;
        size_t warnCount = 0;
        size_t errorCount = 0;
        for (const McpConsoleEntry &entry: snapshot.consoleEntries) {
            const std::string level = ToLowerAscii(entry.level);
            if (level == "info")
                ++infoCount;
            else if (level == "warn" || level == "warning")
                ++warnCount;
            else if (level == "error")
                ++errorCount;
        }

        auto out = json{
            {"lineCount", snapshot.consoleEntries.size()},
            {"infoCount", infoCount},
            {"warnCount", warnCount},
            {"errorCount", errorCount},
        };
        out["recent"] = BuildConsoleJson(snapshot, lineLimit)["lines"];
        return out;
    }

    json BuildBuildStatusJson(const McpEditorSnapshot &snapshot,
                              size_t issueLimit) {
        const size_t safeLimit = ClampLimit(issueLimit, 5, 25);
        json issues = json::array();
        const size_t count = std::min(safeLimit, snapshot.build.issues.size());
        for (size_t i = 0; i < count; ++i)
            issues.push_back(BuildBuildIssueJson(snapshot.build.issues[i]));

        return json{
            {"available", snapshot.build.available},
            {"source", snapshot.build.source},
            {"status", snapshot.build.status},
            {"assetCount", snapshot.build.assetCount},
            {"nodeCount", snapshot.build.nodeCount},
            {"sceneValidationErrors", snapshot.build.sceneValidationErrors},
            {"sceneValidationWarnings", snapshot.build.sceneValidationWarnings},
            {"runtimeBuildErrors", snapshot.build.runtimeBuildErrors},
            {"runtimeBuildWarnings", snapshot.build.runtimeBuildWarnings},
            {"roomCount", snapshot.build.roomCount},
            {"panelCount", snapshot.build.panelCount},
            {"propCount", snapshot.build.propCount},
            {"lightCount", snapshot.build.lightCount},
            {"hasSceneCamera", snapshot.build.hasSceneCamera},
            {"issueCount", snapshot.build.issues.size()},
            {"issues", std::move(issues)},
            {
                "moreIssues", snapshot.build.issues.size() > count
                                  ? snapshot.build.issues.size() - count
                                  : 0
            },
        };
    }

    json BuildSchemaCatalogJson(const McpEditorSnapshot &snapshot,
                                const std::string &kindFilter) {
        const std::string normalizedKind = NormalizeSchemaKind(kindFilter);
        json entries = json::array();

        if (normalizedKind == "all" || normalizedKind == "object") {
            for (const McpSchemaEntrySnapshot &entry: snapshot.schema.objectTypes)
                entries.push_back(BuildSchemaEntrySummaryJson(entry));
        }
        if (normalizedKind == "all" || normalizedKind == "component") {
            for (const McpSchemaEntrySnapshot &entry: snapshot.schema.components)
                entries.push_back(BuildSchemaEntrySummaryJson(entry));
        }

        return json{
            {"kind", normalizedKind},
            {"objectTypeCount", snapshot.schema.objectTypes.size()},
            {"componentCount", snapshot.schema.components.size()},
            {"entryCount", entries.size()},
            {"entries", std::move(entries)},
        };
    }

    json BuildSchemaJson(const McpEditorSnapshot &snapshot, const std::string &name,
                         const std::string &kindFilter) {
        const std::string normalizedKind = NormalizeSchemaKind(kindFilter);
        if (normalizedKind == "all" || normalizedKind == "object") {
            if (const McpSchemaEntrySnapshot *entry =
                    FindSchemaEntry(snapshot.schema.objectTypes, name))
                return BuildSchemaEntryJson(*entry);
        }
        if (normalizedKind == "all" || normalizedKind == "component") {
            if (const McpSchemaEntrySnapshot *entry =
                    FindSchemaEntry(snapshot.schema.components, name))
                return BuildSchemaEntryJson(*entry);
        }
        return json::object();
    }

    json BuildObjectListJson(const McpEditorSnapshot &snapshot, size_t objectLimit,
                             const std::string &typeFilter,
                             const std::string &query, bool selectedOnly,
                             size_t offset) {
        const size_t safeLimit = ClampLimit(objectLimit, 12, kMaxSummaryObjects);
        const std::string normalizedType = ToLowerAscii(typeFilter);
        std::vector<json> matchedObjects;
        matchedObjects.reserve(snapshot.objects.size());
        size_t totalMatches = 0;

        for (const McpObjectSnapshot &object: snapshot.objects) {
            if (!normalizedType.empty() && ToLowerAscii(object.type) != normalizedType)
                continue;
            if (selectedOnly &&
                std::ranges::find(snapshot.selectedObjectIds, object.id) ==
                snapshot.selectedObjectIds.end()) {
                continue;
            }
            if (!ObjectMatchesQuery(object, query))
                continue;

            ++totalMatches;
            matchedObjects.push_back(BuildObjectSummaryJson(object));
        }
        const size_t safeOffset = ClampOffset(offset, matchedObjects.size());
        json objects = json::array();
        const size_t end = std::min(safeOffset + safeLimit, matchedObjects.size());
        for (size_t i = safeOffset; i < end; ++i)
            objects.push_back(std::move(matchedObjects[i]));
        const size_t returned = objects.size();

        return json{
            {"query", query},
            {"type", typeFilter},
            {"selectedOnly", selectedOnly},
            {"offset", safeOffset},
            {"limit", safeLimit},
            {"returned", returned},
            {"hasMore", end < matchedObjects.size()},
            {"matchedObjects", totalMatches},
            {"objects", std::move(objects)},
            {
                "moreObjects", totalMatches > safeOffset + returned
                                   ? totalMatches - safeOffset - returned
                                   : 0
            },
        };
    }

    json BuildHierarchyJson(const McpEditorSnapshot &snapshot, size_t objectLimit,
                            size_t offset) {
        const size_t safeLimit = ClampLimit(objectLimit, 32, kMaxSummaryObjects);
        std::unordered_map<std::string, std::vector<const McpObjectSnapshot *>,
                    StringHash, std::equal_to<> >
                children;
        std::vector<const McpObjectSnapshot *> roots;
        roots.reserve(snapshot.objects.size());

        for (const McpObjectSnapshot &object: snapshot.objects) {
            const std::string parentId = GetParentId(object);
            if (parentId.empty() || !FindObjectById(snapshot, parentId))
                roots.push_back(&object);
            else
                children[parentId].push_back(&object);
        }

        std::vector<json> flattenedEntries;
        flattenedEntries.reserve(snapshot.objects.size());
        std::function<void(const McpObjectSnapshot *, int)> visit =
                [&](const McpObjectSnapshot *object, int depth) {
            if (!object)
                return;
            json item = BuildObjectSummaryJson(*object);
            item["depth"] = depth;
            item["childCount"] = children[object->id].size();
            flattenedEntries.push_back(std::move(item));
            for (const McpObjectSnapshot *child: children[object->id])
                visit(child, depth + 1);
        };

        for (const McpObjectSnapshot *root: roots)
            visit(root, 0);

        const size_t safeOffset = ClampOffset(offset, flattenedEntries.size());
        json entries = json::array();
        const size_t end = std::min(safeOffset + safeLimit, flattenedEntries.size());
        for (size_t i = safeOffset; i < end; ++i)
            entries.push_back(std::move(flattenedEntries[i]));
        const size_t returned = entries.size();

        return json{
            {"objectCount", snapshot.objects.size()},
            {"roots", roots.size()},
            {"offset", safeOffset},
            {"limit", safeLimit},
            {"returned", returned},
            {"hasMore", end < flattenedEntries.size()},
            {"entries", std::move(entries)},
            {
                "moreObjects", snapshot.objects.size() > safeOffset + returned
                                   ? snapshot.objects.size() - safeOffset - returned
                                   : 0
            },
        };
    }

    json BuildObjectJson(const McpObjectSnapshot &object) {
        json props = json::object();
        for (const auto &[key, value]: object.props)
            props[key] = value;

        json components = json::array();
        for (const McpComponentSnapshot &component: object.components) {
            json componentProps = json::object();
            for (const auto &[compKey, compValue]: component.props)
                componentProps[compKey] = compValue;
            components.push_back(json{
                {"type", component.type},
                {"props", std::move(componentProps)},
            });
        }

        auto out = json{
            {"id", object.id},
            {"type", object.type},
            {"position", Vec3ToJson(object.position)},
            {"scale", Vec3ToJson(object.scale)},
            {"yaw", object.yaw},
            {"pitch", object.pitch},
            {"roll", object.roll},
            {"assetId", object.assetId},
            {"props", std::move(props)},
            {"components", std::move(components)},
        };
        if (const std::string parentId = GetParentId(object); !parentId.empty())
            out["parentId"] = parentId;
        return out;
    }

    json BuildObjectEdgesJson(const McpObjectSnapshot &object) {
        static const std::array<Vec3, 8> kLocalCorners = {
            {
                Vec3(-0.5f, -0.5f, -0.5f),
                Vec3(0.5f, -0.5f, -0.5f),
                Vec3(0.5f, 0.5f, -0.5f),
                Vec3(-0.5f, 0.5f, -0.5f),
                Vec3(-0.5f, -0.5f, 0.5f),
                Vec3(0.5f, -0.5f, 0.5f),
                Vec3(0.5f, 0.5f, 0.5f),
                Vec3(-0.5f, 0.5f, 0.5f),
            }
        };
        static const std::array<std::array<int, 2>, 12> kEdgeIndices = {
            {
                {{0, 1}},
                {{1, 2}},
                {{2, 3}},
                {{3, 0}},
                {{4, 5}},
                {{5, 6}},
                {{6, 7}},
                {{7, 4}},
                {{0, 4}},
                {{1, 5}},
                {{2, 6}},
                {{3, 7}},
            }
        };

        const Transform worldFromLocal = BuildObjectTransform(object);
        const Vec3 halfExtents(std::abs(object.scale.x) * 0.5f,
                               std::abs(object.scale.y) * 0.5f,
                               std::abs(object.scale.z) * 0.5f);

        json worldCorners = json::array();
        std::array<Vec3, 8> corners{};
        for (int i = 0; i < 8; ++i) {
            corners[static_cast<size_t>(i)] =
                    worldFromLocal.TransformPoint(kLocalCorners[static_cast<size_t>(i)]);
            worldCorners.push_back(Vec3ToJson(corners[static_cast<size_t>(i)]));
        }

        json worldEdges = json::array();
        for (const auto &edge: kEdgeIndices) {
            worldEdges.push_back(json{
                {"from", Vec3ToJson(corners[edge[0]])},
                {"to", Vec3ToJson(corners[edge[1]])},
            });
        }

        return json{
            {"id", object.id},
            {"type", object.type},
            {"basis", "object_transform_box"},
            {"position", Vec3ToJson(object.position)},
            {"scale", Vec3ToJson(object.scale)},
            {"yaw", object.yaw},
            {"pitch", object.pitch},
            {"roll", object.roll},
            {"center", Vec3ToJson(object.position)},
            {"halfExtents", Vec3ToJson(halfExtents)},
            {"worldCorners", std::move(worldCorners)},
            {"worldEdges", std::move(worldEdges)},
        };
    }

    json BuildAssetJson(const McpAssetSnapshot &asset) {
        return json{
            {"id", asset.id},
            {"mesh", asset.mesh},
            {"renderScale", asset.renderScale},
            {"albedoMap", asset.albedoMap},
        };
    }

    json SearchSnapshot(const McpEditorSnapshot &snapshot, const std::string &query,
                        size_t limit, const std::string &scope) {
        const size_t safeLimit = ClampLimit(limit, 8, 25);
        const std::string normalizedScope =
                ToLowerAscii(scope.empty() ? "all" : scope);

        json objects = json::array();
        if (normalizedScope == "all" || normalizedScope == "objects") {
            for (const McpObjectSnapshot &object: snapshot.objects) {
                if (!ObjectMatchesQuery(object, query))
                    continue;
                objects.push_back(BuildObjectSummaryJson(object));
                if (objects.size() >= safeLimit)
                    break;
            }
        }

        json assets = json::array();
        if (normalizedScope == "all" || normalizedScope == "assets") {
            for (const McpAssetSnapshot &asset: snapshot.assets) {
                if (!AssetMatchesQuery(asset, query))
                    continue;
                assets.push_back(BuildAssetSummaryJson(
                    asset, CountAssetReferences(snapshot, asset.id)));
                if (assets.size() >= safeLimit)
                    break;
            }
        }

        return json{
            {"query", query},
            {"scope", normalizedScope},
            {"objects", std::move(objects)},
            {"assets", std::move(assets)},
        };
    }

    json SearchAssetsSnapshot(const McpEditorSnapshot &snapshot,
                              const std::string &query, size_t limit) {
        return BuildAssetsCatalogJson(snapshot, ClampLimit(limit, 8, 25), query);
    }

    json SearchConsoleSnapshot(const McpEditorSnapshot &snapshot,
                               const std::string &query, size_t limit) {
        const size_t safeLimit = ClampLimit(limit, 8, 25);
        json lines = json::array();
        size_t totalMatches = 0;
        for (auto it = snapshot.consoleEntries.rbegin();
             it != snapshot.consoleEntries.rend(); ++it) {
            if (!ConsoleMatchesQuery(*it, query))
                continue;
            ++totalMatches;
            if (lines.size() >= safeLimit)
                continue;
            lines.push_back(BuildConsoleEntryJson(*it));
        }
        const size_t shownLines = lines.size();
        return json{
            {"query", query},
            {"matchedLines", totalMatches},
            {"lines", std::move(lines)},
            {"moreLines", totalMatches > shownLines ? totalMatches - shownLines : 0},
        };
    }
} // namespace Horo::Mcp
