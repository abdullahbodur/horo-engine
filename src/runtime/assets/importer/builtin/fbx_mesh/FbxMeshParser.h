#pragma once

#include "Horo/Foundation/Result.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Horo::Assets {
/** @brief Rendering-neutral geometry extracted from an FBX source document. */
struct FbxMeshGeometry {
    std::vector<std::array<float, 3>> positions;
    std::vector<std::uint32_t> triangleIndices;
};

/**
 * @brief Extracts all instanced mesh geometry from an ASCII or binary FBX scene.
 * @param source Complete FBX source bytes.
 * @return Parsed positions and triangulated polygon indices.
 */
[[nodiscard]] Result<FbxMeshGeometry> ParseFbxMesh(std::span<const std::uint8_t> source);
} // namespace Horo::Assets
