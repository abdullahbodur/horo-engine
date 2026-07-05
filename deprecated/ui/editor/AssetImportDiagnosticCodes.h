/** @file AssetImportDiagnosticCodes.h
 *  @brief Stable, typed string identifiers for diagnostic codes emitted by the asset importer
 *         subsystem.
 *
 *  Convention:
 *  - Format: @c "asset.<scope>.<reason>" — lowercase, dot-separated.
 *  - @c <scope> is either an importer kind (@c obj, @c texture, @c fbx) or a cross-cutting
 *    surface owned by @ref AssetImportService (@c import, @c importer, @c metadata, @c reimport).
 *  - Codes are part of the public diagnostic contract: never rename or repurpose an existing
 *    constant. Add a new constant when a new failure mode appears; migrate consumers
 *    before deleting an old one.
 *
 *  Usage:
 *  @code
 *  result.diagnostics.push_back(MakeDiagnostic(
 *      AssetDiagnosticSeverity::Error,
 *      DiagnosticCodes::ObjUnsupportedType,
 *      "Selected file is not .obj",
 *      request, ImporterId()));
 *  @endcode
 *
 *  Constants are @c inline @c constexpr @c std::string_view so they have no runtime
 *  initialisation cost, can be compared with @c std::string::operator== / @c == against
 *  @c std::string_view, and may be assigned to @c AssetImportDiagnostic::code (a
 *  @c std::string) via the C++17 @c string=string_view assignment.
 */
#pragma once

#include <string_view>

namespace Horo::Editor::DiagnosticCodes {
    // ---- Cross-cutting (AssetImportService) --------------------------------

    /** @brief Generic import-failure fallback when the importer did not emit a more specific code. */
    inline constexpr std::string_view ImportFailed = "asset.import.failed";

    /** @brief No registered importer matches the source file extension. */
    inline constexpr std::string_view ImporterNotFound = "asset.importer.not_found";

    /** @brief The on-disk @c asset.meta.json sidecar could not be written. */
    inline constexpr std::string_view MetadataSaveFailed = "asset.metadata.save_failed";

    /** @brief A cycle was detected when computing the reimport propagation order. */
    inline constexpr std::string_view ReimportDependencyCycle = "asset.reimport.dependency_cycle";

    /** @brief Reimport refused because the asset has no importer id or source path on record. */
    inline constexpr std::string_view ReimportMetadataMissing = "asset.reimport.metadata_missing";

    /** @brief Reimport refused because the importer recorded in metadata is no longer registered. */
    inline constexpr std::string_view ReimportImporterMissing = "asset.reimport.importer_missing";

    // ---- OBJ static-mesh importer ------------------------------------------

    /** @brief Source file does not have a recognised .obj extension. */
    inline constexpr std::string_view ObjUnsupportedType = "asset.obj.unsupported_type";

    /** @brief Source file path does not resolve to a regular file on disk. */
    inline constexpr std::string_view ObjSourceMissing = "asset.obj.source_missing";

    /** @brief Managed asset directory could not be created for the import. */
    inline constexpr std::string_view ObjCreateDirectoryFailed = "asset.obj.create_directory_failed";

    /** @brief Source OBJ could not be copied into the managed asset directory. */
    inline constexpr std::string_view ObjCopyFailed = "asset.obj.copy_failed";

    // ---- Texture importer --------------------------------------------------

    /** @brief Source file does not have a recognised image extension. */
    inline constexpr std::string_view TextureUnsupportedType = "asset.texture.unsupported_type";

    /** @brief Source file path does not resolve to a regular file on disk. */
    inline constexpr std::string_view TextureSourceMissing = "asset.texture.source_missing";

    /** @brief Managed asset directory could not be created for the texture import. */
    inline constexpr std::string_view TextureCreateDirectoryFailed = "asset.texture.create_directory_failed";

    /** @brief Source image could not be copied into the managed asset directory. */
    inline constexpr std::string_view TextureCopyFailed = "asset.texture.copy_failed";

    // ---- FBX importer (defined ahead of HORO-94 .. HORO-110) ---------------
    //
    // These constants are introduced before the @c FbxAssetImporter exists so PRs that
    // land the FBX importer body, texture handling, skeletal extraction, and animation
    // extraction can reference stable identifiers from day one. Severity column lists
    // the typical severity at the call site.

    /** @brief Source file does not have a recognised .fbx extension. (Error) */
    inline constexpr std::string_view FbxUnsupportedType = "asset.fbx.unsupported_type";

    /** @brief Source FBX path does not resolve to a regular file on disk. (Error) */
    inline constexpr std::string_view FbxSourceMissing = "asset.fbx.source_missing";

    /** @brief Managed asset directory could not be created for the FBX import. (Error) */
    inline constexpr std::string_view FbxCreateDirectoryFailed = "asset.fbx.create_directory_failed";

    /** @brief FBX parsing failed; the file is corrupt or uses an unsupported variant. (Error) */
    inline constexpr std::string_view FbxParseFailed = "asset.fbx.parse_failed";

    /** @brief FBX parsed successfully but contains no usable geometry. (Error) */
    inline constexpr std::string_view FbxNoGeometry = "asset.fbx.no_geometry";

    /** @brief Engine-native mesh binary could not be written to managed storage. (Error) */
    inline constexpr std::string_view FbxMeshWriteFailed = "asset.fbx.mesh_write_failed";

    /** @brief External texture referenced by the FBX could not be located on disk. (Warning) */
    inline constexpr std::string_view FbxExternalTextureMissing = "asset.fbx.external_texture_missing";

    /** @brief External texture was located but copying it into managed storage failed. (Warning) */
    inline constexpr std::string_view FbxExternalTextureCopyFailed = "asset.fbx.external_texture_copy_failed";

    /** @brief Embedded texture blob could not be extracted from the FBX. (Warning) */
    inline constexpr std::string_view FbxEmbeddedTextureExtractFailed = "asset.fbx.embedded_texture_extract_failed";

    /** @brief FBX has no skeleton when one was expected for skeletal-mesh import. (Warning) */
    inline constexpr std::string_view FbxSkeletonMissing = "asset.fbx.skeleton_missing";

    /** @brief Engine-native skeleton binary could not be written to managed storage. (Error) */
    inline constexpr std::string_view FbxSkeletonWriteFailed = "asset.fbx.skeleton_write_failed";

    /** @brief Engine-native animation binary could not be written to managed storage. (Error) */
    inline constexpr std::string_view FbxAnimationWriteFailed = "asset.fbx.animation_write_failed";

    /** @brief FBX unit scale required normalisation; recorded so callers can audit the choice. (Warning) */
    inline constexpr std::string_view FbxUnitScaleWarning = "asset.fbx.unit_scale_warning";

    /** @brief FBX feature is recognised but not currently supported by the engine importer. (Warning) */
    inline constexpr std::string_view FbxUnsupportedFeatureWarning = "asset.fbx.unsupported_feature_warning";
} // namespace Horo::Editor::DiagnosticCodes
