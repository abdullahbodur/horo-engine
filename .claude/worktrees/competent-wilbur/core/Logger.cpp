#include "core/Logger.h"

#include <cstdarg>
#include <cstdio>

namespace Monolith {

void Log(LogLevel level, const char* file, int line, const char* fmt, ...) {
  const char* prefix;
  switch (level) {
    case LogLevel::Info:
      prefix = "[INFO] ";
      break;
    case LogLevel::Warn:
      prefix = "[WARN] ";
      break;
    case LogLevel::Error:
      prefix = "[ERROR] ";
      break;
    default:
      prefix = "[?] ";
      break;
  }

  char msg[1024];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(msg, sizeof(msg), fmt, args);
  va_end(args);

  // Strip path prefix — show only filename
  const char* slash = file;
  for (const char* p = file; *p; p++)
    if (*p == '/' || *p == '\\')
      slash = p + 1;

  if (level == LogLevel::Error)
    std::fprintf(stderr, "%s%s (%s:%d)\n", prefix, msg, slash, line);
  else
    std::printf("%s%s\n", prefix, msg);
}

}  // namespace Monolith
