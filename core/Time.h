#pragma once
#include <cstdint>

namespace Monolith {
class Time {
public:
  static constexpr float FIXED_DT = 1.0f / 120.0f; // 120 Hz physics

  // Call once per frame, before input polling
  static void Tick();

  // Returns time in seconds since the previous frame
  static float DeltaTime() { return s_deltaTime; }

  // Returns total elapsed time in seconds
  static float Elapsed() { return s_elapsed; }

  // Returns current frame number
  static uint64_t FrameCount() { return s_frameCount; }

  // Fixed-step accumulator: returns true while there are steps to consume
  static bool ConsumeFixedStep();

  // [0,1] interpolation alpha for rendering between physics frames
  static float GetInterpolationAlpha() { return s_accumulator / FIXED_DT; }

private:
  static double s_lastTime;
  static float s_deltaTime;
  static float s_elapsed;
  static float s_accumulator;
  static uint64_t s_frameCount;
};
} // namespace Monolith
