#include "Horo/Physics/PhysicsCookedShapeDescriptor.h"
#include "Horo/Physics/PhysicsErrors.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <type_traits>

namespace Horo::Physics {
    namespace {
        /** @brief Builds explicit lookup metadata without claiming any artifact exists. */
        PhysicsCookedShapeDescriptor MakeCookedShape() {
            std::array<std::uint8_t, 16> bytes{};
            bytes[0] = 1;
            return {.asset = Assets::AssetId::FromBytes(bytes),
                    .subresource = PhysicsShapeSubresourceId::FromValue(7),
                    .kind = PhysicsCookedShapeKind::ConvexHull,
                    .cacheKeyDigest = Sha256Digest{},
                    .payloadDigest = Sha256Digest{},
                    .target = PhysicsShapeCookTargetDigest{}};
        }

        TEST_CASE("Cooked shape references preserve each explicit geometry kind without loading content", "[physics][shape][cooked]") {
            auto descriptor = MakeCookedShape();
            for (const auto kind : {PhysicsCookedShapeKind::ConvexHull, PhysicsCookedShapeKind::TriangleMesh,
                                    PhysicsCookedShapeKind::HeightField, PhysicsCookedShapeKind::Compound}) {
                descriptor.kind = kind;
                REQUIRE(ValidatePhysicsCookedShapeDescriptor(descriptor, {}).HasValue());
                REQUIRE(descriptor.kind == kind);
            }
            REQUIRE(descriptor.subresource.Value() == 7);
            static_assert(!std::is_convertible_v<std::uint64_t, PhysicsShapeSubresourceId>);
            static_assert(!std::is_convertible_v<Assets::AssetId, PhysicsShapeSubresourceId>);
        }

        TEST_CASE("Cooked shape references reject each missing identity or digest independently", "[physics][shape][cooked]") {
            using Mutation = void (*)(PhysicsCookedShapeDescriptor &);
            const std::array<Mutation, 5> mutations{
                [](auto &value) {
                value.asset = {};
            },
                [](auto &value) {
                value.subresource = {};
            },
                [](auto &value) {
                value.cacheKeyDigest.reset();
            },
                [](auto &value) {
                value.payloadDigest.reset();
            },
                [](auto &value) {
                value.target.reset();
            },
            };
            for (const auto mutate : mutations) {
                auto descriptor = MakeCookedShape();
                mutate(descriptor);
                const auto result = ValidatePhysicsCookedShapeDescriptor(descriptor, {});
                REQUIRE(result.HasError());
                REQUIRE(result.ErrorValue().code.Value() == PhysicsErrors::DescriptorInvalid.code.Value());
            }
            REQUIRE_FALSE(PhysicsShapeSubresourceId{}.IsValid());
            REQUIRE_FALSE(PhysicsShapeSubresourceId::FromValue(0).IsValid());
            REQUIRE(PhysicsShapeSubresourceId::FromValue(8) != PhysicsShapeSubresourceId::FromValue(7));
        }

        TEST_CASE("Cooked shape reference target matching compares every byte without changing input", "[physics][shape][cooked]") {
            const auto descriptor = MakeCookedShape();
            for (std::size_t index = 0; index < Sha256Digest{}.bytes.size(); ++index) {
                PhysicsShapeCookTargetDigest target;
                target.digest.bytes[index] = 1;
                const auto result = ValidatePhysicsCookedShapeDescriptor(descriptor, target);
                REQUIRE(result.HasError());
                REQUIRE(result.ErrorValue().code.Value() == PhysicsErrors::ProfileUnsupported.code.Value());
                REQUIRE(descriptor.target->digest.bytes[index] == 0);
            }
        }

        TEST_CASE("Cooked shape reference validation distinguishes unknown kinds and explicit zero digests", "[physics][shape][cooked]") {
            auto descriptor = MakeCookedShape();
            REQUIRE(ValidatePhysicsCookedShapeDescriptor(descriptor, {}).HasValue());
            descriptor.kind = static_cast<PhysicsCookedShapeKind>(255);
            const auto result = ValidatePhysicsCookedShapeDescriptor(descriptor, {});
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == PhysicsErrors::OperationUnsupported.code.Value());
            descriptor.kind = PhysicsCookedShapeKind::TriangleMesh;
            descriptor.cacheKeyDigest->bytes[4] = 9;
            descriptor.payloadDigest->bytes[4] = 3;
            descriptor.target->digest.bytes[4] = 5;
            REQUIRE(ValidatePhysicsCookedShapeDescriptor(descriptor, *descriptor.target).HasValue());
            REQUIRE(descriptor.cacheKeyDigest->bytes[4] == 9);
            REQUIRE(descriptor.payloadDigest->bytes[4] == 3);
        }
    }  // namespace
}  // namespace Horo::Physics
