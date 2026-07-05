/** @file AnimBin.cpp
 *  @brief Animation clip binary writer / reader. See AnimBin.h.
 *
 *  AnimationClip exposes its tracks via @c AddTrack / @c GetTracks. We expand the
 *  read path through @c AddTrack so the deserialized clips behave identically to
 *  freshly-built ones.
 */
#include "renderer/AnimBin.h"

#include <array>
#include <format>

#include "renderer/BinaryStream.h"

namespace Horo::AnimBin {
    namespace {
        /** @brief 32-byte header. */
        struct Header {
            uint32_t magic;
            uint32_t version;
            uint32_t clipCount;
            uint32_t reserved0;
            uint32_t reserved1;
            uint32_t reserved2;
            uint32_t reserved3;
            uint32_t reserved4;
        };

        static_assert(sizeof(Header) == 32,
                      "AnimBin header layout must remain 32 bytes; bump kAnimBinVersion before changing it.");

        bool WriteVec3Array(std::ofstream &stream, const std::vector<Vec3> &v) {
            if (const auto count = static_cast<uint32_t>(v.size());
                !BinaryStream::WriteValue(stream, count))
                return false;
            for (const Vec3 &p: v) {
                const std::array<float, 3> buf = {p.x, p.y, p.z};
                if (!BinaryStream::WriteValue(stream, buf))
                    return false;
            }
            return true;
        }

        bool ReadVec3Array(std::ifstream &stream, std::vector<Vec3> &v) {
            uint32_t count = 0;
            if (!BinaryStream::ReadValue(stream, count))
                return false;
            v.resize(count);
            for (uint32_t i = 0; i < count; ++i) {
                std::array<float, 3> buf{};
                if (!BinaryStream::ReadValue(stream, buf))
                    return false;
                v[i] = {buf[0], buf[1], buf[2]};
            }
            return true;
        }

        bool WriteQuatArray(std::ofstream &stream, const std::vector<Quaternion> &v) {
            if (const auto count = static_cast<uint32_t>(v.size());
                !BinaryStream::WriteValue(stream, count))
                return false;
            for (const Quaternion &q: v) {
                const std::array<float, 4> buf = {q.x, q.y, q.z, q.w};
                if (!BinaryStream::WriteValue(stream, buf))
                    return false;
            }
            return true;
        }

        bool ReadQuatArray(std::ifstream &stream, std::vector<Quaternion> &v) {
            uint32_t count = 0;
            if (!BinaryStream::ReadValue(stream, count))
                return false;
            v.resize(count);
            for (uint32_t i = 0; i < count; ++i) {
                std::array<float, 4> buf{};
                if (!BinaryStream::ReadValue(stream, buf))
                    return false;
                v[i] = Quaternion{buf[0], buf[1], buf[2], buf[3]};
            }
            return true;
        }

        bool WriteTrack(std::ofstream &stream, const BoneTrack &track) {
            const auto boneIndex = static_cast<int32_t>(track.boneIndex);
            return BinaryStream::WriteValue(stream, boneIndex) &&
                   BinaryStream::WriteFloatVector(stream, track.positionTimes) &&
                   WriteVec3Array(stream, track.positions) &&
                   BinaryStream::WriteFloatVector(stream, track.rotationTimes) &&
                   WriteQuatArray(stream, track.rotations) &&
                   BinaryStream::WriteFloatVector(stream, track.scaleTimes) &&
                   WriteVec3Array(stream, track.scales);
        }

        bool ReadTrack(std::ifstream &stream, BoneTrack &track) {
            int32_t boneIndex = 0;
            if (!BinaryStream::ReadValue(stream, boneIndex))
                return false;
            track.boneIndex = static_cast<int>(boneIndex);
            return BinaryStream::ReadFloatVector(stream, track.positionTimes) &&
                   ReadVec3Array(stream, track.positions) &&
                   BinaryStream::ReadFloatVector(stream, track.rotationTimes) &&
                   ReadQuatArray(stream, track.rotations) &&
                   BinaryStream::ReadFloatVector(stream, track.scaleTimes) &&
                   ReadVec3Array(stream, track.scales);
        }

