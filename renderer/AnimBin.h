/** @file AnimBin.h
 *  @brief Engine-native binary serialization for animation clips.
 *
 *  AnimBin is the runtime artifact produced by FBX animation import (HORO-108).
 *  It stores one or more @ref AnimationClip records, each holding per-bone
 *  position / rotation / scale tracks with their own time arrays. Tracks use
 *  uniform sampling (typically 30 fps) so the runtime sampler does not have
 *  to interpolate FBX's tangent / curve shapes.
 *
 *  Layout (all multi-byte integers are little-endian):
 *  @code
 *  offset  size  field
 *  0       4     magic       'H','R','A','N' (HoroAnimBin)
 *  4       4     version     uint32_t, currently @ref kAnimBinVersion
 *  8       4     clipCount   uint32_t
 *  12      4     reserved0   uint32_t, write 0
 *  16      4     reserved1   uint32_t, write 0
 *  20      4     reserved2   uint32_t, write 0
 *  24      4     reserved3   uint32_t, write 0
 *  28      4     reserved4   uint32_t, write 0
 *  32      ...   clips       repeated for clipCount
 *  @endcode
 *
 *  Per clip:
 *  @code
 *  4         nameLength   uint32_t
 *  N         name         ASCII bytes (no terminator)
 *  4         duration     float32 (seconds)
 *  4         trackCount   uint32_t
 *  ...       tracks       repeated for trackCount
 *  @endcode
 *
 *  Per track:
 *  @code
 *  4         boneIndex             int32_t (index into the matching skeleton)
 *  4         positionKeyCount      uint32_t
 *  K * 4     positionTimes         K float32
 *  K * 12    positions             K Vec3 (x, y, z)
 *  4         rotationKeyCount      uint32_t
 *  R * 4     rotationTimes         R float32
 *  R * 16    rotations             R Quaternion (x, y, z, w)
 *  4         scaleKeyCount         uint32_t
 *  S * 4     scaleTimes            S float32
 *  S * 12    scales                S Vec3 (x, y, z)
 *  @endcode
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "renderer/AnimationClip.h"

namespace Horo::AnimBin {
    /** @brief Format magic bytes ('H','R','A','N'). */
    inline constexpr uint32_t kAnimBinMagic =
        (static_cast<uint32_t>('H')) |
        (static_cast<uint32_t>('R') << 8) |
        (static_cast<uint32_t>('A') << 16) |
        (static_cast<uint32_t>('N') << 24);

    /** @brief Current on-disk schema version. */
    inline constexpr uint32_t kAnimBinVersion = 1;

    /** @brief Outcome of a read operation. */
    struct ReadResult {
        bool ok = false;                       /**< True on success. */
        std::vector<AnimationClip> clips;      /**< Decoded animation clips. */
        std::string error;                     /**< Human-readable diagnostic on failure; empty on success. */
    };

    /** @brief Outcome of a write operation. */
    struct WriteResult {
        bool ok = false;       /**< True on success. */
        std::string error;     /**< Human-readable diagnostic on failure; empty on success. */
    };

    /** @brief Writes a list of animation clips to disk in the engine-native format.
     *  @param destPath Absolute or project-relative destination file path.
     *  @param clips    Clips to serialize; must not be empty.
     *  @return WriteResult with @c ok = true on success or a populated @c error.
     */
    WriteResult WriteClips(const std::string &destPath,
                            const std::vector<AnimationClip> &clips);

    /** @brief Reads animation clips previously produced by @ref WriteClips. */
    ReadResult ReadClips(const std::string &sourcePath);
} // namespace Horo::AnimBin
