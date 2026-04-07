#include "editor/AssetMetadata.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <utility>

#include "core/ProjectPath.h"
#include "editor/AssetIdentity.h"

using json = nlohmann::json;

namespace Monolith {
namespace Editor {

namespace {

std::string DiagnosticSeverityToString(AssetDiagnosticSeverity severity) {
  switch (severity) {
    case AssetDiagnosticSeverity::Info:
      return "info";
    case AssetDiagnosticSeverity::Warning:
      return "warning";
    case AssetDiagnosticSeverity::Error:
      return "error";
  }
  return "error";
}

AssetDiagnosticSeverity DiagnosticSeverityFromString(const std::string& text) {
  if (text == "info")
    return AssetDiagnosticSeverity::Info;
  if (text == "warning")
    return AssetDiagnosticSeverity::Warning;
  return AssetDiagnosticSeverity::Error;
}

std::string DependencyKindToString(AssetDependencyKind kind) {
  switch (kind) {
    case AssetDependencyKind::Source:
      return "source";
    case AssetDependencyKind::ProducedOutput:
      return "produced_output";
    case AssetDependencyKind::DownstreamAsset:
      return "downstream_asset";
  }
  return "source";
}

AssetDependencyKind DependencyKindFromString(const std::string& text) {
  if (text == "produced_output")
    return AssetDependencyKind::ProducedOutput;
  if (text == "downstream_asset")
    return AssetDependencyKind::DownstreamAsset;
  return AssetDependencyKind::Source;
}

std::vector<std::string> CollectProducedFiles(const AssetDef& asset) {
  std::vector<std::string> producedFiles;
  if (!asset.mesh.empty())
    producedFiles.push_back(asset.mesh);
  if (!asset.albedoMap.empty() &&
      std::find(producedFiles.begin(), producedFiles.end(), asset.albedoMap) == producedFiles.end()) {
    producedFiles.push_back(asset.albedoMap);
  }
  std::sort(producedFiles.begin(), producedFiles.end());
  return producedFiles;
}

}  // namespace

std::filesystem::path GetManagedAssetDirectory(const std::string& assetGuid) {
  if (assetGuid.empty())
    return {};
  return ProjectPath::Root() / "assets" / "models" / assetGuid;
}

std::filesystem::path GetManagedAssetDirectory(const AssetDef& asset) {
  if (!asset.guid.empty())
    return GetManagedAssetDirectory(asset.guid);

  if (asset.mesh.empty())
    return {};
  const std::filesystem::path meshPath = ProjectPath::Resolve(asset.mesh);
  if (meshPath.empty())
    return {};
  return meshPath.parent_path();
}

std::filesystem::path GetAssetMetadataPath(const std::string& assetGuid) {
  if (assetGuid.empty())
    return {};
  return GetManagedAssetDirectory(assetGuid) / "asset.meta.json";
}

bool LoadAssetMetadata(const std::string& assetGuid,
                       AssetMetadata* outMetadata,
                       std::string* outError) {
  if (outError)
    outError->clear();
  if (!outMetadata) {
    if (outError)
      *outError = "Asset metadata output is required.";
    return false;
  }

  const std::filesystem::path metadataPath = GetAssetMetadataPath(assetGuid);
  std::ifstream stream(metadataPath);
  if (!stream.is_open()) {
    if (outError)
      *outError = "Asset metadata file not found.";
    return false;
  }

  json j;
  try {
    stream >> j;
  } catch (const json::exception& e) {
    if (outError)
      *outError = e.what();
    return false;
  }

  AssetMetadata metadata;
  metadata.version = j.value("version", 1);
  metadata.assetId = j.value("assetId", std::string());
  metadata.assetGuid = j.value("assetGuid", std::string());
  metadata.displayName = j.value("displayName", std::string());
  metadata.importerId = j.value("importerId", std::string());
  metadata.sourcePath = j.value("sourcePath", std::string());
  metadata.lastImportSucceeded = j.value("lastImportSucceeded", true);
  metadata.lastImportReason = j.value("lastImportReason", std::string());

  if (j.contains("settings") && j["settings"].is_object()) {
    for (const auto& item : j["settings"].items()) {
      if (item.value().is_string())
        metadata.settings[item.key()] = item.value().get<std::string>();
      else
        metadata.settings[item.key()] = item.value().dump();
    }
  }

  if (j.contains("producedFiles") && j["producedFiles"].is_array()) {
    for (const json& item : j["producedFiles"]) {
      if (item.is_string())
        metadata.producedFiles.push_back(item.get<std::string>());
    }
  }

  if (j.contains("dependencies") && j["dependencies"].is_array()) {
    for (const json& item : j["dependencies"]) {
      if (!item.is_object())
        continue;
      AssetDependencyRecord dep;
      dep.kind = DependencyKindFromString(item.value("kind", std::string("source")));
      dep.value = item.value("value", std::string());
      if (!dep.value.empty())
        metadata.dependencies.push_back(std::move(dep));
    }
  }

  if (j.contains("diagnostics") && j["diagnostics"].is_array()) {
    for (const json& item : j["diagnostics"]) {
      if (!item.is_object())
        continue;
      AssetImportDiagnostic diagnostic;
      diagnostic.severity = DiagnosticSeverityFromString(item.value("severity", std::string("error")));
      diagnostic.code = item.value("code", std::string());
      diagnostic.message = item.value("message", std::string());
      diagnostic.assetGuid = item.value("assetGuid", std::string());
      diagnostic.sourcePath = item.value("sourcePath", std::string());
      diagnostic.importerId = item.value("importerId", std::string());
      metadata.diagnostics.push_back(std::move(diagnostic));
    }
  }

  *outMetadata = std::move(metadata);
  return true;
}

bool SaveAssetMetadata(const AssetMetadata& metadata, std::string* outError) {
  if (outError)
    outError->clear();

  const std::filesystem::path metadataPath = GetAssetMetadataPath(metadata.assetGuid);
  if (metadataPath.empty()) {
    if (outError)
      *outError = "Asset metadata path is empty.";
    return false;
  }

  std::error_code ec;
  std::filesystem::create_directories(metadataPath.parent_path(), ec);
  if (ec) {
    if (outError)
      *outError = ec.message();
    return false;
  }

  json j;
  j["version"] = metadata.version;
  j["assetId"] = metadata.assetId;
  j["assetGuid"] = metadata.assetGuid;
  j["displayName"] = metadata.displayName;
  if (!metadata.importerId.empty())
    j["importerId"] = metadata.importerId;
  if (!metadata.sourcePath.empty())
    j["sourcePath"] = metadata.sourcePath;
  j["lastImportSucceeded"] = metadata.lastImportSucceeded;
  if (!metadata.lastImportReason.empty())
    j["lastImportReason"] = metadata.lastImportReason;

  json settings = json::object();
  std::vector<std::string> settingKeys;
  settingKeys.reserve(metadata.settings.size());
  for (const auto& entry : metadata.settings)
    settingKeys.push_back(entry.first);
  std::sort(settingKeys.begin(), settingKeys.end());
  for (const std::string& key : settingKeys)
    settings[key] = metadata.settings.at(key);
  j["settings"] = std::move(settings);

  j["producedFiles"] = json::array();
  for (const std::string& path : metadata.producedFiles)
    j["producedFiles"].push_back(path);

  j["dependencies"] = json::array();
  for (const AssetDependencyRecord& dep : metadata.dependencies) {
    j["dependencies"].push_back(
        json{{"kind", DependencyKindToString(dep.kind)}, {"value", dep.value}});
  }

  j["diagnostics"] = json::array();
  for (const AssetImportDiagnostic& diagnostic : metadata.diagnostics) {
    j["diagnostics"].push_back(json{{"severity", DiagnosticSeverityToString(diagnostic.severity)},
                                    {"code", diagnostic.code},
                                    {"message", diagnostic.message},
                                    {"assetGuid", diagnostic.assetGuid},
                                    {"sourcePath", diagnostic.sourcePath},
                                    {"importerId", diagnostic.importerId}});
  }

  std::ofstream stream(metadataPath);
  if (!stream.is_open()) {
    if (outError)
      *outError = "Cannot write asset metadata.";
    return false;
  }

  stream << j.dump(2);
  return true;
}

AssetMetadata BuildAssetMetadata(const std::string& assetId, const AssetDef& asset) {
  AssetMetadata metadata;
  metadata.assetId = assetId;
  metadata.assetGuid = asset.guid;
  metadata.displayName = MakeAssetDisplayName(assetId, asset);
  metadata.producedFiles = CollectProducedFiles(asset);
  for (const std::string& path : metadata.producedFiles)
    metadata.dependencies.push_back({AssetDependencyKind::ProducedOutput, path});
  return metadata;
}

bool EnsureAssetMetadataForDocument(SceneDocument* doc, std::string* outError) {
  if (outError)
    outError->clear();
  if (!doc) {
    if (outError)
      *outError = "Scene document is required.";
    return false;
  }

  EnsureAssetIdentity(doc);
  for (auto& entry : doc->assets) {
    AssetMetadata metadata;
    std::string metadataError;
    if (!LoadAssetMetadata(entry.second.guid, &metadata, &metadataError))
      metadata = BuildAssetMetadata(entry.first, entry.second);
    metadata.assetId = entry.first;
    metadata.assetGuid = entry.second.guid;
    metadata.displayName = entry.second.displayName;
    metadata.producedFiles = CollectProducedFiles(entry.second);
    metadata.dependencies.erase(
        std::remove_if(metadata.dependencies.begin(),
                       metadata.dependencies.end(),
                       [](const AssetDependencyRecord& dep) {
                         return dep.kind == AssetDependencyKind::ProducedOutput;
                       }),
        metadata.dependencies.end());
    for (const std::string& path : metadata.producedFiles)
      metadata.dependencies.push_back({AssetDependencyKind::ProducedOutput, path});
    if (!SaveAssetMetadata(metadata, outError))
      return false;
  }
  return true;
}

}  // namespace Editor
}  // namespace Monolith
