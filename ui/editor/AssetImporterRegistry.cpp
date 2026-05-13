/**
 * @file AssetImporterRegistry.cpp
 * @brief Built-in @ref AssetImporter implementations and @ref AssetImporterRegistry registration.
 *
 * Provides OBJ import via @c ObjLoader with companion MTL and referenced textures copied into
 * managed asset folders, plus image extensions for texture assets. Extension lookup normalises
 * to lower-case ASCII.
 */
#include "ui/editor/AssetImporterRegistry.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string_view>

#include "core/ProjectPath.h"
#include "ui/editor/AssetIdentity.h"
#include "ui/editor/AssetImportDiagnosticCodes.h"
#include "ui/editor/EditorAssetImport.h"
#include "renderer/ObjLoader.h"

namespace Horo::Editor {
    namespace {
        /** @brief Returns a copy of @p text with every ASCII letter lowercased. */
        std::string ToLowerAscii(std::string text) {
            std::ranges::transform(text, text.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return text;
        }

        /** @brief True when @p path has a supported image extension (ASCII lower-case test). */
        bool IsTexturePath(const std::string &path) {
            const std::string ext =
                    ToLowerAscii(std::filesystem::path(path).extension().string());
            return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" ||
                   ext == ".tga" || ext == ".webp" || ext == ".hdr";
        }

        /** @brief Copies @p source to @p destination, handling same-file and Windows file_exists races.
         *  @param ec Receives filesystem errors from underlying @c copy_file / @c remove calls.
         *  @return True when @p destination contains a copy of @p source on success.
         */
        bool CopyFileReplacing(const std::filesystem::path &source,
                               const std::filesystem::path &destination,
                               std::error_code &ec) {
            namespace fs = std::filesystem;

            ec.clear();
            if (fs::exists(destination, ec)) {
                if (fs::equivalent(source, destination, ec)) {
                    ec.clear();
                    return true;
                }
                ec.clear();
            }

            fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
            if (!ec)
                return true;

            if (ec != std::make_error_code(std::errc::file_exists))
                return false;

            ec.clear();
            fs::remove(destination, ec);
            if (ec)
                return false;

            fs::copy_file(source, destination, fs::copy_options::none, ec);
            return !ec;
        }

        /** @brief Collects texture filenames referenced by @c map_* directives in an MTL file. */
        std::vector<std::string>
        ScanMtlForTextureNames(const std::filesystem::path &mtlSource) {
            std::vector<std::string> textures;
            std::ifstream mtlFile(mtlSource);
            if (!mtlFile.is_open())
                return textures;
            std::string line;
            while (std::getline(mtlFile, line)) {
                if (line.size() < 8)
                    continue;
                if (const std::string prefix = ToLowerAscii(line.substr(0, 4));
                    prefix != "map_")
                    continue;
                const size_t spacePos = line.find(' ');
                if (spacePos == std::string::npos)
                    continue;
                std::string textureName = line.substr(spacePos + 1);
                while (!textureName.empty() &&
                       (textureName.back() == '\r' || textureName.back() == ' '))
                    textureName.pop_back();
                if (!textureName.empty())
                    textures.push_back(std::move(textureName));
            }
            return textures;
        }

        /** @brief Copies OBJ-linked MTL and textures from @p objSource's folder into @p destinationDir.
         *  @return Project-relative generic paths of copied files, sorted and uniqued.
         */
        std::vector<std::string>
        CopyObjCompanionAssets(const std::filesystem::path &objSource,
                               const std::filesystem::path &destinationDir) {
            namespace fs = std::filesystem;
            std::vector<std::string> copiedFiles;
            const fs::path srcDir = objSource.parent_path();

            std::ifstream objFile(objSource);
            if (!objFile.is_open())
                return copiedFiles;

            std::filesystem::path mtlName;
            std::string line;
            while (std::getline(objFile, line)) {
                if (line.rfind("mtllib ", 0) == 0) {
                    std::string raw = line.substr(7);
                    while (!raw.empty() && (raw.back() == '\r' || raw.back() == ' '))
                        raw.pop_back();
                    mtlName = raw;
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
                CopyFileReplacing(mtlSource, mtlDest, ec);
                if (!ec)
                    copiedFiles.push_back(
                        fs::relative(mtlDest, ProjectPath::Root()).generic_string());
            }

            for (const fs::path textureName: ScanMtlForTextureNames(mtlSource)) {
                const fs::path textureSource = srcDir / textureName;
                const fs::path textureDest = destinationDir / textureName;
                ec.clear();
                if (fs::exists(textureSource, ec) && !ec) {
                    ec.clear();
                    CopyFileReplacing(textureSource, textureDest, ec);
                    if (!ec) {
                        copiedFiles.push_back(
                            fs::relative(textureDest, ProjectPath::Root()).generic_string());
                    }
                }
            }

            std::ranges::sort(copiedFiles);
            auto toErase = std::ranges::unique(copiedFiles);
            copiedFiles.erase(toErase.begin(), toErase.end());
            return copiedFiles;
        }

        /** @brief Builds sidecar metadata for a finished import: settings, produced files, Source + ProducedOutput deps.
         *  @param importerId Registered importer id string stored on the metadata row.
         *  @param producedFiles Paths merged and deduplicated into @c metadata.producedFiles.
         */
        AssetMetadata BuildImportedMetadata(const AssetImportRequest &request,
                                            const AssetDef &asset,
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
            std::ranges::sort(metadata.producedFiles);
            auto toErase = std::ranges::unique(metadata.producedFiles);
            metadata.producedFiles.erase(toErase.begin(), toErase.end());
            metadata.dependencies.emplace_back(AssetDependencyKind::Source,
                                               request.sourcePath);
            for (const std::string &path: metadata.producedFiles)
                metadata.dependencies.emplace_back(AssetDependencyKind::ProducedOutput,
                                                   path);
            return metadata;
        }

        /** @brief Convenience factory for @ref AssetImportDiagnostic rows tied to @p request.
         *
         *  @p code is taken by @c std::string_view so that call sites can pass either a
         *  @c DiagnosticCodes constant or a string literal without an extra allocation.
         */
        AssetImportDiagnostic MakeDiagnostic(AssetDiagnosticSeverity severity,
                                             std::string_view code, std::string message,
                                             const AssetImportRequest &request,
                                             std::string importerId) {
            AssetImportDiagnostic diagnostic;
            diagnostic.severity = severity;
            diagnostic.code = code;
            diagnostic.message = std::move(message);
            diagnostic.assetGuid = request.assetGuid;
            diagnostic.sourcePath = request.sourcePath;
            diagnostic.importerId = std::move(importerId);
            return diagnostic;
        }

        /** @brief Built-in importer that copies OBJ meshes into managed storage with companion materials/textures. */
        class ObjAssetImporter final : public AssetImporter {
        public:
            const char *ImporterId() const override { return "builtin.obj_mesh"; }
            const char *AssetKind() const override { return "static_mesh"; }

            std::vector<std::string> SupportedExtensions() const override {
                return {".obj"};
            }

            /** @brief Validates paths, copies OBJ + companions, assigns mesh/renderScale/albedo from @c ObjLoader hints.
             *  @param request Import inputs including destination GUID and source OBJ path.
             */
            AssetImportResult Import(const AssetImportRequest &request) const override {
                namespace fs = std::filesystem;
                AssetImportResult result;
                if (!IsObjFilePath(request.sourcePath)) {
                    result.error = "Selected file is not .obj";
                    result.diagnostics.push_back(MakeDiagnostic(
                        AssetDiagnosticSeverity::Error, DiagnosticCodes::ObjUnsupportedType,
                        result.error, request, ImporterId()));
                    return result;
                }

                const fs::path sourcePath(request.sourcePath);
                std::error_code ec;
                if (!fs::is_regular_file(sourcePath, ec) || ec) {
                    result.error = "OBJ source path is not a file.";
                    result.diagnostics.push_back(MakeDiagnostic(
                        AssetDiagnosticSeverity::Error, DiagnosticCodes::ObjSourceMissing,
                        result.error, request, ImporterId()));
                    return result;
                }

                const fs::path destDir = GetManagedAssetDirectory(request.assetGuid);
                fs::create_directories(destDir, ec);
                if (ec) {
                    result.error = "Cannot create " + destDir.string() + ": " + ec.message();
                    result.diagnostics.push_back(MakeDiagnostic(
                        AssetDiagnosticSeverity::Error, DiagnosticCodes::ObjCreateDirectoryFailed,
                        result.error, request, ImporterId()));
                    return result;
                }

                const fs::path destObj = destDir / sourcePath.filename();
                CopyFileReplacing(sourcePath, destObj, ec);
                if (ec) {
                    result.error = "Copy failed: " + ec.message();
                    result.diagnostics.push_back(MakeDiagnostic(
                        AssetDiagnosticSeverity::Error, DiagnosticCodes::ObjCopyFailed, result.error,
                        request, ImporterId()));
                    return result;
                }

                std::vector<std::string> producedFiles;
                producedFiles.push_back(
                    fs::relative(destObj, ProjectPath::Root()).generic_string());
                std::vector<std::string> companionFiles =
                        CopyObjCompanionAssets(sourcePath, destDir);
                producedFiles.insert(producedFiles.end(), companionFiles.begin(),
                                     companionFiles.end());

                AssetDef asset;
                asset.guid = request.assetGuid;
                asset.displayName =
                        request.displayName.empty() ? request.assetId : request.displayName;
                asset.mesh = fs::relative(destObj, ProjectPath::Root()).generic_string();
                asset.renderScale = SuggestRenderScale(asset.mesh);

                if (const std::string diffuseTexture =
                            ObjLoader::FindDiffuseTexture(destObj.generic_string());
                    !diffuseTexture.empty()) {
                    asset.albedoMap =
                            fs::relative(fs::path(diffuseTexture), ProjectPath::Root())
                            .generic_string();
                    if (std::ranges::find(producedFiles, asset.albedoMap) ==
                        producedFiles.end())
                        producedFiles.push_back(asset.albedoMap);
                }

                result.ok = true;
                result.asset = asset;
                result.metadata = BuildImportedMetadata(request, asset, ImporterId(),
                                                        std::move(producedFiles));
                result.metadata.diagnostics = result.diagnostics;
                return result;
            }
        };

        /** @brief Built-in importer that copies raster/HDR images into managed storage as texture assets. */
        class TextureCopyImporter final : public AssetImporter {
        public:
            const char *ImporterId() const override { return "builtin.texture_copy"; }
            const char *AssetKind() const override { return "texture"; }

            std::vector<std::string> SupportedExtensions() const override {
                return {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".webp", ".hdr"};
            }

            /** @brief Validates extension, copies the image beside other managed outputs, sets @c albedoMap path.
             *  @param request Import inputs including destination GUID and image source path.
             */
            AssetImportResult Import(const AssetImportRequest &request) const override {
                namespace fs = std::filesystem;
                AssetImportResult result;
                if (!IsTexturePath(request.sourcePath)) {
                    result.error =
                            "Unsupported image type (use png, jpg, bmp, tga, webp, …).";
                    result.diagnostics.push_back(MakeDiagnostic(
                        AssetDiagnosticSeverity::Error, DiagnosticCodes::TextureUnsupportedType,
                        result.error, request, ImporterId()));
                    return result;
                }

                const fs::path sourcePath(request.sourcePath);
                std::error_code ec;
                if (!fs::is_regular_file(sourcePath, ec) || ec) {
                    result.error = "Texture source path is not a file.";
                    result.diagnostics.push_back(MakeDiagnostic(
                        AssetDiagnosticSeverity::Error, DiagnosticCodes::TextureSourceMissing,
                        result.error, request, ImporterId()));
                    return result;
                }

                const fs::path destDir = GetManagedAssetDirectory(request.assetGuid);
                fs::create_directories(destDir, ec);
                if (ec) {
                    result.error = "Cannot create " + destDir.string() + ": " + ec.message();
                    result.diagnostics.push_back(
                        MakeDiagnostic(AssetDiagnosticSeverity::Error,
                                       DiagnosticCodes::TextureCreateDirectoryFailed, result.error,
                                       request, ImporterId()));
                    return result;
                }

                const fs::path destTexture = destDir / sourcePath.filename();
                CopyFileReplacing(sourcePath, destTexture, ec);
                if (ec) {
                    result.error = "Copy failed: " + ec.message();
                    result.diagnostics.push_back(MakeDiagnostic(
                        AssetDiagnosticSeverity::Error, DiagnosticCodes::TextureCopyFailed,
                        result.error, request, ImporterId()));
                    return result;
                }

                AssetDef asset;
                asset.guid = request.assetGuid;
                asset.displayName =
                        request.displayName.empty() ? request.assetId : request.displayName;
                asset.albedoMap =
                        fs::relative(destTexture, ProjectPath::Root()).generic_string();

                result.ok = true;
                result.asset = asset;
                result.metadata =
                        BuildImportedMetadata(request, asset, ImporterId(), {asset.albedoMap});
                result.metadata.diagnostics = result.diagnostics;
                return result;
            }
        };
    } // namespace

    /** @copydoc AssetImporterRegistry::AssetImporterRegistry */
    AssetImporterRegistry::AssetImporterRegistry() {
        Register(std::make_unique<ObjAssetImporter>());
        Register(std::make_unique<TextureCopyImporter>());
    }

    /** @copydoc AssetImporterRegistry::Register */
    void AssetImporterRegistry::Register(std::unique_ptr<AssetImporter> importer) {
        if (!importer)
            return;
        m_importers.push_back(std::move(importer));
    }

    /** @copydoc AssetImporterRegistry::FindByExtension */
    const AssetImporter *
    AssetImporterRegistry::FindByExtension(const std::string &sourcePath) const {
        const std::string ext =
                ToLowerAscii(std::filesystem::path(sourcePath).extension().string());
        for (const auto &importer: m_importers) {
            const std::vector<std::string> extensions = importer->SupportedExtensions();
            if (std::ranges::find(extensions, ext) != extensions.end())
                return importer.get();
        }
        return nullptr;
    }

    /** @copydoc AssetImporterRegistry::FindById */
    const AssetImporter *
    AssetImporterRegistry::FindById(std::string_view importerId) const {
        for (const auto &importer: m_importers) {
            if (importerId == importer->ImporterId())
                return importer.get();
        }
        return nullptr;
    }

    /** @copydoc AssetImporterRegistry::RegisteredImporterIds */
    std::vector<std::string> AssetImporterRegistry::RegisteredImporterIds() const {
        std::vector<std::string> ids;
        ids.reserve(m_importers.size());
        for (const auto &importer: m_importers)
            ids.emplace_back(importer->ImporterId());
        return ids;
    }
} // namespace Horo::Editor
