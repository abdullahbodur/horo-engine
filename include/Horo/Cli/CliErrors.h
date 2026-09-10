#pragma once

/**
 * @file CliErrors.h
 * @brief Stable typed failures emitted by CLI descriptor and registry validation.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Cli::CliErrors {
    /** @brief A descriptor or host policy contains malformed metadata. */
    extern const ErrorCodeDescriptor DescriptorInvalid;
    /** @brief Descriptor metadata exceeds a host-declared admission limit. */
    extern const ErrorCodeDescriptor RegistryCapacityExceeded;
    /** @brief More than one descriptor declares the same hierarchical command path. */
    extern const ErrorCodeDescriptor CommandPathDuplicate;
    /** @brief Option long names or short aliases collide within one command. */
    extern const ErrorCodeDescriptor OptionNameDuplicate;
    /** @brief An option declaration combines incompatible schema fields. */
    extern const ErrorCodeDescriptor OptionSchemaIncompatible;
    /** @brief An output declaration is unversioned or lacks required encodings. */
    extern const ErrorCodeDescriptor OutputSchemaIncompatible;
    /** @brief A descriptor requires a capability not granted by the composition root. */
    extern const ErrorCodeDescriptor CapabilityUnauthorized;
    /** @brief A descriptor is unavailable on the active executable host. */
    extern const ErrorCodeDescriptor HostUnsupported;
    /** @brief A descriptor targets a contract generation unsupported by the host. */
    extern const ErrorCodeDescriptor ContractVersionIncompatible;
}  // namespace Horo::Cli::CliErrors
