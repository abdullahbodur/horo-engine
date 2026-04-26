#include "core/Time.h"

#include <GLFW/glfw3.h>

#include <algorithm>

namespace Horo {
    double Time::s_lastTime = 0.0;
    float Time::s_deltaTime = 0.0f;
    float Time::s_elapsed = 0.0f;
    float Time::s_accumulator = 0.0f;
    uint64_t Time::s_frameCount = 0;

    void Time::Tick() {
        double now = glfwGetTime();
        double diff = now - s_lastTime;
        s_lastTime = now;

        // Clamp to avoid spiral of death on long frame spikes
        constexpr double MAX_DELTA = 0.25;
        diff = std::min(diff, MAX_DELTA);

        s_deltaTime = static_cast<float>(diff);
        s_elapsed += s_deltaTime;
        s_accumulator += s_deltaTime;
        s_frameCount++;
    }

    bool Time::ConsumeFixedStep() {
        if (s_accumulator >= FIXED_DT) {
            s_accumulator -= FIXED_DT;
            return true;
        }
        return false;
    }
} // namespace Horo
