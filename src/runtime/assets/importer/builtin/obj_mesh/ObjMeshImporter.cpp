/**
 * @copydoc ObjMeshImporter.h
 */

#include "ObjMeshImporter.h"
#include "../fbx_mesh/FbxMeshImporter.h"

#include "Horo/Assets/AssetRegistry.h"
#include "../../../AssetErrors.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Assets
{
namespace
{

void WriteLE32(std::vector<std::uint8_t> &out, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF));
}

void WriteFloat(std::vector<std::uint8_t> &out, float value)
{
    std::uint32_t raw;
    std::memcpy(&raw, &value, sizeof(raw));
    WriteLE32(out, raw);
}

struct Vec3 { float x = 0, y = 0, z = 0; };

struct FaceVertex
{
    int positionIndex = -1;
    int texcoordIndex = -1;
    int normalIndex = -1;
};

struct ObjData
{
    std::vector<Vec3> positions;
    std::vector<Vec3> texcoords;
    std::vector<Vec3> normals;
    std::vector<std::vector<FaceVertex>> faces;
    std::vector<std::string> warnings;
};

FaceVertex ParseFaceVertex(std::string_view token)
{
    FaceVertex fv;
    const auto slash1 = token.find('/');
    if (slash1 == std::string_view::npos)
    {
        fv.positionIndex = std::stoi(std::string(token));
        return fv;
    }
    fv.positionIndex = std::stoi(std::string(token.substr(0, slash1)));
    const auto slash2 = token.find('/', slash1 + 1);
    if (slash2 == std::string_view::npos)
    {
        if (const std::string tcStr(token.substr(slash1 + 1)); !tcStr.empty()) fv.texcoordIndex = std::stoi(tcStr);
        return fv;
    }
    if (const std::string tcStr(token.substr(slash1 + 1, slash2 - slash1 - 1)); !tcStr.empty()) fv.texcoordIndex = std::stoi(tcStr);
    if (const std::string nStr(token.substr(slash2 + 1)); !nStr.empty()) fv.normalIndex = std::stoi(nStr);
    return fv;
}

Result<ObjData> ParseObj(std::string_view source)
{
    ObjData data;
    std::istringstream stream{std::string(source)};
    std::string line;
    int lineNumber = 0;

    while (std::getline(stream, line))
    {
        ++lineNumber;
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.erase(0, 1);
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ls(line);
        std::string token;
        ls >> token;

        if (token == "v")
        {
            Vec3 v;
            if (!(ls >> v.x >> v.y >> v.z)) continue;
            data.positions.push_back(v);
        }
        else if (token == "vt")
        {
            Vec3 vt;
            vt.z = 0.0f;
            ls >> vt.x >> vt.y;
            data.texcoords.push_back(vt);
        }
        else if (token == "vn")
        {
            Vec3 vn;
            if (!(ls >> vn.x >> vn.y >> vn.z)) continue;
            data.normals.push_back(vn);
        }
        else if (token == "f")
        {
            std::vector<FaceVertex> face;
            std::string faceToken;
            while (ls >> faceToken) face.push_back(ParseFaceVertex(faceToken));
            if (face.size() < 3) continue;
            if (face.size() > 3)
            {
                for (std::size_t i = 1; i + 1 < face.size(); ++i)
                    data.faces.push_back({face[0], face[i], face[i + 1]});
            }
            else data.faces.push_back(std::move(face));
        }
    }

    if (data.positions.empty())
        return Result<ObjData>::Failure(MakeError(ImportErrors::ObjNoVertices));
    return Result<ObjData>::Success(std::move(data));
}