        bool WriteClip(std::ofstream &stream, const AnimationClip &clip,
                       std::string *errorOut) {
            if (!BinaryStream::WriteLengthPrefixedString(stream, clip.name)) {
                *errorOut = "AnimBin write: failed writing clip name.";
                return false;
            }
            if (const float duration = clip.duration;
                !BinaryStream::WriteValue(stream, duration)) {
                *errorOut = "AnimBin write: failed writing clip duration.";
                return false;
            }
            const std::vector<BoneTrack> &tracks = clip.GetTracks();
            if (const auto trackCount = static_cast<uint32_t>(tracks.size());
                !BinaryStream::WriteValue(stream, trackCount)) {
                *errorOut = "AnimBin write: failed writing track count.";
                return false;
            }
            for (const BoneTrack &track: tracks) {
                if (!WriteTrack(stream, track)) {
                    *errorOut = "AnimBin write: failed writing track keys.";
                    return false;
                }
            }
            return true;
        }

        bool ReadClip(std::ifstream &stream, AnimationClip &clip,
                      std::string *errorOut) {
            if (!BinaryStream::ReadLengthPrefixedString(stream, clip.name)) {
                *errorOut = "AnimBin read: failed reading clip name.";
                return false;
            }
            float duration = 0.0f;
            if (!BinaryStream::ReadValue(stream, duration)) {
                *errorOut = "AnimBin read: failed reading clip duration.";
                return false;
            }
            clip.duration = duration;
            uint32_t trackCount = 0;
            if (!BinaryStream::ReadValue(stream, trackCount)) {
                *errorOut = "AnimBin read: failed reading track count.";
                return false;
            }
            for (uint32_t t = 0; t < trackCount; ++t) {
                BoneTrack track;
                if (!ReadTrack(stream, track)) {
                    *errorOut = "AnimBin read: failed reading track keys.";
                    return false;
                }
                clip.AddTrack(std::move(track));
            }
            return true;
        }

        bool ValidateHeader(const Header &header, std::string *errorOut) {
            if (header.magic != kAnimBinMagic) {
                *errorOut = "AnimBin read: bad magic bytes; not a HoroAnimBin file.";
                return false;
            }
            if (header.version != kAnimBinVersion) {
                *errorOut = std::format(
                    "AnimBin read: unsupported version {} (expected {}).",
                    header.version, kAnimBinVersion);
                return false;
            }
            return true;
        }
    } // namespace

    /** @copydoc Horo::AnimBin::WriteClips */
    WriteResult WriteClips(const std::string &destPath,
                            const std::vector<AnimationClip> &clips) {
        WriteResult result;
        if (clips.empty()) {
            result.error = "AnimBin write: empty clip list.";
            return result;
        }

        std::ofstream stream =
            BinaryStream::OpenForWrite(destPath, "AnimBin write", &result.error);
        if (!stream.is_open()) {
            return result;
        }

        Header header{};
        header.magic = kAnimBinMagic;
        header.version = kAnimBinVersion;
        header.clipCount = static_cast<uint32_t>(clips.size());
        if (!BinaryStream::WriteValue(stream, header)) {
            result.error = "AnimBin write: failed writing header.";
            return result;
        }

        for (const AnimationClip &clip: clips) {
            if (!WriteClip(stream, clip, &result.error))
                return result;
        }

        stream.flush();
        if (!stream.good()) {
            result.error = "AnimBin write: stream entered fail state during flush.";
            return result;
        }
        result.ok = true;
        return result;
    }

    /** @copydoc Horo::AnimBin::ReadClips */
    ReadResult ReadClips(const std::string &sourcePath) {
        ReadResult result;
        std::ifstream stream =
            BinaryStream::OpenForRead(sourcePath, "AnimBin read", &result.error);
        if (!stream.is_open()) {
            return result;
        }

        Header header{};
        if (!BinaryStream::ReadValue(stream, header)) {
            result.error = "AnimBin read: file too short for header.";
            return result;
        }
        if (!ValidateHeader(header, &result.error))
            return result;

        result.clips.reserve(header.clipCount);
        for (uint32_t c = 0; c < header.clipCount; ++c) {
            AnimationClip clip;
            if (!ReadClip(stream, clip, &result.error))
                return result;
            result.clips.push_back(std::move(clip));
        }

        result.ok = true;
        return result;
    }
} // namespace Horo::AnimBin
