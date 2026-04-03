#include "editor/EditorSearch.h"

#include <algorithm>
#include <cctype>

namespace Monolith {
namespace Editor {

namespace {

std::string ToLower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

}  // namespace

const char* ObjectTypeLabel(SceneObjectType type) {
  switch (type) {
    case SceneObjectType::Prop:
      return "prop";
    case SceneObjectType::Light:
      return "light";
    case SceneObjectType::Panel:
      return "board";
    default:
      return "unknown";
  }
}

bool ContainsCaseInsensitive(const std::string& textRaw, const std::string& queryRaw) {
  if (queryRaw.empty())
    return true;
  return ToLower(textRaw).find(ToLower(queryRaw)) != std::string::npos;
}

bool MatchesShortcutQuery(const ShortcutRow& row, const std::string& queryRaw) {
  if (queryRaw.empty())
    return true;

  return ContainsCaseInsensitive(row.category, queryRaw) ||
         ContainsCaseInsensitive(row.command, queryRaw) ||
         ContainsCaseInsensitive(row.keys, queryRaw);
}

bool ObjectMatchesQuickOpenQuery(const SceneObject& obj, const std::string& queryRaw) {
  const char* typeName = ObjectTypeLabel(obj.type);
  return ContainsCaseInsensitive(obj.id + " " + typeName + " " + obj.assetId, queryRaw);
}

bool AssetMatchesQuickOpenQuery(const std::string& assetId,
                                const AssetDef& asset,
                                const std::string& queryRaw) {
  return ContainsCaseInsensitive(assetId + " " + asset.mesh, queryRaw);
}

FilteredListState EvaluateFilteredListState(size_t totalCount,
                                            int shownCount,
                                            const std::string& queryRaw) {
  if (shownCount > 0)
    return FilteredListState::None;
  if (totalCount == 0)
    return FilteredListState::EmptyData;
  if (!queryRaw.empty())
    return FilteredListState::NoMatches;
  return FilteredListState::None;
}

}  // namespace Editor
}  // namespace Monolith
