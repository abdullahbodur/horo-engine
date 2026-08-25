#include "Horo/Foundation/ModuleHost.h"
#include "foundation/FoundationErrors.h"

#include <algorithm>
#include <set>
#include <string>

namespace Horo {
    /** @copydoc ModuleHost::Register */
    Result<void> ModuleHost::Register(const ModuleDescriptor &descriptor) {
        // Registration is inert: local metadata is checked here, but graph-wide rules
        // (duplicates across the set, missing providers, cycles) stay in ActivateRegistered
        // so that registration never depends on the full set's state.
        std::set<std::string, std::less<>> dependencyIds;
        for (const ModuleDependency &dependency : descriptor.dependencies) {
            if (!dependencyIds.emplace(dependency.module.value).second) {
                return Result<void>::Failure(MakeError(ModuleDescriptorErrors::InvalidDescriptor,
                                                       "Module '" + descriptor.id.value + "' repeats a dependency at registration."));
            }
        }
        for (const ActiveModule &active : m_active) {
            if (active.id == descriptor.id) {
                return Result<void>::Failure(
                    MakeError(ModuleDescriptorErrors::DuplicateModule, "Module '" + descriptor.id.value + "' is already active."));
            }
        }
        if (std::ranges::find_if(m_registered, [&descriptor](const ModuleDescriptor &registered) {
            return registered.id == descriptor.id;
        }) != m_registered.end()) {
            return Result<void>::Failure(
                MakeError(ModuleDescriptorErrors::DuplicateModule, "Module '" + descriptor.id.value + "' is already registered."));
        }
        m_registered.push_back(descriptor);
        return Result<void>::Success();
    }
}  // namespace Horo
