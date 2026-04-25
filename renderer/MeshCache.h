#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "renderer/Mesh.h"

namespace Monolith {
struct MeshCacheStringHash {
  using is_transparent = void;

  std::size_t operator()(std::string_view sv) const {
    return std::hash<std::string_view>{}(sv);
  }
};

// File-path keyed cache that avoids reloading the same .obj more than once.
class MeshCache {
public:
  // Return cached mesh, or load it if not yet cached.
  // Throws std::runtime_error if the file cannot be loaded.
  std::shared_ptr<Mesh> Get(const std::string &path);

  void Clear() { m_cache.clear(); }

private:
  std::unordered_map<std::string, std::shared_ptr<Mesh>, MeshCacheStringHash,
                     std::equal_to<>>
      m_cache;
};
} // namespace Monolith
