#include "Horo/Runtime/Render/PresentModeErrors.h"

#include "RenderErrorDescriptor.h"

namespace Horo::Render::PresentModeErrors {
    namespace {
        const ErrorDomainId Domain{"render.present-mode"};
    }  // namespace

    const ErrorCodeDescriptor InvalidRequest =
        Detail::MakeErrorDescriptor(Domain, "render.present_mode.request_invalid", ErrorSeverity::Error,
                                    "Present-mode intent is empty, unbounded, duplicated, or has an invalid required shape.",
                                    "Publish one required mode or a bounded unique Auto preference order.");
    const ErrorCodeDescriptor InvalidCapabilities =
        Detail::MakeErrorDescriptor(Domain, "render.present_mode.capabilities_invalid", ErrorSeverity::Error,
                                    "Present-mode capabilities are empty, unbounded, duplicated, or not canonically ordered.",
                                    "Reject the backend report and preserve the last validated capability snapshot.");
    const ErrorCodeDescriptor UnsupportedModeFact =
        Detail::MakeErrorDescriptor(Domain, "render.present_mode.fact_unsupported", ErrorSeverity::Error,
                                    "Present-mode input contains a value outside the Horo present-mode contract.",
                                    "Update the backend translation or host policy to use a declared Horo mode.");
    const ErrorCodeDescriptor RequiredModeUnavailable =
        Detail::MakeErrorDescriptor(Domain, "render.present_mode.required_unavailable", ErrorSeverity::Error,
                                    "The explicitly required present mode is unavailable.",
                                    "Change explicit host policy or select a backend that reports the required mode.");
    const ErrorCodeDescriptor NoPreferredModeAvailable =
        Detail::MakeErrorDescriptor(Domain, "render.present_mode.no_preferred_mode", ErrorSeverity::Error,
                                    "No host-declared Auto preference is supported.",
                                    "Add an acceptable host fallback or select a backend with a matching mode.");
}  // namespace Horo::Render::PresentModeErrors
