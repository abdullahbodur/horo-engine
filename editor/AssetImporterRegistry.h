#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "editor/AssetMetadata.h"

namespace Monolith {
namespace Editor {

struct AssetImportRequest {
  std::string assetId;
  std::string assetGuid;
  std::string displayName;
  std::string sourcePath;
  std::unordered_map<std::string, std::string> settings;
};

struct AssetImportResult {
  bool ok = false;
  AssetDef asset;
  AssetMetadata metadata;
  std::string error;
  std::vector<AssetImportDiagnostic> diagnostics;
};

class AssetImporter {
 public:
  virtual ~AssetImporter() = default;

  virtual const char* ImporterId() const = 0;
  virtual const char* AssetKind() const = 0;
  virtual std::vector<std::string> SupportedExtensions() const = 0;
  virtual AssetImportResult Import(const AssetImportRequest& request) const = 0;
};

class AssetImporterRegistry {
 public:
  AssetImporterRegistry();

  void Register(std::unique_ptr<AssetImporter> importer);
  const AssetImporter* FindByExtension(const std::string& sourcePath) const;
  const AssetImporter* FindById(const std::string& importerId) const;
  std::vector<std::string> RegisteredImporterIds() const;

 private:
  std::vector<std::unique_ptr<AssetImporter>> m_importers;
};

}  // namespace Editor
}  // namespace Monolith
