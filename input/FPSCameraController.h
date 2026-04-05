#pragma once
#include "math/Vec3.h"
#include "renderer/Camera.h"

namespace Monolith {

// Stateless first-person camera controller.
// Owns yaw/pitch/position state; reads Input::GetMouseDelta() internally on Update().
class FPSCameraController
{
public:
    float mouseSensitivity = 0.15f;
    float pitchMin         = -70.0f;
    float pitchMax         =  70.0f;

    // Call once per frame while game input is active.
    // Reads Input::GetMouseDelta() and integrates yaw/pitch.
    void Update(float dt);

    void SetPosition(const Vec3& pos);
    void SetYaw(float yaw);
    void SetPitch(float pitch);

    // Writes camera.position and camera.target based on current state.
    void ApplyToCamera(Camera& camera) const;

    float GetYaw()   const { return m_yaw; }
    float GetPitch() const { return m_pitch; }

private:
    float m_yaw   = 0.0f;
    float m_pitch = 0.0f;
    Vec3  m_position{0.0f, 0.0f, 0.0f};
};

}  // namespace Monolith
