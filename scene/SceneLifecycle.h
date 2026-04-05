#pragma once
#include <cstdint>

namespace Monolith {

// Strict scene lifecycle state machine.
// Enforces valid transitions to catch loading/unloading bugs early.
// States:
//  Uninitialized → Loading → Active → [Reloading] → Unloading → Uninitialized (cycle)
//
// Gated transitions ensure:
//  - Only one load/unload at a time
//  - No system updates during loading/unloading
//  - Clear error messages on invalid transitions
enum class SceneLifecycleState : uint8_t {
  Uninitialized = 0,  // Initial state; no scene loaded
  Loading = 1,        // Scene is being loaded (systems initialized, entities created)
  Active = 2,         // Scene fully loaded; systems running
  Reloading = 3,      // Hot-reload in progress (preserve runtime state, re-init data)
  Unloading = 4,      // Scene is being unloaded (cleanup phase)
};

// Manages scene lifecycle transitions with validation.
// Non-copyable. Typically owned by Application or top-level container.
class SceneLifecycle {
 public:
  SceneLifecycle() = default;
  ~SceneLifecycle() = default;

  SceneLifecycle(const SceneLifecycle&) = delete;
  SceneLifecycle& operator=(const SceneLifecycle&) = delete;

  // Transition to Loading state (only from Uninitialized or Unloading).
  // Returns true if transition was valid; false otherwise (check GetError()).
  bool BeginLoading();

  // Transition to Active state (only from Loading).
  bool FinishLoading();

  // Transition to Reloading state (only from Active).
  // Reloading → Active (not a separate Finish step; just CompleteReload).
  bool BeginReloading();

  // Complete reload and return to Active (only from Reloading).
  bool CompleteReload();

  // Transition to Unloading state (only from Active or Reloading).
  bool BeginUnloading();

  // Transition back to Uninitialized (only from Unloading).
  bool FinishUnloading();

  // Current state
  SceneLifecycleState GetState() const { return m_state; }

  // Last error message (empty if no error)
  const char* GetError() const { return m_errorMsg; }

  // Convenience: is the scene ready for gameplay updates?
  bool IsActive() const { return m_state == SceneLifecycleState::Active; }

  // Convenience: is a load/unload operation in progress?
  bool IsTransitioning() const {
    return m_state == SceneLifecycleState::Loading || 
           m_state == SceneLifecycleState::Reloading ||
           m_state == SceneLifecycleState::Unloading;
  }

 private:
  SceneLifecycleState m_state = SceneLifecycleState::Uninitialized;
  const char* m_errorMsg = "";

  void SetError(const char* msg) { m_errorMsg = msg; }
};

}  // namespace Monolith
