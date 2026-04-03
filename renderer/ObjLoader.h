#pragma once
#include <string>

#include "renderer/Mesh.h"

namespace Horo {

namespace ObjLoader {
// Load a triangulated .obj file (v/vt/vn lines, triangulated f lines).
// Throws std::runtime_error on failure.
Mesh Load(const std::string& path);

}  // namespace ObjLoader

}  // namespace Horo
