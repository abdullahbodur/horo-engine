#pragma once
// Scene-graph utilities that operate on SceneDocument without requiring any
// ImGui or renderer state. Extracted from EditorLayer.cpp to keep the logic
// independently testable and easy to locate.

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "core/StringHash.h"
#include "editor/SceneDocument.h"
#include "math/Quaternion.h"
#include "math/Vec3.h"

namespace Horo::Editor {
    // Returns true when propKey is a prop that holds a reference to another
    // scene object's id (e.g. "parentId", "followTargetId").
    bool IsObjectReferencePropKey(std::string_view key);

    // Returns the "parentId" prop value of obj, or an empty string_view when unset.
    std::string_view GetParentId(const SceneObject &obj);

    // Returns the index of the object with the given id in doc.objects, or -1.
    int FindObjectIndexById(const SceneDocument &doc, std::string_view id);

    // Returns true when nodeIdx is a descendant of ancestorIdx in the hierarchy.
    // Both indices must be valid indices into doc.objects.
    bool IsDescendantOf(const SceneDocument &doc, int nodeIdx, int ancestorIdx);

    // Snapshot of a parent's position/rotation used by
    // PropagateHierarchyTransformDelta.
    struct ParentTransformState {
        Vec3 position{};
        Quaternion rotation{};
    };

    // After the parent at parentIdx is moved from oldParent to newParent, apply
    // the same rigid-body delta to every descendant in doc.objects. Objects in
    // skipIndices are left untouched (used for multi-select gizmo moves).
    void PropagateHierarchyTransformDelta(
        SceneDocument &doc, int parentIdx, const ParentTransformState &oldParent,
        const ParentTransformState &newParent,
        const std::function<void(const SceneObject &)> &transformCb,
        const std::vector<int> &skipIndices = {});

    // Collects all ids that are either object ids or referenced by object-reference
    // props (parentId, followTargetId).  Used for uniqueness checks.
    std::unordered_set<std::string, StringHash, std::equal_to<> >
    CollectReservedObjectIds(const SceneDocument &doc);

    // Returns true when id is already used as an object id or referenced by any
    // object-reference prop in doc.  ignoreConcreteObjectId lets the caller
    // exclude one specific object's own id (for rename-in-place checks).
    bool IsReservedObjectId(const SceneDocument &doc, std::string_view id,
                            const std::string *ignoreConcreteObjectId = nullptr);

    // Rewrites all object-reference props whose current value is oldId to newId.
    void RewriteObjectIdReferences(SceneDocument *doc, std::string_view oldId,
                                   std::string_view newId);

    // Logs a warning for every object-reference prop that points to an id that
    // does not exist in doc.objects.
    void LogDanglingObjectReferences(const SceneDocument &doc,
                                     std::string_view sourceLabel);

    // Strips characters that are not alphanumeric, '_', or '-' from a prefab stem
    // and removes leading/trailing underscores.  Falls back to "prefab" when empty.
    std::string SanitizePrefabStem(std::string value);

    // Returns a unique .horo path in assets/prefabs/ for the given object.
    std::filesystem::path BuildUniquePrefabPath(const SceneDocument &doc,
                                                const SceneObject &object);
} // namespace Horo::Editor
