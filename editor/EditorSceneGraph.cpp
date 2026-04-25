#include "editor/EditorSceneGraph.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <functional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "core/Logger.h"
#include "core/ProjectPath.h"
#include "math/MathUtils.h"
#include "math/Quaternion.h"
#include "math/Vec3.h"

namespace Monolith::Editor {
    bool IsObjectReferencePropKey(std::string_view key) {
        return key == "parentId" || key == "followTargetId";
    }

    std::string_view GetParentId(const SceneObject &obj) {
        static constexpr std::string_view kEmpty;
        const auto it = obj.props.find("parentId");
        return (it != obj.props.end()) ? std::string_view{it->second} : kEmpty;
    }

    int FindObjectIndexById(const SceneDocument &doc, std::string_view id) {
        if (id.empty())
            return -1;
        const auto it = std::ranges::find_if(
            doc.objects, [&id](const SceneObject &o) { return o.id == id; });
        if (it == doc.objects.end())
            return -1;
        return static_cast<int>(it - doc.objects.begin());
    }

    bool IsDescendantOf(const SceneDocument &doc, int nodeIdx, int ancestorIdx) {
        if (nodeIdx < 0 || ancestorIdx < 0 ||
            nodeIdx >= static_cast<int>(doc.objects.size()) ||
            ancestorIdx >= static_cast<int>(doc.objects.size()))
            return false;

        int cur = nodeIdx;
        const auto guardLimit = static_cast<int>(doc.objects.size());
        int guard = 0;
        while (guard < guardLimit) {
            const auto p = FindObjectIndexById(
                doc, GetParentId(doc.objects[static_cast<size_t>(cur)]));
            if (p < 0)
                return false;
            if (p == ancestorIdx)
                return true;
            cur = p;
            ++guard;
        }
        return false;
    }

    void PropagateHierarchyTransformDelta(
        SceneDocument &doc, int parentIdx, const ParentTransformState &oldParent,
        const ParentTransformState &newParent,
        const std::function<void(const SceneObject &)> &transformCb,
        const std::vector<int> &skipIndices) {
        if (parentIdx < 0 || parentIdx >= static_cast<int>(doc.objects.size()))
            return;

        const Quaternion deltaRot = newParent.rotation * oldParent.rotation.Inverse();
        for (int i = 0; i < static_cast<int>(doc.objects.size()); ++i) {
            if (i == parentIdx || !IsDescendantOf(doc, i, parentIdx))
                continue;
            // Skip children that are themselves being directly manipulated (e.g.
            // multi-select gizmo).
            if (std::ranges::any_of(skipIndices, [i](int sk) { return sk == i; }))
                continue;

            SceneObject &child = doc.objects[static_cast<size_t>(i)];
            const Vec3 oldRel = child.position - oldParent.position;
            child.position = newParent.position + deltaRot * oldRel;

            const Quaternion childRot = Quaternion::FromEuler(
                ToRadians(child.pitch), ToRadians(child.yaw), ToRadians(child.roll));
            const Quaternion rotatedChild = deltaRot * childRot;
            const Vec3 e = rotatedChild.ToEuler();
            child.pitch = ToDegrees(e.x);
            child.yaw = ToDegrees(e.y);
            child.roll = ToDegrees(e.z);

            if (transformCb)
                transformCb(child);
        }
    }

    std::unordered_set<std::string, StringHash, std::equal_to<> >
    CollectReservedObjectIds(const SceneDocument &doc) {
        std::unordered_set<std::string, StringHash, std::equal_to<> > reservedIds;
        reservedIds.reserve(doc.objects.size() * 2);
        for (const SceneObject &obj: doc.objects) {
            if (!obj.id.empty())
                reservedIds.insert(obj.id);
            for (const auto &[key, val]: obj.props) {
                if (IsObjectReferencePropKey(key) && !val.empty())
                    reservedIds.insert(val);
            }
        }
        return reservedIds;
    }

    bool IsReservedObjectId(const SceneDocument &doc, std::string_view id,
                            const std::string *ignoreConcreteObjectId) {
        if (id.empty())
            return false;
        for (const SceneObject &obj: doc.objects) {
            if (!obj.id.empty() &&
                (!ignoreConcreteObjectId || obj.id != *ignoreConcreteObjectId) &&
                obj.id == id)
                return true;
            for (const auto &[key, val]: obj.props) {
                if (!IsObjectReferencePropKey(key) || val.empty())
                    continue;
                if (ignoreConcreteObjectId && val == *ignoreConcreteObjectId)
                    continue;
                if (val == id)
                    return true;
            }
        }
        return false;
    }

    void RewriteObjectIdReferences(SceneDocument *doc, std::string_view oldId,
                                   std::string_view newId) {
        if (!doc || oldId.empty() || oldId == newId)
            return;
        for (SceneObject &object: doc->objects) {
            for (auto &[key, val]: object.props) {
                if (IsObjectReferencePropKey(key) && val == oldId)
                    val = newId;
            }
        }
    }

    void LogDanglingObjectReferences(const SceneDocument &doc,
                                     std::string_view sourceLabel) {
        std::unordered_set<std::string, StringHash, std::equal_to<> > objectIds;
        objectIds.reserve(doc.objects.size() * 2);
        for (const SceneObject &object: doc.objects) {
            if (!object.id.empty())
                objectIds.insert(object.id);
        }
        for (const SceneObject &object: doc.objects) {
            for (const auto &[key, val]: object.props) {
                if (!IsObjectReferencePropKey(key) || val.empty())
                    continue;
                if (!objectIds.contains(val))
                    LogWarn("[Editor] Dangling object reference in {}: {}.{} -> {}",
                            std::string(sourceLabel), object.id, key, val);
            }
        }
    }

    std::string SanitizePrefabStem(std::string value) {
        for (char &ch: value) {
            const bool alphaNum = (ch >= 'a' && ch <= 'z') ||
                                  (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
            if (!alphaNum && ch != '_' && ch != '-')
                ch = '_';
        }
        while (!value.empty() && value.front() == '_')
            value.erase(value.begin());
        while (!value.empty() && value.back() == '_')
            value.pop_back();
        return value.empty() ? "prefab" : value;
    }

    std::filesystem::path BuildUniquePrefabPath(const SceneDocument &doc,
                                                const SceneObject &object) {
        const std::filesystem::path prefabDir =
                ProjectPath::Resolve("assets/prefabs");
        const auto stemBase = SanitizePrefabStem(
            object.id.empty() ? doc.sceneId + "_prefab" : object.id + "_prefab");
        std::filesystem::path candidate = prefabDir / (stemBase + ".horo");
        int suffix = 2;
        while (std::filesystem::exists(candidate)) {
            candidate = prefabDir / std::format("{}_{}.horo", stemBase, suffix);
            ++suffix;
        }
        return candidate;
    }
} // namespace Monolith::Editor
