#include "Horo/Cli/CliCommandRegistry.h"

#include "Horo/Cli/CliErrors.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <locale>
#include <sstream>

namespace Horo::Cli {
    namespace {
        [[nodiscard]] bool IsCanonicalToken(const std::string_view value, const std::size_t maximumBytes) noexcept {
            if (value.empty() || value.size() > maximumBytes || value.front() < 'a' || value.front() > 'z')
                return false;
            return std::ranges::all_of(value, [](const char character) {
                return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '-';
            });
        }

        [[nodiscard]] bool IsSafeSummary(const std::string_view value, const std::size_t maximumBytes) noexcept {
            return !value.empty() && value.size() <= maximumBytes && std::ranges::all_of(value, [](const char character) {
                const auto byte = static_cast<unsigned char>(character);
                return byte >= 0x20U && byte != 0x7FU;
            });
        }

        [[nodiscard]] bool IsCanonicalNamespacedId(const std::string_view value, const std::size_t maximumBytes) noexcept {
            if (value.empty() || value.size() > maximumBytes)
                return false;
            std::size_t begin = 0;
            bool foundSeparator = false;
            while (begin < value.size()) {
                const std::size_t end = value.find('.', begin);
                const std::string_view token = value.substr(begin, end == std::string_view::npos ? value.size() - begin : end - begin);
                if (!IsCanonicalToken(token, maximumBytes))
                    return false;
                if (end == std::string_view::npos)
                    break;
                foundSeparator = true;
                begin = end + 1;
            }
            return foundSeparator;
        }

        template <typename Enum> [[nodiscard]] bool IsKnown(Enum value) noexcept;

        template <> [[nodiscard]] bool IsKnown(const CliHostKind value) noexcept {
            return value == CliHostKind::HoroEngine || value == CliHostKind::HoroPak;
        }

        template <> [[nodiscard]] bool IsKnown(const CliOptionValueKind value) noexcept {
            switch (value) {
                case CliOptionValueKind::Flag:
                case CliOptionValueKind::String:
                case CliOptionValueKind::SignedInteger:
                case CliOptionValueKind::FloatingPoint:
                case CliOptionValueKind::Enumeration:
                case CliOptionValueKind::Path:
                    return true;
            }
            return false;
        }

        template <> [[nodiscard]] bool IsKnown(const CliInteractivePolicy value) noexcept {
            return value == CliInteractivePolicy::Forbidden || value == CliInteractivePolicy::Optional ||
                   value == CliInteractivePolicy::Required;
        }

        template <> [[nodiscard]] bool IsKnown(const CliSideEffectPolicy value) noexcept {
            switch (value) {
                case CliSideEffectPolicy::None:
                case CliSideEffectPolicy::ReadsState:
                case CliSideEffectPolicy::MutatesState:
                case CliSideEffectPolicy::WritesFiles:
                case CliSideEffectPolicy::StartsExternalProcess:
                    return true;
            }
            return false;
        }

        template <> [[nodiscard]] bool IsKnown(const CliCancellationPolicy value) noexcept {
            return value == CliCancellationPolicy::Unsupported || value == CliCancellationPolicy::Cooperative;
        }

        template <> [[nodiscard]] bool IsKnown(const CliStdinPolicy value) noexcept {
            switch (value) {
                case CliStdinPolicy::None:
                case CliStdinPolicy::JsonDocument:
                case CliStdinPolicy::JsonLines:
                case CliStdinPolicy::BinaryStream:
                    return true;
            }
            return false;
        }

        template <> [[nodiscard]] bool IsKnown(const CliCommandOrigin value) noexcept {
            return value == CliCommandOrigin::BuiltIn || value == CliCommandOrigin::Contribution;
        }

        [[nodiscard]] CliHostAvailability AvailabilityFor(const CliHostKind host) noexcept {
            return host == CliHostKind::HoroEngine ? CliHostAvailability::HoroEngine : CliHostAvailability::HoroPak;
        }

        [[nodiscard]] bool HasFormat(const CliOutputFormat formats, const CliOutputFormat requested) noexcept {
            return (formats & requested) == requested;
        }

