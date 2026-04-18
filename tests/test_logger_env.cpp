#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "core/LogBuffer.h"
#include "core/Logger.h"

using namespace Monolith;

namespace {

std::string ReadEnvValue(const char* name) {
  if (!name || !*name)
    return {};
#ifdef _WIN32
  char* rawValue = nullptr;
  size_t len = 0;
  if (_dupenv_s(&rawValue, &len, name) != 0 || !rawValue)
    return {};
  std::unique_ptr<char, decltype(&std::free)> value(rawValue, &std::free);
  return std::string(value.get());
#else
  const char* value = std::getenv(name);
  return value ? std::string(value) : std::string();
#endif
}

class ScopedEnvVar {
 public:
  ScopedEnvVar(const char* name, const char* value)
      : m_name(name ? name : ""), m_previous(ReadEnvValue(name)), m_hadPrevious(!m_previous.empty()) {
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

}  // namespace

TEST_CASE("Logger env warn: filters info/debug but keeps warn/error", "[logger][env][warn]") {
  LogBuffer::Instance().Clear();

  LOG_DEBUG("debug should be filtered");
  LOG_INFO("info should be filtered");
  LOG_WARN("warn should be recorded");
  LOG_ERROR("error should be recorded");

  const std::vector<LogLine> lines = SnapshotLogLines();
  REQUIRE(lines.size() == 2);
  REQUIRE(lines[0].level == LogLevel::Warn);
  REQUIRE(lines[1].level == LogLevel::Error);
  REQUIRE(LogBuffer::Instance().CountInfo() == 0);
  REQUIRE(LogBuffer::Instance().CountWarn() == 1);
  REQUIRE(LogBuffer::Instance().CountError() == 1);
}

TEST_CASE("Logger env error: filters warn/info/debug and keeps error", "[logger][env][error]") {
  LogBuffer::Instance().Clear();

  LOG_DEBUG("debug should be filtered");
  LOG_INFO("info should be filtered");
  LOG_WARN("warn should be filtered");
  LOG_ERROR("error should be recorded");

  const std::vector<LogLine> lines = SnapshotLogLines();
  REQUIRE(lines.size() == 1);
  REQUIRE(lines[0].level == LogLevel::Error);
  REQUIRE(LogBuffer::Instance().CountInfo() == 0);
  REQUIRE(LogBuffer::Instance().CountWarn() == 0);
  REQUIRE(LogBuffer::Instance().CountError() == 1);
}

TEST_CASE("Logger env invalid value falls back to info", "[logger][env][invalid]") {
  LogBuffer::Instance().Clear();

  LOG_INFO("info is kept on fallback");
  LOG_WARN("warn is kept on fallback");
  LOG_ERROR("error is kept on fallback");

  const std::vector<LogLine> lines = SnapshotLogLines();
  REQUIRE(lines.size() == 3);
  REQUIRE(lines[0].level == LogLevel::Info);
  REQUIRE(lines[1].level == LogLevel::Warn);
  REQUIRE(lines[2].level == LogLevel::Error);
}

TEST_CASE("Logger env debug: keeps debug and info levels", "[logger][env][debug]") {
  LogBuffer::Instance().Clear();

  LOG_DEBUG("debug is kept");
  LOG_INFO("info is kept");

  const std::vector<LogLine> lines = SnapshotLogLines();
  REQUIRE(lines.size() == 2);
  REQUIRE(lines[0].level == LogLevel::Debug);
  REQUIRE(lines[1].level == LogLevel::Info);
}

TEST_CASE("Logger env empty value falls back to info", "[logger][env][empty]") {
  LogBuffer::Instance().Clear();

  LOG_DEBUG("debug is filtered on empty env");
  LOG_INFO("info is kept on empty env");

  const std::vector<LogLine> lines = SnapshotLogLines();
  REQUIRE(lines.size() == 1);
  REQUIRE(lines[0].level == LogLevel::Info);
}

TEST_CASE("Logger env mixed-case debug value is normalized", "[logger][env][mixed_debug]") {
  LogBuffer::Instance().Clear();

  LOG_DEBUG("debug survives mixed-case env");
  const std::vector<LogLine> lines = SnapshotLogLines();

  REQUIRE(lines.size() == 1);
  REQUIRE(lines[0].level == LogLevel::Debug);
}

TEST_CASE("Logger env short warn alias keeps warning logs", "[logger][env][warn_alias]") {
  LogBuffer::Instance().Clear();

  LOG_INFO("info is filtered");
  LOG_WARN("warn is kept via alias");

  const std::vector<LogLine> lines = SnapshotLogLines();
  REQUIRE(lines.size() == 1);
  REQUIRE(lines[0].level == LogLevel::Warn);
}

TEST_CASE("Logger default level label handles unknown enum values", "[logger][env][unknown_level]") {
  LogBuffer::Instance().Clear();

  Log(static_cast<LogLevel>(99), __FILE__, __LINE__, "unknown-level-message");

  const std::vector<LogLine> lines = SnapshotLogLines();
  REQUIRE(lines.size() == 1);
  REQUIRE(lines[0].message == "unknown-level-message");
}

TEST_CASE("Logger unset env falls back to info", "[logger][env][unset]") {
  const ScopedEnvVar unsetLevel("MONOLITH_LOG_LEVEL", nullptr);
  LogBuffer::Instance().Clear();

  LOG_DEBUG("debug is filtered when env is absent");
  LOG_INFO("info is kept when env is absent");

  const std::vector<LogLine> lines = SnapshotLogLines();
  REQUIRE(lines.size() == 1);
  REQUIRE(lines[0].level == LogLevel::Info);
}
