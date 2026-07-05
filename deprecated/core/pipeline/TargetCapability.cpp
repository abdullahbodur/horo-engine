#include "core/pipeline/TargetCapability.h"
#include <format>

namespace Horo::Build {

namespace {
bool IsNativeHost(BuildTargetOS os) {
#if defined(_WIN32)
    return os == BuildTargetOS::Windows;
#elif defined(__APPLE__)
    return os == BuildTargetOS::MacOS;
#else
    return os == BuildTargetOS::Linux;
#endif
}
} // namespace

TargetCapability EvaluateTargetCapability(BuildTargetOS os, BuildArch arch, const ToolchainSettingsStore& store) {
    TargetCapability cap;
    cap.os = os;
    cap.arch = arch;

    if (IsNativeHost(os)) {
        cap.state = TargetCapabilityState::Native;
        cap.disableReason = "";
        return cap;
    }

    ReleaseTargetTriple target{os, arch};
    auto toolchains = store.GetToolchainsForTarget(target);

    if (toolchains.empty()) {
        cap.state = TargetCapabilityState::Unsupported;
        cap.disableReason = "No cross-compilation toolchain configured for this target.";
        return cap;
    }

    bool hasEnabled = false;
    bool hasValidEnabled = false;

    for (const auto& tc : toolchains) {
        if (tc.enabled) {
            hasEnabled = true;
            if (tc.lastValidationResult.status == ToolchainValidationResult::Status::Valid) {
                hasValidEnabled = true;
                break;
            }
        }
    }

    if (hasValidEnabled) {
        cap.state = TargetCapabilityState::ReadyCrossCompile;
        cap.disableReason = "";
        return cap;
    }

    if (hasEnabled) {
        cap.state = TargetCapabilityState::InvalidToolchain;
        cap.disableReason = "Enabled toolchain failed validation checks.";
        return cap;
    }

    cap.state = TargetCapabilityState::DisabledToolchain;
    cap.disableReason = "Toolchain is configured but disabled in settings.";
    return cap;
}

/** @copydoc FormatTargetCapabilityBlockReason */
std::string FormatTargetCapabilityBlockReason(const TargetCapability& capability) {
    return std::format(
        "Build blocked: {} {} target cannot be built locally. Reason: {}",
        GetBuildTargetOSLabel(capability.os),
        GetBuildArchLabel(capability.arch),
        capability.disableReason.empty() ? "Target is not enabled." : capability.disableReason);
}

} // namespace Horo::Build
