#include "Horo/Physics/PhysicsErrors.h"
#include "Horo/Physics/PhysicsShapeDescriptor.h"

#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

namespace Horo::Physics {
    namespace {
        /** @brief Requires a stable malformed-dimension diagnostic. */
        void RequireShapeError(const PhysicsShapeDescriptor &shape) {
            const auto result = ValidatePhysicsShapeDescriptor(shape);
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == PhysicsErrors::DescriptorInvalid.code.Value());
        }

        /** @brief Resolves a request and requires one stable Physics error identity. */
        void RequireResolutionError(const PhysicsPrimitiveShapeRequest &request, const ErrorCodeDescriptor &error) {
            const auto result = ResolvePhysicsPrimitiveShape(request);
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == error.code.Value());
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

        TEST_CASE("Physics primitive resolution owns a validated local pose and folds admitted scale", "[physics][shape]") {
            const PhysicsPrimitiveShapeRequest request{PhysicsBoxShape{{1, 2, 3}},
                                                       PhysicsPose{{4, 5, 6}, Math::Quaternion::FromAxisAngle({0, 1, 0}, Math::Pi / 2)},
                                                       PhysicsShapeScale{{2, 3, 4}}};
            const auto result = ResolvePhysicsPrimitiveShape(request);
            REQUIRE(result.HasValue());
            REQUIRE(result.Value().localPose == request.localPose);
            const auto &box = std::get<PhysicsBoxShape>(result.Value().geometry);
            REQUIRE(box.halfExtentsMeters == Math::Vec3{2, 6, 12});
            REQUIRE(std::get<PhysicsBoxShape>(request.geometry).halfExtentsMeters == Math::Vec3{1, 2, 3});
            REQUIRE(request.scale == PhysicsShapeScale{{2, 3, 4}});
        }

        TEST_CASE("Physics sphere and capsule resolution accepts uniform scale only", "[physics][shape]") {
            const auto sphere = ResolvePhysicsPrimitiveShape({PhysicsSphereShape{2}, {}, {{3, 3, 3}}});
            REQUIRE(sphere.HasValue());
            REQUIRE(std::get<PhysicsSphereShape>(sphere.Value().geometry).radiusMeters == 6);

            const auto capsule = ResolvePhysicsPrimitiveShape({PhysicsCapsuleShape{2, 4}, {}, {{0.5F, 0.5F, 0.5F}}});
            REQUIRE(capsule.HasValue());
            REQUIRE(std::get<PhysicsCapsuleShape>(capsule.Value().geometry).radiusMeters == 1);
            REQUIRE(std::get<PhysicsCapsuleShape>(capsule.Value().geometry).cylindricalHalfHeightMeters == 2);

            RequireResolutionError({PhysicsSphereShape{}, {}, {{1, 2, 1}}}, PhysicsErrors::OperationUnsupported);
            RequireResolutionError({PhysicsCapsuleShape{}, {}, {{2, 2, 1}}}, PhysicsErrors::OperationUnsupported);
        }

        TEST_CASE("Physics static plane scale preserves its transformed equation", "[physics][shape]") {
            const float inverseRootTwo = 1.0F / std::sqrt(2.0F);
            const PhysicsPrimitiveShapeRequest request{PhysicsStaticPlaneShape{{inverseRootTwo, inverseRootTwo, 0}, 3},
                                                       PhysicsPose{{1, 2, 3}, {}}, PhysicsShapeScale{{2, 1, 4}}};
            const auto result = ResolvePhysicsPrimitiveShape(request);
            REQUIRE(result.HasValue());
            REQUIRE(result.Value().localPose == request.localPose);
            const auto &plane = std::get<PhysicsStaticPlaneShape>(result.Value().geometry);
            REQUIRE(plane.normal.x == Catch::Approx(1.0F / std::sqrt(5.0F)));
            REQUIRE(plane.normal.y == Catch::Approx(2.0F / std::sqrt(5.0F)));
            REQUIRE(plane.normal.z == 0);
            REQUIRE(plane.signedDistanceMeters == Catch::Approx(6.0F / std::sqrt(2.5F)));
            REQUIRE(std::get<PhysicsStaticPlaneShape>(request.geometry).normal.x == inverseRootTwo);
            REQUIRE(std::get<PhysicsStaticPlaneShape>(request.geometry).signedDistanceMeters == 3);
        }

        TEST_CASE("Physics primitive resolution rejects malformed local poses and authored scales", "[physics][shape]") {
            auto malformedPose = PhysicsPrimitiveShapeRequest{PhysicsBoxShape{}, {}, {}};
            malformedPose.localPose.translation.x = std::numeric_limits<float>::quiet_NaN();
            RequireResolutionError(malformedPose, PhysicsErrors::DescriptorInvalid);

            for (const float invalid : {0.0F, -1.0F, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
                RequireResolutionError({PhysicsBoxShape{}, {}, {{invalid, 1, 1}}}, PhysicsErrors::DescriptorInvalid);
                RequireResolutionError({PhysicsStaticPlaneShape{}, {}, {{1, invalid, 1}}}, PhysicsErrors::DescriptorInvalid);
            }
        }

        TEST_CASE("Physics primitive resolution rejects dimension and plane overflow without mutating source", "[physics][shape]") {
            const float maximum = std::numeric_limits<float>::max();
            const PhysicsPrimitiveShapeRequest boxRequest{PhysicsBoxShape{{maximum, 1, 1}}, {}, {{2, 1, 1}}};
            RequireResolutionError(boxRequest, PhysicsErrors::DescriptorInvalid);
            REQUIRE(std::get<PhysicsBoxShape>(boxRequest.geometry).halfExtentsMeters.x == maximum);

            const PhysicsPrimitiveShapeRequest planeRequest{PhysicsStaticPlaneShape{{1, 0, 0}, maximum}, {}, {{maximum, 1, 1}}};
            RequireResolutionError(planeRequest, PhysicsErrors::DescriptorInvalid);
            REQUIRE(std::get<PhysicsStaticPlaneShape>(planeRequest.geometry).signedDistanceMeters == maximum);
            REQUIRE(planeRequest.scale.factors.x == maximum);
        }
    }  // namespace
}  // namespace Horo::Physics
