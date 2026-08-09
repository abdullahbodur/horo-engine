#include "Horo/Foundation/Telemetry/Telemetry.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace Horo::Telemetry {
    namespace {
        /** @brief Process-lifetime atomics intentionally retained for detached timeout recovery. */
        struct AtomicStatistics {
            std::atomic<std::uint64_t> acceptedRecords{};
            std::atomic<std::uint64_t> exportedRecords{};
            std::atomic<std::uint64_t> droppedRecords{};
            std::atomic<std::uint64_t> filteredRecords{};
            std::atomic<std::uint64_t> contentionDrops{};
            std::atomic<std::uint64_t> queueFullDrops{};
            std::atomic<std::uint64_t> shutdownDrops{};
            std::atomic<std::uint64_t> staleHandleDrops{};
            std::atomic<std::uint64_t> sinkFailures{};
            std::atomic<std::uint64_t> flushTimeouts{};
            std::atomic<std::uint64_t> shutdownTimeouts{};
            std::atomic<std::uint64_t> invalidInstrumentRegistrations{};
            std::atomic<std::uint64_t> rejectedMetricSeries{};
        };

        /** @brief Returns counters whose lifetime extends through process teardown. */
        AtomicStatistics &Health() noexcept {
            static auto *statistics = new AtomicStatistics{};
            return *statistics;
        }

        std::atomic<bool> g_enabled{};
        std::mutex g_lifecycleMutex;
        std::atomic<std::uint32_t> g_nextGeneration{1};

        struct InstrumentRegistration {
            std::uint32_t id{};
            std::uint8_t dimensionCount{};
        };

        struct RegisteredInstrument {
            InstrumentDescriptor descriptor;
            std::vector<std::array<std::uint16_t, MaximumMetricDimensions>> series;
        };

        [[nodiscard]] bool IsCanonicalMetricName(const std::string_view value) noexcept {
            if (value.empty() || value.front() == '.' || value.back() == '.')
                return false;
            bool previousDot = false;
            for (const unsigned char character : value) {
                const bool dot = character == '.';
                if (dot && previousDot)
                    return false;
                if (!dot && character != '_' && !std::islower(character) && !std::isdigit(character))
                    return false;
                previousDot = dot;
            }
            return true;
        }

        [[nodiscard]] bool IsSensitiveKey(std::string_view key) {
            std::string normalized{key};
            std::ranges::transform(normalized, normalized.begin(), [](const unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            static constexpr std::array<std::string_view, 7> fragments{"authorization", "cookie", "credential", "password",
                                                                       "private_key",   "secret", "token"};
            return std::ranges::any_of(fragments, [&normalized](const std::string_view fragment) {
                return normalized.find(fragment) != std::string::npos;
            });
        }

        void RedactFields(std::vector<Field> &fields) {
            std::erase_if(fields, [](const Field &field) {
                return field.privacy == FieldPrivacy::Forbidden;
            });
            for (Field &field : fields) {
                if (field.privacy == FieldPrivacy::SensitiveRedacted || IsSensitiveKey(field.key))
                    field.value = std::string{"[REDACTED]"};
            }
        }

        void RedactRecord(Record &record) {
            if (!record.context.Fields().empty()) {
                Log::LogContextSnapshot redacted;
                for (const auto &[key, value] : record.context.Fields())
                    redacted = redacted.With(key, IsSensitiveKey(key) ? "[REDACTED]" : value);
                record.context = std::move(redacted);
            }
            std::visit([](auto &payload) {
                using Payload = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<Payload, LogRecord> || std::is_same_v<Payload, SpanRecord> ||
                              std::is_same_v<Payload, DiagnosticEvent>)
                    RedactFields(payload.fields);
            }, record.payload);
        }

        [[nodiscard]] bool IsValidDescriptor(const InstrumentDescriptor &descriptor) noexcept {
            if (!IsCanonicalMetricName(descriptor.name) || descriptor.subsystem.empty() || descriptor.unit.empty() ||
                descriptor.maxSeries == 0 || descriptor.dimensions.size() > MaximumMetricDimensions)
                return false;
            for (std::size_t index = 0; index < descriptor.dimensions.size(); ++index) {
                const DimensionDescriptor &dimension = descriptor.dimensions[index];
                if (!IsCanonicalMetricName(dimension.key) || dimension.allowedValues.empty() || dimension.allowedValues.size() > UINT16_MAX)
                    return false;
                if (std::find_if(descriptor.dimensions.begin(), descriptor.dimensions.begin() + static_cast<std::ptrdiff_t>(index),
                                 [&dimension](const DimensionDescriptor &candidate) {
                    return candidate.key == dimension.key;
                }) != descriptor.dimensions.begin() + static_cast<std::ptrdiff_t>(index))
                    return false;
                for (std::size_t valueIndex = 0; valueIndex < dimension.allowedValues.size(); ++valueIndex) {
                    if (dimension.allowedValues[valueIndex].empty() ||
                        std::find(dimension.allowedValues.begin(),
                                  dimension.allowedValues.begin() + static_cast<std::ptrdiff_t>(valueIndex),
                                  dimension.allowedValues[valueIndex]) !=
                            dimension.allowedValues.begin() + static_cast<std::ptrdiff_t>(valueIndex))
                        return false;
                }
            }
            return true;
        }

        class TelemetryState final : public std::enable_shared_from_this<TelemetryState> {
        public:
            TelemetryState(Configuration configuration, std::vector<std::shared_ptr<ISink>> sinks, const std::uint32_t generation)
                : configuration_(configuration), queue_(configuration.queueCapacity), sinks_(std::move(sinks)), generation_(generation) {}

            ~TelemetryState() {
                if (!writer_.joinable())
                    return;
                stopping_.store(true, std::memory_order_release);
                ready_.notify_all();
                if (writer_.get_id() == std::this_thread::get_id())
                    writer_.detach();
                else
                    writer_.join();
            }

            void Start() {
                writer_ = std::thread([self = shared_from_this()] {
                    self->WriterLoop();
                });
            }

            [[nodiscard]] std::uint32_t Generation() const noexcept {
                return generation_;
            }

            [[nodiscard]] std::chrono::milliseconds ShutdownTimeout() const noexcept {
                return configuration_.shutdownTimeout;
            }

            [[nodiscard]] bool HasExited() const noexcept {
                return writerExited_.load(std::memory_order_acquire);
            }

            [[nodiscard]] std::optional<InstrumentRegistration> Register(InstrumentDescriptor descriptor) {
                std::lock_guard lock(descriptorMutex_);
                if (!IsValidDescriptor(descriptor) ||
                    std::ranges::any_of(descriptors_, [&descriptor](const RegisteredInstrument &registered) {
                    return registered.descriptor.name == descriptor.name;
                })) {
                    Health().invalidInstrumentRegistrations.fetch_add(1, std::memory_order_relaxed);
                    return std::nullopt;
                }
                RegisteredInstrument registered{.descriptor = std::move(descriptor)};
                if (registered.descriptor.dimensions.empty())
                    registered.series.push_back({});
                descriptors_.push_back(std::move(registered));
                return InstrumentRegistration{.id = static_cast<std::uint32_t>(descriptors_.size()),
                                              .dimensionCount =
                                                  static_cast<std::uint8_t>(descriptors_.back().descriptor.dimensions.size())};
            }

            [[nodiscard]] bool Bind(const std::uint32_t instrumentId, const InstrumentKind kind,
                                    const std::span<const DimensionValue> dimensions,
                                    std::array<std::uint16_t, MaximumMetricDimensions> &valueIds, std::uint8_t &dimensionCount) {
                std::lock_guard lock(descriptorMutex_);
                if (instrumentId == 0 || instrumentId > descriptors_.size())
                    return false;
                RegisteredInstrument &registered = descriptors_[instrumentId - 1U];
                const InstrumentDescriptor &descriptor = registered.descriptor;
                if (descriptor.kind != kind || dimensions.size() != descriptor.dimensions.size())
                    return false;

                valueIds.fill(0);
                for (std::size_t descriptorIndex = 0; descriptorIndex < descriptor.dimensions.size(); ++descriptorIndex) {
                    const DimensionDescriptor &expected = descriptor.dimensions[descriptorIndex];
                    const auto selected = std::ranges::find(dimensions, expected.key, &DimensionValue::key);
                    if (selected == dimensions.end())
                        return false;
                    if (std::ranges::count(dimensions, expected.key, &DimensionValue::key) != 1)
                        return false;
                    const auto allowed = std::ranges::find(expected.allowedValues, selected->value);
                    if (allowed == expected.allowedValues.end())
                        return false;
                    valueIds[descriptorIndex] = static_cast<std::uint16_t>(std::distance(expected.allowedValues.begin(), allowed) + 1);
                }

                dimensionCount = static_cast<std::uint8_t>(descriptor.dimensions.size());
                if (std::ranges::find(registered.series, valueIds) != registered.series.end())
                    return true;
                if (registered.series.size() >= descriptor.maxSeries)
                    return false;
                registered.series.push_back(valueIds);
                return true;
            }

            [[nodiscard]] bool AllowsSubsystem(const std::string_view subsystem) const noexcept {
                if (configuration_.subsystemPrefixes.empty())
                    return true;
                for (const std::string &prefix : configuration_.subsystemPrefixes) {
                    if (subsystem == prefix)
                        return true;
                    if (subsystem.size() > prefix.size() && subsystem.starts_with(prefix) && subsystem[prefix.size()] == '.')
                        return true;
                }
                return false;
            }

            [[nodiscard]] bool AllowsMetric(const InstrumentDescriptor &descriptor) const noexcept {
                return configuration_.metricCollectionLevel != MetricCollectionLevel::Off &&
                       configuration_.metricCollectionLevel >= descriptor.minimumCollectionLevel && AllowsSubsystem(descriptor.subsystem);
            }

            [[nodiscard]] bool AllowsEvent(const std::string_view subsystem, const Log::Level severity) const noexcept {
                return severity >= configuration_.minimumEventSeverity && severity < Log::Level::Off && AllowsSubsystem(subsystem);
            }

            [[nodiscard]] bool Allows(const Record &record) const noexcept {
                if (!AllowsSubsystem(record.subsystem))
                    return false;
                if (const auto *log = std::get_if<LogRecord>(&record.payload); log != nullptr)
                    return AllowsEvent(record.subsystem, log->severity);
                if (const auto *event = std::get_if<DiagnosticEvent>(&record.payload); event != nullptr)
                    return AllowsEvent(record.subsystem, event->severity);
                return true;
            }

            [[nodiscard]] bool TryPush(Record record) noexcept {
                std::unique_lock lock(queueMutex_, std::try_to_lock);
                if (!lock.owns_lock()) {
                    CountDrop(Health().contentionDrops);
                    return false;
                }
                if (stopping_.load(std::memory_order_acquire)) {
                    CountDrop(Health().shutdownDrops);
                    return false;
                }
                if (count_ == queue_.size()) {
                    Health().queueFullDrops.fetch_add(1, std::memory_order_relaxed);
                    Health().droppedRecords.fetch_add(1, std::memory_order_relaxed);
                    if (configuration_.overflowPolicy == OverflowPolicy::DropNewest)
                        return false;
                    queue_[head_] = Record{};
                    head_ = (head_ + 1U) % queue_.size();
                    --count_;
                }

                record.sequence = nextSequence_++;
                record.timestampUtc = std::chrono::system_clock::now();
                record.monotonicTime = std::chrono::steady_clock::now();
                record.threadId = static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
                queue_[tail_] = std::move(record);
                tail_ = (tail_ + 1U) % queue_.size();
                ++count_;
                lastAccepted_ = queue_[(tail_ + queue_.size() - 1U) % queue_.size()].sequence;
                Health().acceptedRecords.fetch_add(1, std::memory_order_relaxed);
                lock.unlock();
                ready_.notify_one();
                return true;
            }

            [[nodiscard]] bool Flush(const std::chrono::milliseconds timeout) {
                std::uint64_t watermark{};
                {
                    std::lock_guard lock(queueMutex_);
                    watermark = lastAccepted_;
                }
                ready_.notify_one();
                std::unique_lock lock(flushMutex_);
                const bool completed = flushed_.wait_for(lock, timeout, [this, watermark] {
                    return lastExported_.load(std::memory_order_acquire) >= watermark || writerExited_.load(std::memory_order_acquire);
                });
                if (!completed)
                    Health().flushTimeouts.fetch_add(1, std::memory_order_relaxed);
                return completed;
            }

            [[nodiscard]] bool Stop(const std::chrono::milliseconds timeout) {
                bool expected = false;
                if (!stopping_.compare_exchange_strong(expected, true))
                    return WaitAndJoin(timeout);
                {
                    // Synchronize the predicate transition with WriterLoop's
                    // condition-variable wait so the stop notification cannot
                    // be lost between its predicate check and sleep.
                    std::lock_guard lock(queueMutex_);
                }
                ready_.notify_all();
                return WaitAndJoin(timeout);
            }

        private:
            static void CountDrop(std::atomic<std::uint64_t> &reason) noexcept {
                reason.fetch_add(1, std::memory_order_relaxed);
                Health().droppedRecords.fetch_add(1, std::memory_order_relaxed);
            }

            [[nodiscard]] bool WaitAndJoin(const std::chrono::milliseconds timeout) {
                std::unique_lock lock(exitMutex_);
                const bool exited = exited_.wait_for(lock, timeout, [this] {
                    return writerExited_.load(std::memory_order_acquire);
                });
                lock.unlock();
                if (!exited) {
                    if (writer_.joinable())
                        writer_.detach();
                    return false;
                }
                if (writer_.joinable())
                    writer_.join();
                return flushSucceeded_.load(std::memory_order_acquire);
            }

            void WriterLoop() noexcept {
                while (true) {
                    std::optional<Record> record;
                    {
                        std::unique_lock lock(queueMutex_);
                        const bool ready = ready_.wait_for(lock, configuration_.sinkFlushInterval, [this] {
                            return count_ != 0 || stopping_.load(std::memory_order_acquire);
                        });
                        if (!ready) {
                            lock.unlock();
                            FlushSinks();
                            continue;
                        }
                        if (count_ == 0 && stopping_.load(std::memory_order_acquire))
                            break;
                        record.emplace(std::move(queue_[head_]));
                        queue_[head_] = Record{};
                        head_ = (head_ + 1U) % queue_.size();
                        --count_;
                    }
                    Dispatch(*record);
                }

                FlushSinks();
                {
                    std::lock_guard lock(exitMutex_);
                    writerExited_.store(true, std::memory_order_release);
                }
                flushed_.notify_all();
                exited_.notify_all();
            }

            void Dispatch(Record &record) noexcept {
                std::optional<InstrumentDescriptor> descriptor;
                if (const auto *metric = std::get_if<MetricRecord>(&record.payload); metric != nullptr) {
                    std::lock_guard lock(descriptorMutex_);
                    if (metric->instrumentId > 0 && metric->instrumentId <= descriptors_.size())
                        descriptor = descriptors_[metric->instrumentId - 1U].descriptor;
                }
                if (descriptor.has_value() && record.subsystem.empty())
                    record.subsystem = descriptor->subsystem;

                for (const auto &sink : sinks_) {
                    try {
                        sink->Export(record, descriptor ? &*descriptor : nullptr);
                    } catch (...) {
                        Health().sinkFailures.fetch_add(1, std::memory_order_relaxed);
                    }
                }

                Health().exportedRecords.fetch_add(1, std::memory_order_relaxed);
                {
                    std::lock_guard lock(flushMutex_);
                    lastExported_.store(record.sequence, std::memory_order_release);
                }
                flushed_.notify_all();
            }

            void FlushSinks() noexcept {
                for (const auto &sink : sinks_) {
                    try {
                        sink->Flush();
                    } catch (...) {
                        flushSucceeded_.store(false, std::memory_order_release);
                        Health().sinkFailures.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }

            Configuration configuration_;
            std::vector<Record> queue_;
            std::size_t head_{};
            std::size_t tail_{};
            std::size_t count_{};
            std::uint64_t nextSequence_{1};
            std::uint64_t lastAccepted_{};
            std::atomic<std::uint64_t> lastExported_{};
            std::mutex queueMutex_;
            std::condition_variable ready_;
            std::mutex flushMutex_;
            std::condition_variable flushed_;
            std::mutex descriptorMutex_;
            std::vector<RegisteredInstrument> descriptors_;
            std::vector<std::shared_ptr<ISink>> sinks_;
            const std::uint32_t generation_;
            std::atomic<bool> stopping_{};
            std::atomic<bool> writerExited_{};
            std::atomic<bool> flushSucceeded_{true};
            std::mutex exitMutex_;
            std::condition_variable exited_;
            std::thread writer_;
        };

        std::shared_ptr<TelemetryState> g_state;
    }  // namespace

    /** @copydoc Record::Kind */
    RecordKind Record::Kind() const noexcept {
        if (std::holds_alternative<LogRecord>(payload))
            return RecordKind::Log;
        if (std::holds_alternative<MetricRecord>(payload))
            return RecordKind::Metric;
        if (std::holds_alternative<SpanRecord>(payload))
            return RecordKind::Span;
        return RecordKind::Event;
    }

    /** @copydoc Runtime::Initialize */
    bool Runtime::Initialize(const Configuration &configuration, std::shared_ptr<ISink> sink) {
        std::vector<std::shared_ptr<ISink>> sinks;
        if (sink != nullptr)
            sinks.push_back(std::move(sink));
        return Initialize(configuration, std::move(sinks));
    }

    /** @copydoc Runtime::Initialize */
    bool Runtime::Initialize(const Configuration &configuration, std::vector<std::shared_ptr<ISink>> sinks) {
        if (configuration.queueCapacity == 0 || configuration.shutdownTimeout <= std::chrono::milliseconds::zero() ||
            configuration.sinkFlushInterval <= std::chrono::milliseconds::zero())
            return false;

        static_cast<void>(Shutdown());
        sinks.erase(std::remove(sinks.begin(), sinks.end(), nullptr), sinks.end());
        if (!configuration.enabled || sinks.empty()) {
            g_enabled.store(false, std::memory_order_release);
            return true;
        }

        const std::uint32_t generation = g_nextGeneration.fetch_add(1, std::memory_order_relaxed);
        auto state = std::make_shared<TelemetryState>(configuration, std::move(sinks), generation);
        state->Start();
        {
            std::lock_guard lock(g_lifecycleMutex);
            std::atomic_store_explicit(&g_state, std::move(state), std::memory_order_release);
        }
        g_enabled.store(true, std::memory_order_release);
        return true;
    }

    /** @copydoc Runtime::Shutdown */
    bool Runtime::Shutdown() {
        g_enabled.store(false, std::memory_order_release);
        std::shared_ptr<TelemetryState> state;
        {
            std::lock_guard lock(g_lifecycleMutex);
            state = std::atomic_exchange_explicit(&g_state, std::shared_ptr<TelemetryState>{}, std::memory_order_acq_rel);
        }
        if (state == nullptr)
            return true;
        const bool completed = state->Stop(state->ShutdownTimeout());
        if (!completed && !state->HasExited())
            Health().shutdownTimeouts.fetch_add(1, std::memory_order_relaxed);
        return completed;
    }

    /** @copydoc Runtime::SetEnabled */
    void Runtime::SetEnabled(const bool enabled) noexcept {
        g_enabled.store(enabled && std::atomic_load_explicit(&g_state, std::memory_order_acquire) != nullptr, std::memory_order_release);
    }

    /** @copydoc Runtime::IsEnabled */
    bool Runtime::IsEnabled() noexcept {
        return g_enabled.load(std::memory_order_relaxed);
    }

    /** @copydoc Runtime::IsEventEnabled */
    bool Runtime::IsEventEnabled(const std::string_view subsystem, const Log::Level severity) noexcept {
#if !HORO_ENABLE_TELEMETRY
        static_cast<void>(subsystem);
        static_cast<void>(severity);
        return false;
#else
        if (!IsEnabled())
            return false;
        const auto state = std::atomic_load_explicit(&g_state, std::memory_order_acquire);
        return state != nullptr && state->AllowsEvent(subsystem, severity);
#endif
    }

    /** @copydoc Runtime::Flush */
    bool Runtime::Flush(const std::chrono::milliseconds timeout) {
        const auto state = std::atomic_load_explicit(&g_state, std::memory_order_acquire);
        return state == nullptr || state->Flush(timeout);
    }

    /** @copydoc Runtime::EmitRecord */
    bool Runtime::EmitRecord(Record record) noexcept {
        if (!IsEnabled())
            return false;
        const auto state = std::atomic_load_explicit(&g_state, std::memory_order_acquire);
        if (state == nullptr) {
            Health().shutdownDrops.fetch_add(1, std::memory_order_relaxed);
            Health().droppedRecords.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        if (!state->Allows(record)) {
            Health().filteredRecords.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        try {
            RedactRecord(record);
            return state->TryPush(std::move(record));
        } catch (...) {
            Health().droppedRecords.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
    }

    /** @copydoc Runtime::RegisterCounter */
    Counter Runtime::RegisterCounter(InstrumentDescriptor descriptor) {
#if !HORO_ENABLE_TELEMETRY
        static_cast<void>(descriptor);
        return {};
#else
        descriptor.kind = InstrumentKind::Counter;
        const auto state = std::atomic_load_explicit(&g_state, std::memory_order_acquire);
        if (state == nullptr || !state->AllowsMetric(descriptor))
            return {};
        const auto registration = state->Register(std::move(descriptor));
        return registration ? Counter{registration->id, state->Generation(), registration->dimensionCount} : Counter{};
#endif
    }

    /** @copydoc Runtime::RegisterGauge */
    Gauge Runtime::RegisterGauge(InstrumentDescriptor descriptor) {
#if !HORO_ENABLE_TELEMETRY
        static_cast<void>(descriptor);
        return {};
#else
        descriptor.kind = InstrumentKind::Gauge;
        const auto state = std::atomic_load_explicit(&g_state, std::memory_order_acquire);
        if (state == nullptr || !state->AllowsMetric(descriptor))
            return {};
        const auto registration = state->Register(std::move(descriptor));
        return registration ? Gauge{registration->id, state->Generation(), registration->dimensionCount} : Gauge{};
#endif
    }

    /** @copydoc Runtime::RegisterHistogram */
    Histogram Runtime::RegisterHistogram(InstrumentDescriptor descriptor) {
#if !HORO_ENABLE_TELEMETRY
        static_cast<void>(descriptor);
        return {};
#else
        descriptor.kind = InstrumentKind::Histogram;
        const auto state = std::atomic_load_explicit(&g_state, std::memory_order_acquire);
        if (state == nullptr || !state->AllowsMetric(descriptor))
            return {};
        const auto registration = state->Register(std::move(descriptor));
        return registration ? Histogram{registration->id, state->Generation(), registration->dimensionCount} : Histogram{};
#endif
    }

    /** @copydoc Runtime::RegisterTiming */
    Timing Runtime::RegisterTiming(InstrumentDescriptor descriptor) {
#if !HORO_ENABLE_TELEMETRY
        static_cast<void>(descriptor);
        return {};
#else
        descriptor.kind = InstrumentKind::Timing;
        descriptor.unit = "seconds";
        const auto state = std::atomic_load_explicit(&g_state, std::memory_order_acquire);
        if (state == nullptr || !state->AllowsMetric(descriptor))
            return {};
        const auto registration = state->Register(std::move(descriptor));
        return registration ? Timing{registration->id, state->Generation(), registration->dimensionCount} : Timing{};
#endif
    }

    /** @copydoc Runtime::EmitEvent */
    bool Runtime::EmitEvent(const std::string_view subsystem, const std::string_view eventName, const Log::Level severity,
                            const std::string_view message) {
        if (!IsEnabled())
            return false;
        if (!IsEventEnabled(subsystem, severity)) {
            Health().filteredRecords.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        return EmitEvent(subsystem, eventName, severity, message, {}, Log::CaptureLogContext());
    }

    /** @copydoc Runtime::EmitEvent */
    bool Runtime::EmitEvent(const std::string_view subsystem, const std::string_view eventName, const Log::Level severity,
                            const std::string_view message, const Log::LogContextSnapshot &context) {
        return EmitEvent(subsystem, eventName, severity, message, {}, context);
    }

    /** @copydoc Runtime::EmitEvent */
    bool Runtime::EmitEvent(const std::string_view subsystem, const std::string_view eventName, const Log::Level severity,
                            const std::string_view message, const std::span<const Field> fields, const Log::LogContextSnapshot &context) {
        if (!IsEnabled())
            return false;
        const auto state = std::atomic_load_explicit(&g_state, std::memory_order_acquire);
        if (state == nullptr)
            return false;
        if (!state->AllowsEvent(subsystem, severity)) {
            Health().filteredRecords.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        Record record{.subsystem = std::string{subsystem},
                      .context = context,
                      .payload = DiagnosticEvent{.severity = severity,
                                                 .name = std::string{eventName},
                                                 .message = std::string{message},
                                                 .fields = std::vector<Field>{fields.begin(), fields.end()}}};
        try {
            RedactRecord(record);
            return state->TryPush(std::move(record));
        } catch (...) {
            Health().droppedRecords.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
    }

    /** @copydoc Runtime::GetStatistics */
    Statistics Runtime::GetStatistics() noexcept {
        AtomicStatistics &health = Health();
        return {.acceptedRecords = health.acceptedRecords.load(std::memory_order_relaxed),
                .exportedRecords = health.exportedRecords.load(std::memory_order_relaxed),
                .droppedRecords = health.droppedRecords.load(std::memory_order_relaxed),
                .filteredRecords = health.filteredRecords.load(std::memory_order_relaxed),
                .contentionDrops = health.contentionDrops.load(std::memory_order_relaxed),
                .queueFullDrops = health.queueFullDrops.load(std::memory_order_relaxed),
                .shutdownDrops = health.shutdownDrops.load(std::memory_order_relaxed),
                .staleHandleDrops = health.staleHandleDrops.load(std::memory_order_relaxed),
                .sinkFailures = health.sinkFailures.load(std::memory_order_relaxed),
                .flushTimeouts = health.flushTimeouts.load(std::memory_order_relaxed),
                .shutdownTimeouts = health.shutdownTimeouts.load(std::memory_order_relaxed),
                .invalidInstrumentRegistrations = health.invalidInstrumentRegistrations.load(std::memory_order_relaxed),
                .rejectedMetricSeries = health.rejectedMetricSeries.load(std::memory_order_relaxed)};
    }

    bool Runtime::BindMetric(const std::uint32_t instrumentId, const std::uint32_t generation, const InstrumentKind kind,
                             const std::span<const DimensionValue> dimensions, std::array<std::uint16_t, MaximumMetricDimensions> &valueIds,
                             std::uint8_t &dimensionCount) noexcept {
#if !HORO_ENABLE_TELEMETRY
        static_cast<void>(instrumentId);
        static_cast<void>(generation);
        static_cast<void>(kind);
        static_cast<void>(dimensions);
        static_cast<void>(valueIds);
        static_cast<void>(dimensionCount);
        return false;
#else
        const auto state = std::atomic_load_explicit(&g_state, std::memory_order_acquire);
        if (state == nullptr || state->Generation() != generation)
            return false;
        try {
            const bool bound = state->Bind(instrumentId, kind, dimensions, valueIds, dimensionCount);
            if (!bound)
                Health().rejectedMetricSeries.fetch_add(1, std::memory_order_relaxed);
            return bound;
        } catch (...) {
            Health().rejectedMetricSeries.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
#endif
    }

    bool Runtime::RecordMetric(const std::uint32_t instrumentId, const std::uint32_t generation, const InstrumentKind kind,
                               const double value, const std::array<std::uint16_t, MaximumMetricDimensions> &dimensionValueIds,
                               const std::uint8_t dimensionCount) noexcept {
        if (instrumentId == 0 || !IsEnabled())
            return false;
        const auto state = std::atomic_load_explicit(&g_state, std::memory_order_acquire);
        if (state == nullptr || state->Generation() != generation) {
            Health().staleHandleDrops.fetch_add(1, std::memory_order_relaxed);
            Health().droppedRecords.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        return state->TryPush(Record{.payload = MetricRecord{.kind = kind,
                                                             .instrumentId = instrumentId,
                                                             .value = value,
                                                             .dimensionValueIds = dimensionValueIds,
                                                             .dimensionCount = dimensionCount}});
    }

    Counter Counter::WithDimensions(const std::span<const DimensionValue> dimensions) const {
        Counter bound = *this;
        return Runtime::BindMetric(instrumentId_, generation_, InstrumentKind::Counter, dimensions, bound.dimensionValueIds_,
                                   bound.dimensionCount_)
                   ? bound
                   : Counter{};
    }

    Gauge Gauge::WithDimensions(const std::span<const DimensionValue> dimensions) const {
        Gauge bound = *this;
        return Runtime::BindMetric(instrumentId_, generation_, InstrumentKind::Gauge, dimensions, bound.dimensionValueIds_,
                                   bound.dimensionCount_)
                   ? bound
                   : Gauge{};
    }

    Histogram Histogram::WithDimensions(const std::span<const DimensionValue> dimensions) const {
        Histogram bound = *this;
        return Runtime::BindMetric(instrumentId_, generation_, InstrumentKind::Histogram, dimensions, bound.dimensionValueIds_,
                                   bound.dimensionCount_)
                   ? bound
                   : Histogram{};
    }

    Timing Timing::WithDimensions(const std::span<const DimensionValue> dimensions) const {
        Timing bound = *this;
        return Runtime::BindMetric(instrumentId_, generation_, InstrumentKind::Timing, dimensions, bound.dimensionValueIds_,
                                   bound.dimensionCount_)
                   ? bound
                   : Timing{};
    }

#if HORO_ENABLE_TELEMETRY
    /** @copydoc Counter::Add */
    void Counter::Add(const std::uint64_t delta) const noexcept {
        if (!static_cast<bool>(*this))
            return;
        static_cast<void>(Runtime::RecordMetric(instrumentId_, generation_, InstrumentKind::Counter, static_cast<double>(delta),
                                                dimensionValueIds_, dimensionCount_));
    }

    /** @copydoc Gauge::Set */
    void Gauge::Set(const double value) const noexcept {
        if (!static_cast<bool>(*this))
            return;
        static_cast<void>(
            Runtime::RecordMetric(instrumentId_, generation_, InstrumentKind::Gauge, value, dimensionValueIds_, dimensionCount_));
    }

    /** @copydoc Histogram::Observe */
    void Histogram::Observe(const double value) const noexcept {
        if (!static_cast<bool>(*this))
            return;
        static_cast<void>(
            Runtime::RecordMetric(instrumentId_, generation_, InstrumentKind::Histogram, value, dimensionValueIds_, dimensionCount_));
    }

    /** @copydoc Timing::Record */
    void Timing::Record(const std::chrono::nanoseconds duration) const noexcept {
        if (!static_cast<bool>(*this))
            return;
        static_cast<void>(Runtime::RecordMetric(instrumentId_, generation_, InstrumentKind::Timing,
                                                std::chrono::duration<double>(duration).count(), dimensionValueIds_, dimensionCount_));
    }

    /** @copydoc ScopedTimer::ScopedTimer */
    ScopedTimer::ScopedTimer(const Histogram histogram) noexcept : histogram_(histogram), startedAt_(std::chrono::steady_clock::now()) {}

    /** @copydoc ScopedTimer::ScopedTimer */
    ScopedTimer::ScopedTimer(const Timing timing) noexcept : timing_(timing), startedAt_(std::chrono::steady_clock::now()) {}

    /** @copydoc ScopedTimer::~ScopedTimer */
    ScopedTimer::~ScopedTimer() {
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt_);
        if (histogram_)
            histogram_.Observe(elapsed.count());
        if (timing_)
            timing_.Record(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - startedAt_));
    }
#endif
}  // namespace Horo::Telemetry
