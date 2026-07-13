/**
 * @file LogContext.cpp
 * @brief Thread-local MDC stack implementation.
 */

#include "Horo/Foundation/Logging/LogContext.h"

#include <algorithm>
#include <cassert>
#include <vector>

namespace Horo::Log
{
    namespace
    {
        /**
 * @brief Per-thread MDC frame stack.
 *
 * Each `LogContext` owns one frame (a `std::vector<MdcField>`).
 * Frames are stored by index rather than pointer so that the stack
 * can grow without invalidating existing indices.
 */
        struct MdcStack
        {
            std::vector<std::vector<MdcField>> frames;
        };

        MdcStack& MdcState()
        {
            thread_local MdcStack state;
            return state;
        }
    } // namespace

    std::size_t LogContext::PushFrame(std::vector<MdcField> fields)
    {
        auto& state = MdcState();
        const std::size_t index = state.frames.size();
        state.frames.push_back(std::move(fields));
        return index;
    }

    LogContext::~LogContext()
    {
        auto& frames = MdcState().frames;

        // Already removed (e.g. this object was moved-from) — nothing to do.
        if (m_frameIndex >= frames.size())
            return;

        // `LogContext` is only correct under strict LIFO (RAII) destruction: the
        // frame at m_frameIndex must be the topmost one. If some outer context
        // is destroyed before an inner one, erasing here would shift every
        // subsequent frame's real position without updating the still-live
        // LogContext objects that reference them — a silent, hard-to-diagnose
        // corruption of the MDC stack. Assert loudly in debug builds instead of
        // eating the bug quietly; in release builds we still fall back to the
        // erase-by-index behaviour as a best-effort guard rather than crashing.
        assert(m_frameIndex == frames.size() - 1 &&
            "LogContext destroyed out of LIFO order — MDC frame indices for "
            "other still-live LogContext objects are now stale");

        frames.erase(frames.begin() + static_cast<std::ptrdiff_t>(m_frameIndex));
    }

    std::vector<MdcField> GetMdcFields()
    {
        const auto& frames = MdcState().frames;
        if (frames.empty())
            return {};

        // Merge frames outermost-first; innermost value wins on key collision.
        std::vector<MdcField> merged;
        merged.reserve(8); // typical small field count
        for (const auto& frame : frames)
        {
            for (const auto& field : frame)
            {
                const auto it = std::find_if(merged.begin(), merged.end(),
                                             [&](const MdcField& f) { return f.first == field.first; });
                if (it == merged.end())
                    merged.push_back(field);
                else
                    it->second = field.second; // inner overrides outer
            }
        }
        return merged;
    }

    void LogContext::ClearAll()
    {
        MdcState().frames.clear();
    }
} // namespace Horo::Log