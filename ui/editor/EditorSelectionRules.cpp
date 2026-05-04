#include "ui/editor/EditorSelectionRules.h"

#include <format>
#include <string>
#include <vector>

#include "ui/editor/EditorSceneGraph.h"
#include "ui/editor/SceneDocument.h"

namespace Horo::Editor {

std::string ValidateRenameCandidate(const SceneDocument &doc, int targetIndex,
                                    const std::string &draftId) {
  if (targetIndex < 0 || targetIndex >= static_cast<int>(doc.objects.size())) {
    return "Selected object is no longer valid.";
  }
  if (draftId.empty()) {
    return "ID cannot be empty.";
  }
  const int existingIdx = FindObjectIndexById(doc, draftId);
  if (existingIdx >= 0 && existingIdx != targetIndex) {
    return "ID already exists.";
  }
  return {};
}

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
