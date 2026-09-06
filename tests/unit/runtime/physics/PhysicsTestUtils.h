#pragma once

#include "Horo/Physics/PhysicsWorldSettings.h"

namespace Horo::Physics::Test {
    [[nodiscard]] inline PhysicsWorldSettings SmallWorldSettings() {
        PhysicsWorldSettingsDescriptor descriptor;
        descriptor.world.capacity = {16, 32, 16, 4096};
        descriptor.budgets.maximumContactPairs = 32;
        descriptor.budgets.maximumContactConstraints = 16;
        descriptor.budgets.maximumInFlightPairs = 8;
        descriptor.budgets.scratchBytes = 1024 * 1024;
        return PhysicsWorldSettings::Capture(descriptor).Value();
    }
}  // namespace Horo::Physics::Test
