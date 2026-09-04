#include "Horo/Physics/PhysicsErrors.h"
#include "Horo/Physics/PhysicsShapeDescriptor.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>

namespace Horo::Physics {
    namespace {
        /** @brief Requires a stable malformed-dimension diagnostic. */
        void RequireShapeError(const PhysicsShapeDescriptor &shape) {
            const auto result = ValidatePhysicsShapeDescriptor(shape);
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == PhysicsErrors::DescriptorInvalid.code.Value());
        }

        TEST_CASE("Physics analytic shape defaults express normalized SI geometry", "[physics][shape]") {
            REQUIRE(ValidatePhysicsShapeDescriptor(PhysicsBoxShape{}).HasValue());
            REQUIRE(ValidatePhysicsShapeDescriptor(PhysicsSphereShape{}).HasValue());
            REQUIRE(ValidatePhysicsShapeDescriptor(PhysicsCapsuleShape{}).HasValue());
            REQUIRE(ValidatePhysicsShapeDescriptor(PhysicsStaticPlaneShape{}).HasValue());
            REQUIRE(ValidatePhysicsShapeDescriptor(PhysicsStaticPlaneShape{{0, -1, 0}, -2}).HasValue());
        }

        TEST_CASE("Physics analytic dimensions reject zero negative and non-finite values independently", "[physics][shape]") {
            for (const float invalid : {0.0F, -1.0F, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
                RequireShapeError(PhysicsBoxShape{{invalid, 1, 1}});
                RequireShapeError(PhysicsBoxShape{{1, invalid, 1}});
                RequireShapeError(PhysicsBoxShape{{1, 1, invalid}});
                RequireShapeError(PhysicsSphereShape{invalid});
                RequireShapeError(PhysicsCapsuleShape{invalid, 1});
                RequireShapeError(PhysicsCapsuleShape{1, invalid});
            }
        }

        TEST_CASE("Physics plane validation preserves the equation and rejects malformed normals", "[physics][shape]") {
            const auto nan = std::numeric_limits<float>::quiet_NaN();
            const auto infinity = std::numeric_limits<float>::infinity();
            const std::array<PhysicsStaticPlaneShape, 7> invalid{{{{0, 0, 0}, 0},
                                                                  {{0, 2, 0}, 0},
                                                                  {{infinity, 0, 0}, 0},
                                                                  {{0, nan, 0}, 0},
                                                                  {{0, 1, 0}, infinity},
                                                                  {{0, 1, 0}, nan},
                                                                  {{std::numeric_limits<float>::max(), 0, 0}, 0}}};
            for (const auto &plane : invalid)
                RequireShapeError(plane);
            const PhysicsStaticPlaneShape valid{{0, -1, 0}, -3};
            REQUIRE(ValidatePhysicsShapeDescriptor(valid).HasValue());
            REQUIRE(valid.normal.y == -1);
            REQUIRE(valid.signedDistanceMeters == -3);
        }

        TEST_CASE("Physics analytic representation is distinct from motion-specific size admission", "[physics][shape]") {
            const float finite = std::numeric_limits<float>::max();
            REQUIRE(ValidatePhysicsShapeDescriptor(PhysicsBoxShape{{finite, finite, finite}}).HasValue());
            REQUIRE(ValidatePhysicsShapeDescriptor(PhysicsSphereShape{finite}).HasValue());
            REQUIRE(ValidatePhysicsShapeDescriptor(PhysicsCapsuleShape{finite, finite}).HasValue());
        }
    }  // namespace
}  // namespace Horo::Physics
