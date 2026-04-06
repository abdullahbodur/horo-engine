#include "editor/AssetIdentity.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <random>
#include <string>

namespace Monolith {
namespace Editor {

namespace {

std::string Trim(std::string text) {
  const auto first = std::find_if_not(text.begin(), text.end(), [](unsigned char c) {
    return std::isspace(c) != 0;
  });
  const auto last = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char c) {
    return std::isspace(c) != 0;
  }).base();
  if (first >= last)
    return {};
  return std::string(first, last);
}

}  // namespace

std::string GenerateAssetGuid() {
  static thread_local std::mt19937_64 rng(std::random_device{}());
  static constexpr std::array<char, 16> kHex = {
      '0', '1', '2', '3', '4', '5', '6', '7',
      '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
  };

  std::uniform_int_distribution<uint32_t> dist(0, 0xffffffffu);
  std::string guid;
  guid.reserve(32);
  for (int chunk = 0; chunk < 4; ++chunk) {
    const uint32_t value = dist(rng);
    for (int nibble = 7; nibble >= 0; --nibble) {
      const unsigned index = static_cast<unsigned>((value >> (nibble * 4)) & 0xfu);
      guid.push_back(kHex[index]);
    }
  }
  return guid;
}

std::string MakeAssetDisplayName(const std::string& assetId, const AssetDef& asset) {
  const std::string trimmedDisplay = Trim(asset.displayName);
  if (!trimmedDisplay.empty())
    return trimmedDisplay;

  const std::string trimmedAssetId = Trim(assetId);
  if (!trimmedAssetId.empty())
    return trimmedAssetId;

  if (!asset.mesh.empty()) {
    const std::filesystem::path meshPath(asset.mesh);
    const std::string stem = Trim(meshPath.stem().string());
    if (!stem.empty())
      return stem;
  }

  return "Asset";
}

void EnsureAssetIdentity(const std::string& assetId, AssetDef* asset) {
  if (!asset)
    return;
  if (asset->guid.empty())
    asset->guid = GenerateAssetGuid();
  asset->displayName = MakeAssetDisplayName(assetId, *asset);
}

void EnsureAssetIdentity(SceneDocument* doc) {
  if (!doc)
    return;
  for (auto& entry : doc->assets)
    EnsureAssetIdentity(entry.first, &entry.second);
}

}  // namespace Editor
}  // namespace Monolith
