#include "core/LogBuffer.h"

namespace Horo {
    LogBuffer &LogBuffer::Instance() {
        static LogBuffer s;
        return s;
    }

    void LogBuffer::Push(LogLevel level, const char *file, int line,
                         std::string_view message) {
        std::scoped_lock lock(m_mutex);

        using enum LogLevel;
        switch (level) {
            case Info:
                ++m_countInfo;
                break;
            case Warn:
                ++m_countWarn;
                break;
            case Error:
                ++m_countError;
                break;
            case Debug:
                break;
        }

        LogLine entry;
        entry.time = std::chrono::system_clock::now();
        entry.level = level;
        entry.file = file ? file : "";
        entry.line = line;
        entry.message = message;

        m_lines.push_back(std::move(entry));
        while (m_lines.size() > m_maxLines)
            m_lines.pop_front();
        ++m_revision;
    }

    void LogBuffer::Clear() {
        std::scoped_lock lock(m_mutex);
        m_lines.clear();
        m_countInfo = m_countWarn = m_countError = 0;
        ++m_revision;
    }

    void LogBuffer::CopyLinesTo(std::vector<LogLine> *out) const {
        if (!out)
            return;
        std::scoped_lock lock(m_mutex);
        out->assign(m_lines.begin(), m_lines.end());
    }

    int LogBuffer::CountInfo() const {
        std::scoped_lock lock(m_mutex);
        return m_countInfo;
    }

    int LogBuffer::CountWarn() const {
        std::scoped_lock lock(m_mutex);
        return m_countWarn;
    }

    int LogBuffer::CountError() const {
        std::scoped_lock lock(m_mutex);
        return m_countError;
    }

    void LogBuffer::GetCounts(int *outInfo, int *outWarn, int *outErr) const {
        std::scoped_lock lock(m_mutex);
        if (outInfo)
            *outInfo = m_countInfo;
        if (outWarn)
            *outWarn = m_countWarn;
        if (outErr)
            *outErr = m_countError;
    }

    uint64_t LogBuffer::Revision() const {
        std::scoped_lock lock(m_mutex);
        return m_revision;
    }
} // namespace Horo
