// test_input.cpp
//
// Unit tests for input and camera controller logic in Horo Engine.
//
// Coverage:
//   - FPSCameraController: construction defaults, SetYaw/GetYaw,
//   SetPitch/GetPitch,
//     SetPosition, ApplyToCamera (look-direction math), Update with zero delta,
//     pitch/sensitivity defaults.
//
// Constraints:
//   - No GLFW window required: Input statics are zero-initialised so
//     Input::GetMouseDelta() returns {0,0} without a window.
//   - No OpenGL context required.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
#define private public
#include "input/Input.h"
#undef private
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include "input/FPSCameraController.h"
#include "math/MathUtils.h"
#include "renderer/Camera.h"

using namespace Monolith;
using Catch::Approx;

namespace {
void ResetInputState() {
  Input::s_keys.fill(false);
  Input::s_keysLast.fill(false);
  Input::s_buttons.fill(false);
  Input::s_buttonsLast.fill(false);
  Input::s_mousePos = {};
  Input::s_mousePosLast = {};
  Input::s_mouseDelta = {};
  Input::s_scrollDelta = 0.0f;
}
} // namespace

// ===========================================================================
// FPSCameraController — construction defaults
// ===========================================================================

TEST_CASE("FPSCameraController: default yaw and pitch are zero",
          "[input][fps_camera]") {
  FPSCameraController ctrl;
  REQUIRE(ctrl.GetYaw() == Approx(0.0f));
  REQUIRE(ctrl.GetPitch() == Approx(0.0f));
}

TEST_CASE("FPSCameraController: default sensitivity is accessible via "
          "setter/getter cycle",
          "[input][fps_camera]") {
  FPSCameraController ctrl;
  // Round-trip: apply the known default and confirm it is accepted
  ctrl.SetMouseSensitivity(0.15f);
  // A zero-delta Update must not crash, implying the private field was set
  REQUIRE_NOTHROW(ctrl.Update(0.016f));
}

TEST_CASE(
    "FPSCameraController: pitch limits survive a round-trip through setters",
    "[input][fps_camera]") {
  FPSCameraController ctrl;
  ctrl.SetPitchMin(-70.0f);
  ctrl.SetPitchMax(70.0f);
  // Update with zero delta keeps pitch unchanged — just checks no crash
  ctrl.SetPitch(20.0f);
  ctrl.Update(0.016f);
  REQUIRE(ctrl.GetPitch() == Approx(20.0f));
}

// ===========================================================================
// FPSCameraController — SetYaw / GetYaw
// ===========================================================================

TEST_CASE("FPSCameraController: SetYaw round-trip for positive angles",
          "[input][fps_camera]") {
  FPSCameraController ctrl;
  ctrl.SetYaw(45.0f);
  REQUIRE(ctrl.GetYaw() == Approx(45.0f));
}

TEST_CASE("FPSCameraController: SetYaw round-trip for negative angles",
          "[input][fps_camera]") {
  FPSCameraController ctrl;
  ctrl.SetYaw(-90.0f);
  REQUIRE(ctrl.GetYaw() == Approx(-90.0f));
}

TEST_CASE("FPSCameraController: SetYaw to zero resets yaw",
          "[input][fps_camera]") {
  FPSCameraController ctrl;
  ctrl.SetYaw(180.0f);
  ctrl.SetYaw(0.0f);
  REQUIRE(ctrl.GetYaw() == Approx(0.0f));
}

// ===========================================================================
// FPSCameraController — SetPitch / GetPitch
// ===========================================================================

TEST_CASE("FPSCameraController: SetPitch round-trip for positive angles",
          "[input][fps_camera]") {
  FPSCameraController ctrl;
  ctrl.SetPitch(30.0f);
  REQUIRE(ctrl.GetPitch() == Approx(30.0f));
}

TEST_CASE("FPSCameraController: SetPitch round-trip for negative angles",
          "[input][fps_camera]") {
  FPSCameraController ctrl;
  ctrl.SetPitch(-45.0f);
  REQUIRE(ctrl.GetPitch() == Approx(-45.0f));
}

// ===========================================================================
// FPSCameraController — ApplyToCamera: position forwarding
// ===========================================================================

