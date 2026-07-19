/** @copydoc Logger.h */

#include "Horo/Foundation/Logging/Logger.h"
#include "Horo/Foundation/Logging/LogContext.h"

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <mutex>
#include <string>
#include <thread>

#if defined(__APPLE__) || defined(__linux__)
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace Horo::Log
{
    namespace
    {
        std::mutex& LoggerMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        std::string ResolvePlatform()
        {
#if defined(__APPLE__)
            return "macOS";
#elif defined(__linux__)
            return "Linux";
#elif defined(_WIN32)
            return "Windows";
#else
            return "Unknown";
#endif
        }

        std::string ResolveArch()
        {
#if defined(__x86_64__) || defined(_M_X64)
            return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
            return "arm64";
#else
            return "unknown";
#endif
        }

        int ResolveCpuCount()
        {
#if defined(__APPLE__) || defined(__linux__)
            return static_cast<int>(std::thread::hardware_concurrency());
#elif defined(_WIN32)
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            return static_cast<int>(si.dwNumberOfProcessors);
#else
            return 0;
#endif
        }

        /** @brief Returns the log directory, creating it if needed. Expands ~ to $HOME. */
        std::string ResolveLogDir(std::string_view dir)
        {
            std::string resolved{dir};
            if (!resolved.empty() && resolved[0] == '~')
            {
                const char* home = std::getenv("HOME");
                if (home == nullptr)
                    home = std::getenv("USERPROFILE");
                if (home != nullptr)
                    resolved = std::string{home} + resolved.substr(1);
            }
            const std::filesystem::path path{resolved};
            if (path.empty() || path.is_relative())
                return {};
            for (const auto& component : path)
            {
                if (component == "..")
                    return {};
            }
            std::error_code ec;
            std::filesystem::create_directories(path, ec);
            return ec ? std::string{} : path.string();
        }

        /** @brief Formats a UTC timestamp as ISO-8601 with milliseconds. */
        std::string NowUtc()
        {
            const auto now = std::chrono::system_clock::now();
            const auto timeT = std::chrono::system_clock::to_time_t(now);
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

            std::tm tm{};
#if defined(_WIN32)
            gmtime_s(&tm, &timeT);
#else
            gmtime_r(&timeT, &tm);
#endif

            return std::format("{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}.{:03d}Z", tm.tm_year + 1900, tm.tm_mon + 1,
                               tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ms.count());
        }

        /** @brief JSON-escapes a string value, including control characters outside \\n \\r \\t. */
        std::string JsonEscape(std::string_view s)
        {
            static constexpr char kHex[] = "0123456789abcdef";

            std::string out;
            out.reserve(s.size() + 4);
            for (const unsigned char c : s)
            {
                switch (c)
                {
                case '"':
                    out += R"(\")";
                    break;
                case '\\':
                    out += R"(\\)";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                case '\b':
                    out += "\\b";
                    break;
                case '\f':
                    out += "\\f";
                    break;
                default:
                    if (c < 0x20)
                    {
                        // Any other control character: emit \u00XX so the
                        // record stays valid JSON.
                        out += "\\u00";
                        const auto byte = static_cast<std::byte>(c);
                        out += kHex[std::to_integer<unsigned>(byte >> 4) & 0xFU];
                        out += kHex[std::to_integer<unsigned>(byte) & 0xFU];
                    }
                    else
                    {
                        out += static_cast<char>(c);
                    }
                    break;
                }
            }
            return out;
        }
    } // namespace

    Logger& Logger::Instance()
    {
        static Logger logger;
        return logger;
    }

    void Logger::Init(std::string_view logDir, std::string_view baseName)
    {
        auto& self = Instance();
        std::lock_guard lock(LoggerMutex());

        if (self.m_file != nullptr)
            return; // already initialised

        const std::filesystem::path dir = ResolveLogDir(logDir);
        const std::filesystem::path path = dir / (std::string{baseName} + ".jsonl");

        self.m_file = std::fopen(path.string().c_str(), "a");
        self.m_startTime = std::chrono::steady_clock::now();
        self.m_sequence = 0;

        if (self.m_file == nullptr)
        {
            // Don't fail silently: a logger that can't write should still
            // tell someone. Every subsequent Write() call is a no-op until
            // this is fixed.
            std::fprintf(stderr, "[Logger] failed to open log file \"%s\" for append (errno=%d): %s\n", path.c_str(),
                         errno,
                         std::strerror(errno));
        }

        // Respect HORO_LOG_LEVEL env var
        if (const char* env = std::getenv("HORO_LOG_LEVEL"))
        {
            using enum Level;
            const std::string_view sv{env};
            if (sv == "trace")
                self.m_level = Trace;
            else if (sv == "debug")
                self.m_level = Debug;
            else if (sv == "info")
                self.m_level = Info;
            else if (sv == "warn")
                self.m_level = Warn;
            else if (sv == "error")
                self.m_level = Error;
            else if (sv == "critical")
                self.m_level = Critical;
            else if (sv == "off")
                self.m_level = Off;
        }

        // Bootstrap log — goes to stderr if file not yet open
        if (self.m_file != nullptr)
        {
            std::fprintf(
                self.m_file,
                R"({"schemaVersion":1,"level":"info","category":"observability.startup","message":"Logger initialised","path":"%s"})"
                "\n",
                JsonEscape(path.string()).c_str());
            std::fflush(self.m_file);
        }
    }

    void Logger::Shutdown()
    {
        auto& self = Instance();
        std::lock_guard lock(LoggerMutex());

        if (self.m_file != nullptr)
        {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - self.m_startTime)
                .count();

            std::fprintf(
                self.m_file,
                R"({"schemaVersion":1,"level":"info","category":"observability.shutdown","message":"Logger shut down cleanly","elapsedMs":%lld,"sequence":%llu})"
                "\n",
                static_cast<long long>(elapsed), static_cast<unsigned long long>(self.m_sequence));
            std::fflush(self.m_file);
            std::fclose(self.m_file);
            self.m_file = nullptr;
        }
    }

    Level Logger::GetLevel() noexcept
    {
        return Instance().m_level;
    }

    void Logger::SetLevel(const Level level) noexcept
    {
        Instance().m_level = level;
    }

    void Logger::Write(std::string_view category, const Level level, std::string_view message)
    {
        // Enforce the configured level here too, not just in the LOG_* macros —
        // Write() is a public entry point and can be called directly.
        if (level < Instance().m_level || Instance().m_level == Level::Off)
            return;

        // Capture MDC before acquiring the mutex — thread-local read, no lock needed.
        const auto mdcFields = GetMdcFields();

        // Build "mdc":{...} JSON fragment (empty string if no active context).
        std::string mdcJson;
        std::string mdcPrefix;
        if (!mdcFields.empty())
        {
            mdcJson.reserve(64);
            mdcPrefix.reserve(64);
            mdcJson += ",\"mdc\":{";
            mdcPrefix += " [";
            for (std::size_t i = 0; i < mdcFields.size(); ++i)
            {
                if (i > 0)
                {
                    mdcJson += ',';
                    mdcPrefix += ", ";
                }
                mdcJson += '"';
                mdcJson += JsonEscape(mdcFields[i].first);
                mdcJson += "\":\"";
                mdcJson += JsonEscape(mdcFields[i].second);
                mdcJson += '"';

                mdcPrefix += mdcFields[i].first;
                mdcPrefix += '=';
                mdcPrefix += mdcFields[i].second;
            }
            mdcJson += '}';
            mdcPrefix += ']';
        }

        auto& self = Instance();
        std::lock_guard lock(LoggerMutex());

        const auto seq = ++self.m_sequence;
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - self.m_startTime)
            .count();

        const auto pid =
