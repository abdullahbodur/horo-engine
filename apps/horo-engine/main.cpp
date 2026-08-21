#include "Horo/Application/HostObservability.h"
#include "Horo/Foundation/Logging/Logger.h"
#include "Horo/Foundation/Telemetry/Telemetry.h"

#include <filesystem>
#include <iostream>
#include <span>
#include <string_view>

namespace {
    struct Options {
        bool help{};
        bool emitSmoke{};
        std::filesystem::path diagnosticBundle;
    };

    [[nodiscard]] Options ParseOptions(const std::span<char *> arguments) {
        Options options;
        for (std::size_t index = 1; index < arguments.size(); ++index) {
            const std::string_view argument{arguments[index]};
            if (argument == "--help" || argument == "-h")
                options.help = true;
            else if (argument == "--emit-observability-smoke")
                options.emitSmoke = true;
            else if (argument == "--diagnostic-bundle" && index + 1 < arguments.size())
                options.diagnosticBundle = arguments[++index];
        }
        return options;
    }
}  // namespace

int main(const int argc, char **argv) {
    const Options options = ParseOptions(std::span{argv, static_cast<std::size_t>(argc)});
    if (options.help) {
        std::cout << "Usage: horo-engine [--emit-observability-smoke] [--diagnostic-bundle <absolute-output.zip>]\n";
        return 0;
    }

    Horo::Application::HostObservabilityConfiguration configuration{.logging = {.logDirectory = "~/.horo/logs",
                                                                                .baseName = "horo-engine",
                                                                                .hostName = "horo-engine",
                                                                                .hostVersion = HORO_ENGINE_VERSION_STRING},
                                                                    .identity = {.processRole = "cli",
                                                                                 .engineVersion = HORO_ENGINE_VERSION_STRING,
                                                                                 .buildConfiguration = HORO_BUILD_CONFIGURATION,
                                                                                 .sourceRevision = HORO_SOURCE_REVISION}};
    auto observability = Horo::Application::HostObservabilitySession::Start(std::move(configuration));
    if (observability == nullptr) {
        std::cerr << "horo-engine: observability initialization failed\n";
        return 2;
    }

    HORO_LOG_INFO("foundation.host", "Headless host initialized");
    if (options.emitSmoke) {
        const auto gameCounter =
            Horo::Telemetry::Runtime::RegisterCounter({.name = "game.smoke.completed", .subsystem = "Game.Smoke", .unit = "operations"});
        const auto pluginGauge =
            Horo::Telemetry::Runtime::RegisterGauge({.name = "plugin.example.active", .subsystem = "Plugin.example", .unit = "instances"});
        gameCounter.Add();
        pluginGauge.Set(1.0);
        static_cast<void>(Horo::Telemetry::Runtime::EmitEvent("Game.Smoke", "game.smoke.completed", Horo::Log::Level::Info,
                                                              "Headless observability smoke completed"));
    }

    if (!options.diagnosticBundle.empty()) {
        const auto result = observability->GenerateDiagnosticBundle({.outputPath = options.diagnosticBundle});
        if (result.HasError()) {
            std::cerr << "horo-engine: " << result.ErrorValue().message << '\n';
            return 3;
        }
        std::cout << result.Value().outputPath.string() << '\n';
    }
    return 0;
}
