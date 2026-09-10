#include "Horo/Cli/CliOptionParser.h"

#include <array>

namespace Horo::Cli {
    /** @copydoc CliOptionParser::CommonOptions */
    std::span<const CliOptionDescriptor> CliOptionParser::CommonOptions() noexcept {
        using enum CliOptionValueKind;
        static const std::array<CliOptionDescriptor, 4> options{CliOptionDescriptor{.name = "log-level",
                                                                                    .summary = "Set the invocation log threshold.",
                                                                                    .valueKind = Enumeration,
                                                                                    .enumerationValues = {"critical", "debug", "error",
                                                                                                          "info", "trace", "warning"},
                                                                                    .configurationKey = "observability.log.default_level"},
                                                                CliOptionDescriptor{.name = "non-interactive",
                                                                                    .summary = "Disable terminal prompts.",
                                                                                    .valueKind = Flag},
                                                                CliOptionDescriptor{.name = "output",
                                                                                    .shortName = 'o',
                                                                                    .summary = "Select the output encoding.",
                                                                                    .valueKind = Enumeration,
                                                                                    .defaultValue = "human",
                                                                                    .enumerationValues = {"human", "json", "jsonl"},
                                                                                    .configurationKey = "cli.output.format"},
                                                                CliOptionDescriptor{.name = "project",
                                                                                    .shortName = 'p',
                                                                                    .summary = "Select the project root.",
                                                                                    .valueKind = Path,
                                                                                    .configurationKey = "project.root"}};
        return options;
    }
}  // namespace Horo::Cli
