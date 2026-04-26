// test_animation.cpp
//
// Unit tests for the skeletal animation subsystem of Horo Engine.
//
// Coverage:
//   - Skeleton: construction, bone lookup, IsValid(),
//   parentIndex/inverseBindMatrix storage
//   - AnimationClip: duration, track management, Sample() interpolation
//   (position/rotation/scale),
//     boundary clamping, absent-channel defaults, per-bone isolation
//   - AnimationSystem: time advancement, looping, pausing, null-guard, bone
//   matrix output,
//     single-bone skinning math, two-bone parent-child transform propagation
//
// Constraints:
//   - No OpenGL, no file I/O, no random seeds.
//   - All skeleton/clip data constructed in-process.
//   - Fixed deltaTime values throughout.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

#include "math/Mat4.h"
#include "math/Quaternion.h"
#include "math/Vec3.h"
#include "renderer/AnimationClip.h"
#include "renderer/Skeleton.h"
#include "scene/Registry.h"
#include "scene/components/AnimationComponent.h"
#include "scene/systems/AnimationSystem.h"

using namespace Horo;
using Catch::Approx;

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

// Build a minimal one-bone skeleton with an identity inverseBindMatrix.
static std::shared_ptr<Skeleton> MakeSingleBoneSkeleton() {
  auto skel = std::make_shared<Skeleton>();
  Bone root;
  root.name = "Root";
  root.parentIndex = -1;
  root.inverseBindMatrix = Mat4::Identity();
  skel->AddBone(root);
  return skel;
}

// Build a two-bone skeleton: bone 0 is Root (no parent), bone 1 is Child
// (parent=0).
static std::shared_ptr<Skeleton> MakeTwoBoneSkeleton() {
  auto skel = std::make_shared<Skeleton>();

  Bone root;
  root.name = "Root";
  root.parentIndex = -1;
  root.inverseBindMatrix = Mat4::Identity();
  skel->AddBone(root);

  Bone child;
  child.name = "Child";
  child.parentIndex = 0;
  child.inverseBindMatrix = Mat4::Identity();
  skel->AddBone(child);

  return skel;
}

// Build a clip with a single position track for bone 0 spanning [0, 1] seconds.
// Position lerps from (0,0,0) at t=0 to (1,0,0) at t=1.
static std::shared_ptr<AnimationClip> MakePositionClip(float duration = 1.0f) {
  auto clip = std::make_shared<AnimationClip>();
  clip->name = "test_pos";
  clip->duration = duration;

  BoneTrack track;
  track.boneIndex = 0;
  track.positionTimes = {0.0f, duration};
  track.positions = {Vec3{0.0f, 0.0f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f}};
  clip->AddTrack(track);
  return clip;
}

// Add an AnimationComponent wired to `skel` and `clip` to entity `e` in `reg`.
static AnimationComponent &AddAnimComp(Registry &reg, Entity e,
                                       std::shared_ptr<Skeleton> skel,
                                       std::shared_ptr<AnimationClip> clip,
                                       float startTime = 0.0f) {
  AnimationComponent ac;
  ac.skeleton = std::move(skel);
  ac.currentClip = std::move(clip);
  ac.time = startTime;
  ac.speed = 1.0f;
  ac.playing = true;
  ac.loop = true;
  return reg.Add<AnimationComponent>(e, std::move(ac));
}

// ===========================================================================
// Skeleton tests
// ===========================================================================

TEST_CASE("Skeleton: default-constructed skeleton is invalid", "[skeleton]") {
  Skeleton skel;
  REQUIRE_FALSE(skel.IsValid());
  REQUIRE(skel.BoneCount() == 0);
}

TEST_CASE("Skeleton: adding one bone makes it valid and BoneCount returns 1", "[skeleton]") {
  Skeleton skel;
  Bone b;
  b.name = "Hip";
  b.parentIndex = -1;
  b.inverseBindMatrix = Mat4::Identity();
  skel.AddBone(b);

  REQUIRE(skel.IsValid());
  REQUIRE(skel.BoneCount() == 1);
}