#if defined(_WIN32)
            static_cast<int64_t>(GetCurrentProcessId());
#else
            static_cast<int64_t>(getpid());
#endif

        // Write JSONL record
        if (self.m_file != nullptr)
        {
            std::fprintf(
                self.m_file,
                R"({"schemaVersion":1,"timestamp":"%s","elapsedMs":%lld,"sequence":%llu,"level":"%s","category":"%s","message":"%s","pid":%lld%s})"
                "\n",
                NowUtc().c_str(), static_cast<long long>(elapsed), static_cast<unsigned long long>(seq),
                ToString(level),
                JsonEscape(category).c_str(), JsonEscape(message).c_str(), static_cast<long long>(pid),
                mdcJson.c_str());
            std::fflush(self.m_file);
        }

        // Also echo to stderr in debug/dev builds.
        // NOTE: string_view is not guaranteed null-terminated, so it must never
        // be passed to a bare "%s" — use "%.*s" with an explicit length instead.
#ifndef NDEBUG
        std::fprintf(stderr, "[%s]%s %.*s: %.*s\n", ToString(level), mdcPrefix.c_str(),
                     static_cast<int>(category.size()),
                     category.data(), static_cast<int>(message.size()), message.data());
#endif
    }

    void Logger::DumpStartupInfo()
    {
        LOG_INFO("observability.startup", "Process started");

        // Platform
        LOG_INFO("observability.startup", "Platform: %s %s", ResolvePlatform().c_str(), ResolveArch().c_str());

        // CPU
        LOG_INFO("observability.startup", "Logical cores: %d", ResolveCpuCount());

        // Process ID
#if defined(_WIN32)
        LOG_INFO("observability.startup", "PID: %lld", static_cast<long long>(GetCurrentProcessId()));
#else
        LOG_INFO("observability.startup", "PID: %lld", static_cast<long long>(getpid()));
#endif

        // Compiler
#if defined(__clang__)
        LOG_INFO("observability.startup", "Compiler: Clang %d.%d.%d", __clang_major__, __clang_minor__,
                 __clang_patchlevel__);
#elif defined(__GNUC__)
        LOG_INFO("observability.startup", "Compiler: GCC %d.%d", __GNUC__, __GNUC_MINOR__);
#elif defined(_MSC_VER)
        LOG_INFO("observability.startup", "Compiler: MSVC %d", _MSC_VER);
#endif

        // Build config
#ifdef NDEBUG
        LOG_INFO("observability.startup", "Build: Release");
#else
        LOG_INFO("observability.startup", "Build: Debug");
#endif

        // Log level
        LOG_INFO("observability.startup", "Log level: %s", ToString(Logger::GetLevel()));
    }
} // namespace Horo::Log
