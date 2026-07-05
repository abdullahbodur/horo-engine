#pragma once

#include "core/pipeline/ReleaseTypes.h"
#include "core/pipeline/ToolchainSettings.h"
#include <string>

namespace Horo::Build {

/** @brief State of target build capability for local cross-compilation. */
enum class TargetCapabilityState {
    Native,              /**< Supported natively on this host OS. */
    ReadyCrossCompile,   /**< Configured and validated local cross-compilation toolchain. */
    DisabledToolchain,   /**< Toolchain exists and is valid, but is toggled off by user. */
    InvalidToolchain,    /**< Toolchain exists but failed validation (missing paths/compiler). */
    Unsupported          /**< Not native, and no toolchain configured. */
};

/** @brief Evaluation result for a specific build target. */
struct TargetCapability {
    BuildTargetOS os;
    BuildArch arch;
    TargetCapabilityState state;
    std::string disableReason; /**< Empty if enabled (Native/ReadyCrossCompile). Human-readable reason otherwise. */

    [[nodiscard]] bool IsEnabled() const {
        return state == TargetCapabilityState::Native || state == TargetCapabilityState::ReadyCrossCompile;
    }
};

/** @brief Evaluates capability of building for a specific target based on the host OS and toolchain settings.
 *
 *  @param os Target operating system.
 *  @param arch Target architecture.
 *  @param store Toolchain registry containing cross-compile settings.
 *  @return TargetCapability struct indicating if and how the build can proceed. */
TargetCapability EvaluateTargetCapability(BuildTargetOS os, BuildArch arch, const ToolchainSettingsStore& store);

/** @brief Formats the stable visible error for a disabled local build target.
 *
 *  @param capability Disabled target capability result.
 *  @return Copyable user-facing message containing target OS, architecture, and exact reason.
 */
std::string FormatTargetCapabilityBlockReason(const TargetCapability& capability);

} // namespace Horo::Build
