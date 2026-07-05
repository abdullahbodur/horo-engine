/**
 * @file EditorSelectionRules.cpp
 * @brief Rename validation, parent-choice enumeration, and unique ID generation for scene objects.
 */
#include "ui/editor/EditorSelectionRules.h"

#include <format>
#include <string>
#include <vector>

#include "ui/editor/EditorSceneGraph.h"
#include "ui/editor/SceneDocument.h"

namespace Horo::Editor {

/** @copydoc ValidateRenameCandidate */
std::string ValidateRenameCandidate(const SceneDocument &doc, int targetIndex,
                                    const std::string &draftId) {
  if (targetIndex < 0 || targetIndex >= static_cast<int>(doc.objects.size())) {
    return "Selected object is no longer valid.";
  }
  if (draftId.empty()) {
    return "ID cannot be empty.";
  }
  if (const int existingIdx = FindObjectIndexById(doc, draftId);
      existingIdx >= 0 && existingIdx != targetIndex) {
    return "ID already exists.";
  }
  return {};
}

/** @copydoc CollectParentCandidates */
std::vector<std::string> CollectParentCandidates(const SceneDocument &doc,
                                                 int primaryIdx) {
  std::vector<std::string> result;
  for (int i = 0; i < static_cast<int>(doc.objects.size()); ++i) {
    if (i == primaryIdx || IsDescendantOf(doc, i, primaryIdx))
      continue;
    result.push_back(doc.objects[static_cast<size_t>(i)].id);
  }
  return result;
}

/** @copydoc GenerateUniqueId */
std::string GenerateUniqueId(const SceneDocument &doc,
                             std::string_view prefix) {
  const auto reservedIds = CollectReservedObjectIds(doc);
  for (int i = 0; i < 1'000'000; ++i) {
    std::string candidate = std::format("{}_{:03d}", prefix, i);
    if (!reservedIds.contains(candidate))
      return candidate;
  }
  return std::format("{}_new", prefix);
}

} // namespace Horo::Editor
