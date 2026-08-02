#pragma once

#include <cstdint>

namespace Horo::Log
{

    /**
     * @brief Canonical log levels matching the observability contract.
     *
     * Trace describes flow; Info marks lifecycle milestones.
     * Error means an operation failed but the process can continue.
     */
    enum class Level : uint8_t
    {
        Trace    = 0,
        Debug    = 1,
        Info     = 2,
        Warn     = 3,
        Error    = 4,
        Critical = 5,
        Off      = 6,
    };

    /** @brief Returns a human-readable label for the level. */
    [[nodiscard]] constexpr const char *ToString(Level level) noexcept
    {
        switch (level)
        {
        case Level::Trace:    return "trace";
        case Level::Debug:    return "debug";
        case Level::Info:     return "info";
        case Level::Warn:     return "warn";
        case Level::Error:    return "error";
        case Level::Critical: return "critical";
        case Level::Off:      return "off";
        }
        return "unknown";
    }

} // namespace Horo::Log