TEST_CASE("Skeleton: FindBone returns correct index by name", "[skeleton]") {
  Skeleton skel;

  Bone b0;
  b0.name = "Spine";
  b0.parentIndex = -1;
  b0.inverseBindMatrix = Mat4::Identity();
  Bone b1;
  b1.name = "Head";
  b1.parentIndex = 0;
  b1.inverseBindMatrix = Mat4::Identity();
  Bone b2;
  b2.name = "LeftArm";
  b2.parentIndex = 0;
  b2.inverseBindMatrix = Mat4::Identity();
  skel.AddBone(b0);
  skel.AddBone(b1);
  skel.AddBone(b2);

  CHECK(skel.FindBone("Spine") == 0);
  CHECK(skel.FindBone("Head") == 1);
  CHECK(skel.FindBone("LeftArm") == 2);
}

TEST_CASE("Skeleton: FindBone returns -1 for unknown name", "[skeleton]") {
  Skeleton skel;
  Bone b;
  b.name = "Root";
  b.parentIndex = -1;
  b.inverseBindMatrix = Mat4::Identity();
  skel.AddBone(b);

  REQUIRE(skel.FindBone("DoesNotExist") == -1);
  REQUIRE(skel.FindBone("") == -1);
}

TEST_CASE("Skeleton: root bone has parentIndex == -1", "[skeleton]") {
  Skeleton skel;
  Bone root;
  root.name = "Root";
  root.parentIndex = -1;
  root.inverseBindMatrix = Mat4::Identity();
  skel.AddBone(root);

  REQUIRE(skel.GetBone(0).parentIndex == -1);
}

TEST_CASE("Skeleton: child bone stores valid parentIndex", "[skeleton]") {
  auto skel = MakeTwoBoneSkeleton();

  // Bone 0 is root — parent is -1.
  CHECK(skel->GetBone(0).parentIndex == -1);
  // Bone 1 is child — parent is 0.
  CHECK(skel->GetBone(1).parentIndex == 0);
}

TEST_CASE("Skeleton: inverseBindMatrix is stored and retrieved correctly", "[skeleton]") {
  Skeleton skel;
  Bone b;
  b.name = "Root";
  b.parentIndex = -1;
  // Use a non-identity translation matrix as the inverse bind matrix so the
  // round-trip is meaningful.
  b.inverseBindMatrix = Mat4::Translate(Vec3{3.0f, 5.0f, 7.0f});
  skel.AddBone(b);

  const Mat4 &stored = skel.GetBone(0).inverseBindMatrix;
  // Column-major: translation is in m[3][0..2].
  CHECK(stored.m[3][0] == Approx(3.0f));
  CHECK(stored.m[3][1] == Approx(5.0f));
  CHECK(stored.m[3][2] == Approx(7.0f));
}

// ===========================================================================
// AnimationClip tests
// ===========================================================================

TEST_CASE("AnimationClip: default-constructed clip has duration 0 and no tracks", "[clip]") {
  AnimationClip clip;
  CHECK(clip.duration == Approx(0.0f));
  CHECK(clip.GetTracks().empty());
}

TEST_CASE("AnimationClip: single-key position track returns that key at any time", "[clip]") {
  AnimationClip clip;
  clip.duration = 1.0f;

  BoneTrack track;
  track.boneIndex = 0;
  track.positionTimes = {0.5f};
  track.positions = {Vec3{4.0f, 2.0f, -1.0f}};
  clip.AddTrack(track);

  std::vector<Mat4> out(1, Mat4::Identity());

  // Sample at various times — single-key track must always return the same
  // value.
  for (float t : {0.0f, 0.5f, 1.0f, 99.0f}) {
    clip.Sample(t, out);
    // Translation column of out[0] encodes the position.
    CHECK(out[0].m[3][0] == Approx(4.0f).margin(1e-4f));
    CHECK(out[0].m[3][1] == Approx(2.0f).margin(1e-4f));
    CHECK(out[0].m[3][2] == Approx(-1.0f).margin(1e-4f));
  }
}

