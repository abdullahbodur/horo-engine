#pragma once

#include <string>
#include <vector>

#include "math/Mat4.h"
#include "math/Quaternion.h"
#include "math/Vec3.h"

namespace Monolith {

// BoneTrack — all keyframe data for a single bone within one animation clip.
//
// Each channel (position, rotation, scale) has its own independent time array,
// enabling sparse tracks: a bone can carry only rotation keys, for example.
// When a channel's time/value arrays are empty that channel is considered absent
// and the sampler will fall back to a sensible default (zero translation, identity
// rotation, unit scale).
//
// Invariant: positionTimes.size() == positions.size()
//            rotationTimes.size() == rotations.size()
//            scaleTimes.size()    == scales.size()
struct BoneTrack {
  int boneIndex;

  std::vector<float>      positionTimes;
  std::vector<Vec3>       positions;

  std::vector<float>      rotationTimes;
  std::vector<Quaternion> rotations;

  std::vector<float>      scaleTimes;
  std::vector<Vec3>       scales;
};

// AnimationClip — a named, time-bounded collection of BoneTracks.
//
// Sampling is done with linear interpolation (LERP for position/scale,
// SLERP for rotation).  The clip does not loop internally; the caller is
// responsible for clamping or wrapping `time` to [0, duration] before
// calling Sample().
class AnimationClip {
 public:
  std::string name;
  float       duration = 0.0f;  // total clip length in seconds

  // Append a track.  boneIndex must be a valid index into the owning Skeleton.
  void AddTrack(BoneTrack track);

  const std::vector<BoneTrack>& GetTracks() const;

  // Sample the clip at `time` and fill outLocalTransforms with per-bone local
  // TRS matrices.
  //
  // outLocalTransforms must be pre-sized to Skeleton::BoneCount().  Bones not
  // referenced by any track receive Mat4::Identity().
  //
  // Result per bone: Translate(pos) * Mat4::Rotate(rot) * Scale(scale)
  void Sample(float time, std::vector<Mat4>& outLocalTransforms) const;

 private:
  std::vector<BoneTrack> m_tracks;

  // --- Keyframe interpolation helpers ---

  // Returns the interpolated position for `t`.
  // Boundary behaviour: clamp to first/last key; single-key tracks return the key.
  static Vec3 SamplePositions(const BoneTrack& track, float time);

  // Returns the interpolated (slerped) rotation for `t`.
  static Quaternion SampleRotations(const BoneTrack& track, float time);

  // Returns the interpolated scale for `t`.
  static Vec3 SampleScales(const BoneTrack& track, float time);
};

}  // namespace Monolith
