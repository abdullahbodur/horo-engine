#include "Horo/Foundation/Diagnostics/OperationHistory.h"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace Horo::Diagnostics {
    namespace {
        using Json = nlohmann::json;

        [[nodiscard]] const char *ToString(const Telemetry::SpanStatus status) noexcept {
            using enum Telemetry::SpanStatus;
            switch (status) {
                case Succeeded:
                    return "succeeded";
                case Failed:
                    return "failed";
                case Cancelled:
                    return "cancelled";
                case TimedOut:
                    return "timed_out";
                case Unset:
                    return "unset";
            }
            return "unset";
        }

        [[nodiscard]] std::optional<Telemetry::SpanStatus> ParseStatus(const std::string_view value) noexcept {
            using enum Telemetry::SpanStatus;
            if (value == "succeeded")
                return Succeeded;
            if (value == "failed")
                return Failed;
            if (value == "cancelled")
                return Cancelled;
            if (value == "timed_out")
                return TimedOut;
            return std::nullopt;
        }

        [[nodiscard]] bool IsSafeConfiguration(const OperationHistoryConfiguration &configuration) noexcept {
            if (configuration.directory.empty() || !configuration.directory.is_absolute() || configuration.baseName.empty() ||
                configuration.baseName.size() > 64 || configuration.baseName.find_first_of("/\\.:") != std::string::npos)
                return false;
            return std::ranges::none_of(configuration.directory, [](const std::filesystem::path &part) {
                return part == "..";
            });
        }

        [[nodiscard]] Json FieldValueToJson(const Telemetry::FieldValue &value) {
            return std::visit([](const auto &typed) -> Json {
                return typed;
            }, value);
        }

        [[nodiscard]] std::optional<Telemetry::FieldValue> JsonToFieldValue(const Json &value) {
            if (value.is_boolean())
                return value.get<bool>();
            if (value.is_number_unsigned())
                return value.get<std::uint64_t>();
            if (value.is_number_integer())
                return value.get<std::int64_t>();
            if (value.is_number_float())
                return value.get<double>();
            if (value.is_string())
                return value.get<std::string>();
            return std::nullopt;
        }

        [[nodiscard]] Json Serialize(const OperationHistoryRecord &record) {
            Json output{{"schemaVersion", 1},
                        {"operationId", record.operationId},
                        {"parentOperationId", record.parentOperationId},
                        {"status", ToString(record.status)},
                        {"timestampUtcMs", record.timestampUtcMilliseconds},
                        {"durationNs", record.durationNanoseconds},
                        {"subsystem", record.subsystem},
                        {"name", record.name}};
            Json fields = Json::object();
            for (const auto &field : record.fields)
                fields[field.key] = FieldValueToJson(field.value);
            if (!fields.empty())
                output["fields"] = std::move(fields);
            Json context = Json::object();
            for (const auto &[key, value] : record.context.Fields())
                context[key] = value;
            if (!context.empty())
                output["context"] = std::move(context);
            return output;
        }

        [[nodiscard]] std::optional<OperationHistoryRecord> Parse(const std::string &line) {
            try {
                const Json input = Json::parse(line);
                if (input.value("schemaVersion", 0) != 1 || !input.contains("operationId") || !input.contains("status") ||
                    !input.contains("timestampUtcMs") || !input.contains("durationNs") || !input.contains("subsystem") ||
                    !input.contains("name"))
                    return std::nullopt;
                const auto status = ParseStatus(input.at("status").get<std::string>());
                if (!status)
                    return std::nullopt;
                OperationHistoryRecord record{.operationId = input.at("operationId").get<Telemetry::OperationId>(),
                                              .parentOperationId = input.value("parentOperationId", Telemetry::OperationId{}),
                                              .status = *status,
                                              .timestampUtcMilliseconds = input.at("timestampUtcMs").get<std::int64_t>(),
                                              .durationNanoseconds = input.at("durationNs").get<std::int64_t>(),
                                              .subsystem = input.at("subsystem").get<std::string>(),
                                              .name = input.at("name").get<std::string>()};
                if (const auto fields = input.find("fields"); fields != input.end() && fields->is_object()) {
                    for (const auto &[key, value] : fields->items()) {
                        if (auto typed = JsonToFieldValue(value); typed)
                            record.fields.push_back({.key = key, .value = std::move(*typed)});
                    }
                }
                std::vector<Log::MdcField> context;
                if (const auto values = input.find("context"); values != input.end() && values->is_object()) {
                    for (const auto &[key, value] : values->items())
                        if (value.is_string())
                            context.emplace_back(key, value.get<std::string>());
                }
                record.context = Log::LogContextSnapshot{std::move(context)};
                return record;
            } catch (const Json::exception &) {
                return std::nullopt;
            }
        }

        bool RepairPartialTrailingRecord(const std::filesystem::path &path) {
            std::error_code error;
            const std::uintmax_t size = std::filesystem::file_size(path, error);
            if (error || size == 0)
                return !error || !std::filesystem::exists(path);
            std::ifstream input(path, std::ios::binary);
            input.seekg(static_cast<std::streamoff>(size - 1U));
            char character{};
            input.get(character);
            if (character == '\n')
                return true;
            std::uintmax_t offset = size - 1U;
            while (offset > 0) {
                --offset;
                input.seekg(static_cast<std::streamoff>(offset));
                input.get(character);
                if (character == '\n') {
                    std::filesystem::resize_file(path, offset + 1U, error);
                    return !error;
                }
            }
            std::filesystem::resize_file(path, 0, error);
            return !error;
        }
    }  // namespace

    namespace {
        class OperationHistoryError : public std::runtime_error {
        public:
            using std::runtime_error::runtime_error;
        };
    }  // namespace

    struct OperationHistorySink::Impl {
        explicit Impl(OperationHistoryConfiguration value)
            : configuration(std::move(value)), path(configuration.directory / (configuration.baseName + ".jsonl")) {}

        [[nodiscard]] std::filesystem::path RolledPath(const std::size_t index) const {
            return std::filesystem::path{std::format("{}.{}", path.string(), index)};
        }

        void RecoverFile(const std::filesystem::path &candidate) {
            std::ifstream input(candidate);
            std::string line;
            while (std::getline(input, line))
                if (auto record = Parse(line); record)
                    AppendRecovered(std::move(*record));
        }

        void Recover() {
            for (std::size_t index = configuration.maxRolledFiles; index > 0; --index)
                RecoverFile(RolledPath(index));
            RecoverFile(path);
        }

        void AppendRecovered(OperationHistoryRecord record) {
            records.push_back(std::move(record));
            while (records.size() > configuration.maxRecoveredRecords)
                records.pop_front();
        }

        void ReopenForAppend() {
            file = std::fopen(path.string().c_str(), "ab");  // NOSONAR: path derives from a canonical directory and validated base name.
            std::error_code error;
            currentBytes = std::filesystem::file_size(path, error);
            if (error)
                currentBytes = 0;
        }

        void Roll() {
            if (file != nullptr) {
                std::fflush(file);
                std::fclose(file);
                file = nullptr;
            }
            std::error_code error;
            if (configuration.maxRolledFiles == 0) {
                std::filesystem::remove(path, error);
            } else {
                std::filesystem::remove(RolledPath(configuration.maxRolledFiles), error);
                for (std::size_t index = configuration.maxRolledFiles; index > 1; --index) {
                    if (error)
                        break;
                    const auto source = RolledPath(index - 1U);
                    if (std::filesystem::exists(source, error))
                        std::filesystem::rename(source, RolledPath(index), error);
                    else
                        error.clear();
                }
                if (!error && std::filesystem::exists(path, error))
                    std::filesystem::rename(path, RolledPath(1), error);
            }
            if (error) {
                ReopenForAppend();
                throw OperationHistoryError{"failed to roll operation history"};
            }
            file = std::fopen(path.string().c_str(), "wb");
            if (file == nullptr)
                throw OperationHistoryError{"failed to create operation history segment"};
            currentBytes = 0;
        }

        OperationHistoryConfiguration configuration;
        std::filesystem::path path;
        std::FILE *file{};
        std::uintmax_t currentBytes{};
        std::deque<OperationHistoryRecord> records;

        [[nodiscard]] std::mutex &Mutex() noexcept {
            return mutex_;
        }

        /// Guards all mutable fields accessed from Export and Snapshot.
        std::mutex mutex_;
    };

    /** @copydoc OperationHistorySink::Create */
    std::shared_ptr<OperationHistorySink> OperationHistorySink::Create(const OperationHistoryConfiguration &configuration) noexcept {
        if (!IsSafeConfiguration(configuration) || configuration.maxFileBytes == 0 || configuration.maxRecoveredRecords == 0)
            return nullptr;
        try {
            std::error_code error;
            std::filesystem::create_directories(configuration.directory, error);  // NOSONAR
            if (error)
                return nullptr;
            OperationHistoryConfiguration resolved = configuration;
            resolved.directory = std::filesystem::weakly_canonical(configuration.directory, error);
            if (error || !IsSafeConfiguration(resolved))
                return nullptr;
            auto impl = std::make_unique<Impl>(std::move(resolved));
            if (!RepairPartialTrailingRecord(impl->path))
                return nullptr;
            impl->Recover();
            impl->ReopenForAppend();
            if (impl->file == nullptr)
                return nullptr;
            return std::shared_ptr<OperationHistorySink>{new OperationHistorySink{std::move(impl)}};  // NOSONAR(cpp:S5950)
        } catch (...) {
            return nullptr;
        }
    }

    OperationHistorySink::OperationHistorySink(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

    OperationHistorySink::~OperationHistorySink() {
        if (impl_->file != nullptr)
            std::fclose(impl_->file);
    }

    /** @copydoc OperationHistorySink::Export */
    void OperationHistorySink::Export(const Telemetry::Record &record, const Telemetry::InstrumentDescriptor *) {
        const auto *span = std::get_if<Telemetry::SpanRecord>(&record.payload);
        if (span == nullptr || span->status == Telemetry::SpanStatus::Unset)
            return;
        OperationHistoryRecord history{.operationId = span->operationId,
                                       .parentOperationId = span->parentOperationId,
                                       .status = span->status,
                                       .timestampUtcMilliseconds =
                                           std::chrono::duration_cast<std::chrono::milliseconds>(record.timestampUtc.time_since_epoch())
                                               .count(),
                                       .durationNanoseconds = span->duration.count(),
                                       .subsystem = record.subsystem,
                                       .name = span->name,
                                       .fields = span->fields,
                                       .context = record.context};
        std::string line = Serialize(history).dump();
        line += '\n';
        if (line.size() > impl_->configuration.maxFileBytes)
            throw OperationHistoryError{"operation history record exceeds file limit"};

        std::lock_guard lock(impl_->Mutex());
        if (impl_->currentBytes != 0 && impl_->currentBytes + line.size() > impl_->configuration.maxFileBytes)
            impl_->Roll();
        const std::size_t written = std::fwrite(line.data(), 1, line.size(), impl_->file);
        if (written != line.size() || std::fflush(impl_->file) != 0)
            throw OperationHistoryError{"failed to persist operation history"};
        impl_->currentBytes += written;
        impl_->AppendRecovered(std::move(history));
    }

    /** @copydoc OperationHistorySink::Flush */
    void OperationHistorySink::Flush() {
        std::lock_guard lock(impl_->Mutex());
        if (impl_->file != nullptr && std::fflush(impl_->file) != 0)
            throw OperationHistoryError{"failed to flush operation history"};
    }

    /** @copydoc OperationHistorySink::Snapshot */
    std::vector<OperationHistoryRecord> OperationHistorySink::Snapshot() const {
        std::lock_guard lock(impl_->Mutex());
        return {impl_->records.begin(), impl_->records.end()};
    }
}  // namespace Horo::Diagnostics
