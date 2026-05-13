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
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <string_view>
#include <unordered_set>

#include "core/ProjectPath.h"
#include "ui/editor/AssetIdentity.h"
#include "ui/editor/AssetImportDiagnosticCodes.h"
#include "ui/editor/EditorAssetImport.h"
#include "renderer/AnimBin.h"
#include "renderer/FbxLoader.h"
#include "renderer/MeshBin.h"
#include "renderer/ObjLoader.h"
#include "renderer/SkinnedMeshBin.h"

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

        /** @brief Resolves an external FBX texture reference against on-disk candidate paths.
         *  @param record   Texture record captured by @ref Horo::FbxLoader.
         *  @param sourceDir Directory containing the FBX source file.
         *  @return Existing absolute file path on success; empty when no candidate resolves.
         *
         *  Search order (first hit wins):
         *  1. @c absolutePath if absolute and the file exists;
         *  2. @c sourceDir / @c relativePath;
         *  3. @c sourceDir / basename(@c relativePath);
         *  4. @c sourceDir / @c filename (basename hint);
         *  5. @c sourceDir / "textures" / @c filename — many DCCs export here.
         */
        std::filesystem::path
        ResolveExternalTexturePath(const FbxLoader::FbxTextureRecord &record,
                                   const std::filesystem::path &sourceDir) {
            namespace fs = std::filesystem;
            std::error_code ec;

            // FBX files exported on Windows ship backslash-separated paths even
            // when the engine runs on POSIX. Normalise once before constructing
            // any std::filesystem::path so the host-side resolver does not see
            // them as a single filename literal.
            const auto normalise = [](std::string_view path) {
                std::string out(path);
                std::ranges::replace(out, '\\', '/');
                return out;
            };

            std::vector<fs::path> candidates;
            if (!record.absolutePath.empty()) {
                fs::path abs(normalise(record.absolutePath));
                if (abs.is_absolute())
                    candidates.push_back(std::move(abs));
            }
            if (!record.relativePath.empty()) {
                const fs::path normalisedRelative(normalise(record.relativePath));
                candidates.emplace_back(sourceDir / normalisedRelative);
                const fs::path baseFromRel = normalisedRelative.filename();
                if (!baseFromRel.empty())
                    candidates.emplace_back(sourceDir / baseFromRel);
            }
            if (!record.filename.empty()) {
                const fs::path normalisedFilename(normalise(record.filename));
                candidates.emplace_back(sourceDir / normalisedFilename);
                candidates.emplace_back(sourceDir / "textures" / normalisedFilename);
            }

            for (const fs::path &candidate: candidates) {
                if (candidate.empty())
                    continue;
                if (fs::is_regular_file(candidate, ec) && !ec)
                    return candidate;
                ec.clear();
            }
            return {};
        }

        /** @brief Detects the file format suffix from a small image byte signature.
         *  @return Lowercase suffix including the leading dot, or empty when nothing matches.
         *
         *  Supports the formats the engine accepts elsewhere (see TextureCopyImporter):
         *  PNG, JPG/JPEG, BMP, TGA, WebP, HDR. Used when an embedded blob lacks a
         *  filename hint (rare; usually the FBX has a basename for the texture).
         */
        std::string SniffImageExtension(const std::vector<unsigned char> &bytes) {
            const auto starts = [&](std::initializer_list<unsigned char> magic) {
                if (bytes.size() < magic.size())
                    return false;
                std::size_t i = 0;
                for (unsigned char b: magic) {
                    if (bytes[i++] != b)
                        return false;
                }
                return true;
            };
            if (starts({0x89, 0x50, 0x4E, 0x47}))
                return ".png";
            if (starts({0xFF, 0xD8, 0xFF}))
                return ".jpg";
            if (starts({0x42, 0x4D}))
                return ".bmp";
            if (bytes.size() > 12 && bytes[0] == 0x52 && bytes[1] == 0x49 &&
                bytes[2] == 0x46 && bytes[3] == 0x46 && bytes[8] == 0x57 &&
                bytes[9] == 0x45 && bytes[10] == 0x42 && bytes[11] == 0x50)
                return ".webp";
            if (bytes.size() > 11 && bytes[0] == '#' && bytes[1] == '?' &&
                bytes[2] == 'R' && bytes[3] == 'A')
                return ".hdr";
            return {};
        }

        /** @brief Returns @p baseName with its extension replaced by @p ext, or with
         *         @p ext appended when @p baseName has no extension. Pass @c "" to leave it alone.
         */
        std::string EnsureExtension(std::string baseName, const std::string &ext) {
            if (ext.empty())
                return baseName;
            namespace fs = std::filesystem;
            fs::path p(baseName);
            if (p.extension().empty())
                return baseName + ext;
            p.replace_extension(ext);
            return p.string();
        }

        /** @brief Resolves and copies / writes every texture record from @p loaded into managed storage.
         *
         *  - Embedded textures (@c record.embeddedBytes non-empty) are written to
         *    @p destDir / @p record.filename. When the captured filename has no
         *    extension, one is sniffed from the byte signature.
         *  - External textures are resolved via @ref ResolveExternalTexturePath and
         *    copied into @p destDir.
         *  - Each successfully resolved external source path is appended to
         *    @p outExternalSourcePaths so the importer can record them as additional
         *    @c Source dependencies in the metadata sidecar (HORO-97). Embedded
         *    textures do not contribute a Source row beyond the FBX itself.
         *  - On the first successful diffuse texture, @p outAlbedoMap is set to the
         *    project-relative produced-file path so the importer can wire it into
         *    @c AssetDef::albedoMap.
         *  - Per-texture failures are appended as @c Warning diagnostics keyed on
         *    @c FbxExternalTextureMissing / @c FbxExternalTextureCopyFailed /
         *    @c FbxEmbeddedTextureExtractFailed; they never fail the overall import.
         */
        /** @brief True when @p filename is safe as a leaf inside the managed asset directory.
         *
         *  Rejects any candidate that would escape the per-asset folder via path
         *  components (`/`, `\`, `..`) or absolute paths.
         */
        bool IsSafeBasename(std::string_view filename) {
            if (filename.empty() || filename == "." || filename == "..")
                return false;
            return std::ranges::none_of(filename, [](char ch) {
                return ch == '/' || ch == '\\';
            });
        }

        /** @brief Sanitises an FBX-derived filename hint into a single-segment basename.
         *
         *  Keeps the basename of the candidate path; replaces any residual unsafe
         *  characters with @c '_' so callers cannot escape the managed asset
         *  directory. Falls back to a generic name when the input collapses to
         *  empty or @c "." / @c "..".
         */
        std::string SanitiseTextureBasename(std::string_view raw) {
            if (raw.empty())
                return std::string{"texture.png"};
            const std::filesystem::path candidate(raw);
            std::string base = candidate.filename().string();
            for (char &ch: base) {
                if (ch == '/' || ch == '\\' || ch == ':')
                    ch = '_';
            }
            if (base.empty() || base == "." || base == "..")
                return std::string{"texture.png"};
            return base;
        }

        /** @brief Inputs to @ref ApplyFbxTextures bundled to keep the parameter count manageable. */
        struct ApplyFbxTexturesContext {
            const std::filesystem::path &sourcePath; /**< Absolute path to the source FBX file. */
            const std::filesystem::path &destDir;    /**< Managed asset destination directory. */
            const AssetImportRequest &request;       /**< Import request used for diagnostics. */
            const char *importerId;                  /**< Importer id stamped on diagnostics. */
        };

        /** @brief Resolves and copies / writes every texture record from @p loaded into managed storage.
         *
         *  - Embedded textures (@c record.embeddedBytes non-empty) are written to
         *    @p ctx.destDir / sanitised(record.filename). Missing extensions are
         *    sniffed from the byte signature.
         *  - External textures are resolved via @ref ResolveExternalTexturePath and
         *    copied into @p ctx.destDir.
         *  - On-disk filenames are deduplicated against the running
         *    @p producedFiles list so two textures sharing a basename do not
         *    overwrite each other.
         *  - On the first successful diffuse texture, @p outAlbedoMap is set to the
         *    project-relative produced-file path so the importer can wire it into
         *    @c AssetDef::albedoMap.
         *  - Per-texture failures are appended as @c Warning diagnostics keyed on
         *    @c FbxExternalTextureMissing / @c FbxExternalTextureCopyFailed /
         *    @c FbxEmbeddedTextureExtractFailed; they never fail the overall import.
         */
        void ApplyFbxTextures(const std::vector<FbxLoader::FbxTextureRecord> &textures,
                              const ApplyFbxTexturesContext &ctx,
                              std::vector<AssetImportDiagnostic> &diagnostics,
                              std::vector<std::string> &producedFiles,
                              std::vector<std::string> &outExternalSourcePaths,
                              std::string &outAlbedoMap) {
            namespace fs = std::filesystem;
            const fs::path sourceDir = ctx.sourcePath.parent_path();

            struct StringHash {
                using is_transparent = void;
                std::size_t operator()(std::string_view sv) const noexcept {
                    return std::hash<std::string_view>{}(sv);
                }
            };
            std::unordered_set<std::string, StringHash, std::equal_to<>> usedBasenames;
            for (const std::string &produced: producedFiles)
                usedBasenames.insert(fs::path(produced).filename().string());

            const auto pickUniqueBasename = [&](std::string base) {
                if (!usedBasenames.contains(base))
                    return base;
                const fs::path baseAsPath(base);
                const std::string stem = baseAsPath.stem().string();
                const std::string ext = baseAsPath.extension().string();
                for (int suffix = 1; suffix < 1024; ++suffix) {
                    std::string candidate =
                        stem + "_" + std::to_string(suffix) + ext;
                    if (!usedBasenames.contains(candidate))
                        return candidate;
                }
                return base;
            };

            const auto recordProducedTexture = [&](const fs::path &dest,
                                                   const std::string &basename,
                                                   const FbxLoader::FbxTextureRecord &rec) {
                const std::string projectRelative =
                    fs::relative(dest, ProjectPath::Root()).generic_string();
                producedFiles.push_back(projectRelative);
                usedBasenames.insert(basename);
                if (rec.isDiffuseAlbedo && outAlbedoMap.empty())
                    outAlbedoMap = projectRelative;
            };

            for (const FbxLoader::FbxTextureRecord &record: textures) {
                std::string filename = SanitiseTextureBasename(record.filename);
                if (!IsSafeBasename(filename))
                    continue;

                if (!record.embeddedBytes.empty()) {
                    if (fs::path(filename).extension().empty())
                        filename = EnsureExtension(filename, SniffImageExtension(record.embeddedBytes));
                    filename = pickUniqueBasename(filename);
                    const fs::path dest = ctx.destDir / filename;
                    std::ofstream out(dest, std::ios::binary | std::ios::trunc);
                    if (const bool wrote =
                            out.is_open() &&
                            out.write(reinterpret_cast<const char *>(record.embeddedBytes.data()),
                                       static_cast<std::streamsize>(record.embeddedBytes.size())) &&
                            out.good();
                        !wrote) {
                        diagnostics.push_back(MakeDiagnostic(
                            AssetDiagnosticSeverity::Warning,
                            DiagnosticCodes::FbxEmbeddedTextureExtractFailed,
                            std::format("Failed to extract embedded texture '{}'.", filename),
                            ctx.request, ctx.importerId));
                        continue;
                    }
                    recordProducedTexture(dest, filename, record);
                    continue;
                }

                const fs::path resolved = ResolveExternalTexturePath(record, sourceDir);
                if (resolved.empty()) {
                    diagnostics.push_back(MakeDiagnostic(
                        AssetDiagnosticSeverity::Warning,
                        DiagnosticCodes::FbxExternalTextureMissing,
                        std::format("External texture '{}' not found near source FBX.", filename),
                        ctx.request, ctx.importerId));
                    continue;
                }
                filename = pickUniqueBasename(filename);
                const fs::path dest = ctx.destDir / filename;
                if (std::error_code ec; !CopyFileReplacing(resolved, dest, ec) || ec) {
                    diagnostics.push_back(MakeDiagnostic(
                        AssetDiagnosticSeverity::Warning,
                        DiagnosticCodes::FbxExternalTextureCopyFailed,
                        std::format("Failed to copy external texture '{}' ({}).", filename, ec.message()),
                        ctx.request, ctx.importerId));
                    continue;
                }
                recordProducedTexture(dest, filename, record);
                outExternalSourcePaths.push_back(resolved.string());
            }
        }

        /** @brief Built-in importer that extracts static-mesh geometry from an FBX file
         *         into the engine-native @c .mesh.bin format under managed asset storage.
         *
         *  Pipeline:
         *  - Validate extension and source-file presence.
         *  - Create the per-GUID managed directory under @c assets/&lt;guid&gt;/.
         *  - Run @ref Horo::FbxLoader::LoadStaticMesh for combined CPU vertex/index data.
         *  - Persist the result via @ref Horo::MeshBin::WriteStaticMesh as @c &lt;stem&gt;.mesh.bin.
         *  - Compute @c renderScale via the shared height-fitting helper using the
         *    decoded vertex AABB so the asset matches the established 2-unit-tall convention.
         *  - Record the produced @c .mesh.bin in @c producedFiles and emit metadata
         *    sidecar identical in shape to the OBJ importer.
         *
         *  Source-side textures and materials are intentionally out of scope; HORO-95 / HORO-96
         *  introduce external + embedded texture handling on top of this base.
         */
        class FbxAssetImporter final : public AssetImporter {
        public:
            const char *ImporterId() const override { return "builtin.fbx_static_mesh"; }
            const char *AssetKind() const override { return "static_mesh"; }

            std::vector<std::string> SupportedExtensions() const override {
                return {".fbx"};
            }

            /** @brief Extracts static geometry from @p request.sourcePath, writes a managed @c .mesh.bin,
             *         and returns the populated @ref AssetImportResult.
             */
            AssetImportResult Import(const AssetImportRequest &request) const override {
                namespace fs = std::filesystem;
                AssetImportResult result;
                if (!IsFbxFilePath(request.sourcePath)) {
                    result.error = "Selected file is not .fbx";
                    result.diagnostics.push_back(MakeDiagnostic(
                        AssetDiagnosticSeverity::Error, DiagnosticCodes::FbxUnsupportedType,
                        result.error, request, ImporterId()));
                    return result;
                }

                const fs::path sourcePath(request.sourcePath);
                std::error_code ec;
                if (!fs::is_regular_file(sourcePath, ec) || ec) {
                    result.error = "FBX source path is not a file.";
                    result.diagnostics.push_back(MakeDiagnostic(
                        AssetDiagnosticSeverity::Error, DiagnosticCodes::FbxSourceMissing,
                        result.error, request, ImporterId()));
                    return result;
                }

                const fs::path destDir = GetManagedAssetDirectory(request.assetGuid);
                fs::create_directories(destDir, ec);
                if (ec) {
                    result.error = "Cannot create " + destDir.string() + ": " + ec.message();
                    result.diagnostics.push_back(MakeDiagnostic(
                        AssetDiagnosticSeverity::Error, DiagnosticCodes::FbxCreateDirectoryFailed,
                        result.error, request, ImporterId()));
                    return result;
                }

                FbxLoader::FbxLoadResult loaded =
                        FbxLoader::LoadStaticMesh(sourcePath.string());
                if (!loaded.ok) {
                    const std::string_view code =
                            loaded.errorCode == "fbx.no_geometry"
                                ? DiagnosticCodes::FbxNoGeometry
                                : DiagnosticCodes::FbxParseFailed;
                    result.error = loaded.error.empty()
                                       ? "FBX parse failed."
                                       : loaded.error;
                    result.diagnostics.push_back(MakeDiagnostic(
                        AssetDiagnosticSeverity::Error, code, result.error, request,
                        ImporterId()));
                    return result;
                }

                if (loaded.hasSkinning) {
                    FbxLoader::FbxSkeletalLoadResult skeletal =
                            FbxLoader::LoadSkeletalMesh(sourcePath.string());
                    if (!skeletal.ok) {
                        std::string_view code = DiagnosticCodes::FbxParseFailed;
                        if (skeletal.errorCode == "fbx.skeleton_missing")
                            code = DiagnosticCodes::FbxSkeletonMissing;
                        else if (skeletal.errorCode == "fbx.no_geometry")
                            code = DiagnosticCodes::FbxNoGeometry;
                        result.error = skeletal.error.empty()
                                           ? "FBX skeletal extraction failed."
                                           : skeletal.error;
                        result.diagnostics.push_back(MakeDiagnostic(
                            AssetDiagnosticSeverity::Error, code, result.error, request,
                            ImporterId()));
                        return result;
                    }

                    const fs::path destSkinnedBin =
                            destDir / (sourcePath.stem().string() + ".skinned.bin");
                    if (auto skinnedWrite =
                            SkinnedMeshBin::WriteSkinnedMesh(
                                destSkinnedBin.string(), skeletal.vertices,
                                skeletal.indices, skeletal.bones);
                        !skinnedWrite.ok) {
                        result.error = skinnedWrite.error.empty()
                                           ? "Failed writing engine-native skinned mesh binary."
                                           : skinnedWrite.error;
                        result.diagnostics.push_back(MakeDiagnostic(
                            AssetDiagnosticSeverity::Error,
                            DiagnosticCodes::FbxSkeletonWriteFailed, result.error,
                            request, ImporterId()));
                        return result;
                    }

                    std::vector<std::string> producedFiles;
                    producedFiles.push_back(
                        fs::relative(destSkinnedBin, ProjectPath::Root()).generic_string());

                    // HORO-108: extract animation clips for the skeleton's bones.
                    std::vector<std::string> boneNames;
                    boneNames.reserve(skeletal.bones.size());
                    for (const Bone &bone: skeletal.bones)
                        boneNames.push_back(bone.name);
                    if (auto animResult =
                            FbxLoader::LoadAnimations(sourcePath.string(), boneNames);
                        animResult.ok && !animResult.clips.empty()) {
                        const fs::path destAnimBin =
                            destDir / (sourcePath.stem().string() + ".anim.bin");
                        const AnimBin::WriteResult animWrite =
                            AnimBin::WriteClips(destAnimBin.string(), animResult.clips);
                        if (animWrite.ok) {
                            producedFiles.push_back(
                                fs::relative(destAnimBin, ProjectPath::Root())
                                    .generic_string());
                        } else {
                            result.diagnostics.push_back(MakeDiagnostic(
                                AssetDiagnosticSeverity::Warning,
                                DiagnosticCodes::FbxAnimationWriteFailed,
                                animWrite.error.empty()
                                    ? "Failed writing animation binary."
                                    : animWrite.error,
                                request, ImporterId()));
                        }
                    }

                    std::string albedoMapPath;
                    std::vector<std::string> externalSourcePaths;
                    ApplyFbxTextures(loaded.textures,
                                     {sourcePath, destDir, request, ImporterId()},
                                     result.diagnostics, producedFiles,
                                     externalSourcePaths, albedoMapPath);

                    AssetDef asset;
                    asset.guid = request.assetGuid;
                    asset.displayName = request.displayName.empty() ? request.assetId
                                                                    : request.displayName;
                    asset.mesh =
                        fs::relative(destSkinnedBin, ProjectPath::Root()).generic_string();
                    if (!albedoMapPath.empty())
                        asset.albedoMap = albedoMapPath;
                    {
                        const float height = skeletal.aabbMax.y - skeletal.aabbMin.y;
                        const float scale = (height < 1e-6f) ? 1.0f : (2.0f / height);
                        asset.renderScale = std::format("{:.4f},{:.4f},{:.4f}", scale,
                                                       scale, scale);
                    }

                    result.ok = true;
                    result.asset = asset;
                    result.metadata = BuildImportedMetadata(request, asset, ImporterId(),
                                                            std::move(producedFiles));
                    for (const std::string &externalSource: externalSourcePaths) {
                        const auto duplicate = std::ranges::find_if(
                            result.metadata.dependencies,
                            [&](const AssetDependencyRecord &dep) {
                                return dep.kind == AssetDependencyKind::Source &&
                                       dep.value == externalSource;
                            });
                        if (duplicate == result.metadata.dependencies.end())
                            result.metadata.dependencies.emplace_back(
                                AssetDependencyKind::Source, externalSource);
                    }
                    result.metadata.diagnostics = result.diagnostics;
                    return result;
                }

                const fs::path destMeshBin =
                        destDir / (sourcePath.stem().string() + ".mesh.bin");
                if (auto writeResult =
                        MeshBin::WriteStaticMesh(destMeshBin.string(), loaded.vertices,
                                                 loaded.indices);
                    !writeResult.ok) {
                    result.error = writeResult.error.empty()
                                       ? "Failed writing engine-native mesh binary."
                                       : writeResult.error;
                    result.diagnostics.push_back(MakeDiagnostic(
                        AssetDiagnosticSeverity::Error, DiagnosticCodes::FbxMeshWriteFailed,
                        result.error, request, ImporterId()));
                    return result;
                }

                std::vector<std::string> producedFiles;
                producedFiles.push_back(
                    fs::relative(destMeshBin, ProjectPath::Root()).generic_string());

                std::string albedoMapPath;
                std::vector<std::string> externalSourcePaths;
                ApplyFbxTextures(loaded.textures,
                                 {sourcePath, destDir, request, ImporterId()},
                                 result.diagnostics, producedFiles,
                                 externalSourcePaths, albedoMapPath);

                AssetDef asset;
                asset.guid = request.assetGuid;
                asset.displayName =
                        request.displayName.empty() ? request.assetId : request.displayName;
                asset.mesh = fs::relative(destMeshBin, ProjectPath::Root()).generic_string();
                if (!albedoMapPath.empty())
                    asset.albedoMap = albedoMapPath;
                {
                    // Fit-to-2-units along Y, mirroring SuggestRenderScale's
                    // behaviour but using the AABB the loader already computed
                    // so we never re-parse the source FBX.
                    const float height = loaded.aabbMax.y - loaded.aabbMin.y;
                    const float scale = (height < 1e-6f) ? 1.0f : (2.0f / height);
                    asset.renderScale = std::format("{:.4f},{:.4f},{:.4f}", scale,
                                                   scale, scale);
                }

                result.ok = true;
                result.asset = asset;
                result.metadata = BuildImportedMetadata(request, asset, ImporterId(),
                                                        std::move(producedFiles));
                // Resolved external texture source paths participate in
                // dependency tracking so reimport propagation observes
                // upstream texture changes. Embedded textures do not add a
                // Source row beyond the FBX itself.
                for (const std::string &externalSource: externalSourcePaths) {
                    const auto duplicate = std::ranges::find_if(
                        result.metadata.dependencies,
                        [&](const AssetDependencyRecord &dep) {
                            return dep.kind == AssetDependencyKind::Source &&
                                   dep.value == externalSource;
                        });
                    if (duplicate == result.metadata.dependencies.end())
                        result.metadata.dependencies.emplace_back(
                            AssetDependencyKind::Source, externalSource);
                }
                result.metadata.diagnostics = result.diagnostics;
                return result;
            }
        };
    } // namespace

    /** @copydoc AssetImporterRegistry::AssetImporterRegistry */
    AssetImporterRegistry::AssetImporterRegistry() {
        Register(std::make_unique<ObjAssetImporter>());
        Register(std::make_unique<TextureCopyImporter>());
        Register(std::make_unique<FbxAssetImporter>());
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