TEST_CASE("AnimationClip: two-key position track lerps to midpoint at t=0.5", "[clip]") {
  AnimationClip clip;
  clip.duration = 1.0f;

  BoneTrack track;
  track.boneIndex = 0;
  track.positionTimes = {0.0f, 1.0f};
  track.positions = {Vec3{0.0f, 0.0f, 0.0f}, Vec3{2.0f, 4.0f, -6.0f}};
  clip.AddTrack(track);

  std::vector<Mat4> out(1, Mat4::Identity());
  clip.Sample(0.5f, out);

  CHECK(out[0].m[3][0] == Approx(1.0f).margin(1e-4f));
  CHECK(out[0].m[3][1] == Approx(2.0f).margin(1e-4f));
  CHECK(out[0].m[3][2] == Approx(-3.0f).margin(1e-4f));
}

TEST_CASE("AnimationClip: two-key rotation track slerps at t=0.5", "[clip]") {
  // Slerp from identity to a 90-degree Y rotation at t=0.5 should yield a
  // 45-degree Y rotation.  We verify it by checking the resulting matrix is
  // neither identity nor a full 90-degree rotation — and that no translation
  // was introduced (translation column should be zero for a pure rotation).
  AnimationClip clip;
  clip.duration = 1.0f;

  const Quaternion qStart = Quaternion::Identity();
  const Quaternion qEnd =
      Quaternion::FromAxisAngle(Vec3{0.0f, 1.0f, 0.0f},
                                std::numbers::pi_v<float> * 0.5f); // 90 deg Y

  BoneTrack track;
  track.boneIndex = 0;
  track.rotationTimes = {0.0f, 1.0f};
  track.rotations = {qStart, qEnd};
  clip.AddTrack(track);

  std::vector<Mat4> out(1, Mat4::Identity());
  clip.Sample(0.5f, out);

  // The translation column of a pure rotation matrix must be zero.
  CHECK(out[0].m[3][0] == Approx(0.0f).margin(1e-4f));
  CHECK(out[0].m[3][1] == Approx(0.0f).margin(1e-4f));
  CHECK(out[0].m[3][2] == Approx(0.0f).margin(1e-4f));

  // The diagonal element m[0][0] for a Y-rotation of angle θ is cos(θ).
  // At 45 degrees: cos(π/4) ≈ 0.7071.  At 0 degrees it is 1.0, at 90 it is 0.
  // So the result must be strictly between 0 and 1 — confirm it is near 0.7071.
  const Quaternion qMid = Quaternion::Slerp(qStart, qEnd, 0.5f);
  const Mat4 expected = Mat4::Rotate(qMid);
  CHECK(out[0].m[0][0] == Approx(expected.m[0][0]).margin(1e-4f));
  CHECK(out[0].m[2][2] == Approx(expected.m[2][2]).margin(1e-4f));
}

TEST_CASE("AnimationClip: two-key scale track lerps to midpoint at t=0.5", "[clip]") {
  AnimationClip clip;
  clip.duration = 1.0f;

  BoneTrack track;
  track.boneIndex = 0;
  track.scaleTimes = {0.0f, 1.0f};
  track.scales = {Vec3{1.0f, 1.0f, 1.0f}, Vec3{3.0f, 5.0f, 7.0f}};
  clip.AddTrack(track);

  std::vector<Mat4> out(1, Mat4::Identity());
  clip.Sample(0.5f, out);

  // Scale is encoded in the diagonal of the matrix when there is no rotation.
  // Mat4::Scale(s) produces m[0][0]=sx, m[1][1]=sy, m[2][2]=sz.
  // Combined TRS with identity rotation:
  //   Translate(0) * Rotate(identity) * Scale(mid) → diagonal = mid scale.
  CHECK(out[0].m[0][0] == Approx(2.0f).margin(1e-4f)); // lerp(1,3,0.5)
  CHECK(out[0].m[1][1] == Approx(3.0f).margin(1e-4f)); // lerp(1,5,0.5)
  CHECK(out[0].m[2][2] == Approx(4.0f).margin(1e-4f)); // lerp(1,7,0.5)
}

