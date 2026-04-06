#pragma once

#include "core/Logger.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace Monolith {

struct LogLine {
  std::chrono::system_clock::time_point time{};
  LogLevel level = LogLevel::Info;
  std::string file;
  int line = 0;
  std::string message;
};

// Thread-safe ring buffer for in-editor Console. Also tracks per-level counts (all pushes).
class LogBuffer {
 public:
  static LogBuffer& Instance();

  void Push(LogLevel level, const char* file, int line, const std::string& message);
  void Clear();

  void CopyLinesTo(std::vector<LogLine>* out) const;

  int CountInfo() const;
  int CountWarn() const;
  int CountError() const;
  void GetCounts(int* outInfo, int* outWarn, int* outErr) const;

  void SetMaxLines(size_t n) { m_maxLines = n; }
  size_t MaxLines() const { return m_maxLines; }

  // Increments on Push/Clear — UI can skip CopyLinesTo when unchanged.
  uint64_t Revision() const;

 private:
  LogBuffer() = default;

  mutable std::mutex m_mutex;
  std::deque<LogLine> m_lines;
  size_t m_maxLines = 1000;
  int m_countInfo = 0;
  int m_countWarn = 0;
  int m_countError = 0;
  uint64_t m_revision = 0;
};

}  // namespace Monolith
