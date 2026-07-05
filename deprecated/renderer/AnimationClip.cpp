#include "renderer/AnimationClip.h"

#include <algorithm>
#include <utility>

namespace Horo {
    // AnimationClip — public interface

    void AnimationClip::AddTrack(BoneTrack track) {
        m_tracks.push_back(std::move(track));
    }

    const std::vector<BoneTrack> &AnimationClip::GetTracks() const {
        return m_tracks;
    }

    void AnimationClip::Sample(float time,
                               std::vector<Mat4> &outLocalTransforms) const {
        // Default every bone to the identity matrix so bones without tracks are
        // unaffected by the current animation.
        for (auto &mat: outLocalTransforms) {
            mat = Mat4::Identity();
        }

        for (const BoneTrack &track: m_tracks) {
            const int idx = track.boneIndex;
            if (idx < 0 || idx >= static_cast<int>(outLocalTransforms.size())) {
                continue; // safety guard against malformed track data
            }

            // Sample each channel — fall back to neutral values when the channel is
            // absent.
            const Vec3 pos =
                    track.positions.empty() ? Vec3::Zero() : SamplePositions(track, time);

            const Quaternion rot = track.rotations.empty()
                                       ? Quaternion::Identity()
                                       : SampleRotations(track, time);

            const Vec3 scale =
                    track.scales.empty() ? Vec3::One() : SampleScales(track, time);

            // Compose local TRS: T * R * S
            // Mat4::Rotate() builds a pure rotation matrix from a quaternion.
            outLocalTransforms[static_cast<size_t>(idx)] =
                    Mat4::Translate(pos) * Mat4::Rotate(rot) * Mat4::Scale(scale);
        }
    }

    // Private helpers — keyframe lookup and interpolation

    // FindInterval returns the index of the last keyframe whose time is <= `time`.
    // Uses std::lower_bound for O(log n) lookup, then steps back by one.
    //
    // The returned index `lo` is guaranteed to be in [0, count-2] when count >= 2,
    // so callers can safely access times[lo], times[lo+1], values[lo],
    // values[lo+1]. A return value of -1 signals "time is before the first key"
    // (clamp to first). A return value of count-1 signals "time is at or beyond the
    // last key".
    static int FindInterval(const std::vector<float> &times, float time) {
        if (times.size() == 1) {
            return 0; // single key — always return it
        }

        // lower_bound gives first time >= query time
        auto it = std::ranges::lower_bound(times, time);

        if (it == times.begin()) {
            // time is before or exactly at the first keyframe
            return 0;
        }
        if (it == times.end()) {
            // time is at or beyond the last keyframe
            return static_cast<int>(times.size()) - 1;
        }

        // it points to the first key strictly greater than `time`; step back to get
        // the last key that is <= `time`.
        --it;
        return static_cast<int>(it - times.begin());
    }

    Vec3 AnimationClip::SamplePositions(const BoneTrack &track, float time) {
        const auto &times = track.positionTimes;
        const auto &values = track.positions;

        const int lo = FindInterval(times, time);

        // Clamp: at or beyond last key
        if (lo >= static_cast<int>(times.size()) - 1) {
            return values.back();
        }

        const int hi = lo + 1;

        // Compute normalised blend factor clamped to [0, 1].
        // Clamping handles queries before the first key (negative raw t) and
        // floating-point overshoot past the last interval.
        const float span =
                times[static_cast<size_t>(hi)] - times[static_cast<size_t>(lo)];
        const float raw =
                (span > 0.0f) ? (time - times[static_cast<size_t>(lo)]) / span : 0.0f;
        const float t = std::clamp(raw, 0.0f, 1.0f);

        return Vec3::Lerp(values[static_cast<size_t>(lo)],
                          values[static_cast<size_t>(hi)], t);
    }

    Quaternion AnimationClip::SampleRotations(const BoneTrack &track, float time) {
        const auto &times = track.rotationTimes;
        const auto &values = track.rotations;

        const int lo = FindInterval(times, time);

        if (lo >= static_cast<int>(times.size()) - 1) {
            return values.back().Normalized();
        }

        const int hi = lo + 1;

        const float span =
                times[static_cast<size_t>(hi)] - times[static_cast<size_t>(lo)];
        const float raw =
                (span > 0.0f) ? (time - times[static_cast<size_t>(lo)]) / span : 0.0f;
        const float t = std::clamp(raw, 0.0f, 1.0f);

        return Quaternion::Slerp(values[static_cast<size_t>(lo)],
                                 values[static_cast<size_t>(hi)], t);
    }

    Vec3 AnimationClip::SampleScales(const BoneTrack &track, float time) {
        const auto &times = track.scaleTimes;
        const auto &values = track.scales;

        const int lo = FindInterval(times, time);

        if (lo >= static_cast<int>(times.size()) - 1) {
            return values.back();
        }

        const int hi = lo + 1;

        const float span =
                times[static_cast<size_t>(hi)] - times[static_cast<size_t>(lo)];
        const float raw =
                (span > 0.0f) ? (time - times[static_cast<size_t>(lo)]) / span : 0.0f;
        const float t = std::clamp(raw, 0.0f, 1.0f);

        return Vec3::Lerp(values[static_cast<size_t>(lo)],
                          values[static_cast<size_t>(hi)], t);
    }
} // namespace Horo
