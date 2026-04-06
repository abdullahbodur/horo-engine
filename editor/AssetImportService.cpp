#include "editor/AssetImportService.h"

#include <algorithm>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "editor/AssetIdentity.h"

namespace Monolith {
namespace Editor {

namespace {

void RemoveDependenciesOfKind(AssetMetadata* metadata, AssetDependencyKind kind) {
  if (!metadata)
    return;
  metadata->dependencies.erase(
      std::remove_if(metadata->dependencies.begin(),
                     metadata->dependencies.end(),
                     [kind](const AssetDependencyRecord& dep) { return dep.kind == kind; }),
      metadata->dependencies.end());
}

void AppendUniqueDependency(AssetMetadata* metadata, AssetDependencyKind kind, const std::string& value) {
  if (!metadata || value.empty())
    return;
  const auto it = std::find_if(metadata->dependencies.begin(),
                               metadata->dependencies.end(),
                               [&](const AssetDependencyRecord& dep) {
                                 return dep.kind == kind && dep.value == value;
                               });
  if (it == metadata->dependencies.end())
    metadata->dependencies.push_back({kind, value});
}

bool LoadOrBuildMetadata(const std::string& assetId,
                         const AssetDef& asset,
                         AssetMetadata* outMetadata) {
  if (!outMetadata)
    return false;
  std::string error;
  if (LoadAssetMetadata(asset.guid, outMetadata, &error))
    return true;
  *outMetadata = BuildAssetMetadata(assetId, asset);
  outMetadata->assetGuid = asset.guid;
  outMetadata->displayName = asset.displayName;
  return true;
}

}  // namespace

AssetImportResult AssetImportService::RunImporter(const AssetImporter& importer,
                                                  const AssetImportRequest& request) const {
  AssetImportResult result = importer.Import(request);
  if (!result.ok)
    return result;

  EnsureAssetIdentity(request.assetId, &result.asset);
  result.metadata.assetId = request.assetId;
  result.metadata.assetGuid = request.assetGuid;
  result.metadata.displayName = result.asset.displayName;
  result.metadata.lastImportSucceeded = true;
  return result;
}

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
  result = RunImporter(*importer, request);
  if (!result.ok)
    return result;

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
  LoadOrBuildMetadata(assetId, *asset, &existingMetadata);
  existingMetadata.settings["albedoSourcePath"] = sourcePath;

  const AssetImportRequest request{assetId, asset->guid, asset->displayName, sourcePath, existingMetadata.settings};
  AssetImportResult result = RunImporter(*importer, request);
  if (!result.ok) {
    if (outError)
      *outError = result.error;
    return false;
  }

  asset->albedoMap = result.asset.albedoMap;
  existingMetadata.assetId = assetId;
  existingMetadata.assetGuid = asset->guid;
  existingMetadata.displayName = asset->displayName;
  existingMetadata.settings = result.metadata.settings;
  existingMetadata.producedFiles = result.metadata.producedFiles;
  existingMetadata.lastImportSucceeded = true;
  RemoveDependenciesOfKind(&existingMetadata, AssetDependencyKind::ProducedOutput);
  AppendUniqueDependency(&existingMetadata, AssetDependencyKind::Source, sourcePath);
  for (const std::string& produced : result.metadata.producedFiles)
    AppendUniqueDependency(&existingMetadata, AssetDependencyKind::ProducedOutput, produced);

