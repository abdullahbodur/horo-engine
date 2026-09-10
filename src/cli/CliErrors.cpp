#include "Horo/Cli/CliErrors.h"

namespace Horo::Cli::CliErrors {
    namespace {
        const ErrorDomainId CliDomain{"horo.cli"};
    }

    const ErrorCodeDescriptor DescriptorInvalid{.domain = CliDomain,
                                                .code = ErrorCode{"cli.descriptor_invalid"},
                                                .defaultSeverity = ErrorSeverity::Error,
                                                .summary = "CLI command descriptor is invalid.",
                                                .remediationHint = "Correct the inert command metadata before host activation.",
                                                .retryable = false,
                                                .userActionable = false};

    const ErrorCodeDescriptor RegistryCapacityExceeded{.domain = CliDomain,
                                                       .code = ErrorCode{"cli.registry_capacity_exceeded"},
                                                       .defaultSeverity = ErrorSeverity::Error,
                                                       .summary = "CLI command registry capacity was exceeded.",
                                                       .remediationHint = "Reduce contributed metadata or revise the explicit host limits.",
                                                       .retryable = false,
                                                       .userActionable = false};

    const ErrorCodeDescriptor CommandPathDuplicate{.domain = CliDomain,
                                                   .code = ErrorCode{"cli.command_path_duplicate"},
                                                   .defaultSeverity = ErrorSeverity::Error,
                                                   .summary = "CLI command path is already registered.",
                                                   .remediationHint = "Assign every command one unique hierarchical path.",
                                                   .retryable = false,
                                                   .userActionable = false};

    const ErrorCodeDescriptor OptionNameDuplicate{.domain = CliDomain,
                                                  .code = ErrorCode{"cli.option_name_duplicate"},
                                                  .defaultSeverity = ErrorSeverity::Error,
                                                  .summary = "CLI option name is duplicated within a command.",
                                                  .remediationHint = "Use unique long names and short aliases within the command.",
                                                  .retryable = false,
                                                  .userActionable = false};

    const ErrorCodeDescriptor OptionSchemaIncompatible{.domain = CliDomain,
                                                       .code = ErrorCode{"cli.option_schema_incompatible"},
                                                       .defaultSeverity = ErrorSeverity::Error,
                                                       .summary = "CLI option schema contains incompatible metadata.",
                                                       .remediationHint = "Align the option kind, defaults, and enumeration values.",
                                                       .retryable = false,
                                                       .userActionable = false};

    const ErrorCodeDescriptor OutputSchemaIncompatible{.domain = CliDomain,
                                                       .code = ErrorCode{"cli.output_schema_incompatible"},
                                                       .defaultSeverity = ErrorSeverity::Error,
                                                       .summary = "CLI output schema contains incompatible metadata.",
                                                       .remediationHint =
                                                           "Declare a versioned schema with human and machine output formats.",
                                                       .retryable = false,
                                                       .userActionable = false};

    const ErrorCodeDescriptor CapabilityUnauthorized{.domain = CliDomain,
                                                     .code = ErrorCode{"cli.capability_unauthorized"},
                                                     .defaultSeverity = ErrorSeverity::Error,
                                                     .summary = "CLI command requires an unauthorized capability.",
                                                     .remediationHint = "Approve the capability at composition or omit the contribution.",
                                                     .retryable = false,
                                                     .userActionable = false};

    const ErrorCodeDescriptor HostUnsupported{.domain = CliDomain,
                                              .code = ErrorCode{"cli.host_unsupported"},
                                              .defaultSeverity = ErrorSeverity::Error,
                                              .summary = "CLI command does not support the active host.",
                                              .remediationHint = "Select descriptors declared for this executable composition.",
                                              .retryable = false,
                                              .userActionable = false};

    const ErrorCodeDescriptor ContractVersionIncompatible{.domain = CliDomain,
                                                          .code = ErrorCode{"cli.contract_version_incompatible"},
                                                          .defaultSeverity = ErrorSeverity::Error,
                                                          .summary = "CLI command descriptor contract is incompatible with the host.",
                                                          .remediationHint = "Use the host contract major and no newer minor version.",
                                                          .retryable = false,
                                                          .userActionable = false};
}  // namespace Horo::Cli::CliErrors