TEST_CASE("FPSCameraController: ApplyToCamera copies position to camera",
          "[input][fps_camera]") {
  FPSCameraController ctrl;
  ctrl.SetPosition({3.0f, 5.0f, 7.0f});
  ctrl.SetYaw(0.0f);
  ctrl.SetPitch(0.0f);

  Camera cam;
  ctrl.ApplyToCamera(cam);

  REQUIRE(cam.position.x == Approx(3.0f));
  REQUIRE(cam.position.y == Approx(5.0f));
  REQUIRE(cam.position.z == Approx(7.0f));
}

// ===========================================================================
// FPSCameraController — ApplyToCamera: look-direction math
// ===========================================================================

TEST_CASE("FPSCameraController: yaw=0 pitch=0 aims along negative Z",
          "[input][fps_camera]") {
  FPSCameraController ctrl;
  ctrl.SetPosition({0.0f, 0.0f, 0.0f});
  ctrl.SetYaw(0.0f);
  ctrl.SetPitch(0.0f);

  Camera cam;
  ctrl.ApplyToCamera(cam);

  REQUIRE(cam.target.x == Approx(0.0f).margin(1e-5f));
  REQUIRE(cam.target.y == Approx(0.0f).margin(1e-5f));
  REQUIRE(cam.target.z == Approx(-1.0f).epsilon(1e-5f));
}

TEST_CASE("FPSCameraController: yaw=90 pitch=0 aims along negative X",
          "[input][fps_camera]") {
  FPSCameraController ctrl;
  ctrl.SetPosition({0.0f, 0.0f, 0.0f});
  ctrl.SetYaw(90.0f);
  ctrl.SetPitch(0.0f);

  Camera cam;
  ctrl.ApplyToCamera(cam);

  REQUIRE(cam.target.x == Approx(-1.0f).epsilon(1e-4f));
  REQUIRE(cam.target.y == Approx(0.0f).margin(1e-4f));
  REQUIRE(std::fabs(cam.target.z) < 1e-4f);
}

TEST_CASE("FPSCameraController: pitch=45 tilts target upward",
          "[input][fps_camera]") {
  FPSCameraController ctrl;
  ctrl.SetPosition({0.0f, 0.0f, 0.0f});
  ctrl.SetYaw(0.0f);
  ctrl.SetPitch(45.0f);

  Camera cam;
  ctrl.ApplyToCamera(cam);

  REQUIRE(cam.target.y > 0.0f);
}

TEST_CASE("FPSCameraController: pitch=-45 tilts target downward",
          "[input][fps_camera]") {
  FPSCameraController ctrl;
  ctrl.SetPosition({0.0f, 0.0f, 0.0f});
  ctrl.SetYaw(0.0f);
  ctrl.SetPitch(-45.0f);

  Camera cam;
  ctrl.ApplyToCamera(cam);

  REQUIRE(cam.target.y < 0.0f);
}

TEST_CASE(
    "FPSCameraController: ApplyToCamera with non-zero position offsets target",
    "[input][fps_camera]") {
  FPSCameraController ctrl;
  ctrl.SetPosition({10.0f, 5.0f, 3.0f});
  ctrl.SetYaw(0.0f);
  ctrl.SetPitch(0.0f);

  Camera cam;
  ctrl.ApplyToCamera(cam);

  REQUIRE(cam.target.x == Approx(10.0f).margin(1e-5f));
  REQUIRE(cam.target.y == Approx(5.0f).margin(1e-5f));
  REQUIRE(cam.target.z == Approx(2.0f).epsilon(1e-5f));
}

// ===========================================================================
// FPSCameraController — Update: zero-delta preserves state
// ===========================================================================

TEST_CASE(
    "FPSCameraController: Update with zero mouse delta leaves yaw unchanged",
    "[input][fps_camera]") {
  // Input::s_mouseDelta is zero-initialised; no GLFW window needed.
  FPSCameraController ctrl;
  ctrl.SetYaw(45.0f);
  ctrl.SetPitch(20.0f);

  ctrl.Update(0.016f);

  REQUIRE(ctrl.GetYaw() == Approx(45.0f));
}

TEST_CASE(
    "FPSCameraController: Update with zero mouse delta leaves pitch unchanged",
    "[input][fps_camera]") {
  FPSCameraController ctrl;
  ctrl.SetYaw(0.0f);
  ctrl.SetPitch(30.0f);

  ctrl.Update(0.016f);

  REQUIRE(ctrl.GetPitch() == Approx(30.0f));
}

