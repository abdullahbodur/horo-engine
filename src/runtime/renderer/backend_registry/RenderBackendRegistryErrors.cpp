#include "RenderBackendRegistryErrors.h"

namespace Horo::Render::RegistryErrors
{
namespace
{
const ErrorDomainId Domain{"horo.render"};

[[nodiscard]] ErrorCodeDescriptor Describe(const char *code, const char *summary, const char *remediation,
                                           const bool retryable = false)
{
    return ErrorCodeDescriptor{.domain = Domain,
                               .code = ErrorCode{code},
                               .defaultSeverity = ErrorSeverity::Error,
                               .summary = summary,
                               .remediationHint = remediation,
                               .retryable = retryable,
                               .userActionable = false};
}
} // namespace

const ErrorCodeDescriptor BackendNotFound =
    Describe("render.registry.backend_not_found", "Renderer backend is not registered.",
             "Register the requested backend before creating it.");
const ErrorCodeDescriptor DuplicateBackend =
    Describe("render.registry.duplicate_backend", "Renderer backend ID is already registered.",
             "Use one provider for each stable backend ID.");
const ErrorCodeDescriptor InvalidDescriptor =
    Describe("render.registry.invalid_descriptor", "Renderer backend descriptor is invalid.",
             "Provide a valid ID, display name, and provider.");
const ErrorCodeDescriptor NotSealed =
    Describe("render.registry.not_sealed", "Renderer backend registry is not sealed.",
             "Seal backend registration before runtime selection.");
const ErrorCodeDescriptor ProviderException =
    Describe("render.registry.provider_exception", "Renderer backend provider threw an exception.",
             "Inspect the provider implementation and diagnostics.", true);
const ErrorCodeDescriptor ProviderReturnedNull =
    Describe("render.registry.provider_returned_null", "Renderer backend provider returned no backend.",
             "Return a valid backend instance from the registered provider.");
const ErrorCodeDescriptor Sealed =
    Describe("render.registry.sealed", "Renderer backend registry is already sealed.",
             "Register all backends before sealing the registry.");
} // namespace Horo::Render::RegistryErrors
