/** @file EditorSelectionRules.h
 *  @brief Pure selection and identity validation rules extracted from EditorLayer.
 */
#pragma once
// Pure selection and identity validation rules extracted from EditorLayer.
// No ImGui or GL dependencies; fully unit-testable.

#include <string>
#include <vector>

#include "ui/editor/SceneDocument.h"

namespace Horo::Editor {

/** @brief Validates a proposed rename of the object at targetIndex to draftId.
 *
 *  Does not modify the document.
 *
 *  @param doc         Scene document providing the full object and prop context.
 *  @param targetIndex Index of the object being renamed in doc.objects.
 *  @param draftId     Proposed new ID string.
 *  @return An empty string on success, or a human-readable error message on failure.
 */
std::string ValidateRenameCandidate(const SceneDocument &doc, int targetIndex,
                                    const std::string &draftId);

/** @brief Returns the IDs of all objects that are valid parent choices for the given object.
 *
 *  Excludes the object itself and all of its descendants.
 *
 *  @param doc        Scene document providing the full object hierarchy.
 *  @param primaryIdx Index of the object for which parent candidates are collected.
 *  @return Vector of object ID strings that may legally serve as a parent.
 */
std::vector<std::string> CollectParentCandidates(const SceneDocument &doc,
                                                 int primaryIdx);

/** @brief Generates a unique object ID with the given prefix that does not collide with existing IDs.
 *
 *  Falls back to "&lt;prefix&gt;_new" if the ID space is exhausted.
 *
 *  @param doc    Scene document whose objects and object-reference props define reserved IDs.
 *  @param prefix String prefix used to construct candidate IDs.
 *  @return A unique ID string safe to insert into doc.
 */
std::string GenerateUniqueId(const SceneDocument &doc, std::string_view prefix);

} // namespace Horo::Editor
