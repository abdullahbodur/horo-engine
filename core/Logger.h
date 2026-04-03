#pragma once
#include <cstdio>
#include <string_view>

namespace Horo {

enum class LogLevel { Info, Warn, Error };

void Log(LogLevel level, const char* file, int line, const char* fmt, ...);

}  // namespace Horo

// NOLINT: intentional GNU extension for zero-arg VA_ARGS compatibility
#define LOG_INFO(fmt, ...) \
  Horo::Log(Horo::LogLevel::Info, __FILE__, __LINE__, fmt __VA_OPT__(, ) __VA_ARGS__)
#define LOG_WARN(fmt, ...) \
  Horo::Log(Horo::LogLevel::Warn, __FILE__, __LINE__, fmt __VA_OPT__(, ) __VA_ARGS__)
#define LOG_ERROR(fmt, ...) \
  Horo::Log(Horo::LogLevel::Error, __FILE__, __LINE__, fmt __VA_OPT__(, ) __VA_ARGS__)