        [[nodiscard]] bool ValidFormats(const CliOutputFormat formats) noexcept {
            constexpr auto KnownBits = static_cast<std::uint8_t>(CliOutputFormat::Human) |
                                       static_cast<std::uint8_t>(CliOutputFormat::Json) |
                                       static_cast<std::uint8_t>(CliOutputFormat::JsonLines);
            const auto bits = static_cast<std::uint8_t>(formats);
            const bool hasMachineFormat = HasFormat(formats, CliOutputFormat::Json) || HasFormat(formats, CliOutputFormat::JsonLines);
            return (bits & static_cast<std::uint8_t>(~KnownBits)) == 0 && HasFormat(formats, CliOutputFormat::Human) && hasMachineFormat;
        }

        [[nodiscard]] bool ValidAvailability(const CliHostAvailability availability) noexcept {
            constexpr auto KnownBits = static_cast<std::uint8_t>(CliHostAvailability::All);
            const auto bits = static_cast<std::uint8_t>(availability);
            return bits != 0 && (bits & static_cast<std::uint8_t>(~KnownBits)) == 0;
        }

        [[nodiscard]] bool ValidInteger(const std::string_view value) noexcept {
            std::int64_t parsed{};
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
            return error == std::errc{} && end == value.data() + value.size();
        }

        [[nodiscard]] bool ValidFloat(const std::string &value) {
            std::istringstream stream{value};
            stream.imbue(std::locale::classic());
            double parsed{};
            stream >> parsed;
            return stream && stream.peek() == std::char_traits<char>::eof() && std::isfinite(parsed);
        }

        [[nodiscard]] bool ContainsCapability(const std::span<const CliCapabilityId> capabilities,
                                              const CliCapabilityId &candidate) noexcept {
            return std::ranges::find(capabilities, candidate) != capabilities.end();
        }

        [[nodiscard]] bool ValidLimits(const CliCommandRegistryLimits &limits) noexcept {
            return limits.maximumCommands > 0 && limits.maximumPathSegments > 0 && limits.maximumOptionsPerCommand > 0 &&
                   limits.maximumCapabilitiesPerCommand > 0 && limits.maximumEnumerationValues > 0 && limits.maximumIdentifierBytes > 0 &&
                   limits.maximumSummaryBytes > 0;
        }

        [[nodiscard]] Result<void> ValidatePolicy(const CliCommandRegistryPolicy &policy) {
            if (!IsKnown(policy.activeHost) || policy.supportedContractVersion.major == 0 || !ValidLimits(policy.limits))
                return Result<void>::Failure(MakeError(CliErrors::DescriptorInvalid, "CLI registry policy is invalid."));
            for (std::size_t index = 0; index < policy.grantedCapabilities.size(); ++index) {
                const CliCapabilityId &capability = policy.grantedCapabilities[index];
                if (!IsCanonicalNamespacedId(capability.value, policy.limits.maximumIdentifierBytes) ||
                    ContainsCapability(std::span{policy.grantedCapabilities}.first(index), capability))
                    return Result<void>::Failure(MakeError(CliErrors::DescriptorInvalid, "CLI registry capability grants are invalid."));
            }
            return Result<void>::Success();
        }

        [[nodiscard]] bool HasDuplicateOption(const std::span<const CliOptionDescriptor> options, const std::size_t candidate) noexcept {
            for (std::size_t prior = 0; prior < candidate; ++prior) {
                if (options[prior].name == options[candidate].name ||
                    (options[prior].shortName && options[prior].shortName == options[candidate].shortName))
                    return true;
            }
            return false;
        }

        [[nodiscard]] bool HasDuplicateString(const std::span<const std::string> values, const std::size_t candidate) noexcept {
            return std::ranges::find(values.first(candidate), values[candidate]) != values.first(candidate).end();
        }

