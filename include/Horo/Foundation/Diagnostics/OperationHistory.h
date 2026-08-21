#pragma once

/**
 * @file OperationHistory.h
 * @brief Bounded restart-readable terminal operation history sink.
 */

#include "Horo/Foundation/Telemetry/Operation.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Horo::Diagnostics {
    /** @brief Persistent operation-history storage and recovery limits. */
    struct OperationHistoryConfiguration {
        std::filesystem::path directory;
        std::string baseName{"operations"};
        std::uintmax_t maxFileBytes{2U * 1024U * 1024U};
        std::size_t maxRolledFiles{3};
        std::size_t maxRecoveredRecords{1024};
    };

    /** @brief Restart-readable terminal operation summary. */
    struct OperationHistoryRecord {
        Telemetry::OperationId operationId{};
        Telemetry::OperationId parentOperationId{};
        Telemetry::SpanStatus status{Telemetry::SpanStatus::Unset};
        std::int64_t timestampUtcMilliseconds{};
        std::int64_t durationNanoseconds{};
        std::string subsystem;
        std::string name;
        std::vector<Telemetry::Field> fields;
        Log::LogContextSnapshot context;
    };

    /** @brief Consumer-thread sink that persists only terminal operation/span records. */
    class OperationHistorySink final : public Telemetry::ISink {
    public:
        /**
         * @brief Creates a sink and recovers valid retained records.
         * @param configuration Absolute storage path and bounded retention limits.
         * @return Sink, or null when storage cannot be safely established.
         */
        [[nodiscard]] static std::shared_ptr<OperationHistorySink> Create(const OperationHistoryConfiguration &configuration) noexcept;

        ~OperationHistorySink() override;
        OperationHistorySink(const OperationHistorySink &) = delete;
        OperationHistorySink &operator=(const OperationHistorySink &) = delete;

        /** @copydoc Telemetry::ISink::Export */
        void Export(const Telemetry::Record &record, const Telemetry::InstrumentDescriptor *descriptor) override;
        /** @copydoc Telemetry::ISink::Flush */
        void Flush() override;

        /**
         * @brief Returns a bounded immutable copy ordered oldest to newest.
         * @return Recovered and newly persisted terminal operation summaries.
         */
        [[nodiscard]] std::vector<OperationHistoryRecord> Snapshot() const;

    private:
        struct Impl;
        explicit OperationHistorySink(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> impl_;
    };
}  // namespace Horo::Diagnostics
