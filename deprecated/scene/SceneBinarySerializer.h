/**
 * @file SceneBinarySerializer.h
 * @brief Binary serialization of SceneProjectModel using MessagePack (via nlohmann::json).
 *
 * Provides lossless round-trip serialization of the typed engine scene model
 * to and from a compact binary representation.  The binary format is standard
 * MessagePack produced by nlohmann::json::to_msgpack; no custom framing or
 * headers are added — the caller is responsible for file-level framing if needed.
 */
#pragma once

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace Horo {

// Forward declarations
struct SceneProjectModel;

/**
 * @brief Exception thrown when binary scene data cannot be parsed.
 *
 * Wraps @c std::runtime_error with message strings suitable for logs or UI.
 */
class SceneBinarySerializerException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/**
 * @brief Serializes a SceneProjectModel to a MessagePack binary buffer.
 *
 * @param model  The typed scene-project model to serialize.
 * @return       A byte vector containing the MessagePack representation.
 *
 * @note The buffer is suitable for direct file I/O or embedding in a
 *       larger archive format (e.g., horopak).
 */
std::vector<uint8_t>
SerializeSceneToBinary(const SceneProjectModel &model);

/**
 * @brief Deserializes a MessagePack binary buffer back into a SceneProjectModel.
 *
 * @param data  Raw bytes previously produced by SerializeSceneToBinary().
 * @return      A fully reconstructed SceneProjectModel.
 *
 * @throws SceneBinarySerializerException if the data is not valid MessagePack
 *         or the structure does not match the expected schema.
 */
SceneProjectModel
DeserializeSceneFromBinary(const std::vector<uint8_t> &data);

} // namespace Horo
