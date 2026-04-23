#pragma once

#include <string>

#include "AssetImporterRegistry.h"

namespace Monolith {
namespace Editor {

struct AssetReimportRecord {
  std::string assetId;
  std::string assetGuid;
  std::string reason;
  bool ok = false;
  std::string error;
};

struct AssetReimportResult {
  bool ok = false;
  std::vector<std::string> order;
  std::vector<AssetReimportRecord> records;
  std::string error;
};

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
  AssetReimportResult ReimportAssetWithDependents(SceneDocument* doc,
                                                  const std::string& rootAssetGuid,
                                                  const std::string& reason) const;

  const AssetImporterRegistry& Registry() const { return m_registry; }

 private:
  AssetImportResult RunImporter(const AssetImporter& importer, const AssetImportRequest& request) const;
  AssetImporterRegistry m_registry;
};

}  // namespace Editor
}  // namespace Monolith
