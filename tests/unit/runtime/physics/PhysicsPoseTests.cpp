#include "Horo/Physics/PhysicsErrors.h"
#include "Horo/Physics/PhysicsPose.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <type_traits>

namespace Horo::Physics {
    namespace {
        TEST_CASE("Physics poses preserve finite translation and either quaternion sign", "[physics][pose]") {
            REQUIRE(ValidatePhysicsPose({}).HasValue());
            const PhysicsPose positive{{1, -2, 3}, {0, 0, 0, 1}};
            const PhysicsPose negative{{1, -2, 3}, {0, 0, 0, -1}};
            REQUIRE(ValidatePhysicsPose(positive).HasValue());
            REQUIRE(ValidatePhysicsPose(negative).HasValue());
            REQUIRE(positive != negative);
            REQUIRE(positive.rotation.w == 1);
            REQUIRE(negative.rotation.w == -1);
            REQUIRE(positive.translation == Math::Vec3{1, -2, 3});
            static_assert(std::is_trivially_copyable_v<PhysicsPose>);
        }

        TEST_CASE("Physics pose validation rejects non-finite translation and rotation", "[physics][pose]") {
            using Mutation = void (*)(PhysicsPose &);
            const std::array<Mutation, 4> mutations{
                [](auto &pose) {
                pose.translation.x = std::numeric_limits<float>::infinity();
            },
                [](auto &pose) {
                pose.translation.z = std::numeric_limits<float>::quiet_NaN();
            },
                [](auto &pose) {
                pose.rotation.y = std::numeric_limits<float>::infinity();
            },
                [](auto &pose) {
                pose.rotation.w = std::numeric_limits<float>::quiet_NaN();
            },
            };
            for (const auto mutate : mutations) {
                PhysicsPose pose;
                mutate(pose);
                const auto result = ValidatePhysicsPose(pose);
                REQUIRE(result.HasError());
                REQUIRE(result.ErrorValue().code.Value() == PhysicsErrors::DescriptorInvalid.code.Value());
            }
        }

        TEST_CASE("Physics pose rotation validation checks bounded squared norm without overflow or repair", "[physics][pose]") {
            for (const auto rotation :
                 {Math::Quaternion{0, 0, 0, 0}, Math::Quaternion{0, 0, 0, 2}, Math::Quaternion{std::numeric_limits<float>::max(), 0, 0, 1},
                  Math::Quaternion{0, 0, 0, 1.000001F}}) {
                const PhysicsPose pose{{}, rotation};
                const auto result = ValidatePhysicsPose(pose);
                REQUIRE(result.HasError());
                REQUIRE(result.ErrorValue().code.Value() == PhysicsErrors::DescriptorInvalid.code.Value());
                REQUIRE(pose.rotation == rotation);
            }
            const PhysicsPose nearUnit{{}, {0, 0, 0, std::nextafter(1.0F, 2.0F)}};
            REQUIRE(ValidatePhysicsPose(nearUnit).HasValue());
            REQUIRE(nearUnit.rotation.w > 1.0F);
            const PhysicsPose rotated{{}, Math::Quaternion::FromAxisAngle({0, 1, 0}, Math::Pi / 2)};
            REQUIRE(ValidatePhysicsPose(rotated).HasValue());
        }
    }  // namespace
}  // namespace Horo::Physics
