#pragma once
#include <stdexcept>
#include <string>

#include "renderer/Mesh.h"

namespace Monolith {

namespace ObjLoader {
class ObjLoaderException : public std::runtime_error {
 public:
  explicit ObjLoaderException(const std::string& message) : std::runtime_error(message) {}
};

// Load a triangulated .obj file (v/vt/vn lines, triangulated f lines).
// Throws ObjLoaderException on failure.
Mesh Load(const std::string& path);

struct ObjAABB
{
    Vec3 min = {  1e30f,  1e30f,  1e30f };
    Vec3 max = { -1e30f, -1e30f, -1e30f };
    bool valid = false;
};
// Parse only vertex positions; no GPU upload. Returns {valid=false} on any error.
ObjAABB ComputeAABB(const std::string& path);

// Parse the MTL file referenced by the given OBJ and return the path to
// the map_Kd (diffuse) texture, resolved relative to the OBJ directory.
// Returns empty string if the MTL is absent or has no map_Kd.
std::string FindDiffuseTexture(const std::string& objPath);

}  // namespace ObjLoader

}  // namespace Monolith
