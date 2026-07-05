#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "core/LogBuffer.h"
#include "core/Logger.h"

using namespace Horo;

namespace {
std::string ReadEnvValue(const char *name) {
  if (!name || !*name)
    return {};
#ifdef _WIN32
  size_t len = 0;
  if (getenv_s(&len, nullptr, 0, name) != 0 || len <= 1)
    return {};
  std::vector<char> value(len);
  if (getenv_s(&len, value.data(), value.size(), name) != 0 || len <= 1)
    return {};
  return std::string(value.data());
#else
  const char *value = std::getenv(name);
  return value ? std::string(value) : std::string();
#endif
}

class ScopedEnvVar {
public:
  ScopedEnvVar(const char *name, const char *value)
      : m_name(name ? name : ""), m_previous(ReadEnvValue(name)),
        m_hadPrevious(!m_previous.empty()) {
    if (m_name.empty())
      return;
#ifdef _WIN32
    _putenv_s(m_name.c_str(), value ? value : "");
#else
    if (value)
      setenv(m_name.c_str(), value, 1);
    else
      unsetenv(m_name.c_str());
#endif
  }

  ~ScopedEnvVar() {
    if (m_name.empty())
      return;
#ifdef _WIN32
    if (m_hadPrevious)
      _putenv_s(m_name.c_str(), m_previous.c_str());
    else
      _putenv_s(m_name.c_str(), "");
#else
    if (m_hadPrevious)
      setenv(m_name.c_str(), m_previous.c_str(), 1);
    else
      unsetenv(m_name.c_str());
#endif
  }

  ScopedEnvVar(const ScopedEnvVar &) = delete;

  ScopedEnvVar &operator=(const ScopedEnvVar &) = delete;

  ScopedEnvVar(ScopedEnvVar &&) = delete;

  ScopedEnvVar &operator=(ScopedEnvVar &&) = delete;

private:
  std::string m_name;
  std::string m_previous;
  bool m_hadPrevious = false;
};

std::vector<LogLine> SnapshotLogLines() {
  std::vector<LogLine> lines;
  LogBuffer::Instance().CopyLinesTo(&lines);
  return lines;
}
} // namespace

TEST_CASE("Logger env warn: filters info/debug but keeps warn/error", "[logger][env][warn]") {
  const ScopedEnvVar logLevel("HORO_LOG_LEVEL", "warning");
  LogBuffer::Instance().Clear();

  LogDebug("debug should be filtered");
  LogInfo("info should be filtered");
  LogWarn("warn should be recorded");
  LogError("error should be recorded");

  const std::vector<LogLine> lines = SnapshotLogLines();
  REQUIRE(lines.size() == 2);
  REQUIRE(lines[0].level == LogLevel::Warn);
  REQUIRE(lines[1].level == LogLevel::Error);
  REQUIRE(LogBuffer::Instance().CountInfo() == 0);
  REQUIRE(LogBuffer::Instance().CountWarn() == 1);
  REQUIRE(LogBuffer::Instance().CountError() == 1);
}

TEST_CASE("Logger env error: filters warn/info/debug and keeps error", "[logger][env][error]") {
  const ScopedEnvVar logLevel("HORO_LOG_LEVEL", "error");
  LogBuffer::Instance().Clear();

  LogDebug("debug should be filtered");
  LogInfo("info should be filtered");
  LogWarn("warn should be filtered");
  LogError("error should be recorded");

  const std::vector<LogLine> lines = SnapshotLogLines();
  REQUIRE(lines.size() == 1);
  REQUIRE(lines[0].level == LogLevel::Error);
  REQUIRE(LogBuffer::Instance().CountInfo() == 0);
  REQUIRE(LogBuffer::Instance().CountWarn() == 0);
  REQUIRE(LogBuffer::Instance().CountError() == 1);
}

TEST_CASE("Logger env invalid value falls back to info", "[logger][env][invalid]") {
  const ScopedEnvVar logLevel("HORO_LOG_LEVEL", "verbose");
  LogBuffer::Instance().Clear();

  LogInfo("info is kept on fallback");
  LogWarn("warn is kept on fallback");
  LogError("error is kept on fallback");

  const std::vector<LogLine> lines = SnapshotLogLines();
  REQUIRE(lines.size() == 3);
  REQUIRE(lines[0].level == LogLevel::Info);
  REQUIRE(lines[1].level == LogLevel::Warn);
  REQUIRE(lines[2].level == LogLevel::Error);
}

TEST_CASE("Logger env debug: keeps debug and info levels", "[logger][env][debug]") {
  const ScopedEnvVar logLevel("HORO_LOG_LEVEL", "debug");
  LogBuffer::Instance().Clear();

  LogDebug("debug is kept");
  LogInfo("info is kept");

  const std::vector<LogLine> lines = SnapshotLogLines();
  REQUIRE(lines.size() == 2);
  REQUIRE(lines[0].level == LogLevel::Debug);
  REQUIRE(lines[1].level == LogLevel::Info);
}

TEST_CASE("Logger env empty value falls back to info", "[logger][env][empty]") {
  const ScopedEnvVar logLevel("HORO_LOG_LEVEL", "");
  LogBuffer::Instance().Clear();

  LogDebug("debug is filtered on empty env");
  LogInfo("info is kept on empty env");

  const std::vector<LogLine> lines = SnapshotLogLines();
  REQUIRE(lines.size() == 1);
  REQUIRE(lines[0].level == LogLevel::Info);
}

TEST_CASE("Logger env mixed-case debug value is normalized", "[logger][env][mixed_debug]") {
  const ScopedEnvVar logLevel("HORO_LOG_LEVEL", "DeBuG");
  LogBuffer::Instance().Clear();

  LogDebug("debug survives mixed-case env");
  const std::vector<LogLine> lines = SnapshotLogLines();

  REQUIRE(lines.size() == 1);
  REQUIRE(lines[0].level == LogLevel::Debug);
}

TEST_CASE("Logger env short warn alias keeps warning logs", "[logger][env][warn_alias]") {
  const ScopedEnvVar logLevel("HORO_LOG_LEVEL", "warn");
  LogBuffer::Instance().Clear();

  LogInfo("info is filtered");
  LogWarn("warn is kept via alias");

  const std::vector<LogLine> lines = SnapshotLogLines();
  REQUIRE(lines.size() == 1);
  REQUIRE(lines[0].level == LogLevel::Warn);
}

TEST_CASE("Logger default level label handles unknown enum values", "[logger][env][unknown_level]") {
  const ScopedEnvVar logLevel("HORO_LOG_LEVEL", "info");
  LogBuffer::Instance().Clear();

  LogImpl(static_cast<LogLevel>(99), std::source_location::current(),
          "unknown-level-message");

  const std::vector<LogLine> lines = SnapshotLogLines();
  REQUIRE(lines.size() == 1);
  REQUIRE(lines[0].message == "unknown-level-message");
}

TEST_CASE("Logger unset env falls back to info", "[logger][env][unset]") {
  const ScopedEnvVar unsetLevel("HORO_LOG_LEVEL", nullptr);
  LogBuffer::Instance().Clear();

  LogDebug("debug is filtered when env is absent");
  LogInfo("info is kept when env is absent");

  const std::vector<LogLine> lines = SnapshotLogLines();
  REQUIRE(lines.size() == 1);
  REQUIRE(lines[0].level == LogLevel::Info);
}
