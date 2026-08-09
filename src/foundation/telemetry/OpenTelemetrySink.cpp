#include "Horo/Foundation/Telemetry/OpenTelemetrySink.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <curl/curl.h>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace Horo::Telemetry {
    namespace {
        using Json = nlohmann::json;

        class CurlOtlpTransport final : public IOtlpTransport {
        public:
            explicit CurlOtlpTransport(std::string endpoint, std::vector<OtlpHttpHeader> headers)
                : endpoint_(std::move(endpoint)), headers_(std::move(headers)) {
                while (!endpoint_.empty() && endpoint_.back() == '/')
                    endpoint_.pop_back();
            }

            bool Post(const OtlpSignal signal, const std::string_view jsonPayload, const std::chrono::milliseconds timeout) override {
                static std::once_flag curlInitialization;
                static bool curlReady{};
                std::call_once(curlInitialization, [] {
                    curlReady = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
                });
                if (!curlReady)
                    return false;
                CURL *curl = curl_easy_init();
                if (curl == nullptr)
                    return false;
                const char *suffix = signal == OtlpSignal::Logs ? "/v1/logs" : signal == OtlpSignal::Metrics ? "/v1/metrics" : "/v1/traces";
                const std::string url = endpoint_ + suffix;
                curl_slist *headers = curl_slist_append(nullptr, "Content-Type: application/json");
                if (headers == nullptr) {
                    curl_easy_cleanup(curl);
                    return false;
                }
                for (const OtlpHttpHeader &header : headers_) {
                    const std::string line = header.name + ": " + header.value;
                    curl_slist *appended = curl_slist_append(headers, line.c_str());
                    if (appended == nullptr) {
                        curl_slist_free_all(headers);
                        curl_easy_cleanup(curl);
                        return false;
                    }
                    headers = appended;
                }
                curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
                curl_easy_setopt(curl, CURLOPT_POST, 1L);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonPayload.data());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(jsonPayload.size()));
                curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout.count()));
                curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
                const CURLcode result = curl_easy_perform(curl);
                long responseCode{};
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
                curl_slist_free_all(headers);
                curl_easy_cleanup(curl);
                return result == CURLE_OK && responseCode >= 200 && responseCode < 300;
            }

        private:
            std::string endpoint_;
            std::vector<OtlpHttpHeader> headers_;
        };

        [[nodiscard]] bool HasForbiddenHeaderCharacter(const std::string_view value) noexcept {
            return value.find_first_of("\r\n") != std::string_view::npos;
        }

        [[nodiscard]] bool MatchesAuthority(const std::string_view endpoint, const std::string_view prefix) noexcept {
            if (!endpoint.starts_with(prefix))
                return false;
            if (endpoint.size() == prefix.size())
                return true;
            const char delimiter = endpoint[prefix.size()];
            return delimiter == ':' || delimiter == '/';
        }

        [[nodiscard]] bool IsAllowedEndpoint(const OpenTelemetryConfiguration &configuration) noexcept {
            if (configuration.endpoint.starts_with("https://"))
                return configuration.endpoint.size() > std::string_view{"https://"}.size();
            if (!configuration.allowInsecureLocalhost)
                return false;
            return MatchesAuthority(configuration.endpoint, "http://127.0.0.1") ||
                   MatchesAuthority(configuration.endpoint, "http://localhost") || MatchesAuthority(configuration.endpoint, "http://[::1]");
        }

        [[nodiscard]] bool IsValidConfiguration(const OpenTelemetryConfiguration &configuration) noexcept {
            constexpr std::size_t kMaximumBatchRecords = 4096;
            constexpr std::size_t kMaximumBufferedRecords = 65'536;
            constexpr std::size_t kMaximumPayloadBytes = 16U * 1024U * 1024U;
            constexpr std::size_t kMaximumHeaders = 32;
            constexpr std::size_t kMaximumHeaderBytes = 8U * 1024U;
            constexpr std::size_t kMaximumRedactionFragments = 32;
            if (!configuration.exportApproved || configuration.serviceName.empty() || !IsAllowedEndpoint(configuration) ||
                configuration.endpoint.size() > 2048 || configuration.serviceName.size() > 256 || configuration.maxBatchRecords == 0 ||
                configuration.maxBatchRecords > kMaximumBatchRecords || configuration.maxBufferedRecords < configuration.maxBatchRecords ||
                configuration.maxBufferedRecords > kMaximumBufferedRecords || configuration.maxPayloadBytes == 0 ||
                configuration.maxPayloadBytes > kMaximumPayloadBytes || configuration.maxAttempts == 0 || configuration.maxAttempts > 8 ||
                configuration.requestTimeout <= std::chrono::milliseconds::zero() ||
                configuration.requestTimeout > std::chrono::seconds{30} || configuration.retryDelay < std::chrono::milliseconds::zero() ||
                configuration.retryDelay > std::chrono::seconds{1} || configuration.headers.size() > kMaximumHeaders ||
                configuration.redactedAttributeKeyFragments.size() > kMaximumRedactionFragments)
                return false;
            for (const OtlpHttpHeader &header : configuration.headers) {
                if (header.name.empty() || HasForbiddenHeaderCharacter(header.name) || HasForbiddenHeaderCharacter(header.value) ||
                    header.name.find(':') != std::string::npos || header.name.size() + header.value.size() > kMaximumHeaderBytes)
                    return false;
            }
            for (const std::string &fragment : configuration.redactedAttributeKeyFragments) {
                if (fragment.empty() || fragment.size() > 128)
                    return false;
            }
            return true;
        }

        template <typename Duration>
        [[nodiscard]] std::string TimeUnixNanos(const std::chrono::time_point<std::chrono::system_clock, Duration> timestamp) {
            return std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(timestamp.time_since_epoch()).count());
        }

        [[nodiscard]] std::string HexId(const std::uint64_t value, const bool trace) {
            std::array<char, 33> text{};
            if (trace)
                std::snprintf(text.data(), text.size(), "%016llx%016llx", static_cast<unsigned long long>(value),
                              static_cast<unsigned long long>(value ^ 0x9e3779b97f4a7c15ULL));
            else
                std::snprintf(text.data(), text.size(), "%016llx", static_cast<unsigned long long>(value));
            return text.data();
        }

        [[nodiscard]] std::uint64_t HashId(const std::string_view value) noexcept {
            std::uint64_t result = 1469598103934665603ULL;
            for (const unsigned char character : value) {
                result ^= character;
                result *= 1099511628211ULL;
            }
            return result == 0 ? 1 : result;
        }

        [[nodiscard]] std::optional<std::string_view> ContextValue(const Record &record, const std::string_view key) noexcept {
            for (const auto &[candidate, value] : record.context.Fields()) {
                if (candidate == key)
                    return value;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::string TraceId(const Record &record, const std::uint64_t fallback) {
            const auto correlation = ContextValue(record, "correlation.id");
            return HexId(correlation ? HashId(*correlation) : fallback, true);
        }

        [[nodiscard]] Json OtlpValue(const FieldValue &value) {
            return std::visit([](const auto &typed) -> Json {
                using Value = std::decay_t<decltype(typed)>;
                if constexpr (std::is_same_v<Value, bool>)
                    return {{"boolValue", typed}};
                else if constexpr (std::is_same_v<Value, std::string>)
                    return {{"stringValue", typed}};
                else if constexpr (std::is_floating_point_v<Value>)
                    return {{"doubleValue", typed}};
                else
                    return {{"intValue", std::to_string(typed)}};
            }, value);
        }

        void AppendAttribute(Json &attributes, const std::string_view key, Json value) {
            attributes.push_back({{"key", key}, {"value", std::move(value)}});
        }

        [[nodiscard]] std::string Lowercase(std::string_view value) {
            std::string result{value};
            for (char &character : result) {
                if (character >= 'A' && character <= 'Z')
                    character = static_cast<char>(character - 'A' + 'a');
            }
            return result;
        }

        [[nodiscard]] bool IsSensitiveKey(const std::string_view key, const OpenTelemetryConfiguration &configuration) {
            const std::string normalized = Lowercase(key);
            for (const std::string &fragment : configuration.redactedAttributeKeyFragments) {
                if (normalized.find(Lowercase(fragment)) != std::string::npos)
                    return true;
            }
            return false;
        }

        void AppendConfiguredAttribute(Json &attributes, const std::string_view key, Json value,
                                       const OpenTelemetryConfiguration &configuration, std::atomic<std::uint64_t> &redactedAttributes) {
            if (IsSensitiveKey(key, configuration)) {
                value = {{"stringValue", "[REDACTED]"}};
                redactedAttributes.fetch_add(1, std::memory_order_relaxed);
            }
            AppendAttribute(attributes, key, std::move(value));
        }

        [[nodiscard]] Json CommonAttributes(const Record &record, const OpenTelemetryConfiguration &configuration,
                                            std::atomic<std::uint64_t> &redactedAttributes) {
            Json attributes = Json::array();
            AppendAttribute(attributes, "horo.subsystem", {{"stringValue", record.subsystem}});
            AppendAttribute(attributes, "thread.id", {{"stringValue", std::to_string(record.threadId)}});
            for (const auto &[key, value] : record.context.Fields())
                AppendConfiguredAttribute(attributes, key, {{"stringValue", value}}, configuration, redactedAttributes);
            return attributes;
        }

        void AppendFields(Json &attributes, const std::span<const Field> fields, const OpenTelemetryConfiguration &configuration,
                          std::atomic<std::uint64_t> &redactedAttributes) {
            for (const Field &field : fields)
                AppendConfiguredAttribute(attributes, field.key, OtlpValue(field.value), configuration, redactedAttributes);
        }

        [[nodiscard]] int SeverityNumber(const Log::Level level) noexcept {
            switch (level) {
                case Log::Level::Trace:
                    return 1;
                case Log::Level::Debug:
                    return 5;
                case Log::Level::Info:
                    return 9;
                case Log::Level::Warn:
                    return 13;
                case Log::Level::Error:
                    return 17;
                case Log::Level::Critical:
                    return 21;
                case Log::Level::Off:
                    return 0;
            }
            return 0;
        }

        [[nodiscard]] Json Resource(const std::string &serviceName) {
            return {{"attributes", Json::array({{{"key", "service.name"}, {"value", {{"stringValue", serviceName}}}}})}};
        }
    }  // namespace

    struct OpenTelemetrySink::Impl {
        struct Pending {
            Record record;
            std::optional<InstrumentDescriptor> descriptor;
        };

        Impl(OpenTelemetryConfiguration value, std::shared_ptr<IOtlpTransport> selectedTransport)
            : configuration(std::move(value)), transport(std::move(selectedTransport)) {
            pending.reserve(configuration.maxBufferedRecords);
        }

        [[nodiscard]] bool PostPayload(const OtlpSignal signal, const Json &payload, const std::uint64_t recordCount) {
            const std::string serialized = payload.dump();
            if (serialized.size() > configuration.maxPayloadBytes) {
                droppedRecords.fetch_add(recordCount, std::memory_order_relaxed);
                return false;
            }
            for (std::uint32_t attempt = 0; attempt < configuration.maxAttempts; ++attempt) {
                if (transport->Post(signal, serialized, configuration.requestTimeout)) {
                    exportedRecords.fetch_add(recordCount, std::memory_order_relaxed);
                    return true;
                }
                if (attempt + 1U < configuration.maxAttempts) {
                    retryAttempts.fetch_add(1, std::memory_order_relaxed);
                    if (configuration.retryDelay > std::chrono::milliseconds::zero())
                        std::this_thread::sleep_for(configuration.retryDelay);
                }
            }
            droppedRecords.fetch_add(recordCount, std::memory_order_relaxed);
            return false;
        }

        [[nodiscard]] bool ExportBatch() {
            if (pending.empty())
                return true;
            Json logs = Json::array();
            Json metrics = Json::array();
            Json spans = Json::array();
            std::size_t logCount{};
            std::size_t metricCount{};
            std::size_t spanCount{};
            std::size_t unmappedCount{};
            for (const Pending &item : pending) {
                Json attributes = CommonAttributes(item.record, configuration, redactedAttributes);
                if (const auto *log = std::get_if<LogRecord>(&item.record.payload)) {
                    AppendAttribute(attributes, "log.category", {{"stringValue", log->category}});
                    AppendFields(attributes, log->fields, configuration, redactedAttributes);
                    logs.push_back({{"timeUnixNano", TimeUnixNanos(item.record.timestampUtc)},
                                    {"severityNumber", SeverityNumber(log->severity)},
                                    {"severityText", Log::ToString(log->severity)},
                                    {"body", {{"stringValue", log->message}}},
                                    {"attributes", std::move(attributes)}});
                    ++logCount;
                } else if (const auto *event = std::get_if<DiagnosticEvent>(&item.record.payload)) {
                    AppendAttribute(attributes, "event.name", {{"stringValue", event->name}});
                    AppendFields(attributes, event->fields, configuration, redactedAttributes);
                    logs.push_back({{"timeUnixNano", TimeUnixNanos(item.record.timestampUtc)},
                                    {"severityNumber", SeverityNumber(event->severity)},
                                    {"severityText", Log::ToString(event->severity)},
                                    {"body", {{"stringValue", event->message}}},
                                    {"attributes", std::move(attributes)}});
                    ++logCount;
                } else if (const auto *metric = std::get_if<MetricRecord>(&item.record.payload); metric != nullptr && item.descriptor) {
                    for (std::size_t index = 0; index < metric->dimensionCount && index < item.descriptor->dimensions.size(); ++index) {
                        const std::uint16_t valueId = metric->dimensionValueIds[index];
                        if (valueId > 0 && valueId <= item.descriptor->dimensions[index].allowedValues.size())
                            AppendConfiguredAttribute(attributes, item.descriptor->dimensions[index].key,
                                                      {{"stringValue", item.descriptor->dimensions[index].allowedValues[valueId - 1U]}},
                                                      configuration, redactedAttributes);
                    }
                    Json point{{"timeUnixNano", TimeUnixNanos(item.record.timestampUtc)},
                               {"asDouble", metric->value},
                               {"attributes", std::move(attributes)}};
                    Json data;
                    const char *signalKey{};
                    if (metric->kind == InstrumentKind::Counter)
                        signalKey = "sum";
                    else if (metric->kind == InstrumentKind::Gauge)
                        signalKey = "gauge";
                    else {
                        signalKey = "histogram";
                        point.erase("asDouble");
                        point["count"] = "1";
                        point["sum"] = metric->value;
                    }
                    const auto existing = std::find_if(metrics.begin(), metrics.end(), [&](const Json &candidate) {
                        return candidate.value("name", "") == item.descriptor->name && candidate.contains(signalKey);
                    });
                    if (existing != metrics.end())
                        (*existing)[signalKey]["dataPoints"].push_back(std::move(point));
                    else {
                        if (metric->kind == InstrumentKind::Counter)
                            data = {
                                {signalKey,
                                 {{"aggregationTemporality", 1}, {"isMonotonic", true}, {"dataPoints", Json::array({std::move(point)})}}}};
                        else if (metric->kind == InstrumentKind::Gauge)
                            data = {{signalKey, {{"dataPoints", Json::array({std::move(point)})}}}};
                        else
                            data = {{signalKey, {{"aggregationTemporality", 1}, {"dataPoints", Json::array({std::move(point)})}}}};
                        data["name"] = item.descriptor->name;
                        data["unit"] = item.descriptor->unit;
                        metrics.push_back(std::move(data));
                    }
                    ++metricCount;
                } else if (const auto *span = std::get_if<SpanRecord>(&item.record.payload)) {
                    AppendFields(attributes, span->fields, configuration, redactedAttributes);
                    const auto started = item.record.timestampUtc - span->duration;
                    const int statusCode = span->status == SpanStatus::Succeeded ? 1 : span->status == SpanStatus::Unset ? 0 : 2;
                    spans.push_back({{"traceId", TraceId(item.record, span->operationId)},
                                     {"spanId", HexId(span->operationId, false)},
                                     {"parentSpanId", span->parentOperationId == 0 ? std::string{} : HexId(span->parentOperationId, false)},
                                     {"name", span->name},
                                     {"kind", 1},
                                     {"startTimeUnixNano", TimeUnixNanos(started)},
                                     {"endTimeUnixNano", TimeUnixNanos(item.record.timestampUtc)},
                                     {"attributes", std::move(attributes)},
                                     {"status", {{"code", statusCode}}}});
                    ++spanCount;
                } else
                    ++unmappedCount;
            }

            bool success = true;
            if (!logs.empty()) {
                const Json payload{
                    {"resourceLogs",
                     Json::array({{{"resource", Resource(configuration.serviceName)},
                                   {"scopeLogs", Json::array({{{"scope", {{"name", "horo"}}}, {"logRecords", std::move(logs)}}})}}})}};
                success = PostPayload(OtlpSignal::Logs, payload, logCount) && success;
            }
            if (!metrics.empty()) {
                const Json payload{
                    {"resourceMetrics",
                     Json::array({{{"resource", Resource(configuration.serviceName)},
                                   {"scopeMetrics", Json::array({{{"scope", {{"name", "horo"}}}, {"metrics", std::move(metrics)}}})}}})}};
                success = PostPayload(OtlpSignal::Metrics, payload, metricCount) && success;
            }
            if (!spans.empty()) {
                const Json payload{
                    {"resourceSpans",
                     Json::array({{{"resource", Resource(configuration.serviceName)},
                                   {"scopeSpans", Json::array({{{"scope", {{"name", "horo"}}}, {"spans", std::move(spans)}}})}}})}};
                success = PostPayload(OtlpSignal::Traces, payload, spanCount) && success;
            }
            if (unmappedCount != 0) {
                droppedRecords.fetch_add(unmappedCount, std::memory_order_relaxed);
                success = false;
            }
            if (!success)
                failedBatches.fetch_add(1, std::memory_order_relaxed);
            pending.clear();
            return success;
        }

        OpenTelemetryConfiguration configuration;
        std::shared_ptr<IOtlpTransport> transport;
        mutable std::mutex mutex;
        std::vector<Pending> pending;
        std::atomic<std::uint64_t> acceptedRecords{};
        std::atomic<std::uint64_t> exportedRecords{};
        std::atomic<std::uint64_t> droppedRecords{};
        std::atomic<std::uint64_t> failedBatches{};
        std::atomic<std::uint64_t> retryAttempts{};
        std::atomic<std::uint64_t> redactedAttributes{};
    };

    std::shared_ptr<OpenTelemetrySink> OpenTelemetrySink::Create(const OpenTelemetryConfiguration &configuration) noexcept {
        if (!IsValidConfiguration(configuration))
            return nullptr;
        try {
            return Create(configuration, std::make_shared<CurlOtlpTransport>(configuration.endpoint, configuration.headers));
        } catch (...) {
            return nullptr;
        }
    }

    std::shared_ptr<OpenTelemetrySink> OpenTelemetrySink::Create(const OpenTelemetryConfiguration &configuration,
                                                                 std::shared_ptr<IOtlpTransport> transport) noexcept {
        if (!IsValidConfiguration(configuration) || transport == nullptr)
            return nullptr;
        try {
            return std::shared_ptr<OpenTelemetrySink>{new OpenTelemetrySink{std::make_unique<Impl>(configuration, std::move(transport))}};
        } catch (...) {
            return nullptr;
        }
    }

    OpenTelemetrySink::OpenTelemetrySink(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

    OpenTelemetrySink::~OpenTelemetrySink() = default;

    void OpenTelemetrySink::Export(const Record &record, const InstrumentDescriptor *descriptor) {
        std::lock_guard lock(impl_->mutex);
        if (impl_->pending.size() >= impl_->configuration.maxBufferedRecords) {
            impl_->droppedRecords.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        impl_->pending.push_back(
            {.record = record, .descriptor = descriptor == nullptr ? std::optional<InstrumentDescriptor>{} : std::optional{*descriptor}});
        impl_->acceptedRecords.fetch_add(1, std::memory_order_relaxed);
        if (impl_->pending.size() >= impl_->configuration.maxBatchRecords && !impl_->ExportBatch())
            throw std::runtime_error{"OTLP batch export failed"};
    }

    void OpenTelemetrySink::Flush() {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->ExportBatch())
            throw std::runtime_error{"OTLP flush failed"};
    }

    OpenTelemetryStatistics OpenTelemetrySink::Statistics() const noexcept {
        return {.acceptedRecords = impl_->acceptedRecords.load(std::memory_order_relaxed),
                .exportedRecords = impl_->exportedRecords.load(std::memory_order_relaxed),
                .droppedRecords = impl_->droppedRecords.load(std::memory_order_relaxed),
                .failedBatches = impl_->failedBatches.load(std::memory_order_relaxed),
                .retryAttempts = impl_->retryAttempts.load(std::memory_order_relaxed),
                .redactedAttributes = impl_->redactedAttributes.load(std::memory_order_relaxed)};
    }
}  // namespace Horo::Telemetry
