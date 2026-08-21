#pragma once

/**
 * @file OpenTelemetrySink.h
 * @brief Optional Horo-owned OTLP export sink with no OpenTelemetry types in its API.
 */

#include "Horo/Foundation/Telemetry/Telemetry.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Telemetry {
    /** @brief OTLP signal endpoint selected by the sink. */
    enum class OtlpSignal : std::uint8_t {
        Logs,
        Metrics,
        Traces
    };

    /** @brief One validated HTTP header owned by the default OTLP transport. */
    struct OtlpHttpHeader {
        std::string name;
        std::string value;
    };

    /** @brief Narrow transport seam used by the optional exporter and deterministic tests. */
    class IOtlpTransport {
    public:
        virtual ~IOtlpTransport() = default;
        /**
         * @brief Posts one OTLP/HTTP JSON batch.
         * @param signal OTLP signal endpoint.
         * @param jsonPayload Complete OTLP JSON request body.
         * @param timeout Strict request deadline.
         * @return True only for a successful transport response.
         */
        [[nodiscard]] virtual bool Post(OtlpSignal signal, std::string_view jsonPayload, std::chrono::milliseconds timeout) = 0;
    };

    /** @brief Bounded optional OTLP exporter configuration. */
    struct OpenTelemetryConfiguration {
        std::string endpoint{"https://127.0.0.1:4318"};
        std::string serviceName{"horo-engine"};
        std::vector<OtlpHttpHeader> headers;
        std::vector<std::string> redactedAttributeKeyFragments{"authorization", "cookie", "password", "secret", "token"};
        std::size_t maxBatchRecords{128};
        std::size_t maxBufferedRecords{1024};
        std::size_t maxPayloadBytes{1024U * 1024U};
        std::uint32_t maxAttempts{2};
        std::chrono::milliseconds requestTimeout{std::chrono::seconds{2}};
        std::chrono::milliseconds retryDelay{std::chrono::milliseconds{25}};
        bool exportApproved{false};
        bool allowInsecureLocalhost{false};
    };

    /** @brief Monotonic optional exporter delivery and overflow counters. */
    struct OpenTelemetryStatistics {
        std::uint64_t acceptedRecords{};
        std::uint64_t exportedRecords{};
        std::uint64_t droppedRecords{};
        std::uint64_t failedBatches{};
        std::uint64_t retryAttempts{};
        std::uint64_t redactedAttributes{};
    };

    /** @brief Failure raised when an approved OTLP batch cannot be exported. */
    class OpenTelemetryExportError final : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    /** @brief Consumer-side Horo record to OTLP/HTTP JSON adapter. */
    class OpenTelemetrySink final : public ISink {
    public:
        /** @brief Factory-only construction token used to support allocation through std::make_shared. */
        class ConstructionKey {
        public:
            ConstructionKey(const ConstructionKey &) = default;

        private:
            ConstructionKey() = default;
            friend class OpenTelemetrySink;
        };

        /**
         * @brief Creates an approved exporter with the default bounded HTTP transport.
         * @param configuration Endpoint, privacy, batching, retry, and approval policy.
         * @return Sink, or null when configuration validation or allocation fails.
         */
        [[nodiscard]] static std::shared_ptr<OpenTelemetrySink> Create(const OpenTelemetryConfiguration &configuration) noexcept;
        /**
         * @brief Creates an approved exporter using an injected consumer-thread transport.
         * @param configuration Endpoint, privacy, batching, retry, and approval policy.
         * @param transport Transport invoked only from the observability consumer thread.
         * @return Sink, or null when configuration validation or allocation fails.
         */
        [[nodiscard]] static std::shared_ptr<OpenTelemetrySink> Create(const OpenTelemetryConfiguration &configuration,
                                                                       std::shared_ptr<IOtlpTransport> transport) noexcept;

        /** @brief Releases the exporter after all dispatcher use has stopped. */
        ~OpenTelemetrySink() override;
        OpenTelemetrySink(const OpenTelemetrySink &) = delete;
        OpenTelemetrySink &operator=(const OpenTelemetrySink &) = delete;
        /**
         * @brief Buffers one record and exports a batch when its configured limit is reached.
         * @param record Record
         * to buffer.
         * @param descriptor Metric descriptor, or null for non-metric records.
         * @throws
         * OpenTelemetryExportError when a full batch cannot be exported.
         */
        void Export(const Record &record, const InstrumentDescriptor *descriptor) override;
        /**
         * @brief Exports all currently buffered records.
         * @throws OpenTelemetryExportError when the buffered batch
         * cannot be exported.
         */
        void Flush() override;
        /**
         * @brief Returns bounded exporter health counters.
         * @return Monotonic delivery, retry, drop, failure, and redaction counters.
         */
        [[nodiscard]] OpenTelemetryStatistics Statistics() const noexcept;

        /**
         * @brief Constructs a sink through its factory-only construction token.
         * @param key Factory-owned
         * construction authority.
         * @param configuration Validated exporter configuration.
         * @param transport
         * Consumer-thread transport owned by the sink.
         */
        OpenTelemetrySink(ConstructionKey key, OpenTelemetryConfiguration configuration, std::shared_ptr<IOtlpTransport> transport);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}  // namespace Horo::Telemetry
