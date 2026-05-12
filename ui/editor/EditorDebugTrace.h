/** @file EditorDebugTrace.h
 *  @brief Optional stderr tracing for the in-game editor, enabled via HORO_EDITOR_TRACE=1.
 */
#pragma once

// Optional stderr tracing for the in-game editor (not routed through game Log /
// LogBuffer). Debug builds only. Enable with environment variable
// HORO_EDITOR_TRACE=1

#include <cstdio>
#include <cstdlib>
#include <format>
#include <string>

namespace Horo::Editor {
    /** @brief Returns true when editor tracing is enabled for the current process.
     *
     *  Checks the HORO_EDITOR_TRACE environment variable once per process and
     *  caches the result.  Always returns false in release builds.
     *
     *  @return True when tracing output should be emitted to stderr.
     */
    inline bool EditorTraceEnabled() {
#ifndef NDEBUG
        static int checked = 0;
        static bool on = false;
        if (!checked) {
            checked = 1;
#if defined(_WIN32) && defined(_MSC_VER)
            char *value = nullptr;
            size_t len = 0;
            if (_dupenv_s(&value, &len, "HORO_EDITOR_TRACE") == 0 && value) {
                on = value[0] != '\0' && value[0] != '0';
                std::free(value);
            }
#else
            const char *e = std::getenv("HORO_EDITOR_TRACE");
            on = e && e[0] != '\0' && e[0] != '0';
#endif
        }
        return on;
#else
        return false;
#endif
    }

    /** @brief Writes a formatted trace message to stderr when tracing is enabled.
     *
     *  Messages are prefixed with "[EDITOR] " and terminated with a newline.
     *  This is a no-op in release builds or when HORO_EDITOR_TRACE is unset.
     *
     *  @param fmt  std::format format string.
     *  @param args Arguments forwarded to std::format.
     */
    template<typename... Args>
    inline void EditorTrace(std::format_string<Args...> fmt, Args &&... args) {
        if (!EditorTraceEnabled())
            return;
        std::fputs("[EDITOR] ", stderr);
        const std::string msg = std::format(fmt, std::forward<Args>(args)...);
        std::fputs(msg.c_str(), stderr);
        std::fputc('\n', stderr);
        std::fflush(stderr);
    }
} // namespace Horo::Editor
