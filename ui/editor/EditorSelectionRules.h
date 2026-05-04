#pragma once
// Pure selection and identity validation rules extracted from EditorLayer.
// No ImGui or GL dependencies; fully unit-testable.

#include <string>
#include <vector>

#include "ui/editor/SceneDocument.h"

namespace Horo::Editor {

// Validates a proposed rename of the object at targetIndex to draftId.
// Returns an empty string on success; returns a human-readable error message
// on failure.  Does not modify the document.
std::string ValidateRenameCandidate(const SceneDocument &doc, int targetIndex,
                                    const std::string &draftId);

// Returns the IDs of all objects that are valid parent choices for the object
// at primaryIdx (excludes itself and all its descendants).
std::vector<std::string> CollectParentCandidates(const SceneDocument &doc,
                                                 int primaryIdx);

// Generates a unique object ID with the given prefix that does not collide with
// any existing object or object-reference prop in doc.
// Falls back to "<prefix>_new" if the space is exhausted.
std::string GenerateUniqueId(const SceneDocument &doc, std::string_view prefix);

} // namespace Horo::Editor
