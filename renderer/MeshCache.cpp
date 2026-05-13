/** @file MeshCache.cpp
 *  @brief File-path-keyed runtime mesh cache implementation.
 *
 *  Loads supported source formats once per absolute path; unsupported formats
 *  fall back to a box mesh with an explicit warning. The previous "rewrite
 *  .fbx/.glb/.gltf → .obj" hack has been removed (HORO-99) — the runtime is
 *  now honest about which formats it understands.
 *
 *  Currently supported runtime formats: @c .obj. Engine-native @c .mesh.bin
 *  loading is planned for HORO-100 and will be wired in by extending the
 *  extension dispatch below; importer-side production of @c .mesh.bin is
 *  already in place under HORO-94.
 */
#include "renderer/MeshCache.h"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "core/Logger.h"
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

        /** @brief Returns a fallback box mesh and logs that the format is unsupported at runtime. */
        std::shared_ptr<Mesh> UnsupportedFormatFallback(const std::string &path) {
            LogWarn("MeshCache::Get — runtime cannot load '{}' (only .obj is "
                    "currently supported); using fallback box mesh", path);
            return std::make_shared<Mesh>(Mesh::CreateBox());
        }
    } // namespace

    std::shared_ptr<Mesh> MeshCache::Get(const std::string &path) {
        if (auto it = m_cache.find(path); it != m_cache.end())
            return it->second;

        std::shared_ptr<Mesh> mesh = EndsWithInsensitive(path, ".obj")
                                         ? LoadObjOrFallback(path)
                                         : UnsupportedFormatFallback(path);

        return m_cache.try_emplace(path, mesh).first->second;
    }
} // namespace Horo
