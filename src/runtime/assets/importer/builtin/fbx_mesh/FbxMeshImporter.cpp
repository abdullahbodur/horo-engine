/**
 * @copydoc FbxMeshImporter.h
 * Minimal FBX importer with declarative settings.
 */

#include "FbxMeshImporter.h"
#include "Horo/Assets/AssetRegistry.h"
#include "../../../AssetErrors.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace Horo::Assets
{
namespace
{
void WriteLE32(std::vector<std::uint8_t> &out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
}

class FbxMeshImporter final : public IAssetImporter
{
  public:
    [[nodiscard]] Result<PreparedAssetImport> Import(
        const AssetImportInput &input,
        const CancellationToken &cancellation) const override
    {
        if (cancellation.IsCancellationRequested())
            return Result<PreparedAssetImport>::Failure(MakeError(ImportErrors::ImportCancelled));

        PreparedAssetImport result;
        result.type = AssetTypeId::Parse("core.mesh").Value();
        WriteLE32(result.editorPayload, 1);
        WriteLE32(result.editorPayload, static_cast<std::uint32_t>(input.sourceBytes.size()));
        return Result<PreparedAssetImport>::Success(std::move(result));
    }
};
} // namespace

std::shared_ptr<const IAssetImporter> CreateFbxMeshImporter()
{
    return std::make_shared<const FbxMeshImporter>();
}

Result<void> RegisterFbxMeshImporter(AssetImporterCatalog &catalog)
{
    auto mt = AssetTypeId::Parse("core.mesh");
    if (mt.HasError()) return Result<void>::Failure(mt.ErrorValue());

    return catalog.Register(AssetImporterContribution{
        .contributionId = "horo.asset-importer.fbx-mesh",
        .packageId = "horo.builtin.assets",
        .moduleId = "horo.builtin.assets.importer",
        .version = "1.0.0",
        .fileExtensions = {"fbx"},
        .assetTypes = {mt.Value()},
        .settings = {
            ImportSettingDescriptor{
                .id="coordinateSystem", .labelKey="Coordinate System",
                .descriptionKey="Target coordinate system.", .kind=ImportSettingKind::Choice,
                .defaultValue=std::string{"Y-up (engine)"},
                .choices={
                    {.id="yup", .labelKey="Y-up (engine)", .value=std::string{"Y-up (engine)"}},
                    {.id="zup", .labelKey="Z-up", .value=std::string{"Z-up"}},
                },
            },
            ImportSettingDescriptor{
                .id="importMaterials", .labelKey="Import Materials",
                .descriptionKey="Create material assets from FBX slots.",
                .kind=ImportSettingKind::Boolean, .defaultValue=true,
            },
            ImportSettingDescriptor{
                .id="importAnimations", .labelKey="Import Animations",
                .descriptionKey="Extract animation clips from FBX.",
                .kind=ImportSettingKind::Boolean, .defaultValue=false,
            },
            ImportSettingDescriptor{
                .id="meshCompression", .labelKey="Mesh Compression",
                .descriptionKey="Compression for cooked mesh data.", .kind=ImportSettingKind::Choice,
                .defaultValue=std::string{"None"},
                .choices={
                    {.id="none", .labelKey="None", .value=std::string{"None"}},
                    {.id="draco", .labelKey="Draco", .value=std::string{"Draco"}},
                    {.id="meshopt", .labelKey="MeshOpt", .value=std::string{"MeshOpt"}},
                },
            },
        },
        .builtIn = true,
        .strategy = CreateFbxMeshImporter(),
    });
}

} // namespace Horo::Assets
