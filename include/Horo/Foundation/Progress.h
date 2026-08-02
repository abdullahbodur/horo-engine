#pragma once

#include <algorithm>
#include <string>
#include <string_view>

namespace Horo
{
    /** @brief Immutable progress state for one named operation phase. */
    struct ProgressSnapshot
    {
        std::string phase;
        float value = 0.0F;
    };

    /** @brief Enforces monotonic progress within a phase; a phase transition may reset progress. */
    class ProgressTracker
    {
    public:
        [[nodiscard]] bool Report(const std::string_view phase, const float value)
        {
            const float bounded = std::clamp(value, 0.0F, 1.0F);
            if (m_snapshot.phase == phase)
            {
                if (bounded < m_snapshot.value) return false;
                m_snapshot.value = bounded;
                return true;
            }
            m_snapshot.phase = phase;
            m_snapshot.value = bounded;
            return true;
        }
        [[nodiscard]] const ProgressSnapshot &Snapshot() const noexcept { return m_snapshot; }
    private:
        ProgressSnapshot m_snapshot;
    };
} // namespace Horo
