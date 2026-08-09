#pragma once

/**
 * @file MeshEditorPayload.h
 * @brief Canonical schema contract for built-in mesh editor payloads.
 */

#include <cstdint>

namespace Horo::Assets {
    /** @brief Current schema written and consumed by every built-in mesh editor-payload path. */
    inline constexpr std::uint32_t MeshEditorPayloadSchemaVersion = 2;
}  // namespace Horo::Assets