        [[nodiscard]] Result<void> ValidateOption(const CliOptionDescriptor &option, const CliCommandRegistryLimits &limits) {
            if (!IsCanonicalToken(option.name, limits.maximumIdentifierBytes) ||
                !IsSafeSummary(option.summary, limits.maximumSummaryBytes) || !IsKnown(option.valueKind) ||
                (option.shortName &&
                 !((*option.shortName >= 'a' && *option.shortName <= 'z') || (*option.shortName >= 'A' && *option.shortName <= 'Z') ||
                   (*option.shortName >= '0' && *option.shortName <= '9'))))
                return Result<void>::Failure(MakeError(CliErrors::DescriptorInvalid, "CLI option metadata is invalid."));

            if (option.enumerationValues.size() > limits.maximumEnumerationValues)
                return Result<void>::Failure(MakeError(CliErrors::RegistryCapacityExceeded));

            if ((option.required && option.defaultValue) || (option.sensitive && option.defaultValue) ||
                (option.valueKind == CliOptionValueKind::Flag &&
                 (option.defaultValue || !option.enumerationValues.empty() || option.sensitive)) ||
                (option.valueKind != CliOptionValueKind::Enumeration && !option.enumerationValues.empty()) ||
                (option.valueKind == CliOptionValueKind::Enumeration && option.enumerationValues.empty()))
                return Result<void>::Failure(MakeError(CliErrors::OptionSchemaIncompatible));

            for (std::size_t index = 0; index < option.enumerationValues.size(); ++index) {
                if (!IsCanonicalToken(option.enumerationValues[index], limits.maximumIdentifierBytes) ||
                    HasDuplicateString(option.enumerationValues, index))
                    return Result<void>::Failure(MakeError(CliErrors::OptionSchemaIncompatible));
            }
            if (!option.defaultValue)
                return Result<void>::Success();

            if (option.defaultValue->size() > limits.maximumIdentifierBytes)
                return Result<void>::Failure(MakeError(CliErrors::RegistryCapacityExceeded));
            if (!std::ranges::all_of(*option.defaultValue, [](const char character) {
                const auto byte = static_cast<unsigned char>(character);
                return byte >= 0x20U && byte != 0x7FU;
            }))
                return Result<void>::Failure(MakeError(CliErrors::OptionSchemaIncompatible));

            bool validDefault = !option.defaultValue->empty();
            if (option.valueKind == CliOptionValueKind::Enumeration)
                validDefault = std::ranges::find(option.enumerationValues, *option.defaultValue) != option.enumerationValues.end();
            else if (option.valueKind == CliOptionValueKind::SignedInteger)
                validDefault = ValidInteger(*option.defaultValue);
            else if (option.valueKind == CliOptionValueKind::FloatingPoint)
                validDefault = ValidFloat(*option.defaultValue);
            return validDefault ? Result<void>::Success() : Result<void>::Failure(MakeError(CliErrors::OptionSchemaIncompatible));
        }

        [[nodiscard]] Result<void> ValidateDescriptor(const CliCommandDescriptor &descriptor, const CliCommandRegistryPolicy &policy) {
            const auto &limits = policy.limits;
            if (descriptor.path.segments.empty() || descriptor.path.segments.size() > limits.maximumPathSegments ||
                !IsSafeSummary(descriptor.summary, limits.maximumSummaryBytes) ||
                descriptor.options.size() > limits.maximumOptionsPerCommand ||
                descriptor.requiredCapabilities.size() > limits.maximumCapabilitiesPerCommand || !IsKnown(descriptor.interactive) ||
                !IsKnown(descriptor.sideEffects) || !IsKnown(descriptor.cancellation) || !IsKnown(descriptor.stdinPolicy) ||
                !IsKnown(descriptor.origin) || !IsCanonicalNamespacedId(descriptor.ownerId, limits.maximumIdentifierBytes) ||
                !IsCanonicalNamespacedId(descriptor.output.id, limits.maximumIdentifierBytes) || !ValidAvailability(descriptor.hosts))
                return Result<void>::Failure(MakeError(CliErrors::DescriptorInvalid));

            if (descriptor.output.version == 0 || !ValidFormats(descriptor.output.formats))
                return Result<void>::Failure(MakeError(CliErrors::OutputSchemaIncompatible));

            if ((descriptor.timeout.defaultMilliseconds == 0) != (descriptor.timeout.maximumMilliseconds == 0) ||
                descriptor.timeout.defaultMilliseconds > descriptor.timeout.maximumMilliseconds)
                return Result<void>::Failure(MakeError(CliErrors::DescriptorInvalid, "CLI timeout policy is invalid."));

            if (!std::ranges::all_of(descriptor.path.segments, [&limits](const std::string &segment) {
                return IsCanonicalToken(segment, limits.maximumIdentifierBytes);
            }))
                return Result<void>::Failure(MakeError(CliErrors::DescriptorInvalid, "CLI command path is invalid."));

            if ((descriptor.hosts & AvailabilityFor(policy.activeHost)) == CliHostAvailability::None)
                return Result<void>::Failure(MakeError(CliErrors::HostUnsupported));

            if (descriptor.contractVersion.major == 0 || descriptor.contractVersion.major != policy.supportedContractVersion.major ||
                descriptor.contractVersion.minor > policy.supportedContractVersion.minor)
                return Result<void>::Failure(MakeError(CliErrors::ContractVersionIncompatible));

            for (std::size_t index = 0; index < descriptor.options.size(); ++index) {
                if (HasDuplicateOption(descriptor.options, index))
                    return Result<void>::Failure(MakeError(CliErrors::OptionNameDuplicate));
                if (const Result<void> valid = ValidateOption(descriptor.options[index], limits); valid.HasError())
                    return valid;
            }

            for (std::size_t index = 0; index < descriptor.requiredCapabilities.size(); ++index) {
                const CliCapabilityId &capability = descriptor.requiredCapabilities[index];
                if (!IsCanonicalNamespacedId(capability.value, limits.maximumIdentifierBytes) ||
                    ContainsCapability(std::span{descriptor.requiredCapabilities}.first(index), capability))
                    return Result<void>::Failure(MakeError(CliErrors::DescriptorInvalid, "CLI capability requirement is invalid."));
                if (!ContainsCapability(policy.grantedCapabilities, capability))
                    return Result<void>::Failure(MakeError(CliErrors::CapabilityUnauthorized));
            }
            return Result<void>::Success();
        }