TEST_CASE("AnimationClip: Sample before first key clamps to first key value", "[clip]") {
  AnimationClip clip;
  clip.duration = 2.0f;

  BoneTrack track;
  track.boneIndex = 0;
  track.positionTimes = {0.5f, 1.5f};
  track.positions = {Vec3{10.0f, 0.0f, 0.0f}, Vec3{20.0f, 0.0f, 0.0f}};
  clip.AddTrack(track);

  std::vector<Mat4> out(1, Mat4::Identity());
  clip.Sample(0.0f, out); // before first key at 0.5

  CHECK(out[0].m[3][0] == Approx(10.0f).margin(1e-4f));
}

TEST_CASE("AnimationClip: Sample after last key clamps to last key value", "[clip]") {
  AnimationClip clip;
  clip.duration = 2.0f;

  BoneTrack track;
  track.boneIndex = 0;
  track.positionTimes = {0.5f, 1.5f};
  track.positions = {Vec3{10.0f, 0.0f, 0.0f}, Vec3{20.0f, 0.0f, 0.0f}};
  clip.AddTrack(track);

  std::vector<Mat4> out(1, Mat4::Identity());
  clip.Sample(2.0f, out); // after last key at 1.5

  CHECK(out[0].m[3][0] == Approx(20.0f).margin(1e-4f));
}

TEST_CASE("AnimationClip: bone with no track receives Mat4::Identity", "[clip]") {
  AnimationClip clip;
  clip.duration = 1.0f;

  // Track only covers bone 0; bone 1 is intentionally absent.
  BoneTrack track;
  track.boneIndex = 0;
  track.positionTimes = {0.0f};
  track.positions = {Vec3{5.0f, 0.0f, 0.0f}};
  clip.AddTrack(track);

  std::vector<Mat4> out(2, Mat4::Identity());
  clip.Sample(0.5f, out);

  const Mat4 identity = Mat4::Identity();
  // Bone 1 (index 1) must remain exactly identity.
  for (int col = 0; col < 4; ++col)
    for (int row = 0; row < 4; ++row)
      CHECK(out[1].m[col][row] == Approx(identity.m[col][row]).margin(1e-5f));
}

TEST_CASE("AnimationClip: position-only track defaults rotation to identity and scale to one", "[clip]") {
  AnimationClip clip;
  clip.duration = 1.0f;

  BoneTrack track;
  track.boneIndex = 0;
  track.positionTimes = {0.0f};
  track.positions = {Vec3{0.0f, 0.0f, 0.0f}};
  // rotationTimes and scaleTimes are deliberately left empty.
  clip.AddTrack(track);

  std::vector<Mat4> out(1, Mat4::Identity());
  clip.Sample(0.5f, out);

  // With identity rotation and unit scale, the TRS matrix equals Translate(0) =
  // identity.
  const Mat4 identity = Mat4::Identity();
  for (int col = 0; col < 4; ++col)
    for (int row = 0; row < 4; ++row)
      CHECK(out[0].m[col][row] == Approx(identity.m[col][row]).margin(1e-5f));
}

TEST_CASE("AnimationClip: multiple tracks for different bones each get their own data", "[clip]") {
  AnimationClip clip;
  clip.duration = 1.0f;

  // Bone 0 moves along X.
  BoneTrack t0;
  t0.boneIndex = 0;
  t0.positionTimes = {0.0f, 1.0f};
  t0.positions = {Vec3{0.0f, 0.0f, 0.0f}, Vec3{10.0f, 0.0f, 0.0f}};

  // Bone 1 moves along Y.
  BoneTrack t1;
  t1.boneIndex = 1;
  t1.positionTimes = {0.0f, 1.0f};
  t1.positions = {Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 20.0f, 0.0f}};

  clip.AddTrack(t0);
  clip.AddTrack(t1);

  std::vector<Mat4> out(2, Mat4::Identity());
  clip.Sample(0.5f, out);

  // Bone 0: x should be 5, y and z should be 0.
  CHECK(out[0].m[3][0] == Approx(5.0f).margin(1e-4f));
  CHECK(out[0].m[3][1] == Approx(0.0f).margin(1e-4f));

  // Bone 1: y should be 10, x and z should be 0.
  CHECK(out[1].m[3][0] == Approx(0.0f).margin(1e-4f));
  CHECK(out[1].m[3][1] == Approx(10.0f).margin(1e-4f));
}

