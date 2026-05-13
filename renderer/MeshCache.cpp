/** @file MeshCache.cpp
 *  @brief File-path-keyed runtime mesh cache implementation.
 *
 *  Loads supported source formats once per absolute path; unsupported formats
 *  fall back to a box mesh with an explicit warning.
 *
 *  Supported runtime formats:
 *  - @c .obj        — Wavefront OBJ via @ref ObjLoader::Load (legacy authoring path).
 *  - @c .mesh.bin   — Engine-native binary produced by asset importers
 *                     (FBX importer in HORO-94; future importers feed the same
 *                     format). Loaded via @ref MeshBin::ReadStaticMesh and
 *                     uploaded into a fresh @ref Mesh through @c SetData.
 *
 *  The earlier silent @c .fbx/.glb/.gltf → .obj rewrite was removed in HORO-99;
 *  unrecognised extensions now fall back to a box mesh with a warning that
 *  names the actual unsupported extension.
 */
#include "renderer/MeshCache.h"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "core/Logger.h"
#include "renderer/MeshBin.h"
#include "renderer/ObjLoader.h"

namespace Horo {
    namespace {
        /** @brief Case-insensitive suffix test for ASCII paths. */
        bool EndsWithInsensitive(std::string_view s, std::string_view suffix) {
            if (s.size() < suffix.size())
                return false;
            return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin(),
                              [](char a, char b) {
                                  return std::tolower(static_cast<unsigned char>(a)) ==
                                         std::tolower(static_cast<unsigned char>(b));
                              });
        }

        /** @brief Loads a Wavefront OBJ via @ref ObjLoader, returning a fallback box on failure.
         *  @param path Resolved source path; expected to end in ".obj" (case-insensitive).
         *  @return Newly constructed shared @ref Mesh — never null.
         */
        std::shared_ptr<Mesh> LoadObjOrFallback(const std::string &path) {
            try {
                return std::make_shared<Mesh>(ObjLoader::Load(path));
            } catch (const ObjLoader::ObjLoaderException &e) {
                LogWarn("MeshCache::Get — failed to load '{}': {}; using fallback box mesh",
                        path, e.what());
                return std::make_shared<Mesh>(Mesh::CreateBox());
            }
        }

        /** @brief Loads an engine-native @c .mesh.bin via @ref MeshBin::ReadStaticMesh.
         *  @param path Resolved source path; expected to end in ".mesh.bin" (case-insensitive).
         *  @return Newly constructed shared @ref Mesh — never null. Falls back to a box mesh on any
         *          read or upload failure (with a warning that names the underlying error).
         */
        std::shared_ptr<Mesh> LoadMeshBinOrFallback(const std::string &path) {
            const MeshBin::ReadResult readResult = MeshBin::ReadStaticMesh(path);
            if (!readResult.ok) {
                LogWarn("MeshCache::Get — failed to load '{}': {}; using fallback box mesh",
                        path, readResult.error);
                return std::make_shared<Mesh>(Mesh::CreateBox());
            }
            // Defensive: MeshBin::ReadStaticMesh validates header counts and
            // vertex stride, but a malformed payload could still reference
            // out-of-range indices. Reject before uploading so downstream
            // renderer code cannot read past the vertex buffer.
            const std::uint32_t vertexCount =
                    static_cast<std::uint32_t>(readResult.vertices.size());
            for (std::uint32_t index: readResult.indices) {
                if (index >= vertexCount) {
                    LogWarn("MeshCache::Get — '{}' has out-of-range index {} (vertex count {}); "
                            "using fallback box mesh",
                            path, index, vertexCount);
                    return std::make_shared<Mesh>(Mesh::CreateBox());
                }
            }
            auto mesh = std::make_shared<Mesh>();
            mesh->SetData(readResult.vertices, readResult.indices);
            return mesh;
        }

        /** @brief Returns a fallback box mesh and logs that the format is unsupported at runtime. */
        std::shared_ptr<Mesh> UnsupportedFormatFallback(const std::string &path) {
            LogWarn("MeshCache::Get — runtime cannot load '{}' (only .obj and .mesh.bin "
                    "are currently supported); using fallback box mesh", path);
            return std::make_shared<Mesh>(Mesh::CreateBox());
        }
    } // namespace

    std::shared_ptr<Mesh> MeshCache::Get(const std::string &path) {
        if (auto it = m_cache.find(path); it != m_cache.end())
            return it->second;

        std::shared_ptr<Mesh> mesh;
        if (EndsWithInsensitive(path, ".mesh.bin"))
            mesh = LoadMeshBinOrFallback(path);
        else if (EndsWithInsensitive(path, ".obj"))
            mesh = LoadObjOrFallback(path);
        else
            mesh = UnsupportedFormatFallback(path);

        return m_cache.try_emplace(path, mesh).first->second;
    }
} // namespace Horo
