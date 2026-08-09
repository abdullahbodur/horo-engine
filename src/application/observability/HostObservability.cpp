#include "Horo/Application/HostObservability.h"

#include <array>
#include <atomic>
#include <cstdlib>
#include <string_view>
#include <system_error>
#include <thread>

namespace Horo::Application {
    namespace {
        std::atomic<bool> g_hostSessionActive{};

        /** @brief Resolves the one legacy home-relative form accepted by logger composition. */
        [[nodiscard]] std::filesystem::path ResolveLogDirectory(const std::filesystem::path &path) {
            const std::string text = path.string();
            if (text != "~" && !text.starts_with("~/") && !text.starts_with("~\\")) {
                std::error_code error;
                const std::filesystem::path absolute = std::filesystem::absolute(path, error);
                return error ? std::filesystem::path{} : absolute.lexically_normal();
            }
#if defined(_WIN32)
            const char *home = std::getenv("USERPROFILE");
#else
            const char *home = std::getenv("HOME");
#endif
            if (home == nullptr || *home == '\0')
                return {};
            if (text.size() == 1)
                return std::filesystem::path{home}.lexically_normal();
            return (std::filesystem::path{home} / text.substr(2)).lexically_normal();
        }

        [[nodiscard]] std::string OperatingSystemName() {
#if defined(_WIN32)
            return "windows";
#elif defined(__APPLE__)
            return "macos";
#elif defined(__linux__)
            return "linux";
#else
            return "unknown";
#endif
        }

        [[nodiscard]] std::string ArchitectureName() {
#if defined(__aarch64__) || defined(_M_ARM64)
            return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
            return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
            return "x86";
#else
            return "unknown";
#endif
        }

        void AppendMetadata(std::vector<std::pair<std::string, std::string>> &metadata, const std::string_view key,
                            const std::string &value) {
            if (!value.empty())
                metadata.emplace_back(key, value);
        }

        void AppendOptionalEntry(std::vector<Diagnostics::DiagnosticBundleEntry> &entries, const std::optional<HostDiagnosticFile> &file) {
            if (file.has_value())
                entries.push_back(
                    {.sourcePath = file->sourcePath, .archivePath = file->archivePath, .optional = true, .redactSensitiveText = true});
        }
    }  // namespace

