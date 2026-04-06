#include "editor/AssetImportService.h"

#include <algorithm>

#include "editor/AssetIdentity.h"

namespace Monolith {
namespace Editor {

AssetImportResult AssetImportService::ImportAssetFromSource(
    const std::string& sourcePath,
    const std::string& assetId,
    const std::string& assetGuid,
    const std::string& displayName,
    const std::unordered_map<std::string, std::string>& settings) const {
  AssetImportResult result;
  const AssetImporter* importer = m_registry.FindByExtension(sourcePath);
  if (!importer) {
    result.error = "No importer registered for this file type.";
    return result;
  }

  const AssetImportRequest request{assetId, assetGuid, displayName, sourcePath, settings};
  result = importer->Import(request);
  if (!result.ok)
    return result;

  EnsureAssetIdentity(assetId, &result.asset);
  if (!SaveAssetMetadata(result.metadata, &result.error))
    result.ok = false;
  return result;
}

bool AssetImportService::ImportTextureForAsset(const std::string& sourcePath,
                                               const std::string& assetId,
                                               AssetDef* asset,
                                               std::string* outError) const {
  if (outError)
    outError->clear();
  if (!asset) {
    if (outError)
      *outError = "Asset is required.";
    return false;
  }

  EnsureAssetIdentity(assetId, asset);
  const AssetImporter* importer = m_registry.FindById("builtin.texture_copy");
  if (!importer) {
    if (outError)
      *outError = "Texture importer is not registered.";
    return false;
  }

  AssetMetadata existingMetadata;
  std::string metadataError;
  if (!LoadAssetMetadata(asset->guid, &existingMetadata, &metadataError)) {
    existingMetadata = BuildAssetMetadata(assetId, *asset);
    existingMetadata.assetGuid = asset->guid;
    existingMetadata.displayName = asset->displayName;
  }

  existingMetadata.settings["albedoSourcePath"] = sourcePath;
  const AssetImportRequest request{assetId, asset->guid, asset->displayName, sourcePath, existingMetadata.settings};
  AssetImportResult result = importer->Import(request);
  if (!result.ok) {
    if (outError)
      *outError = result.error;
    return false;
  }

  asset->albedoMap = result.asset.albedoMap;
  existingMetadata.displayName = asset->displayName;
  existingMetadata.settings = result.metadata.settings;
  existingMetadata.producedFiles = result.metadata.producedFiles;
  existingMetadata.dependencies.erase(
      std::remove_if(existingMetadata.dependencies.begin(),
                     existingMetadata.dependencies.end(),
                     [](const AssetDependencyRecord& dep) {
                       return dep.kind == AssetDependencyKind::ProducedOutput;
                     }),
      existingMetadata.dependencies.end());
  existingMetadata.dependencies.push_back({AssetDependencyKind::Source, sourcePath});
  for (const std::string& produced : result.metadata.producedFiles)
    existingMetadata.dependencies.push_back({AssetDependencyKind::ProducedOutput, produced});

  if (!SaveAssetMetadata(existingMetadata, outError))
    return false;
  return true;
}

bool AssetImportService::SaveMetadataForAsset(const std::string& assetId,
                                              const AssetDef& asset,
                                              std::string* outError) const {
  AssetMetadata metadata;
  if (!LoadAssetMetadata(asset.guid, &metadata, outError))
    metadata = BuildAssetMetadata(assetId, asset);

  metadata.assetId = assetId;
  metadata.assetGuid = asset.guid;
  metadata.displayName = asset.displayName;
  metadata.producedFiles = BuildAssetMetadata(assetId, asset).producedFiles;
  metadata.dependencies.erase(
      std::remove_if(metadata.dependencies.begin(),
                     metadata.dependencies.end(),
                     [](const AssetDependencyRecord& dep) {
                       return dep.kind == AssetDependencyKind::ProducedOutput;
                     }),
      metadata.dependencies.end());
  for (const std::string& produced : metadata.producedFiles)
    metadata.dependencies.push_back({AssetDependencyKind::ProducedOutput, produced});
  return SaveAssetMetadata(metadata, outError);
}

}  // namespace Editor
}  // namespace Monolith
