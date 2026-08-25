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
        if (StateOf(descriptor.id).has_value()) {
            return Result<void>::Failure(
                MakeError(ModuleDescriptorErrors::DuplicateModule, "Module '" + descriptor.id.value + "' already has a host lifetime."));
        }
        m_registered.push_back(descriptor);
        m_states.push_back(ModuleStateRecord{.id = descriptor.id});
        return Result<void>::Success();
    }
}  // namespace Horo
