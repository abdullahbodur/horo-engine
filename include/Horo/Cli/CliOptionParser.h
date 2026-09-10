#pragma once

/**
 * @file CliOptionParser.h
 * @brief Pure bounded translation from command-line inputs into typed CLI requests.
 */

#include "Horo/Cli/CliCommandRegistry.h"
#include "Horo/Foundation/Configuration.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Horo::Cli {
    /** @brief Normalized path value whose spelling is owned by the platform adapter. */
    struct CliPathValue final {
        std::string value; /**< Platform-normalized path spelling. */

        [[nodiscard]] bool operator==(const CliPathValue &) const noexcept = default;
    };

    /** @brief Typed scalar accepted by the CLI grammar. */
    using CliParsedValue = std::variant<bool, std::int64_t, double, std::string, CliPathValue>;

    /** @brief Winning source retained for every parsed option value. */
    enum class CliValueSource : std::uint8_t {
        Invocation,
        Configuration,
        DescriptorDefault,
    };

    /** @brief One typed value plus safe precedence provenance. */
    struct CliResolvedValue final {
        CliParsedValue value;                                   /**< Parsed value. */
        CliValueSource source{};                                /**< Winning CLI-level source. */
        std::optional<ConfigurationSource> configurationSource; /**< Exact configuration provenance, when applicable. */
    };

    /** @brief All resolved occurrences for one canonical option name. */
    struct CliParsedOption final {
        std::string name;                     /**< Long option name without `--`. */
        std::vector<CliResolvedValue> values; /**< One value, or multiple values for a repeatable option. */
    };

    /** @brief One typed positional argument associated with its descriptor name. */
    struct CliParsedPositional final {
        std::string name;     /**< Positional descriptor identity. */
        CliParsedValue value; /**< Parsed non-sensitive value. */
    };

    /** @brief Explicit bounded stdin payload admitted by a command descriptor. */
    struct CliParsedInput final {
        CliStdinPolicy policy{};      /**< Grammar used to validate the payload. */
        std::vector<std::byte> bytes; /**< Complete bounded payload captured once. */
    };

    /** @brief Validated command request produced before dispatch or application initialization. */
    struct CliCommandRequest final {
        CommandPath command;                          /**< Exact accepted command path. */
        std::vector<CliParsedOption> options;         /**< Resolved options in canonical name order. */
        std::vector<CliParsedPositional> positionals; /**< Positional values in command order. */
        std::optional<CliParsedInput> input;          /**< Explicit stdin payload, when selected. */
        bool interactive{};                           /**< Whether the dispatcher may open declared prompts. */
    };

    /** @brief Resource limits enforced before parser-owned input is retained. */
    struct CliParserLimits final {
        std::size_t maximumArguments{4096};             /**< Maximum argv entries after the executable name. */
        std::size_t maximumArgumentBytes{64 * 1024};    /**< Maximum bytes in one argv entry. */
        std::size_t maximumDiagnostics{64};             /**< Maximum structured syntax findings. */
        std::size_t maximumInputBytes{4 * 1024 * 1024}; /**< Maximum selected stdin payload. */
        std::size_t maximumJsonLines{65'536};           /**< Maximum JSONL records in one payload. */
    };

    /** @brief Platform-owned path normalization seam used without giving the parser filesystem authority. */
    class ICliPathNormalizer {
    public:
        virtual ~ICliPathNormalizer() = default;

        /**
         * @brief Normalizes one bounded path spelling without starting command work.
         * @param path Raw command-line or configuration path.
         * @return Platform-normalized spelling or a typed platform failure.
         */
        [[nodiscard]] virtual Result<std::string> Normalize(std::string_view path) const = 0;
    };

    /** @brief Host-owned reader called only for an explicitly selected, descriptor-admitted stdin mode. */
    class ICliInputReader {
    public:
        virtual ~ICliInputReader() = default;

        /**
         * @brief Reads at most the requested byte count from the selected input channel.
         * @param maximumBytes Hard upper bound including any framing bytes.
         * @return Captured bytes or a typed host/platform failure.
         */
        [[nodiscard]] virtual Result<std::vector<std::byte>> Read(std::size_t maximumBytes) = 0;
    };

    /** @brief Captured host facts supplied explicitly to one pure parser invocation. */
    struct CliParserContext final {
        const ConfigurationSnapshot *configuration{}; /**< Optional already-resolved immutable configuration. */
        const ICliPathNormalizer *pathNormalizer{};   /**< Required only when a path value is present. */
        ICliInputReader *inputReader{};               /**< Read only when stdin is explicitly selected. */
        bool stdinSelected{};                         /**< Explicit request to consume stdin. */
        CliStdinPolicy selectedStdinPolicy{};         /**< User-selected grammar; must match the descriptor. */
        bool terminalAvailable{};                     /**< Safe host fact; does not itself authorize prompting. */
    };

    /** @brief Stateless bounded command-line parser owned by `HoroEngine::CliHost`. */
    class CliOptionParser final {
    public:
        /**
         * @brief Parses one command request without dispatching work or performing ambient input reads.
         * @param arguments Arguments after the executable name.
         * @param registry Immutable accepted command registry.
         * @param context Explicit configuration, platform, stdin, and terminal seams.
         * @param limits Hard parser and input bounds.
         * @return Typed request or one `horo.cli` parsing error with bounded ordered diagnostics.
         * @throws std::bad_alloc When request or diagnostic storage cannot be allocated.
         */
        [[nodiscard]] static Result<CliCommandRequest> Parse(std::span<const std::string_view> arguments,
                                                             const CliCommandRegistry &registry, const CliParserContext &context = {},
                                                             const CliParserLimits &limits = {});

        /**
         * @brief Returns the immutable shared option schema used by every command.
         * @return Stable process-lifetime descriptor view in canonical long-name order.
         */
        [[nodiscard]] static std::span<const CliOptionDescriptor> CommonOptions() noexcept;
    };
}  // namespace Horo::Cli