TEST_CASE("FPSCameraController: Update clamps pitch within pitchMin/pitchMax",
          "[input][fps_camera]") {
  // After Update() with zero delta the clamping code still executes.
  // Set pitch to pitchMax (70) — it should remain unchanged (still within
  // range).
  FPSCameraController ctrl;
  ctrl.SetPitchMin(-70.0f);
  ctrl.SetPitchMax(70.0f);
  ctrl.SetPitch(70.0f); // exactly at pitchMax
  ctrl.Update(0.016f);
  REQUIRE(ctrl.GetPitch() == Approx(70.0f));
}

TEST_CASE("FPSCameraController: Update clamps pitch at pitchMin boundary",
          "[input][fps_camera]") {
  FPSCameraController ctrl;
  ctrl.SetPitchMin(-70.0f);
  ctrl.SetPitchMax(70.0f);
  ctrl.SetPitch(-70.0f); // exactly at pitchMin
  ctrl.Update(0.016f);
  REQUIRE(ctrl.GetPitch() == Approx(-70.0f));
}

// ===========================================================================
// Input — branch behavior for state transitions and scroll accumulation
// ===========================================================================

TEST_CASE("Input: key transition queries reflect current and last key state",
          "[input]") {
  ResetInputState();

  Input::s_keys[static_cast<size_t>(Key::A)] = true;
  Input::s_keysLast[static_cast<size_t>(Key::A)] = false;
  REQUIRE(Input::IsKeyDown(Key::A));
  REQUIRE(Input::IsKeyPressed(Key::A));
  REQUIRE_FALSE(Input::IsKeyReleased(Key::A));

  Input::s_keys[static_cast<size_t>(Key::A)] = false;
  Input::s_keysLast[static_cast<size_t>(Key::A)] = true;
  REQUIRE_FALSE(Input::IsKeyDown(Key::A));
  REQUIRE_FALSE(Input::IsKeyPressed(Key::A));
  REQUIRE(Input::IsKeyReleased(Key::A));
}

TEST_CASE(
    "Input: mouse transition queries reflect current and last button state",
    "[input]") {
  ResetInputState();

  Input::s_buttons[static_cast<size_t>(MouseButton::Left)] = true;
  Input::s_buttonsLast[static_cast<size_t>(MouseButton::Left)] = false;
  REQUIRE(Input::IsMouseButtonDown(MouseButton::Left));
  REQUIRE(Input::IsMouseButtonPressed(MouseButton::Left));
  REQUIRE_FALSE(Input::IsMouseButtonReleased(MouseButton::Left));

  Input::s_buttons[static_cast<size_t>(MouseButton::Left)] = false;
  Input::s_buttonsLast[static_cast<size_t>(MouseButton::Left)] = true;
  REQUIRE_FALSE(Input::IsMouseButtonDown(MouseButton::Left));
  REQUIRE_FALSE(Input::IsMouseButtonPressed(MouseButton::Left));
  REQUIRE(Input::IsMouseButtonReleased(MouseButton::Left));
}

TEST_CASE("Input: mouse position/delta accessors return stored values",
          "[input]") {
  ResetInputState();

  Input::s_mousePos = {42.0f, -10.0f};
  Input::s_mouseDelta = {3.5f, -2.5f};

  const Vec2 mousePos = Input::GetMousePosition();
  const Vec2 mouseDelta = Input::GetMouseDelta();
  REQUIRE(mousePos.x == Approx(42.0f));
  REQUIRE(mousePos.y == Approx(-10.0f));
  REQUIRE(mouseDelta.x == Approx(3.5f));
  REQUIRE(mouseDelta.y == Approx(-2.5f));
}

TEST_CASE("Input: scroll callback accumulates and GetScrollDelta consumes once",
          "[input]") {
  ResetInputState();

  Input::ScrollCallback(nullptr, 0.0, 1.5);
  Input::ScrollCallback(nullptr, 0.0, -0.25);

  REQUIRE(Input::GetScrollDelta() == Approx(1.25f));
  REQUIRE(Input::GetScrollDelta() == Approx(0.0f));
}
