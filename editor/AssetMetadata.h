#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "editor/SceneDocument.h"

namespace Monolith::Editor {
enum class AssetDependencyKind {
  Source,
  ProducedOutput,
  DownstreamAsset,
};

enum class AssetDiagnosticSeverity {
  Info,
  Warning,
  Error,
};

struct AssetImportDiagnostic {
  AssetDiagnosticSeverity severity = AssetDiagnosticSeverity::Error;
  std::string code;
  std::string message;
  std::string assetGuid;
  std::string sourcePath;
  std::string importerId;
};

struct AssetDependencyRecord {
  AssetDependencyKind kind = AssetDependencyKind::Source;
  std::string value;
};

struct AssetMetadata {
  int version = 1;
  std::string assetId;
  std::string assetGuid;
  std::string displayName;
  std::string importerId;
  std::string sourcePath;
  std::unordered_map<std::string, std::string, StringHash, std::equal_to<>>
      settings;
  std::vector<std::string> producedFiles;
  std::vector<AssetDependencyRecord> dependencies;
  bool lastImportSucceeded = true;
  std::string lastImportReason;
  std::vector<AssetImportDiagnostic> diagnostics;
};

std::filesystem::path GetManagedAssetDirectory(const AssetDef &asset);

std::filesystem::path GetManagedAssetDirectory(const std::string &assetGuid);

std::filesystem::path GetAssetMetadataPath(const std::string &assetGuid);

bool LoadAssetMetadata(const std::string &assetGuid, AssetMetadata *outMetadata,
                       std::string *outError);

bool SaveAssetMetadata(const AssetMetadata &metadata, std::string *outError);

AssetMetadata BuildAssetMetadata(const std::string &assetId,
                                 const AssetDef &asset);

bool EnsureAssetMetadataForDocument(SceneDocument *doc, std::string *outError);
} // namespace Monolith::Editor