  if (!SaveAssetMetadata(existingMetadata, outError))
    return false;
  return true;
}

bool AssetImportService::SaveMetadataForAsset(const std::string& assetId,
                                              const AssetDef& asset,
                                              std::string* outError) const {
  AssetMetadata metadata;
  LoadOrBuildMetadata(assetId, asset, &metadata);

  metadata.assetId = assetId;
  metadata.assetGuid = asset.guid;
  metadata.displayName = asset.displayName;
  metadata.producedFiles = BuildAssetMetadata(assetId, asset).producedFiles;
  RemoveDependenciesOfKind(&metadata, AssetDependencyKind::ProducedOutput);
  for (const std::string& produced : metadata.producedFiles)
    AppendUniqueDependency(&metadata, AssetDependencyKind::ProducedOutput, produced);
  return SaveAssetMetadata(metadata, outError);
}

AssetReimportResult AssetImportService::ReimportAssetWithDependents(SceneDocument* doc,
                                                                    const std::string& rootAssetGuid,
                                                                    const std::string& reason) const {
  AssetReimportResult result;
  if (!doc) {
    result.error = "Scene document is required.";
    return result;
  }

  std::unordered_map<std::string, std::string> assetIdByGuid;
  std::unordered_map<std::string, AssetMetadata> metadataByGuid;
  for (const auto& entry : doc->assets) {
    assetIdByGuid[entry.second.guid] = entry.first;
    AssetMetadata metadata;
    if (LoadOrBuildMetadata(entry.first, entry.second, &metadata))
      metadataByGuid[entry.second.guid] = std::move(metadata);
  }

  if (assetIdByGuid.count(rootAssetGuid) == 0) {
    result.error = "Asset metadata not found for reimport.";
    return result;
  }

  std::unordered_map<std::string, std::set<std::string>> reverseGraph;
  std::unordered_map<std::string, std::string> dependencyReason;
  for (const auto& entry : metadataByGuid) {
    const std::string& assetGuid = entry.first;
    for (const AssetDependencyRecord& dep : entry.second.dependencies) {
      if (dep.kind != AssetDependencyKind::DownstreamAsset || dep.value.empty())
        continue;
      reverseGraph[dep.value].insert(assetGuid);
    }
  }

  std::set<std::string> impacted;
  std::vector<std::string> queue{rootAssetGuid};
  impacted.insert(rootAssetGuid);
  while (!queue.empty()) {
    const std::string current = queue.back();
    queue.pop_back();
    auto dependentsIt = reverseGraph.find(current);
    if (dependentsIt == reverseGraph.end())
      continue;
    for (const std::string& dependentGuid : dependentsIt->second) {
      if (impacted.insert(dependentGuid).second) {
        queue.push_back(dependentGuid);
        dependencyReason[dependentGuid] = current;
      }
    }
  }

  std::unordered_map<std::string, std::set<std::string>> adjacency;
  std::unordered_map<std::string, size_t> indegree;
  for (const std::string& guid : impacted)
    indegree[guid] = 0;
  for (const auto& entry : reverseGraph) {
    if (impacted.count(entry.first) == 0)
      continue;
    for (const std::string& dependentGuid : entry.second) {
      if (impacted.count(dependentGuid) == 0)
        continue;
      adjacency[entry.first].insert(dependentGuid);
      ++indegree[dependentGuid];
    }
  }

  std::set<std::string> ready;
  for (const auto& entry : indegree) {
    if (entry.second == 0)
      ready.insert(entry.first);
  }

  while (!ready.empty()) {
    const std::string current = *ready.begin();
    ready.erase(ready.begin());
    result.order.push_back(current);
    for (const std::string& dependentGuid : adjacency[current]) {
      size_t& count = indegree[dependentGuid];
      if (count > 0)
        --count;
      if (count == 0)
        ready.insert(dependentGuid);
    }
  }

  if (result.order.size() != impacted.size()) {
    result.error = "Asset dependency cycle detected during reimport propagation.";
    return result;
  }

  for (const std::string& guid : result.order) {
    auto assetIt =
        std::find_if(doc->assets.begin(),
                     doc->assets.end(),
                     [&](const auto& entry) { return entry.second.guid == guid; });
    if (assetIt == doc->assets.end()) {
      result.records.push_back({{}, guid, {}, false, "Asset not found in document."});
      result.error = "Asset not found in document during reimport.";
      return result;
    }

    AssetMetadata metadata;
    LoadOrBuildMetadata(assetIt->first, assetIt->second, &metadata);
    if (metadata.importerId.empty() || metadata.sourcePath.empty()) {
      result.records.push_back(
          {assetIt->first, guid, reason, false, "Asset has no importer metadata or source path."});
      result.error = "Asset has no importer metadata or source path.";
      return result;
    }

    const AssetImporter* importer = m_registry.FindById(metadata.importerId);
    if (!importer)
      importer = m_registry.FindByExtension(metadata.sourcePath);
    if (!importer) {
      result.records.push_back({assetIt->first, guid, reason, false, "Registered importer not found."});
      result.error = "Registered importer not found.";
      return result;
    }

    const std::string recordReason =
        (guid == rootAssetGuid)
            ? reason
            : "Dependency changed: " + dependencyReason[guid];
    AssetImportRequest request{assetIt->first,
                               assetIt->second.guid,
                               assetIt->second.displayName,
                               metadata.sourcePath,
                               metadata.settings};
    AssetImportResult importResult = RunImporter(*importer, request);
    if (!importResult.ok) {
      metadata.lastImportSucceeded = false;
      metadata.lastImportReason = recordReason;
      SaveAssetMetadata(metadata, nullptr);
      result.records.push_back({assetIt->first, guid, recordReason, false, importResult.error});
      result.error = importResult.error;
      return result;
    }

    if (metadata.settings.count("albedoSourcePath") != 0 && !metadata.settings.at("albedoSourcePath").empty()) {
      const AssetImporter* textureImporter = m_registry.FindById("builtin.texture_copy");
      if (textureImporter) {
        AssetImportRequest textureRequest{assetIt->first,
                                          assetIt->second.guid,
                                          assetIt->second.displayName,
                                          metadata.settings.at("albedoSourcePath"),
                                          metadata.settings};
        AssetImportResult textureResult = RunImporter(*textureImporter, textureRequest);
        if (textureResult.ok) {
          importResult.asset.albedoMap = textureResult.asset.albedoMap;
          importResult.metadata.producedFiles.insert(importResult.metadata.producedFiles.end(),
                                                     textureResult.metadata.producedFiles.begin(),
                                                     textureResult.metadata.producedFiles.end());
        }
      }
    }

    AssetMetadata updatedMetadata = metadata;
    updatedMetadata.assetId = assetIt->first;
    updatedMetadata.assetGuid = assetIt->second.guid;
    updatedMetadata.displayName = importResult.asset.displayName;
    updatedMetadata.importerId = metadata.importerId;
    updatedMetadata.sourcePath = metadata.sourcePath;
    updatedMetadata.settings = metadata.settings;
    updatedMetadata.producedFiles = importResult.metadata.producedFiles;
    updatedMetadata.lastImportSucceeded = true;
    updatedMetadata.lastImportReason = recordReason;
    RemoveDependenciesOfKind(&updatedMetadata, AssetDependencyKind::ProducedOutput);
    RemoveDependenciesOfKind(&updatedMetadata, AssetDependencyKind::Source);
    AppendUniqueDependency(&updatedMetadata, AssetDependencyKind::Source, metadata.sourcePath);
    for (const std::string& produced : updatedMetadata.producedFiles)
      AppendUniqueDependency(&updatedMetadata, AssetDependencyKind::ProducedOutput, produced);

    assetIt->second.mesh = importResult.asset.mesh;
    assetIt->second.renderScale = importResult.asset.renderScale;
    assetIt->second.albedoMap = importResult.asset.albedoMap;
    assetIt->second.displayName = importResult.asset.displayName;

    std::string saveError;
    if (!SaveAssetMetadata(updatedMetadata, &saveError)) {
      result.records.push_back({assetIt->first, guid, recordReason, false, saveError});
      result.error = saveError;
      return result;
    }

    result.records.push_back({assetIt->first, guid, recordReason, true, {}});
  }

  result.ok = true;
  return result;
}

}  // namespace Editor
}  // namespace Monolith
