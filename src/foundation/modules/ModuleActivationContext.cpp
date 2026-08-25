#include "Horo/Foundation/ModuleHost.h"
#include "foundation/FoundationErrors.h"

#include <utility>

namespace Horo {
    /** @copydoc ModuleActivationContext::ModuleActivationContext */
    ModuleActivationContext::ModuleActivationContext(ModuleId identifier, const DependencyBindings bindings) noexcept
        : m_module(std::move(identifier)), m_bindings(bindings) {}

    /** @copydoc ModuleActivationContext::Module */
    const ModuleId &ModuleActivationContext::Module() const noexcept {
        return m_module;
    }

    /** @copydoc ModuleActivationContext::Bindings */
    ModuleActivationContext::DependencyBindings ModuleActivationContext::Bindings() const noexcept {
        return m_bindings;
    }

    /** @copydoc ModuleActivationContext::AttachInstance */
    Result<void> ModuleActivationContext::AttachInstance(std::unique_ptr<IModuleInstance> instance) {
        if (instance == nullptr) {
            return Result<void>::Failure(
                MakeError(ModuleDescriptorErrors::InvalidDescriptor, "Module '" + m_module.value + "' attached a null instance."));
        }
        m_instance = std::move(instance);
        return Result<void>::Success();
    }
}  // namespace Horo
