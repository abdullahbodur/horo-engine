#pragma once

#include <string>

#include "editor/SceneDocument.h"

namespace Monolith {
namespace Editor {

struct ShortcutRow {
  const char* category;
  const char* command;
  const char* keys;
};

enum class FilteredListState {
  None,
  EmptyData,
  NoMatches,
};

const char* ObjectTypeLabel(SceneObjectType type);
bool ContainsCaseInsensitive(const std::string& textRaw, const std::string& queryRaw);
bool MatchesShortcutQuery(const ShortcutRow& row, const std::string& queryRaw);
bool ObjectMatchesQuickOpenQuery(const SceneObject& obj, const std::string& queryRaw);
bool AssetMatchesQuickOpenQuery(const std::string& assetId, const AssetDef& asset, const std::string& queryRaw);
FilteredListState EvaluateFilteredListState(size_t totalCount, int shownCount, const std::string& queryRaw);

}  // namespace Editor
}  // namespace Monolith
