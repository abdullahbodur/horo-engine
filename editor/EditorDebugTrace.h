#pragma once

// Optional stderr tracing for the in-game editor (not routed through game Log /
// LogBuffer). Debug builds only. Enable with environment variable
// MONOLITH_EDITOR_TRACE=1

#include <cstdio>
#include <cstdlib>
#include <format>
#include <string>

namespace Monolith::Editor {
inline bool EditorTraceEnabled() {
#ifndef NDEBUG
  static int checked = 0;
  static bool on = false;
  if (!checked) {
    checked = 1;
#if defined(_WIN32) && defined(_MSC_VER)
    char *value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, "MONOLITH_EDITOR_TRACE") == 0 && value) {
      on = value[0] != '\0' && value[0] != '0';
      std::free(value);
    }
#else
    const char *e = std::getenv("MONOLITH_EDITOR_TRACE");
    on = e && e[0] != '\0' && e[0] != '0';
#endif
  }
  return on;
#else
  return false;
#endif
}

template <typename... Args>
inline void EditorTrace(std::format_string<Args...> fmt, Args &&...args) {
  if (!EditorTraceEnabled())
    return;
  std::fputs("[EDITOR] ", stderr);
  const std::string msg = std::format(fmt, std::forward<Args>(args)...);
  std::fputs(msg.c_str(), stderr);
  std::fputc('\n', stderr);
  std::fflush(stderr);
}
} // namespace Monolith::Editor
