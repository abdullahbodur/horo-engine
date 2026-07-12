#include "Horo/Foundation/DiagnosticsEngine.h"
#include "Horo/Foundation/DataBus.h"

namespace Horo
{
    /** @copydoc DiagnosticsEngine::DiagnosticsEngine */
    DiagnosticsEngine::DiagnosticsEngine(EngineDataBus *dataBus) : m_dataBus(dataBus) {}

    /** @copydoc DiagnosticsEngine::Report(Diagnostic) */
    void DiagnosticsEngine::Report(Diagnostic diagnostic)
    {
        {
            std::lock_guard lock(m_mutex);
            if (m_diagnostics.size() >= kMaxHistory)
            {
                m_diagnostics.erase(m_diagnostics.begin());
            }
            m_diagnostics.push_back(diagnostic);
        }
        if (m_dataBus != nullptr)
        {
            m_dataBus->PublishAsync(DiagnosticPublishedEvent{.diagnostic = std::move(diagnostic)});
        }
    }

    /** @copydoc DiagnosticsEngine::Report(Error) */
    void DiagnosticsEngine::Report(Error error)
    {
        // First report any nested diagnostics
        for (const auto &diag : error.diagnostics)
        {
            Report(diag);
        }

        {
            std::lock_guard lock(m_mutex);
            if (m_errors.size() >= kMaxHistory)
            {
                m_errors.erase(m_errors.begin());
            }
            m_errors.push_back(error);
        }
        if (m_dataBus != nullptr)
        {
            m_dataBus->PublishAsync(ErrorPublishedEvent{.error = std::move(error)});
        }
    }

    /** @copydoc DiagnosticsEngine::RecentDiagnostics */
    std::vector<Diagnostic> DiagnosticsEngine::RecentDiagnostics() const
    {
        std::lock_guard lock(m_mutex);
        return m_diagnostics;
    }

    /** @copydoc DiagnosticsEngine::RecentErrors */
    std::vector<Error> DiagnosticsEngine::RecentErrors() const
    {
        std::lock_guard lock(m_mutex);
        return m_errors;
    }

    /** @copydoc DiagnosticsEngine::Clear */
    void DiagnosticsEngine::Clear()
    {
        std::lock_guard lock(m_mutex);
        m_diagnostics.clear();
        m_errors.clear();
    }
} // namespace Horo