// ===========================================================================
// AnimationSystem integration tests
// ===========================================================================

TEST_CASE("AnimationSystem: advances time by dt * speed", "[animation-system]") {
  Registry reg;
  Entity e = reg.Create();
  auto &ac =
      AddAnimComp(reg, e, MakeSingleBoneSkeleton(), MakePositionClip(1.0f));
  ac.speed = 2.0f;
  ac.time = 0.0f;
  ac.loop = false;

  AnimationSystem sys;
  sys.OnUpdate(reg, 0.1f);

  // time += 0.1 * 2.0 = 0.2
  CHECK(reg.Get<AnimationComponent>(e).time == Approx(0.2f).margin(1e-5f));
}

TEST_CASE("AnimationSystem: wraps time when loop=true and time exceeds duration", "[animation-system]") {
  Registry reg;
  Entity e = reg.Create();
  auto clip = MakePositionClip(1.0f);
  auto &ac = AddAnimComp(reg, e, MakeSingleBoneSkeleton(), clip);
  ac.time = 0.9f;
  ac.speed = 1.0f;
  ac.loop = true;

  AnimationSystem sys;
  sys.OnUpdate(reg, 0.2f); // 0.9 + 0.2 = 1.1 → fmod(1.1, 1.0) = 0.1

  CHECK(reg.Get<AnimationComponent>(e).time == Approx(0.1f).margin(1e-4f));
}

TEST_CASE("AnimationSystem: does not advance time when playing=false", "[animation-system]") {
  Registry reg;
  Entity e = reg.Create();
  auto &ac =
      AddAnimComp(reg, e, MakeSingleBoneSkeleton(), MakePositionClip(1.0f));
  ac.playing = false;
  ac.time = 0.3f;

  AnimationSystem sys;
  sys.OnUpdate(reg, 0.5f);

  REQUIRE(reg.Get<AnimationComponent>(e).time == Approx(0.3f).margin(1e-5f));
}

TEST_CASE("AnimationSystem: does not advance when currentClip is null", "[animation-system]") {
  Registry reg;
  Entity e = reg.Create();

  AnimationComponent ac;
  ac.skeleton = MakeSingleBoneSkeleton();
  ac.currentClip = nullptr;
  ac.time = 0.5f;
  ac.playing = true;
  reg.Add<AnimationComponent>(e, std::move(ac));

  AnimationSystem sys;
  sys.OnUpdate(reg, 0.1f);

  REQUIRE(reg.Get<AnimationComponent>(e).time == Approx(0.5f).margin(1e-5f));
  REQUIRE(reg.Get<AnimationComponent>(e).boneMatrices.empty());
}

TEST_CASE("AnimationSystem: does not advance when skeleton is null", "[animation-system]") {
  Registry reg;
  Entity e = reg.Create();

  AnimationComponent ac;
  ac.skeleton = nullptr;
  ac.currentClip = MakePositionClip(1.0f);
  ac.time = 0.2f;
  ac.playing = true;
  reg.Add<AnimationComponent>(e, std::move(ac));

  AnimationSystem sys;
  sys.OnUpdate(reg, 0.1f);

  REQUIRE(reg.Get<AnimationComponent>(e).time == Approx(0.2f).margin(1e-5f));
  REQUIRE(reg.Get<AnimationComponent>(e).boneMatrices.empty());
}

TEST_CASE("AnimationSystem: boneMatrices.size() equals skeleton BoneCount after update", "[animation-system]") {
  Registry reg;
  Entity e = reg.Create();
  AddAnimComp(reg, e, MakeTwoBoneSkeleton(), MakePositionClip(1.0f));

  AnimationSystem sys;
  sys.OnUpdate(reg, 0.016f);

  const auto &ac = reg.Get<AnimationComponent>(e);
  REQUIRE(static_cast<int>(ac.boneMatrices.size()) == 2);
}

