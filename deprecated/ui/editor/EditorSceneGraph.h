/** @file EditorSceneGraph.h
 *  @brief Scene-graph utilities that operate on SceneDocument without requiring ImGui or renderer state.
 */
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
#include "ui/editor/SceneDocument.h"
#include "math/Quaternion.h"
#include "math/Vec3.h"

namespace Horo::Editor {
    // Returns true when propKey is a prop that holds a reference to another
    // scene object's id (e.g. "parentId", "followTargetId").
    /** @brief Returns true when a property key holds a reference to another scene object's ID.
     *  @param key Property key to test (e.g. "parentId", "followTargetId").
     *  @return True if the key is a known object-reference property.
     */
    bool IsObjectReferencePropKey(std::string_view key);

    // Returns the "parentId" prop value of obj, or an empty string_view when unset.
    /** @brief Returns the parentId property value of an object.
     *  @param obj The scene object to query.
     *  @return The parentId string_view, or an empty string_view when unset.
     */
    std::string_view GetParentId(const SceneObject &obj);

    // Returns the index of the object with the given id in doc.objects, or -1.
    /** @brief Finds the index of an object by ID within the document's object list.
     *  @param doc Scene document to search.
     *  @param id  Object ID to look up.
     *  @return Zero-based index into doc.objects, or -1 if not found.
     */
    int FindObjectIndexById(const SceneDocument &doc, std::string_view id);

    // Returns true when nodeIdx is a descendant of ancestorIdx in the hierarchy.
    // Both indices must be valid indices into doc.objects.
    /** @brief Returns true when nodeIdx is a descendant of ancestorIdx in the hierarchy.
     *  @param doc         Scene document containing the object list.
     *  @param nodeIdx     Index of the potential descendant; must be a valid index.
     *  @param ancestorIdx Index of the potential ancestor; must be a valid index.
     *  @return True if nodeIdx is a direct or indirect child of ancestorIdx.
     */
    bool IsDescendantOf(const SceneDocument &doc, int nodeIdx, int ancestorIdx);

    /** @brief Snapshot of a parent's position and rotation used by PropagateHierarchyTransformDelta. */
    struct ParentTransformState {
        Vec3 position{};       /**< World-space position of the parent. */
        Quaternion rotation{}; /**< World-space rotation of the parent. */
    };

    // After the parent at parentIdx is moved from oldParent to newParent, apply
    // the same rigid-body delta to every descendant in doc.objects. Objects in
    // skipIndices are left untouched (used for multi-select gizmo moves).
    /** @brief Propagates a rigid-body transform delta from a parent to all its descendants.
     *  @param doc         Scene document containing the object list.
     *  @param parentIdx   Index of the object that was moved.
     *  @param oldParent   Transform state of the parent before the move.
     *  @param newParent   Transform state of the parent after the move.
     *  @param transformCb Callback invoked for each descendant after its transform is updated.
     *  @param skipIndices Indices to leave untouched (e.g. other selected objects in a multi-select move).
     */
    void PropagateHierarchyTransformDelta(
        SceneDocument &doc, int parentIdx, const ParentTransformState &oldParent,
        const ParentTransformState &newParent,
        const std::function<void(const SceneObject &)> &transformCb,
        const std::vector<int> &skipIndices = {});

    // Collects all ids that are either object ids or referenced by object-reference
    // props (parentId, followTargetId).  Used for uniqueness checks.
    /** @brief Collects all object IDs and all IDs referenced by object-reference props in the document.
     *  @param doc Scene document to scan.
     *  @return Set of all reserved ID strings used for uniqueness checks.
     */
    std::unordered_set<std::string, StringHash, std::equal_to<> >
    CollectReservedObjectIds(const SceneDocument &doc);

    // Returns true when id is already used as an object id or referenced by any
    // object-reference prop in doc.  ignoreConcreteObjectId lets the caller
    // exclude one specific object's own id (for rename-in-place checks).
    /** @brief Returns true when an ID is already in use within the document.
     *  @param doc                    Scene document to check.
     *  @param id                     Candidate ID string.
     *  @param ignoreConcreteObjectId When non-null, excludes this specific object ID from the check.
     *  @return True if id collides with an existing object ID or object-reference prop value.
     */
    bool IsReservedObjectId(const SceneDocument &doc, std::string_view id,
                            const std::string *ignoreConcreteObjectId = nullptr);

    // Rewrites all object-reference props whose current value is oldId to newId.
    /** @brief Rewrites all object-reference props that point to oldId so they point to newId instead.
     *  @param doc   Scene document to update in place.
     *  @param oldId ID value to replace.
     *  @param newId Replacement ID value.
     */
    void RewriteObjectIdReferences(SceneDocument *doc, std::string_view oldId,
                                   std::string_view newId);

    // Logs a warning for every object-reference prop that points to an id that
    // does not exist in doc.objects.
    /** @brief Logs a warning for every dangling object-reference prop in the document.
     *  @param doc         Scene document to validate.
     *  @param sourceLabel Label prepended to each log warning to identify the call site.
     */
    void LogDanglingObjectReferences(const SceneDocument &doc,
                                     std::string_view sourceLabel);

    // Strips characters that are not alphanumeric, '_', or '-' from a prefab stem
    // and removes leading/trailing underscores.  Falls back to "prefab" when empty.
    /** @brief Sanitises a prefab stem to contain only alphanumeric characters, underscores, and hyphens.
     *  @param value Raw stem string to sanitise.
     *  @return Sanitised stem, or "prefab" if the result would be empty.
     */
    std::string SanitizePrefabStem(std::string value);

    // Returns a unique .horo path in assets/prefabs/ for the given object.
    /** @brief Builds a unique prefab file path in assets/prefabs/ for the given object.
     *  @param doc    Scene document used to check for path collisions.
     *  @param object Scene object whose ID seeds the file name.
     *  @return Absolute path to a non-colliding .horo prefab file.
     */
    std::filesystem::path BuildUniquePrefabPath(const SceneDocument &doc,
                                                const SceneObject &object);
} // namespace Horo::Editor
