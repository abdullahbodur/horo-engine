#include "Horo/Foundation/ModuleHost.h"

#include "foundation/FoundationErrors.h"

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <utility>

namespace Horo {
    namespace {
        /** @brief Creates a typed composition failure from a stable error descriptor. */
        template <typename ValueT> [[nodiscard]] Result<ValueT> Fail(const ErrorCodeDescriptor &descriptor, std::string message) {
            return Result<ValueT>::Failure(MakeError(descriptor, std::move(message)));
        }

        /** @brief Deactivates one active entry and releases its activation context. */
        void DeactivateOne(ActiveModule &active) noexcept {
            if (active.deactivate != nullptr && active.context != nullptr)
                active.deactivate(*active.context);
            active.context.reset();
        }

        /** @brief Rolls back every activation started after @p base in reverse order. */
        void RollBackTo(std::vector<ActiveModule> &activeModules, const std::size_t base) noexcept {
            while (activeModules.size() > base) {
                DeactivateOne(activeModules.back());
                activeModules.pop_back();
            }
        }
    }  // namespace

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
        if (instance == nullptr)
            return Fail<void>(ModuleDescriptorErrors::InvalidDescriptor, "Module '" + m_module.value + "' attached a null instance.");
        m_instance = std::move(instance);
        return Result<void>::Success();
    }

    /** @copydoc ModuleHost::Register */
    Result<void> ModuleHost::Register(const ModuleDescriptor &descriptor) {
        // Registration is inert: local metadata is checked here, but graph-wide rules
        // (duplicates across the set, missing providers, cycles) stay in ActivateRegistered
        // so that registration never depends on the full set's state.
        std::set<std::string, std::less<>> dependencyIds;
        for (const ModuleDependency &dependency : descriptor.dependencies) {
            if (!dependencyIds.emplace(dependency.module.value).second)
                return Fail<void>(ModuleDescriptorErrors::InvalidDescriptor,
                                  "Module '" + descriptor.id.value + "' repeats a dependency at registration.");
        }
        for (const ActiveModule &active : m_active) {
            if (active.id == descriptor.id)
                return Fail<void>(ModuleDescriptorErrors::DuplicateModule, "Module '" + descriptor.id.value + "' is already active.");
        }
        if (std::ranges::find_if(m_registered, [&descriptor](const ModuleDescriptor &registered) {
            return registered.id == descriptor.id;
        }) != m_registered.end()) {
            return Fail<void>(ModuleDescriptorErrors::DuplicateModule, "Module '" + descriptor.id.value + "' is already registered.");
        }
        m_registered.push_back(descriptor);
        return Result<void>::Success();
    }

    /** @copydoc ModuleHost::ActivateRegistered */
    Result<std::size_t> ModuleHost::ActivateRegistered(const ModuleActivationContext::DependencyBindings bindings) {
        // Graph validation runs before any callback: a rejected set leaves the host untouched.
        const Result<ValidatedModuleGraph> validated = ValidateModuleGraph(m_registered);
        if (validated.HasError())
            return Result<std::size_t>::Failure(validated.ErrorValue());

        // Index descriptors by identity so activation follows the validated ID order.
        std::vector<const ModuleDescriptor *> ordered;
        ordered.reserve(validated.Value().initializationOrder.size());
        for (const ModuleId &id : validated.Value().initializationOrder) {
            const auto found = std::ranges::find_if(m_registered, [&id](const ModuleDescriptor &descriptor) {
                return descriptor.id == id;
            });
            ordered.push_back(&*found);
        }

        // Roll back only modules started by this call; a prior successful activation
        // may still own the front of the active list (incremental compose-then-activate).
        const std::size_t base = m_active.size();
        for (const ModuleDescriptor *descriptor : ordered) {
            auto context = std::make_unique<ModuleActivationContext>(descriptor->id, bindings);
            if (descriptor->lifecycle.activate != nullptr) {
                if (const Result<void> activated = descriptor->lifecycle.activate(*context); activated.HasError()) {
                    RollBackTo(m_active, base);
                    return Fail<std::size_t>(ModuleDescriptorErrors::InvalidDescriptor,
                                             "Activation of module '" + descriptor->id.value + "' failed.");
                }
            }
            ActiveModule active{.id = descriptor->id, .deactivate = descriptor->lifecycle.deactivate, .context = std::move(context)};
            m_active.push_back(std::move(active));
        }
        m_registered.clear();
        return Result<std::size_t>::Success(m_active.size() - base);
    }

    /** @copydoc ModuleHost::DeactivateAll */
    void ModuleHost::DeactivateAll() noexcept {
        RollBackTo(m_active, 0);
        m_registered.clear();
    }

    /** @copydoc ModuleHost::HasActiveModules */
    bool ModuleHost::HasActiveModules() const noexcept {
        return !m_active.empty();
    }
}  // namespace Horo
