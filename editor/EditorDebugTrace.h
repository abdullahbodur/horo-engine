#pragma once

// Optional stderr tracing for the in-game editor (not routed through game Log / LogBuffer).
// Debug builds only. Enable with environment variable MONOLITH_EDITOR_TRACE=1

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <stdlib.h>  // _dupenv_s
#endif

namespace Monolith {
namespace Editor {

inline bool EditorTraceEnabled() {
#ifndef NDEBUG
  static int  checked = 0;
  static bool on      = false;
  if (!checked) {
    checked = 1;
#ifdef _WIN32
    char*   buf = nullptr;
    size_t  sz  = 0;
    if (_dupenv_s(&buf, &sz, "MONOLITH_EDITOR_TRACE") == 0 && buf) {
      on = buf[0] != '\0' && buf[0] != '0';
      std::free(buf);
    }
#else
    const char* e = std::getenv("MONOLITH_EDITOR_TRACE");
    on            = e && e[0] != '\0' && e[0] != '0';
#endif
  }
  return on;
#else
  return false;
#endif
}

inline void EditorTrace(const char* fmt, ...) {
  if (!EditorTraceEnabled())
    return;
  std::fputs("[EDITOR] ", stderr);
  va_list args;
  va_start(args, fmt);
  std::vfprintf(stderr, fmt, args);
  va_end(args);
  std::fputc('\n', stderr);
  std::fflush(stderr);
}

}  // namespace Editor
}  // namespace Monolith
