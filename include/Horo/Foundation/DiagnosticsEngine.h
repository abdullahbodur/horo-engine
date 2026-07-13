#pragma once

#include "Horo/Foundation/Diagnostics.h"
#include "Horo/Foundation/ErrorCode.h"

#include <mutex>
#include <vector>

namespace Horo
{
    class EngineDataBus;

    /** @file DiagnosticsEngine.h
     * @brief Central diagnostic and error hub with ring-buffer storage and immediate DataBus notification.
     */

    /** @brief Process-scoped diagnostic and error hub that stores bounded history and immediately notifies DataBus subscribers. */
    class DiagnosticsEngine
    {
    public:
        /**
         * @brief Constructs the engine with optional connection to the process DataBus.
         * @param dataBus Borrowed process DataBus used to publish real-time diagnostic and error events.
         */
        explicit DiagnosticsEngine(EngineDataBus *dataBus = nullptr);

        DiagnosticsEngine(const DiagnosticsEngine &) = delete;
        DiagnosticsEngine &operator=(const DiagnosticsEngine &) = delete;

        /**
         * @brief Records a diagnostic finding and asynchronously publishes DiagnosticPublishedEvent via DataBus.
         * @param diagnostic The non-fatal validation finding to store and broadcast.
         */
        void Report(Diagnostic diagnostic);

        /**
         * @brief Records an error finding, publishes ErrorPublishedEvent via DataBus, and reports nested diagnostics.
         * @param error The typed error to store and broadcast.
         */
        void Report(Error error);

        /**
         * @brief Retrieves recent diagnostics up to the history limit.
         * @return Vector of stored diagnostics from oldest to newest.
         */
        [[nodiscard]] std::vector<Diagnostic> RecentDiagnostics() const;

        /**
         * @brief Retrieves recent errors up to the history limit.
         * @return Vector of stored errors from oldest to newest.
         */
        [[nodiscard]] std::vector<Error> RecentErrors() const;

        /**
         * @brief Clears all stored diagnostics and errors.
         */
        void Clear();

    private:
        mutable std::mutex m_mutex;
        EngineDataBus *m_dataBus = nullptr;
        std::vector<Diagnostic> m_diagnostics;
        std::vector<Error> m_errors;
        static constexpr std::size_t kMaxHistory = 256;
    };
} // namespace Horo
