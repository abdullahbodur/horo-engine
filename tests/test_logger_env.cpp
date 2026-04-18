#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "core/LogBuffer.h"
#include "core/Logger.h"

using namespace Monolith;

namespace {

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
