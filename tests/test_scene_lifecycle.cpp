#include <catch2/catch_test_macros.hpp>
#include "scene/SceneLifecycle.h"

namespace Monolith {

TEST_CASE("SceneLifecycle: Valid linear transitions", "[scene][lifecycle]") {
  SceneLifecycle lc;

  // Uninitialized → Loading
  REQUIRE(lc.GetState() == SceneLifecycleState::Uninitialized);
  REQUIRE(lc.BeginLoading());
  REQUIRE(lc.GetState() == SceneLifecycleState::Loading);

  // Loading → Active
  REQUIRE(lc.FinishLoading());
  REQUIRE(lc.GetState() == SceneLifecycleState::Active);

  // Active → Unloading
  REQUIRE(lc.BeginUnloading());
  REQUIRE(lc.GetState() == SceneLifecycleState::Unloading);

  // Unloading → Uninitialized
  REQUIRE(lc.FinishUnloading());
  REQUIRE(lc.GetState() == SceneLifecycleState::Uninitialized);
}

TEST_CASE("SceneLifecycle: Hot-reload cycle", "[scene][lifecycle]") {
  SceneLifecycle lc;

  // Setup: load scene
  REQUIRE(lc.BeginLoading());
  REQUIRE(lc.FinishLoading());
  REQUIRE(lc.IsActive());

  // Hot-reload: Active → Reloading → Active
  REQUIRE(lc.BeginReloading());
  REQUIRE(lc.GetState() == SceneLifecycleState::Reloading);
  REQUIRE(!lc.IsActive());

  REQUIRE(lc.CompleteReload());
  REQUIRE(lc.GetState() == SceneLifecycleState::Active);
  REQUIRE(lc.IsActive());
}

TEST_CASE("SceneLifecycle: Invalid transition FinishLoading before BeginLoading", "[scene][lifecycle]") {
  SceneLifecycle lc;

  // Should fail: FinishLoading when not Loading
  REQUIRE(!lc.FinishLoading());
  REQUIRE(lc.GetState() == SceneLifecycleState::Uninitialized);
  REQUIRE(lc.GetError() != nullptr);
  REQUIRE(std::string(lc.GetError()).find("FinishLoading") != std::string::npos);
}

TEST_CASE("SceneLifecycle: Invalid transition BeginLoading while Loading", "[scene][lifecycle]") {
  SceneLifecycle lc;

  // Begin first load
  REQUIRE(lc.BeginLoading());

  // Should fail: can't load again while already loading
  REQUIRE(!lc.BeginLoading());
  REQUIRE(lc.GetState() == SceneLifecycleState::Loading);
  REQUIRE(lc.GetError() != nullptr);
}

TEST_CASE("SceneLifecycle: Invalid transition BeginReloading from Uninitialized", "[scene][lifecycle]") {
  SceneLifecycle lc;

  // Can't reload if not Active
  REQUIRE(!lc.BeginReloading());
  REQUIRE(lc.GetState() == SceneLifecycleState::Uninitialized);
}

TEST_CASE("SceneLifecycle: Invalid transition BeginUnloading from Loading", "[scene][lifecycle]") {
  SceneLifecycle lc;

  REQUIRE(lc.BeginLoading());

  // Can't unload while still loading
  REQUIRE(!lc.BeginUnloading());
  REQUIRE(lc.GetState() == SceneLifecycleState::Loading);
}

TEST_CASE("SceneLifecycle: IsTransitioning utility", "[scene][lifecycle]") {
  SceneLifecycle lc;

  REQUIRE(!lc.IsTransitioning());  // Uninitialized

  REQUIRE(lc.BeginLoading());
  REQUIRE(lc.IsTransitioning());  // Loading

  REQUIRE(lc.FinishLoading());
  REQUIRE(!lc.IsTransitioning());  // Active

  REQUIRE(lc.BeginReloading());
  REQUIRE(lc.IsTransitioning());  // Reloading

  REQUIRE(lc.CompleteReload());
  REQUIRE(!lc.IsTransitioning());  // Active again

  REQUIRE(lc.BeginUnloading());
  REQUIRE(lc.IsTransitioning());  // Unloading

  REQUIRE(lc.FinishUnloading());
  REQUIRE(!lc.IsTransitioning());  // Uninitialized
}

TEST_CASE("SceneLifecycle: Multiple load cycles", "[scene][lifecycle]") {
  SceneLifecycle lc;

  // First cycle
  REQUIRE(lc.BeginLoading());
  REQUIRE(lc.FinishLoading());
  REQUIRE(lc.IsActive());

  REQUIRE(lc.BeginUnloading());
  REQUIRE(lc.FinishUnloading());
  REQUIRE(lc.GetState() == SceneLifecycleState::Uninitialized);

  // Second cycle (should work identically)
  REQUIRE(lc.BeginLoading());
  REQUIRE(lc.FinishLoading());
  REQUIRE(lc.IsActive());

  REQUIRE(lc.BeginUnloading());
  REQUIRE(lc.FinishUnloading());
  REQUIRE(lc.GetState() == SceneLifecycleState::Uninitialized);
}

}  // namespace Monolith
