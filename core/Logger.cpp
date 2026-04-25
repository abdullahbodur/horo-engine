#include "core/Logger.h"
#include "core/LogBuffer.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ranges>
#include <source_location>
#include <string>
#include <vector>

namespace Monolith {
    namespace {
        LogLevel ParseLogLevelFromEnv() {
#ifdef _WIN32
            size_t len = 0;
            if (getenv_s(&len, nullptr, 0, "MONOLITH_LOG_LEVEL") != 0 || len <= 1)
                return LogLevel::Info;
            std::vector<char> value(len);
            if (getenv_s(&len, value.data(), value.size(), "MONOLITH_LOG_LEVEL") != 0 ||
                len <= 1)
                return LogLevel::Info;
            std::string level(value.data());
#else
            const char *raw = std::getenv("MONOLITH_LOG_LEVEL");
            if (!raw || !*raw)
                return LogLevel::Info;
            std::string level(raw);
#endif
            std::ranges::transform(level, level.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });

            if (level == "debug")
                return LogLevel::Debug;
            if (level == "warn" || level == "warning")
                return LogLevel::Warn;
            if (level == "error")
                return LogLevel::Error;
            return LogLevel::Info;
        }

        bool ShouldLog(LogLevel level) {
            static const LogLevel minimumLevel = ParseLogLevelFromEnv();
            return static_cast<int>(level) >= static_cast<int>(minimumLevel);
        }
    } // namespace

    void LogImpl(LogLevel level, const std::source_location &loc,
                 std::string_view message) {
        if (!ShouldLog(level))
            return;

        const char *prefix;
        switch (level) {
                using enum LogLevel;
            case Debug:
                prefix = "[DEBUG] ";
                break;
            case Info:
                prefix = "[INFO] ";
                break;
            case Warn:
                prefix = "[WARN] ";
                break;
            case Error:
                prefix = "[ERROR] ";
                break;
            default:
                prefix = "[?] ";
                break;
        }

        // Strip path prefix — show only filename
        const char *file = loc.file_name();
        const char *slash = file;
        for (const char *p = file; *p; p++)
            if (*p == '/' || *p == '\\')
                slash = p + 1;

        const auto line = static_cast<int>(loc.line());

        if (level == LogLevel::Error || level == LogLevel::Warn)
            std::fprintf(stderr, "%s%.*s (%s:%d)\n", prefix,
                         static_cast<int>(message.size()), message.data(), slash, line);
        else
            std::fprintf(stdout, "%s%.*s\n", prefix, static_cast<int>(message.size()),
                         message.data());

        if (level == LogLevel::Error || level == LogLevel::Warn)
            std::fflush(stderr);
        else
            std::fflush(stdout);

        LogBuffer::Instance().Push(level, slash, line, message);
    }
} // namespace Monolith