TEST_CASE("AnimationSystem: single root bone with identity inverseBindMatrix and position track — boneMatrices[0] encodes the sampled position at t=0.5", "[animation-system]") {
  // The clip has a position track: pos(0)=(0,0,0), pos(1)=(1,0,0).
  // At time=0 after the update (dt=0.5, so time advances from 0 to 0.5 with
  // loop):
  //   localTransform = Translate({0.5,0,0}) * Rotate(identity) * Scale({1,1,1})
  //   globalTransform[0] = localTransform[0]
  //   boneMatrices[0]    = globalTransform[0] * Identity = Translate({0.5,0,0})
  Registry reg;
  Entity e = reg.Create();
  AddAnimComp(reg, e, MakeSingleBoneSkeleton(), MakePositionClip(1.0f));

  AnimationSystem sys;
  sys.OnUpdate(reg, 0.5f); // time advances from 0 → 0.5

  const auto &ac = reg.Get<AnimationComponent>(e);
  REQUIRE(ac.boneMatrices.size() == 1);

  // Translation column of the final skinning matrix must match the interpolated
  // position.
  CHECK(ac.boneMatrices[0].m[3][0] == Approx(0.5f).margin(1e-4f));
  CHECK(ac.boneMatrices[0].m[3][1] == Approx(0.0f).margin(1e-4f));
  CHECK(ac.boneMatrices[0].m[3][2] == Approx(0.0f).margin(1e-4f));
}

TEST_CASE("AnimationSystem: single bone at t=0 with position (0,0,0) produces identity boneMatrix", "[animation-system]") {
  // At t=0 the only position key is (0,0,0), rotation defaults to identity,
  // scale to (1,1,1). inverseBindMatrix is identity.  Expected result:
  // identity.
  Registry reg;
  Entity e = reg.Create();

  auto clip = std::make_shared<AnimationClip>();
  clip->name = "zero_pos";
  clip->duration = 1.0f;

  BoneTrack track;
  track.boneIndex = 0;
  track.positionTimes = {0.0f};
  track.positions = {Vec3{0.0f, 0.0f, 0.0f}};
  clip->AddTrack(track);

  AnimationComponent ac;
  ac.skeleton = MakeSingleBoneSkeleton();
  ac.currentClip = clip;
  ac.time = 0.0f;
  ac.speed = 0.0f; // freeze time so dt doesn't move us
  ac.playing = true;
  ac.loop = true;
  reg.Add<AnimationComponent>(e, std::move(ac));

  AnimationSystem sys;
  sys.OnUpdate(reg, 0.0f); // dt=0 → time stays at 0

  const auto &result = reg.Get<AnimationComponent>(e);
  REQUIRE(result.boneMatrices.size() == 1);

  const Mat4 identity = Mat4::Identity();
  for (int col = 0; col < 4; ++col)
    for (int row = 0; row < 4; ++row)
      CHECK(result.boneMatrices[0].m[col][row] ==
            Approx(identity.m[col][row]).margin(1e-5f));
}

