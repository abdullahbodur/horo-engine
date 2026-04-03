#pragma once
#include <string>

#include "renderer/Mesh.h"

namespace Monolith {

namespace ObjLoader {
// Load a triangulated .obj file (v/vt/vn lines, triangulated f lines).
// Throws std::runtime_error on failure.
Mesh Load(const std::string& path);

struct ObjAABB
{
    Vec3 min = {  1e30f,  1e30f,  1e30f };
    Vec3 max = { -1e30f, -1e30f, -1e30f };
    bool valid = false;
};
// Parse only vertex positions; no GPU upload. Returns {valid=false} on any error.
ObjAABB ComputeAABB(const std::string& path);

}  // namespace ObjLoader

}  // namespace Monolith