        [[nodiscard]] std::string JoinPath(const CommandPath &path) {
            std::string result;
            for (const std::string &segment : path.segments) {
                if (!result.empty())
                    result.push_back(' ');
                result.append(segment);
            }
            return result;
        }

        [[nodiscard]] bool HasPrefix(const CommandPath &path, const CommandPath &prefix) noexcept {
            return prefix.segments.size() <= path.segments.size() &&
                   std::ranges::equal(prefix.segments, std::span{path.segments}.first(prefix.segments.size()));
        }

        void Canonicalize(CliCommandDescriptor &descriptor) {
            std::ranges::sort(descriptor.options, {}, &CliOptionDescriptor::name);
            for (CliOptionDescriptor &option : descriptor.options)
                std::ranges::sort(option.enumerationValues);
            std::ranges::sort(descriptor.requiredCapabilities, {}, &CliCapabilityId::value);
        }

        void AppendFormats(std::string &help, const CliOutputFormat formats) {
            help.append("Output: human");
            if (HasFormat(formats, CliOutputFormat::Json))
                help.append(", json");
            if (HasFormat(formats, CliOutputFormat::JsonLines))
                help.append(", jsonl");
            help.push_back('\n');
        }

        void AppendOptionValue(std::string &help, const CliOptionDescriptor &option) {
            switch (option.valueKind) {
                case CliOptionValueKind::Flag:
                    return;
                case CliOptionValueKind::String:
                    help.append(" <string>");
                    return;
                case CliOptionValueKind::SignedInteger:
                    help.append(" <integer>");
                    return;
                case CliOptionValueKind::FloatingPoint:
                    help.append(" <number>");
                    return;
                case CliOptionValueKind::Path:
                    help.append(" <path>");
                    return;
                case CliOptionValueKind::Enumeration:
                    help.append(" <");
                    for (std::size_t index = 0; index < option.enumerationValues.size(); ++index) {
                        if (index > 0)
                            help.push_back('|');
                        help.append(option.enumerationValues[index]);
                    }
                    help.push_back('>');
                    return;
            }
        }

        void AppendOptionPolicy(std::string &help, const CliOptionDescriptor &option) {
            if (option.required)
                help.append(" (required)");
            else if (option.defaultValue && !option.sensitive)
                help.append(" (default: ").append(*option.defaultValue).push_back(')');
            if (option.repeatable)
                help.append(" (repeatable)");
        }
    }  // namespace