TEST_CASE("AnimationSystem: two-bone hierarchy — parent transform propagates into child global matrix", "[animation-system]") {
  auto skel =
      MakeTwoBoneSkeleton(); // both bones have Identity inverseBindMatrix

  auto clip = std::make_shared<AnimationClip>();
  clip->name = "two_bone";
  clip->duration = 1.0f;

  // Root: constant position (2,0,0).
  BoneTrack rootTrack;
  rootTrack.boneIndex = 0;
  rootTrack.positionTimes = {0.0f};
  rootTrack.positions = {Vec3{2.0f, 0.0f, 0.0f}};
  clip->AddTrack(rootTrack);

  // Child: constant position (3,0,0) in local space.
  BoneTrack childTrack;
  childTrack.boneIndex = 1;
  childTrack.positionTimes = {0.0f};
  childTrack.positions = {Vec3{3.0f, 0.0f, 0.0f}};
  clip->AddTrack(childTrack);

  Registry reg;
  Entity e = reg.Create();

  AnimationComponent ac;
  ac.skeleton = skel;
  ac.currentClip = clip;
  ac.time = 0.0f;
  ac.speed = 0.0f; // freeze time
  ac.playing = true;
  ac.loop = true;
  reg.Add<AnimationComponent>(e, std::move(ac));

  AnimationSystem sys;
  sys.OnUpdate(reg, 0.0f);

  const auto &result = reg.Get<AnimationComponent>(e);
  REQUIRE(result.boneMatrices.size() == 2);

  // Root bone: translation should be (2,0,0).
  CHECK(result.boneMatrices[0].m[3][0] == Approx(2.0f).margin(1e-4f));
  CHECK(result.boneMatrices[0].m[3][1] == Approx(0.0f).margin(1e-4f));
  CHECK(result.boneMatrices[0].m[3][2] == Approx(0.0f).margin(1e-4f));

  // Child bone: global translation should be parent (2) + child (3) = 5 along
  // X.
  CHECK(result.boneMatrices[1].m[3][0] == Approx(5.0f).margin(1e-4f));
  CHECK(result.boneMatrices[1].m[3][1] == Approx(0.0f).margin(1e-4f));
  CHECK(result.boneMatrices[1].m[3][2] == Approx(0.0f).margin(1e-4f));
}

TEST_CASE("AnimationSystem: two-bone hierarchy — parent rotation propagates to child", "[animation-system]") {
  // Rotate the parent 90 degrees around Y.  Child is at local (1,0,0).
  // After 90-degree Y rotation, the child's world position becomes (0,0,-1) in
  // X-Z plane.
  //
  // globalTransform[0] = Rotate(90Y)
  // globalTransform[1] = Rotate(90Y) * Translate({1,0,0})
  // boneMatrices[1]    = globalTransform[1] * Identity
  //
  // Rotate(90Y) * (1,0,0,1) → approximately (0,0,-1) in world space (right-hand
  // coords). We verify via the boneMatrices[1] translation column.

  auto skel = MakeTwoBoneSkeleton();

  auto clip = std::make_shared<AnimationClip>();
  clip->name = "rot_propagate";
  clip->duration = 1.0f;

  const Quaternion q90Y = Quaternion::FromAxisAngle(
      Vec3{0.0f, 1.0f, 0.0f}, std::numbers::pi_v<float> * 0.5f);

  // Root: constant 90-degree Y rotation, no translation.
  BoneTrack rootTrack;
  rootTrack.boneIndex = 0;
  rootTrack.rotationTimes = {0.0f};
  rootTrack.rotations = {q90Y};
  clip->AddTrack(rootTrack);

  // Child: translate (1,0,0) in its local space.
  BoneTrack childTrack;
  childTrack.boneIndex = 1;
  childTrack.positionTimes = {0.0f};
  childTrack.positions = {Vec3{1.0f, 0.0f, 0.0f}};
  clip->AddTrack(childTrack);

  Registry reg;
  Entity e = reg.Create();

  AnimationComponent ac;
  ac.skeleton = skel;
  ac.currentClip = clip;
  ac.time = 0.0f;
  ac.speed = 0.0f;
  ac.playing = true;
  ac.loop = true;
  reg.Add<AnimationComponent>(e, std::move(ac));

  AnimationSystem sys;
  sys.OnUpdate(reg, 0.0f);

  const auto &result = reg.Get<AnimationComponent>(e);
  REQUIRE(result.boneMatrices.size() == 2);

  // Compute expected child world position manually:
  //   globalTransform[1] = Rotate(90Y) * Translate({1,0,0})
  // TransformPoint of (1,0,0) through Rotate(90Y) gives (0,0,-1).
  // The translation column of a product (Rotate * Translate) equals Rotate
  // applied to the translation vector of the Translate matrix, which is
  // (1,0,0). q90Y * (1,0,0) ≈ (0, 0, -1).
  const Vec3 rotated = q90Y * Vec3{1.0f, 0.0f, 0.0f};
  CHECK(result.boneMatrices[1].m[3][0] == Approx(rotated.x).margin(1e-4f));
  CHECK(result.boneMatrices[1].m[3][1] == Approx(rotated.y).margin(1e-4f));
  CHECK(result.boneMatrices[1].m[3][2] == Approx(rotated.z).margin(1e-4f));
}