    /** @copydoc HostObservabilitySession::Start */
    std::unique_ptr<HostObservabilitySession> HostObservabilitySession::Start(HostObservabilityConfiguration configuration) {
        bool expected = false;
        if (!g_hostSessionActive.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return nullptr;
        configuration.logging.logDirectory = ResolveLogDirectory(configuration.logging.logDirectory);
        if (configuration.logging.logDirectory.empty() || !configuration.logging.logDirectory.is_absolute()) {
            g_hostSessionActive.store(false, std::memory_order_release);
            return nullptr;
        }
        if (!Log::Logger::Init(configuration.logging)) {
            g_hostSessionActive.store(false, std::memory_order_release);
            return nullptr;
        }

        auto session = std::unique_ptr<HostObservabilitySession>{new HostObservabilitySession{std::move(configuration)}};
        const HostObservabilityIdentity &identity = session->configuration_.identity;
        const std::array fields{
            Telemetry::Field{.key = "process.role", .value = identity.processRole},
            Telemetry::Field{.key = "engine.version", .value = identity.engineVersion},
            Telemetry::Field{.key = "build.configuration", .value = identity.buildConfiguration},
            Telemetry::Field{.key = "source.revision", .value = identity.sourceRevision},
            Telemetry::Field{.key = "os.name", .value = OperatingSystemName()},
            Telemetry::Field{.key = "cpu.architecture", .value = ArchitectureName()},
            Telemetry::Field{.key = "renderer.backend", .value = identity.rendererBackend},
            Telemetry::Field{.key = "device.name", .value = identity.deviceName},
            Telemetry::Field{.key = "project.id", .value = identity.projectId},
            Telemetry::Field{.key = "project.version", .value = identity.projectVersion},
        };
        const std::uint64_t acceptedBefore = Log::Logger::Statistics().acceptedRecords;
        for (std::size_t attempt = 0; attempt < 64 && Log::Logger::Statistics().acceptedRecords == acceptedBefore; ++attempt) {
            Log::Logger::Write("observability.startup", Log::Level::Info, "Host identity snapshot", fields);
            std::this_thread::yield();
        }
        return session;
    }

    /** @copydoc HostObservabilitySession::~HostObservabilitySession */
    HostObservabilitySession::~HostObservabilitySession() {
        Shutdown();
    }

    /** @copydoc HostObservabilitySession::GenerateDiagnosticBundle */
    Result<Diagnostics::DiagnosticBundleSummary> HostObservabilitySession::GenerateDiagnosticBundle(
        const HostDiagnosticBundleRequest &request) const {
        static_cast<void>(Log::Logger::Flush());

        Diagnostics::DiagnosticBundleRequest bundle;
        bundle.outputPath = request.outputPath;
        bundle.maxInputBytes = request.maxInputBytes;
        bundle.metadata = request.metadata;
        const HostObservabilityIdentity &identity = configuration_.identity;
        AppendMetadata(bundle.metadata, "application.name", configuration_.logging.hostName);
        AppendMetadata(bundle.metadata, "application.version", configuration_.logging.hostVersion);
        AppendMetadata(bundle.metadata, "process.role", identity.processRole);
        AppendMetadata(bundle.metadata, "engine.version", identity.engineVersion);
        AppendMetadata(bundle.metadata, "build.configuration", identity.buildConfiguration);
        AppendMetadata(bundle.metadata, "source.revision", identity.sourceRevision);
        AppendMetadata(bundle.metadata, "os.name", OperatingSystemName());
        AppendMetadata(bundle.metadata, "cpu.architecture", ArchitectureName());
        AppendMetadata(bundle.metadata, "renderer.backend", identity.rendererBackend);
        AppendMetadata(bundle.metadata, "device.name", identity.deviceName);
        AppendMetadata(bundle.metadata, "project.id", identity.projectId);
        AppendMetadata(bundle.metadata, "project.version", identity.projectVersion);

        const std::filesystem::path &directory = configuration_.logging.logDirectory;
        const std::string &baseName = configuration_.logging.baseName;
        bundle.entries.push_back({.sourcePath = directory / (baseName + ".jsonl"),
                                  .archivePath = "logs/" + baseName + ".jsonl",
                                  .optional = true,
                                  .redactSensitiveText = true});
        bundle.entries.push_back({.sourcePath = directory / (baseName + ".operations.jsonl"),
                                  .archivePath = "history/" + baseName + ".operations.jsonl",
                                  .optional = true,
                                  .redactSensitiveText = true});
        for (std::size_t index = 1; index <= configuration_.logging.maxRolledFiles; ++index) {
            const std::string suffix = "." + std::to_string(index);
            bundle.entries.push_back({.sourcePath = directory / (baseName + ".jsonl" + suffix),
                                      .archivePath = "logs/" + baseName + ".jsonl" + suffix,
                                      .optional = true,
                                      .redactSensitiveText = true});
            bundle.entries.push_back({.sourcePath = directory / (baseName + ".operations.jsonl" + suffix),
                                      .archivePath = "history/" + baseName + ".operations.jsonl" + suffix,
                                      .optional = true,
                                      .redactSensitiveText = true});
        }
        bundle.entries.push_back({.sourcePath = directory / (baseName + ".session.json"),
                                  .archivePath = "metadata/session.json",
                                  .optional = true,
                                  .redactSensitiveText = true});
        bundle.entries.push_back({.sourcePath = directory / (baseName + ".shutdown.json"),
                                  .archivePath = "metadata/shutdown.json",
                                  .optional = true,
                                  .redactSensitiveText = true});
        AppendOptionalEntry(bundle.entries, request.crashMetadata);
        AppendOptionalEntry(bundle.entries, request.configuration);
        AppendOptionalEntry(bundle.entries, request.packageSummary);
        return Diagnostics::GenerateDiagnosticBundle(bundle);
    }

    /** @copydoc HostObservabilitySession::Shutdown */
    void HostObservabilitySession::Shutdown() noexcept {
        if (!active_)
            return;
        active_ = false;
        Log::Logger::Shutdown();
        g_hostSessionActive.store(false, std::memory_order_release);
    }

    /** @copydoc HostObservabilitySession::LogDirectory */
    const std::filesystem::path &HostObservabilitySession::LogDirectory() const noexcept {
        return configuration_.logging.logDirectory;
    }

    HostObservabilitySession::HostObservabilitySession(HostObservabilityConfiguration configuration)
        : configuration_(std::move(configuration)) {}
}  // namespace Horo::Application
