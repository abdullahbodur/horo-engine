/** @file AnimBin.cpp
 *  @brief Animation clip binary writer / reader. See AnimBin.h.
 *
 *  AnimationClip exposes its tracks via @c AddTrack / @c GetTracks. We expand the
 *  read path through @c AddTrack so the deserialized clips behave identically to
 *  freshly-built ones.
 */
#include "renderer/AnimBin.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>

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

        template <typename T>
        bool WriteBytes(std::ofstream &stream, const T *data, std::size_t size) {
            stream.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
            return stream.good();
        }

        template <typename T>
        bool ReadBytes(std::ifstream &stream, T *data, std::size_t size) {
            stream.read(reinterpret_cast<char *>(data), static_cast<std::streamsize>(size));
            return stream.good();
        }

        bool WriteFloatArray(std::ofstream &stream, const std::vector<float> &v) {
            if (const auto count = static_cast<uint32_t>(v.size());
                !WriteBytes(stream, &count, sizeof(count)))
                return false;
            return v.empty() || WriteBytes(stream, v.data(), v.size() * sizeof(float));
        }

        bool ReadFloatArray(std::ifstream &stream, std::vector<float> &v) {
            uint32_t count = 0;
            if (!ReadBytes(stream, &count, sizeof(count)))
                return false;
            v.resize(count);
            return count == 0 || ReadBytes(stream, v.data(), count * sizeof(float));
        }

        bool WriteVec3Array(std::ofstream &stream, const std::vector<Vec3> &v) {
            if (const auto count = static_cast<uint32_t>(v.size());
                !WriteBytes(stream, &count, sizeof(count)))
                return false;
            for (const Vec3 &p: v) {
                const std::array<float, 3> buf = {p.x, p.y, p.z};
                if (!WriteBytes(stream, buf.data(), sizeof(buf)))
                    return false;
            }
            return true;
        }

        bool ReadVec3Array(std::ifstream &stream, std::vector<Vec3> &v) {
            uint32_t count = 0;
            if (!ReadBytes(stream, &count, sizeof(count)))
                return false;
            v.resize(count);
            for (uint32_t i = 0; i < count; ++i) {
                std::array<float, 3> buf{};
                if (!ReadBytes(stream, buf.data(), sizeof(buf)))
                    return false;
                v[i] = {buf[0], buf[1], buf[2]};
            }
            return true;
        }

        bool WriteQuatArray(std::ofstream &stream, const std::vector<Quaternion> &v) {
            if (const auto count = static_cast<uint32_t>(v.size());
                !WriteBytes(stream, &count, sizeof(count)))
                return false;
            for (const Quaternion &q: v) {
                const std::array<float, 4> buf = {q.x, q.y, q.z, q.w};
                if (!WriteBytes(stream, buf.data(), sizeof(buf)))
                    return false;
            }
            return true;
        }

        bool ReadQuatArray(std::ifstream &stream, std::vector<Quaternion> &v) {
            uint32_t count = 0;
            if (!ReadBytes(stream, &count, sizeof(count)))
                return false;
            v.resize(count);
            for (uint32_t i = 0; i < count; ++i) {
                std::array<float, 4> buf{};
                if (!ReadBytes(stream, buf.data(), sizeof(buf)))
                    return false;
                v[i] = Quaternion{buf[0], buf[1], buf[2], buf[3]};
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

        std::error_code ec;
        const std::filesystem::path path(destPath);
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec) {
                result.error =
                        "AnimBin write: cannot create destination directory: " +
                        ec.message();
                return result;
            }
        }

        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream.is_open()) {
            result.error = "AnimBin write: cannot open destination file.";
            return result;
        }

        Header header{};
        header.magic = kAnimBinMagic;
        header.version = kAnimBinVersion;
        header.clipCount = static_cast<uint32_t>(clips.size());
        if (!WriteBytes(stream, &header, sizeof(header))) {
            result.error = "AnimBin write: failed writing header.";
            return result;
        }

        for (const AnimationClip &clip: clips) {
            if (const auto nameLength = static_cast<uint32_t>(clip.name.size());
                !WriteBytes(stream, &nameLength, sizeof(nameLength)) ||
                (nameLength > 0 &&
                 !WriteBytes(stream, clip.name.data(), nameLength))) {
                result.error = "AnimBin write: failed writing clip name.";
                return result;
            }
            if (const float duration = clip.duration;
                !WriteBytes(stream, &duration, sizeof(duration))) {
                result.error = "AnimBin write: failed writing clip duration.";
                return result;
            }
            const std::vector<BoneTrack> &tracks = clip.GetTracks();
            if (const auto trackCount = static_cast<uint32_t>(tracks.size());
                !WriteBytes(stream, &trackCount, sizeof(trackCount))) {
                result.error = "AnimBin write: failed writing track count.";
                return result;
            }
            for (const BoneTrack &track: tracks) {
                if (const auto boneIndex = static_cast<int32_t>(track.boneIndex);
                    !WriteBytes(stream, &boneIndex, sizeof(boneIndex))) {
                    result.error = "AnimBin write: failed writing track bone index.";
                    return result;
                }
                if (!WriteFloatArray(stream, track.positionTimes) ||
                    !WriteVec3Array(stream, track.positions) ||
                    !WriteFloatArray(stream, track.rotationTimes) ||
                    !WriteQuatArray(stream, track.rotations) ||
                    !WriteFloatArray(stream, track.scaleTimes) ||
                    !WriteVec3Array(stream, track.scales)) {
                    result.error = "AnimBin write: failed writing track keys.";
                    return result;
                }
            }
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
        if (std::error_code ec; !std::filesystem::is_regular_file(sourcePath, ec) || ec) {
            result.error = "AnimBin read: source path is not a regular file.";
            return result;
        }
        std::ifstream stream(sourcePath, std::ios::binary);
        if (!stream.is_open()) {
            result.error = "AnimBin read: cannot open source file.";
            return result;
        }
        Header header{};
        if (!ReadBytes(stream, &header, sizeof(header))) {
            result.error = "AnimBin read: file too short for header.";
            return result;
        }
        if (header.magic != kAnimBinMagic) {
            result.error = "AnimBin read: bad magic bytes; not a HoroAnimBin file.";
            return result;
        }
        if (header.version != kAnimBinVersion) {
            result.error = std::format("AnimBin read: unsupported version {} (expected {}).",
                                       header.version, kAnimBinVersion);
            return result;
        }

        result.clips.reserve(header.clipCount);
        for (uint32_t c = 0; c < header.clipCount; ++c) {
            AnimationClip clip;
            uint32_t nameLength = 0;
            if (!ReadBytes(stream, &nameLength, sizeof(nameLength))) {
                result.error = "AnimBin read: failed reading clip name length.";
                return result;
            }
            if (nameLength > 0) {
                clip.name.resize(nameLength);
                if (!ReadBytes(stream, clip.name.data(), nameLength)) {
                    result.error = "AnimBin read: failed reading clip name.";
                    return result;
                }
            }
            float duration = 0.0f;
            if (!ReadBytes(stream, &duration, sizeof(duration))) {
                result.error = "AnimBin read: failed reading clip duration.";
                return result;
            }
            clip.duration = duration;
            uint32_t trackCount = 0;
            if (!ReadBytes(stream, &trackCount, sizeof(trackCount))) {
                result.error = "AnimBin read: failed reading track count.";
                return result;
            }
            for (uint32_t t = 0; t < trackCount; ++t) {
                BoneTrack track;
                int32_t boneIndex = 0;
                if (!ReadBytes(stream, &boneIndex, sizeof(boneIndex))) {
                    result.error = "AnimBin read: failed reading track bone index.";
                    return result;
                }
                track.boneIndex = static_cast<int>(boneIndex);
                if (!ReadFloatArray(stream, track.positionTimes) ||
                    !ReadVec3Array(stream, track.positions) ||
                    !ReadFloatArray(stream, track.rotationTimes) ||
                    !ReadQuatArray(stream, track.rotations) ||
                    !ReadFloatArray(stream, track.scaleTimes) ||
                    !ReadVec3Array(stream, track.scales)) {
                    result.error = "AnimBin read: failed reading track keys.";
                    return result;
                }
                clip.AddTrack(std::move(track));
            }
            result.clips.push_back(std::move(clip));
        }

        result.ok = true;
        return result;
    }
} // namespace Horo::AnimBin
