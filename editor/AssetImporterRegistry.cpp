#include "editor/AssetImporterRegistry.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

#include "core/ProjectPath.h"
#include "editor/AssetIdentity.h"
#include "editor/EditorAssetImport.h"
#include "renderer/ObjLoader.h"

namespace Monolith {
namespace Editor {

namespace {

std::string ToLowerAscii(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

bool IsTexturePath(const std::string& path) {
  const std::string ext = ToLowerAscii(std::filesystem::path(path).extension().string());
  return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" ||
         ext == ".webp" || ext == ".hdr";
}

std::vector<std::string> CopyObjCompanionAssets(const std::filesystem::path& objSource,
                                                const std::filesystem::path& destinationDir) {
  namespace fs = std::filesystem;
  std::vector<std::string> copiedFiles;
  const fs::path srcDir = objSource.parent_path();

  std::ifstream objFile(objSource);
  if (!objFile.is_open())
    return copiedFiles;

  std::string mtlName;
  std::string line;
  while (std::getline(objFile, line)) {
    if (line.rfind("mtllib ", 0) == 0) {
      mtlName = line.substr(7);
      while (!mtlName.empty() && (mtlName.back() == '\r' || mtlName.back() == ' '))
        mtlName.pop_back();
      break;
    }
  }
  if (mtlName.empty())
    return copiedFiles;

  std::error_code ec;
  const fs::path mtlSource = srcDir / mtlName;
  const fs::path mtlDest = destinationDir / mtlName;
  if (fs::exists(mtlSource, ec) && !ec) {
    ec.clear();
    fs::copy_file(mtlSource, mtlDest, fs::copy_options::overwrite_existing, ec);
    if (!ec)
      copiedFiles.push_back(fs::relative(mtlDest, ProjectPath::Root()).generic_string());
  }

  std::ifstream mtlFile(mtlSource);
  if (!mtlFile.is_open())
    return copiedFiles;

  while (std::getline(mtlFile, line)) {
    if (line.size() < 8)
      continue;
    std::string prefix = ToLowerAscii(line.substr(0, 4));
    if (prefix != "map_")
      continue;
    const size_t spacePos = line.find(' ');
    if (spacePos == std::string::npos)
      continue;
    std::string textureName = line.substr(spacePos + 1);
    while (!textureName.empty() && (textureName.back() == '\r' || textureName.back() == ' '))
      textureName.pop_back();
    if (textureName.empty())
      continue;

    const fs::path textureSource = srcDir / textureName;
    const fs::path textureDest = destinationDir / textureName;
    ec.clear();
    if (fs::exists(textureSource, ec) && !ec) {
      ec.clear();
      fs::copy_file(textureSource, textureDest, fs::copy_options::overwrite_existing, ec);
      if (!ec) {
        copiedFiles.push_back(fs::relative(textureDest, ProjectPath::Root()).generic_string());
      }
    }
  }

  std::sort(copiedFiles.begin(), copiedFiles.end());
  copiedFiles.erase(std::unique(copiedFiles.begin(), copiedFiles.end()), copiedFiles.end());
  return copiedFiles;
}

AssetMetadata BuildImportedMetadata(const AssetImportRequest& request,
                                    const AssetDef& asset,
                                    std::string importerId,
                                    std::vector<std::string> producedFiles) {
  AssetMetadata metadata;
  metadata.assetId = request.assetId;
  metadata.assetGuid = request.assetGuid;
  metadata.displayName = MakeAssetDisplayName(request.assetId, asset);
  metadata.importerId = std::move(importerId);
  metadata.sourcePath = request.sourcePath;
  metadata.settings = request.settings;
  metadata.producedFiles = std::move(producedFiles);
  std::sort(metadata.producedFiles.begin(), metadata.producedFiles.end());
  metadata.producedFiles.erase(std::unique(metadata.producedFiles.begin(), metadata.producedFiles.end()),
                               metadata.producedFiles.end());
  metadata.dependencies.push_back({AssetDependencyKind::Source, request.sourcePath});
  for (const std::string& path : metadata.producedFiles)
    metadata.dependencies.push_back({AssetDependencyKind::ProducedOutput, path});
  return metadata;
}

class ObjAssetImporter final : public AssetImporter {
 public:
  const char* ImporterId() const override { return "builtin.obj_mesh"; }
  const char* AssetKind() const override { return "static_mesh"; }

  std::vector<std::string> SupportedExtensions() const override {
    return {".obj"};
  }

  AssetImportResult Import(const AssetImportRequest& request) const override {
    namespace fs = std::filesystem;
    AssetImportResult result;
    if (!IsObjFilePath(request.sourcePath)) {
      result.error = "Selected file is not .obj";
      return result;
    }

    const fs::path sourcePath(request.sourcePath);
    std::error_code ec;
    if (!fs::is_regular_file(sourcePath, ec) || ec) {
      result.error = "OBJ source path is not a file.";
      return result;
    }

    const fs::path destDir = GetManagedAssetDirectory(request.assetGuid);
    fs::create_directories(destDir, ec);
    if (ec) {
      result.error = "Cannot create " + destDir.string() + ": " + ec.message();
      return result;
    }

    const fs::path destObj = destDir / sourcePath.filename();
    fs::copy_file(sourcePath, destObj, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      result.error = "Copy failed: " + ec.message();
      return result;
    }

    std::vector<std::string> producedFiles;
    producedFiles.push_back(fs::relative(destObj, ProjectPath::Root()).generic_string());
    std::vector<std::string> companionFiles = CopyObjCompanionAssets(sourcePath, destDir);
    producedFiles.insert(producedFiles.end(), companionFiles.begin(), companionFiles.end());

    AssetDef asset;
    asset.guid = request.assetGuid;
    asset.displayName = request.displayName.empty() ? request.assetId : request.displayName;
    asset.mesh = fs::relative(destObj, ProjectPath::Root()).generic_string();
    asset.renderScale = SuggestRenderScale(asset.mesh);

    const std::string diffuseTexture = ObjLoader::FindDiffuseTexture(destObj.generic_string());
    if (!diffuseTexture.empty()) {
      asset.albedoMap = fs::relative(fs::path(diffuseTexture), ProjectPath::Root()).generic_string();
      if (std::find(producedFiles.begin(), producedFiles.end(), asset.albedoMap) == producedFiles.end())
        producedFiles.push_back(asset.albedoMap);
    }

    result.ok = true;
    result.asset = asset;
    result.metadata = BuildImportedMetadata(request, asset, ImporterId(), std::move(producedFiles));
    return result;
  }
};

class TextureCopyImporter final : public AssetImporter {
 public:
  const char* ImporterId() const override { return "builtin.texture_copy"; }
  const char* AssetKind() const override { return "texture"; }

  std::vector<std::string> SupportedExtensions() const override {
    return {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".webp", ".hdr"};
  }

  AssetImportResult Import(const AssetImportRequest& request) const override {
    namespace fs = std::filesystem;
    AssetImportResult result;
    if (!IsTexturePath(request.sourcePath)) {
      result.error = "Unsupported image type (use png, jpg, bmp, tga, webp, …).";
      return result;
    }

    const fs::path sourcePath(request.sourcePath);
    std::error_code ec;
    if (!fs::is_regular_file(sourcePath, ec) || ec) {
      result.error = "Texture source path is not a file.";
      return result;
    }

    const fs::path destDir = GetManagedAssetDirectory(request.assetGuid);
    fs::create_directories(destDir, ec);
    if (ec) {
      result.error = "Cannot create " + destDir.string() + ": " + ec.message();
      return result;
    }

    const fs::path destTexture = destDir / sourcePath.filename();
    fs::copy_file(sourcePath, destTexture, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      result.error = "Copy failed: " + ec.message();
      return result;
    }

    AssetDef asset;
    asset.guid = request.assetGuid;
    asset.displayName = request.displayName.empty() ? request.assetId : request.displayName;
    asset.albedoMap = fs::relative(destTexture, ProjectPath::Root()).generic_string();

    result.ok = true;
    result.asset = asset;
    result.metadata = BuildImportedMetadata(
        request, asset, ImporterId(), {asset.albedoMap});
    return result;
  }
};

}  // namespace

AssetImporterRegistry::AssetImporterRegistry() {
  Register(std::make_unique<ObjAssetImporter>());
  Register(std::make_unique<TextureCopyImporter>());
}

void AssetImporterRegistry::Register(std::unique_ptr<AssetImporter> importer) {
  if (!importer)
    return;
  m_importers.push_back(std::move(importer));
}

const AssetImporter* AssetImporterRegistry::FindByExtension(const std::string& sourcePath) const {
  const std::string ext = ToLowerAscii(std::filesystem::path(sourcePath).extension().string());
  for (const auto& importer : m_importers) {
    const std::vector<std::string> extensions = importer->SupportedExtensions();
    if (std::find(extensions.begin(), extensions.end(), ext) != extensions.end())
      return importer.get();
  }
  return nullptr;
}

const AssetImporter* AssetImporterRegistry::FindById(const std::string& importerId) const {
  for (const auto& importer : m_importers) {
    if (importerId == importer->ImporterId())
      return importer.get();
  }
  return nullptr;
}

std::vector<std::string> AssetImporterRegistry::RegisteredImporterIds() const {
  std::vector<std::string> ids;
  ids.reserve(m_importers.size());
  for (const auto& importer : m_importers)
    ids.push_back(importer->ImporterId());
  return ids;
}

}  // namespace Editor
}  // namespace Monolith
