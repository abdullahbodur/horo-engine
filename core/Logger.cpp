#include "core/Logger.h"
#include "core/LogBuffer.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <string>

namespace Monolith {
namespace {

LogLevel ParseLogLevelFromEnv() {
#ifdef _WIN32
  char* rawValue = nullptr;
  size_t len = 0;
  if (_dupenv_s(&rawValue, &len, "MONOLITH_LOG_LEVEL") != 0 || !rawValue)
    return LogLevel::Info;
  std::string level(rawValue);
  std::free(rawValue);
#else
  const char* raw = std::getenv("MONOLITH_LOG_LEVEL");
  if (!raw || !*raw)
    return LogLevel::Info;
  std::string level(raw);
#endif
  std::transform(level.begin(), level.end(), level.begin(), [](unsigned char ch) {
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

}  // namespace

void Log(LogLevel level, const char* file, int line, const char* fmt, ...) {
  if (!ShouldLog(level))
    return;

  const char* prefix;
  switch (level) {
    case LogLevel::Debug:
      prefix = "[DEBUG] ";
      break;
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
  if (level == LogLevel::Error)
    std::fflush(stderr);
  else
    std::fflush(stdout);

  LogBuffer::Instance().Push(level, slash, line, std::string(msg));
}

}  // namespace Monolith
