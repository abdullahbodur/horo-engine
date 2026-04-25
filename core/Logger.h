#pragma once
#include <format>
#include <source_location>
#include <string_view>

namespace Monolith {
enum class LogLevel { Debug, Info, Warn, Error };

void LogImpl(LogLevel level, const std::source_location &loc,
             std::string_view message);
} // namespace Monolith

// ---------------------------------------------------------------------------
// Format-string logging helpers using CTAD + std::source_location (C++20).
//
// These are intentionally defined as template STRUCTS in the global namespace
// rather than as function templates or macros.  The struct/CTAD pattern is
// required for two reasons:
//
//   1. Deduction ambiguity: trailing std::source_location default parameters
//      on variadic function templates cause Clang to mis-deduce Args as an
//      empty pack and attempt to bind the format argument as the location.
//      CTAD constructors do not suffer from this because the deduction guide
//      is evaluated separately from the constructor's default arguments.
//
//   2. Macro conflict avoidance: third-party headers (e.g. imgui_te_context.h)
//      declare member functions named LogInfo / LogError inside class bodies.
//      Macros expand at the preprocessor level and corrupt such declarations;
//      class templates do not — inside a class body `void LogInfo(...)` simply
//      declares a member function and never touches the global struct name.
//
// Usage (identical to function calls, including inside Monolith sub-namespaces
// via ADL / enclosing namespace lookup):
//
//   LogWarn("failed to load '{}': {}", path, e.what());
//   LogInfo("editor ready");
// ---------------------------------------------------------------------------

template <typename... Args> struct LogDebug {
  LogDebug(std::format_string<Args...> fmt, const Args &...args,
           std::source_location loc = std::source_location::current()) {
    Monolith::LogImpl(Monolith::LogLevel::Debug, loc,
                      std::vformat(fmt.get(), std::make_format_args(args...)));
  }
};

template <typename... Args>
LogDebug(std::format_string<Args...>, const Args &...) -> LogDebug<Args...>;

template <typename... Args> struct LogInfo {
  LogInfo(std::format_string<Args...> fmt, const Args &...args,
          std::source_location loc = std::source_location::current()) {
    Monolith::LogImpl(Monolith::LogLevel::Info, loc,
                      std::vformat(fmt.get(), std::make_format_args(args...)));
  }
};

template <typename... Args>
LogInfo(std::format_string<Args...>, const Args &...) -> LogInfo<Args...>;

template <typename... Args> struct LogWarn {
  LogWarn(std::format_string<Args...> fmt, const Args &...args,
          std::source_location loc = std::source_location::current()) {
    Monolith::LogImpl(Monolith::LogLevel::Warn, loc,
                      std::vformat(fmt.get(), std::make_format_args(args...)));
  }
};

template <typename... Args>
LogWarn(std::format_string<Args...>, const Args &...) -> LogWarn<Args...>;

template <typename... Args> struct LogError {
  LogError(std::format_string<Args...> fmt, const Args &...args,
           std::source_location loc = std::source_location::current()) {
    Monolith::LogImpl(Monolith::LogLevel::Error, loc,
                      std::vformat(fmt.get(), std::make_format_args(args...)));
  }
};

template <typename... Args>
LogError(std::format_string<Args...>, const Args &...) -> LogError<Args...>;
