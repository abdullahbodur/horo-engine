#include "editor/input/EditorScrollSmoother.h"

#include <algorithm>
#include <cmath>

namespace Horo::Editor {
    namespace {
        constexpr float MaximumPendingWheelUnits = 4.0F;
        constexpr float WheelUnitsPerSecond = 7.5F;
        constexpr float MaximumFrameSeconds = 1.0F / 60.0F;
        constexpr float MinimumFrameStep = 0.001F;

        void QueueAxis(float &pending, const float delta) noexcept {
            if (!std::isfinite(delta) || delta == 0.0F)
                return;
            if (pending * delta < 0.0F)
                pending = 0.0F;
            pending = std::clamp(pending + delta, -MaximumPendingWheelUnits, MaximumPendingWheelUnits);
        }

        [[nodiscard]] float ConsumeAxis(float &pending, const float limit) noexcept {
            const float emitted = std::clamp(pending, -limit, limit);
            pending -= emitted;
            if (std::abs(pending) < MinimumFrameStep)
                pending = 0.0F;
            return emitted;
        }
    }  // namespace

    /** @copydoc EditorScrollDelta::IsEmpty */
    bool EditorScrollDelta::IsEmpty() const noexcept {
        return horizontal == 0.0F && vertical == 0.0F;
    }

    /** @copydoc EditorScrollSmoother::Queue */
    void EditorScrollSmoother::Queue(const float horizontal, const float vertical) noexcept {
        QueueAxis(pendingHorizontal_, horizontal);
        QueueAxis(pendingVertical_, vertical);
    }

    /** @copydoc EditorScrollSmoother::Consume */
    EditorScrollDelta EditorScrollSmoother::Consume(const float deltaSeconds) noexcept {
        if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F)
            return {};
        const float limit = WheelUnitsPerSecond * std::min(deltaSeconds, MaximumFrameSeconds);
        return {
            .horizontal = ConsumeAxis(pendingHorizontal_, limit),
            .vertical = ConsumeAxis(pendingVertical_, limit),
        };
    }

    /** @copydoc EditorScrollSmoother::Reset */
    void EditorScrollSmoother::Reset() noexcept {
        pendingHorizontal_ = 0.0F;
        pendingVertical_ = 0.0F;
    }
}  // namespace Horo::Editor
