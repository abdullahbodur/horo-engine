#pragma once

#include "Horo/Foundation/ErrorCode.h"
#include "Horo/Physics/PhysicsWorldSettings.h"

#include <catch2/catch_test_macros.hpp>

namespace Horo::Physics::Test {
    template <typename ResultType> inline void RequireError(const ResultType &result, const ErrorCodeDescriptor &expected) {
        REQUIRE(result.HasError());
        REQUIRE(result.ErrorValue().code.Value() == expected.code.Value());
    }

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
