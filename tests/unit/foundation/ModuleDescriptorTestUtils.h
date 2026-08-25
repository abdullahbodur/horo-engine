#pragma once

#include "Horo/Foundation/ModuleDescriptor.h"

#include <string>
#include <utility>

namespace Horo::Test {
    [[nodiscard]] inline ModuleDescriptor MakeModule(std::string id, const ModuleContractVersion version = {1, 0, 0}) {
        ModuleDescriptor descriptor;
        descriptor.id = ModuleId{std::move(id)};
        descriptor.version = version;
        return descriptor;
    }
}  // namespace Horo::Test