class ObjMeshImporter final : public IAssetImporter
{
  public:
    [[nodiscard]] Result<PreparedAssetImport> Import(
        const AssetImportInput &input,
        const CancellationToken &cancellation) const override
    {
        if (cancellation.IsCancellationRequested())
            return Result<PreparedAssetImport>::Failure(MakeError(ImportErrors::ImportCancelled));

        std::string_view source(reinterpret_cast<const char *>(input.sourceBytes.data()), input.sourceBytes.size());
        auto objResult = ParseObj(source);
        if (objResult.HasError())
            return Result<PreparedAssetImport>::Failure(objResult.ErrorValue());

        const auto &obj = objResult.Value();
        PreparedAssetImport result;
        result.type = AssetTypeId::Parse("core.mesh").Value();

        float minX = obj.positions[0].x, minY = obj.positions[0].y, minZ = obj.positions[0].z;
        float maxX = minX, maxY = minY, maxZ = minZ;
        for (const auto &p : obj.positions)
        {
            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
            minZ = std::min(minZ, p.z); maxZ = std::max(maxZ, p.z);
        }

        auto &payload = result.editorPayload;
        WriteLE32(payload, 1);
        WriteLE32(payload, static_cast<std::uint32_t>(obj.positions.size()));
        WriteLE32(payload, static_cast<std::uint32_t>(obj.faces.size()));
        WriteFloat(payload, minX); WriteFloat(payload, minY); WriteFloat(payload, minZ);
        WriteFloat(payload, maxX); WriteFloat(payload, maxY); WriteFloat(payload, maxZ);

        std::uint32_t posSize = static_cast<std::uint32_t>(obj.positions.size() * 3 * sizeof(float));
        std::uint32_t tcSize = static_cast<std::uint32_t>(obj.texcoords.size() * 2 * sizeof(float));
        std::uint32_t nSize = static_cast<std::uint32_t>(obj.normals.size() * 3 * sizeof(float));
        WriteLE32(payload, posSize);
        WriteLE32(payload, tcSize);
        WriteLE32(payload, nSize);

        for (const auto &p : obj.positions) { WriteFloat(payload, p.x); WriteFloat(payload, p.y); WriteFloat(payload, p.z); }
        for (const auto &t : obj.texcoords) { WriteFloat(payload, t.x); WriteFloat(payload, t.y); }
        for (const auto &n : obj.normals) { WriteFloat(payload, n.x); WriteFloat(payload, n.y); WriteFloat(payload, n.z); }

        return Result<PreparedAssetImport>::Success(std::move(result));
    }
};

} // namespace

std::shared_ptr<const IAssetImporter> CreateObjMeshImporter()
{
    return std::make_shared<const ObjMeshImporter>();
}

Result<void> RegisterObjMeshImporter(AssetImporterCatalog &catalog)
{
    auto meshType = AssetTypeId::Parse("core.mesh");
    if (meshType.HasError()) return Result<void>::Failure(meshType.ErrorValue());

    return catalog.Register(AssetImporterContribution{
        .contributionId = "horo.asset-importer.obj-mesh",
        .packageId = "horo.builtin.assets",
        .moduleId = "horo.builtin.assets.importer",
        .version = "1.0.0",
        .fileExtensions = {"obj"},
        .assetTypes = {meshType.Value()},
        .settings = {
            ImportSettingDescriptor{
                .id = "coordinateSystem", .labelKey = "Coordinate System",
                .descriptionKey = "Target coordinate system.", .kind = ImportSettingKind::Choice,
                .defaultValue = std::string{"Y-up (engine)"},
                .choices = {
                    {.id="yup", .labelKey="Y-up (engine)", .value=std::string{"Y-up (engine)"}},
                    {.id="zup", .labelKey="Z-up", .value=std::string{"Z-up"}},
                },
            },
            ImportSettingDescriptor{
                .id = "unitScale", .labelKey = "Unit Scale",
                .descriptionKey = "Scale factor from source units to engine units.",
                .kind = ImportSettingKind::Float, .defaultValue = 1.0, .minimum = 0.001, .maximum = 1000.0,
            },
            ImportSettingDescriptor{
                .id = "importNormals", .labelKey = "Import Normals",
                .descriptionKey = "How vertex normals are determined.", .kind = ImportSettingKind::Choice,
                .defaultValue = std::string{"Import from source"},
                .choices = {
                    {.id="import", .labelKey="Import from source", .value=std::string{"Import from source"}},
                    {.id="smooth", .labelKey="Calculate smooth", .value=std::string{"Calculate smooth"}},
                    {.id="flat", .labelKey="Calculate flat", .value=std::string{"Calculate flat"}},
                },
            },
        },
        .builtIn = true,
        .strategy = CreateObjMeshImporter(),
    });
}

Result<void> RegisterAllBuiltinImporters(AssetImporterCatalog &catalog)
{
    auto r = RegisterObjMeshImporter(catalog);
    if (r.HasError()) return r;
    r = RegisterFbxMeshImporter(catalog);
    return r;
}

} // namespace Horo::Assets
