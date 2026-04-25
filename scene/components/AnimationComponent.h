#pragma once

#include <memory>
#include <vector>

#include "math/Mat4.h"
#include "renderer/AnimationClip.h"
#include "renderer/Skeleton.h"

namespace Monolith {
struct AnimationComponent {
  std::shared_ptr<Skeleton> skeleton;
  std::shared_ptr<AnimationClip> currentClip;
  float time = 0.0f;  // current playback position in seconds
  float speed = 1.0f; // playback speed multiplier
  bool loop = true;
  bool playing = true;

  // Computed by AnimationSystem each frame.
  // Size == skeleton->BoneCount() when valid.
  // Contains the final skinning matrices (globalTransform * inverseBindMatrix).
  std::vector<Mat4> boneMatrices;
};
} // namespace Monolith
