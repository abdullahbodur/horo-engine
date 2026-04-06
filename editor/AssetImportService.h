#pragma once

#include <string>

#include "editor/AssetImporterRegistry.h"

namespace Monolith {
namespace Editor {

class AssetImportService {
 public:
  AssetImportService() = default;

  AssetImportResult ImportAssetFromSource(const std::string& sourcePath,
                                          const std::string& assetId,
                                          const std::string& assetGuid,
                                          const std::string& displayName,
                                          const std::unordered_map<std::string, std::string>& settings = {}) const;
  bool ImportTextureForAsset(const std::string& sourcePath,
                             const std::string& assetId,
                             AssetDef* asset,
                             std::string* outError) const;
  bool SaveMetadataForAsset(const std::string& assetId, const AssetDef& asset, std::string* outError) const;

  const AssetImporterRegistry& Registry() const { return m_registry; }

 private:
  AssetImporterRegistry m_registry;
};

}  // namespace Editor
}  // namespace Monolith
