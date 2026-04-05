#include "input/FPSCameraController.h"

#include <algorithm>
#include <cmath>

#include "input/Input.h"
#include "math/MathUtils.h"

namespace Monolith {

void FPSCameraController::Update(float /*dt*/)
{
    Vec2 delta = Input::GetMouseDelta();
    m_yaw   -= delta.x * mouseSensitivity;
    m_pitch -= delta.y * mouseSensitivity;
    m_pitch = std::max(pitchMin, std::min(pitchMax, m_pitch));
}

void FPSCameraController::SetPosition(const Vec3& pos)
{
    m_position = pos;
}

void FPSCameraController::SetYaw(float yaw)
{
    m_yaw = yaw;
}

void FPSCameraController::SetPitch(float pitch)
{
    m_pitch = pitch;
}

void FPSCameraController::ApplyToCamera(Camera& camera) const
{
    const float yawRad   = ToRadians(m_yaw);
    const float pitchRad = ToRadians(m_pitch);

    // Standard first-person look direction derived from spherical coordinates.
    // yaw rotates around Y; pitch tilts up/down.
    const Vec3 lookDir = {
        -std::sin(yawRad) * std::cos(pitchRad),
         std::sin(pitchRad),
        -std::cos(yawRad) * std::cos(pitchRad),
    };

    camera.position = m_position;
    camera.target   = m_position + lookDir;
}

}  // namespace Monolith
