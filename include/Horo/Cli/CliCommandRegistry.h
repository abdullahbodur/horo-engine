#pragma once

/**
 * @file CliCommandRegistry.h
 * @brief Host-owned validation, indexing, discovery, and generated help for CLI commands.
 */

#include "Horo/Cli/CliCommandDescriptor.h"
#include "Horo/Foundation/Result.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Cli {
    /** @brief Explicit bounds applied before descriptor metadata is copied into a registry. */
    struct CliCommandRegistryLimits final {
        std::size_t maximumCommands{512};              /**< Maximum accepted descriptors. */
        std::size_t maximumPathSegments{16};           /**< Maximum segments in one path. */
        std::size_t maximumOptionsPerCommand{128};     /**< Maximum options in one command. */
        std::size_t maximumCapabilitiesPerCommand{64}; /**< Maximum requirements in one command. */
        std::size_t maximumEnumerationValues{256};     /**< Maximum values in one enum option. */
        std::size_t maximumIdentifierBytes{128};       /**< Maximum bytes in an identifier or path segment. */
        std::size_t maximumSummaryBytes{1024};         /**< Maximum bytes in user-facing summary text. */
    };

    /** @brief Host admission policy evaluated for every built-in and contributed descriptor. */
    struct CliCommandRegistryPolicy final {
        CliHostKind activeHost{};                         /**< Composition root building the registry. */
        CliContractVersion supportedContractVersion{};    /**< Newest compatible descriptor contract. */
        std::vector<CliCapabilityId> grantedCapabilities; /**< Exact capabilities approved for this host. */
        CliCommandRegistryLimits limits{};                /**< Bounded registry and metadata limits. */
    };

    /** @brief Immutable accepted command set and its sole help/discovery inventory. */
    class CliCommandRegistry final {
    public:
        /**
         * @brief Validates and atomically builds one deterministic command registry.
         * @param descriptors Built-in and approved contributed descriptors; both use the same validation path.
         * @param policy Active host, compatible contract, capability grants, and metadata bounds.
         * @return Registry sorted lexicographically by command path, or a typed activation failure.
         * @throws std::bad_alloc When copying accepted descriptor metadata fails.
         *
         * Validation is inert and invokes no command adapter or lifecycle callback. A failure publishes no
         * partial registry. Contract compatibility requires the same major version and a descriptor minor
         * version no newer than the host; patch versions do not remove compatibility within an admitted minor.
         */
        [[nodiscard]] static Result<CliCommandRegistry> Create(std::span<const CliCommandDescriptor> descriptors,
                                                               const CliCommandRegistryPolicy &policy);

        /** @brief Returns all accepted descriptors in deterministic path order. @return Registry-owned stable view. */
        [[nodiscard]] std::span<const CliCommandDescriptor> Commands() const noexcept;

        /**
         * @brief Resolves one exact hierarchical command path.
         * @param path Canonical path to resolve.
         * @return Registry-owned descriptor pointer, or null when no accepted command matches.
         */
        [[nodiscard]] const CliCommandDescriptor *Find(const CommandPath &path) const noexcept;

        /**
         * @brief Discovers accepted commands below a hierarchical prefix.
         * @param prefix Empty for the full inventory, otherwise the leading command segments to match.
         * @return Registry-owned descriptor pointers in deterministic path order.
         * @throws std::bad_alloc When allocating the returned pointer collection fails.
         */
        [[nodiscard]] std::vector<const CliCommandDescriptor *> Discover(const CommandPath &prefix = {}) const;

        /**
         * @brief Discovers unique next path segments for shell completion.
         * @param prefix Already-completed hierarchical path segments.
         * @return Registry-owned segment views in deterministic lexical order.
         * @throws std::bad_alloc When allocating the returned view collection fails.
         */
        [[nodiscard]] std::vector<std::string_view> DiscoverNextSegments(const CommandPath &prefix = {}) const;

        /**
         * @brief Generates deterministic LF-delimited help solely from accepted descriptors.
         * @param programName Executable name shown in usage lines.
         * @param path Empty for the command inventory or an exact command path for detailed help.
         * @return Generated help, or an empty string when the requested exact path is not accepted.
         * @throws std::bad_alloc When formatting help fails.
         */
        [[nodiscard]] std::string GenerateHelp(std::string_view programName, const CommandPath &path = {}) const;

    private:
        std::vector<CliCommandDescriptor> commands_;
    };
}  // namespace Horo::Cli
