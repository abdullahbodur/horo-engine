#pragma once

#include "Horo/Foundation/Logging/LogLevel.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>

namespace Horo::Log
{

    /**
     * @brief Minimal structured logger with JSONL file output.
     *
     * All editor, CLI, and game hosts share this logger. It supports
     * runtime level filtering by category prefix and writes one JSON
     * object per line to a rotating file in the platform log directory.
     *
     * Usage:
     *   Logger::Init("~/.horo/logs", "horo-editor");
     *   HORO_LOG_INFO("editor.startup", "Editor initialised");
     *   HORO_LOG_DEBUG("editor.modal.settings", "Theme changed to {}", name);
     *
     * Disabled levels do not evaluate format arguments.
     */
    class Logger
    {
    public:
        Logger(const Logger &) = delete;
        Logger &operator=(const Logger &) = delete;
        Logger(Logger &&) = delete;
        Logger &operator=(Logger &&) = delete;

        /** @brief Initialises the global logger. Must be called once at startup. */
        static void Init(std::string_view logDir, std::string_view baseName);

        /** @brief Shuts down the logger, flushes and closes the file. */
        static void Shutdown();

        /** @brief Returns the active global minimum level. */
        [[nodiscard]] static Level GetLevel() noexcept;

        /** @brief Sets the active global minimum level. */
        static void SetLevel(Level level) noexcept;

        /**
         * @brief Logs a structured record if `level` passes the current filter.
         *
         * @param category  Stable dotted category (e.g. "editor.startup").
         * @param level     Severity of this record.
         * @param message   Pre-formatted message. Format arguments are evaluated
         *                  only when the record passes the level gate.
         */
        static void Write(std::string_view category, Level level, std::string_view message);

        /** @brief Emits the startup system-information snapshot. */
        static void DumpStartupInfo();

    private:
        Logger() = default;

        static Logger &Instance();

        std::FILE *m_file = nullptr;
        Level m_level = Level::Info;
        std::chrono::steady_clock::time_point m_startTime;
        uint64_t m_sequence = 0;
    };

} // namespace Horo::Log

// ── Convenience macros ──────────────────────────────────────────────────
// Disabled levels compile to nothing (no format argument evaluation).

#define HORO_LOG_TRACE(cat, ...)                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (::Horo::Log::Logger::GetLevel() <= ::Horo::Log::Level::Trace)                                              \
            ::Horo::Log::Logger::Write((cat), ::Horo::Log::Level::Trace, ::Horo::Log::FormatArgs(__VA_ARGS__));       \
    } while (false)

#define HORO_LOG_DEBUG(cat, ...)                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (::Horo::Log::Logger::GetLevel() <= ::Horo::Log::Level::Debug)                                              \
            ::Horo::Log::Logger::Write((cat), ::Horo::Log::Level::Debug, ::Horo::Log::FormatArgs(__VA_ARGS__));       \
    } while (false)

#define HORO_LOG_INFO(cat, ...)                                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        if (::Horo::Log::Logger::GetLevel() <= ::Horo::Log::Level::Info)                                               \
            ::Horo::Log::Logger::Write((cat), ::Horo::Log::Level::Info, ::Horo::Log::FormatArgs(__VA_ARGS__));        \
    } while (false)

#define HORO_LOG_WARN(cat, ...)                                                                                        \
    ::Horo::Log::Logger::Write((cat), ::Horo::Log::Level::Warn, ::Horo::Log::FormatArgs(__VA_ARGS__))

#define HORO_LOG_ERROR(cat, ...)                                                                                       \
    ::Horo::Log::Logger::Write((cat), ::Horo::Log::Level::Error, ::Horo::Log::FormatArgs(__VA_ARGS__))

#define HORO_LOG_CRITICAL(cat, ...)                                                                                    \
    ::Horo::Log::Logger::Write((cat), ::Horo::Log::Level::Critical, ::Horo::Log::FormatArgs(__VA_ARGS__))

namespace Horo::Log
{

    /**
     * @brief Simple format helper — avoids non-POD variadic UB.
     *
     * Accepts fundamental types and `const char *` only.
     * For std::string, pass `.c_str()` explicitly.
     */
    template <typename... Args>
    [[nodiscard]] std::string FormatArgs(const char *fmt, Args &&...args)
    {
        const int size = std::snprintf(nullptr, 0, fmt, std::forward<Args>(args)...);
        if (size <= 0)
            return {};
        std::string result(static_cast<std::size_t>(size) + 1, '\0');
        std::snprintf(result.data(), result.size(), fmt, std::forward<Args>(args)...);
        result.pop_back();
        return result;
    }

    [[nodiscard]] inline std::string FormatArgs(const char *msg)
    {
        return {msg};
    }

} // namespace Horo::Log
