#include "GameplayIdentityValidation.h"
#include "SystemRegistryDetail.h"

#include <algorithm>

namespace Horo::Gameplay::Detail {
    bool HasValidSystemIdentity(const GameplaySystemDescriptor &descriptor, const std::string_view moduleId) noexcept {
        return descriptor.id.IsValid() && descriptor.id.Value().size() > moduleId.size() + 1 &&
               descriptor.id.Value().starts_with(moduleId) && descriptor.id.Value()[moduleId.size()] == '.';
    }

    bool HasValidSystemAccess(const GameplaySystemDescriptor &descriptor) {
        if (ContainsInvalidOrDuplicateIds<ComponentTypeId>(descriptor.access.reads) ||
            ContainsInvalidOrDuplicateIds<ComponentTypeId>(descriptor.access.writes))
            return false;
        return std::ranges::none_of(descriptor.access.reads, [&descriptor](const ComponentTypeId &read) {
            return std::ranges::find(descriptor.access.writes, read) != descriptor.access.writes.end();
        });
    }
}  // namespace Horo::Gameplay::Detail
