#include "Horo/Runtime/Render/RenderBackendRegistry.h"

#include <algorithm>
#include <utility>

namespace Horo::Render
{
    namespace
    {
        /** @brief Creates one registry-domain typed error. */
        [[nodiscard]] Error MakeRegistryError(std::string code, std::string message)
        {
            return Error{
                ErrorCode{std::move(code)}, ErrorDomainId{"horo.render"}, ErrorSeverity::Error, std::move(message), {}
            };
        }
    } // namespace

    /** @copydoc RenderBackendRegistry::Register */
    Result<void> RenderBackendRegistry::Register(RenderBackendDescriptor descriptor)
    {
        if (sealed_)
        {
            return Result<void>::Failure(
                MakeRegistryError("render.registry.sealed", "Renderer backend registry is already sealed."));
        }

        if (!descriptor.id.IsValid() || descriptor.displayName.empty() || !descriptor.provider)
        {
            return Result<void>::Failure(MakeRegistryError(
                "render.registry.invalid_descriptor",
                "Renderer backend descriptor requires an ID, name, and provider."));
        }

        const auto duplicate = std::ranges::find_if(
            descriptors_, [&](const RenderBackendDescriptor& registered) { return registered.id == descriptor.id; });
        if (duplicate != descriptors_.end())
        {
            return Result<void>::Failure(
                MakeRegistryError("render.registry.duplicate_backend",
                                  "Renderer backend ID is already registered: " + descriptor.id.Value()));
        }

        descriptors_.push_back(std::move(descriptor));
        return Result<void>::Success();
    }

    /** @copydoc RenderBackendRegistry::Seal */
    Result<void> RenderBackendRegistry::Seal() noexcept
    {
        sealed_ = true;
        return Result<void>::Success();
    }

    /** @copydoc RenderBackendRegistry::Create */
    Result<std::unique_ptr<IRenderBackend>> RenderBackendRegistry::Create(const RenderBackendId& id) const
    {
        if (!sealed_)
        {
            return Result<std::unique_ptr<IRenderBackend>>::Failure(MakeRegistryError(
                "render.registry.not_sealed", "Renderer backend registry must be sealed before creation."));
        }

        const auto descriptor = std::ranges::find_if(
            descriptors_, [&](const RenderBackendDescriptor& registered) { return registered.id == id; });
        if (descriptor == descriptors_.end())
        {
            return Result<std::unique_ptr<IRenderBackend>>::Failure(MakeRegistryError(
                "render.registry.backend_not_found", "Renderer backend is not compiled into this host: " + id.Value()));
        }

        Result<std::unique_ptr<IRenderBackend>> backend = [&descriptor]()
        {
            try
            {
                return descriptor->provider->Create();
            }
            catch (...)
            {
                return Result<std::unique_ptr<IRenderBackend>>::Failure(MakeRegistryError(
                    "render.registry.provider_exception", "The renderer backend provider threw an exception."));
            }
        }();
        if (backend.HasError())
        {
            return backend;
        }
        if (!backend.Value())
        {
            return Result<std::unique_ptr<IRenderBackend>>::Failure(MakeRegistryError(
                "render.registry.provider_returned_null",
                "The renderer backend provider returned no backend instance."));
        }
        return backend;
    }

    /** @copydoc RenderBackendRegistry::Descriptors */
    std::span<const RenderBackendDescriptor> RenderBackendRegistry::Descriptors() const noexcept
    {
        return descriptors_;
    }
} // namespace Horo::Render