TEST_CASE("AnimationSystem: non-identity inverseBindMatrix is applied to final bone matrix", "[animation-system]") {
  // Single bone.  inverseBindMatrix translates -2 along X.
  // Clip positions the bone at +2 along X.
  // Expected boneMatrix = Translate(2,0,0) * Translate(-2,0,0) = Identity
  // translation.

  auto skel = std::make_shared<Skeleton>();
  Bone root;
  root.name = "Root";
  root.parentIndex = -1;
  root.inverseBindMatrix = Mat4::Translate(Vec3{-2.0f, 0.0f, 0.0f});
  skel->AddBone(root);

  auto clip = std::make_shared<AnimationClip>();
  clip->name = "inv_bind";
  clip->duration = 1.0f;

  BoneTrack track;
  track.boneIndex = 0;
  track.positionTimes = {0.0f};
  track.positions = {Vec3{2.0f, 0.0f, 0.0f}};
  clip->AddTrack(track);

  Registry reg;
  Entity e = reg.Create();

  AnimationComponent ac;
  ac.skeleton = skel;
  ac.currentClip = clip;
  ac.time = 0.0f;
  ac.speed = 0.0f;
  ac.playing = true;
  ac.loop = true;
  reg.Add<AnimationComponent>(e, std::move(ac));

  AnimationSystem sys;
  sys.OnUpdate(reg, 0.0f);

  const auto &result = reg.Get<AnimationComponent>(e);
  REQUIRE(result.boneMatrices.size() == 1);

  // Translate(2) * Translate(-2) → net translation is 0.
  CHECK(result.boneMatrices[0].m[3][0] == Approx(0.0f).margin(1e-4f));
  CHECK(result.boneMatrices[0].m[3][1] == Approx(0.0f).margin(1e-4f));
  CHECK(result.boneMatrices[0].m[3][2] == Approx(0.0f).margin(1e-4f));
}

TEST_CASE("AnimationSystem: non-looping clip clamps time at duration when dt overshoots", "[animation-system]") {
  Registry reg;
  Entity e = reg.Create();
  auto &ac =
      AddAnimComp(reg, e, MakeSingleBoneSkeleton(), MakePositionClip(1.0f));
  ac.loop = false;
  ac.time = 0.8f;

  AnimationSystem sys;
  sys.OnUpdate(reg, 1.0f); // 0.8 + 1.0 = 1.8 → clamp to 1.0

  CHECK(reg.Get<AnimationComponent>(e).time == Approx(1.0f).margin(1e-5f));
}

TEST_CASE("AnimationSystem: boneMatrices is resized correctly when skeleton grows between updates", "[animation-system]") {
  // Simulate an entity whose skeleton reference is replaced mid-game (not a
  // realistic engine operation, but verifies that the system resizes on every
  // update rather than assuming a stable size).
  Registry reg;
  Entity e = reg.Create();
  AddAnimComp(reg, e, MakeSingleBoneSkeleton(), MakePositionClip(1.0f));

  AnimationSystem sys;
  sys.OnUpdate(reg, 0.016f);
  REQUIRE(reg.Get<AnimationComponent>(e).boneMatrices.size() == 1);

  // Swap in a two-bone skeleton and a clip covering both bones.
  auto &ac = reg.Get<AnimationComponent>(e);
  ac.skeleton = MakeTwoBoneSkeleton();
  ac.currentClip =
      MakePositionClip(1.0f); // track is for bone 0 only — bone 1 gets identity

  sys.OnUpdate(reg, 0.016f);
  REQUIRE(reg.Get<AnimationComponent>(e).boneMatrices.size() == 2);
}