    /** @copydoc CliCommandRegistry::Create */
    Result<CliCommandRegistry> CliCommandRegistry::Create(const std::span<const CliCommandDescriptor> descriptors,
                                                          const CliCommandRegistryPolicy &policy) {
        if (const Result<void> validPolicy = ValidatePolicy(policy); validPolicy.HasError())
            return Result<CliCommandRegistry>::Failure(validPolicy.ErrorValue());
        if (descriptors.size() > policy.limits.maximumCommands)
            return Result<CliCommandRegistry>::Failure(MakeError(CliErrors::RegistryCapacityExceeded));

        for (const CliCommandDescriptor &descriptor : descriptors) {
            if (const Result<void> validDescriptor = ValidateDescriptor(descriptor, policy); validDescriptor.HasError())
                return Result<CliCommandRegistry>::Failure(validDescriptor.ErrorValue());
        }

        CliCommandRegistry registry;
        registry.commands_.assign(descriptors.begin(), descriptors.end());
        for (CliCommandDescriptor &descriptor : registry.commands_)
            Canonicalize(descriptor);
        std::ranges::sort(registry.commands_, {}, &CliCommandDescriptor::path);
        for (std::size_t index = 1; index < registry.commands_.size(); ++index) {
            if (registry.commands_[index - 1].path == registry.commands_[index].path)
                return Result<CliCommandRegistry>::Failure(MakeError(CliErrors::CommandPathDuplicate));
        }
        return Result<CliCommandRegistry>::Success(std::move(registry));
    }

    /** @copydoc CliCommandRegistry::Commands */
    std::span<const CliCommandDescriptor> CliCommandRegistry::Commands() const noexcept {
        return commands_;
    }

    /** @copydoc CliCommandRegistry::Find */
    const CliCommandDescriptor *CliCommandRegistry::Find(const CommandPath &path) const noexcept {
        const auto found = std::ranges::lower_bound(commands_, path, {}, &CliCommandDescriptor::path);
        return found != commands_.end() && found->path == path ? &*found : nullptr;
    }

    /** @copydoc CliCommandRegistry::Discover */
    std::vector<const CliCommandDescriptor *> CliCommandRegistry::Discover(const CommandPath &prefix) const {
        std::vector<const CliCommandDescriptor *> discovered;
        discovered.reserve(commands_.size());
        for (const CliCommandDescriptor &descriptor : commands_) {
            if (HasPrefix(descriptor.path, prefix))
                discovered.push_back(&descriptor);
        }
        return discovered;
    }

    /** @copydoc CliCommandRegistry::DiscoverNextSegments */
    std::vector<std::string_view> CliCommandRegistry::DiscoverNextSegments(const CommandPath &prefix) const {
        std::vector<std::string_view> segments;
        for (const CliCommandDescriptor &descriptor : commands_) {
            if (!HasPrefix(descriptor.path, prefix) || descriptor.path.segments.size() == prefix.segments.size())
                continue;
            const std::string_view candidate = descriptor.path.segments[prefix.segments.size()];
            if (std::ranges::find(segments, candidate) == segments.end())
                segments.push_back(candidate);
        }
        std::ranges::sort(segments);
        return segments;
    }

    /** @copydoc CliCommandRegistry::GenerateHelp */
    std::string CliCommandRegistry::GenerateHelp(const std::string_view programName, const CommandPath &path) const {
        std::string help;
        if (path.segments.empty()) {
            help.append("Usage: ").append(programName).append(" <command> [options]\n\nCommands:\n");
            for (const CliCommandDescriptor &descriptor : commands_)
                help.append("  ").append(JoinPath(descriptor.path)).append("  ").append(descriptor.summary).push_back('\n');
            return help;
        }

        const CliCommandDescriptor *descriptor = Find(path);
        if (!descriptor)
            return {};
        help.append("Usage: ").append(programName).push_back(' ');
        help.append(JoinPath(path));
        if (!descriptor->options.empty())
            help.append(" [options]");
        help.append("\n\n").append(descriptor->summary).push_back('\n');
        if (!descriptor->options.empty()) {
            help.append("\nOptions:\n");
            for (const CliOptionDescriptor &option : descriptor->options) {
                help.append("  ");
                if (option.shortName) {
                    help.push_back('-');
                    help.push_back(*option.shortName);
                    help.append(", ");
                }
                help.append("--").append(option.name);
                AppendOptionValue(help, option);
                help.append("  ").append(option.summary);
                AppendOptionPolicy(help, option);
                help.push_back('\n');
            }
        }
        help.push_back('\n');
        AppendFormats(help, descriptor->output.formats);
        return help;
    }
}  // namespace Horo::Cli
