#include "Horo/Runtime/Scene/PrimitiveMesh.h"

#include <array>
#include <cassert>
#include <cmath>
#include <optional>
#include <thread>

namespace
{
using namespace Horo;
using namespace Horo::Render;
using namespace Horo::Runtime;

void ValidateMesh(const MeshData &mesh)
{
    assert(mesh.IsValid());
    for (const MeshVertex &vertex : mesh.vertices)
    {
        assert(Math::NearlyEqual(Math::Length(vertex.normal), 1.0F, 0.0002F));
        assert(vertex.uv.x >= 0.0F && vertex.uv.x <= 1.0F);
        assert(vertex.uv.y >= 0.0F && vertex.uv.y <= 1.0F);
    }
    for (std::size_t index = 0; index < mesh.indices.size(); index += 3)
    {
        const MeshVertex &a = mesh.vertices[mesh.indices[index]];
        const MeshVertex &b = mesh.vertices[mesh.indices[index + 1]];
        const MeshVertex &c = mesh.vertices[mesh.indices[index + 2]];
        const Math::Vec3 face = Math::Cross(b.position - a.position, c.position - a.position);
        assert(Math::Length(face) > Math::DefaultEpsilon);
        const Math::Vec3 expected = a.normal + b.normal + c.normal;
        assert(Math::Dot(face, expected) > -0.00001F);
    }
}

void EveryBuiltInGeneratesDeterministicallyWithDocumentedBounds()
{
    constexpr std::array types{PrimitiveMeshType::Box,      PrimitiveMeshType::Sphere, PrimitiveMeshType::Capsule,
                               PrimitiveMeshType::Cylinder, PrimitiveMeshType::Cone,   PrimitiveMeshType::Plane,
                               PrimitiveMeshType::Quad};
    for (const PrimitiveMeshType type : types)
    {
        const PrimitiveMeshDescriptor descriptor = PrimitiveMeshDescriptor::Defaults(type);
        const Result<MeshData> first = PrimitiveMeshGenerator::Generate(descriptor);
        const Result<MeshData> second = PrimitiveMeshGenerator::Generate(descriptor);
        assert(first.HasValue() && second.HasValue());
        ValidateMesh(first.Value());
        assert(first.Value().vertices == second.Value().vertices);
        assert(first.Value().indices == second.Value().indices);
        assert(Math::NearlyEqual(first.Value().localBounds.minimum, second.Value().localBounds.minimum));
        assert(Math::NearlyEqual(first.Value().localBounds.maximum, second.Value().localBounds.maximum));
    }

    const auto box = PrimitiveMeshGenerator::Generate(PrimitiveMeshDescriptor::Defaults(PrimitiveMeshType::Box));
    assert(Math::NearlyEqual(box.Value().localBounds.minimum, {-0.5F, -0.5F, -0.5F}));
    assert(Math::NearlyEqual(box.Value().localBounds.maximum, {0.5F, 0.5F, 0.5F}));
    const auto capsule =
        PrimitiveMeshGenerator::Generate(PrimitiveMeshDescriptor::Defaults(PrimitiveMeshType::Capsule));
    assert(Math::NearlyEqual(capsule.Value().localBounds.minimum.y, -1.0F));
    assert(Math::NearlyEqual(capsule.Value().localBounds.maximum.y, 1.0F));
    const auto cone = PrimitiveMeshGenerator::Generate(PrimitiveMeshDescriptor::Defaults(PrimitiveMeshType::Cone));
    assert(Math::NearlyEqual(cone.Value().localBounds.minimum.y, 0.0F));
    assert(Math::NearlyEqual(cone.Value().localBounds.maximum.y, 1.0F));
    const auto plane = PrimitiveMeshGenerator::Generate(PrimitiveMeshDescriptor::Defaults(PrimitiveMeshType::Plane));
    assert(Math::NearlyEqual(plane.Value().localBounds.minimum, {-5.0F, 0.0F, -5.0F}));
    assert(Math::NearlyEqual(plane.Value().localBounds.maximum, {5.0F, 0.0F, 5.0F}));
}

void InvalidRecipesReturnStableTypedErrors()
{
    PrimitiveMeshDescriptor invalid = PrimitiveMeshDescriptor::Defaults(PrimitiveMeshType::Sphere);
    invalid.parameters = SphereMeshParameters{.radius = -1.0F};
    auto result = PrimitiveMeshGenerator::Generate(invalid);
    assert(result.HasError() && result.ErrorValue().domain.Value() == "horo.runtime.scene.primitives");
    assert(result.ErrorValue().code.Value() == "primitive.invalid_radius");

    invalid.parameters = BoxMeshParameters{};
    result = PrimitiveMeshGenerator::Generate(invalid);
    assert(result.HasError() && result.ErrorValue().code.Value() == "primitive.parameter_mismatch");

    invalid = PrimitiveMeshDescriptor::Defaults(PrimitiveMeshType::Capsule);
    invalid.parameters = CapsuleMeshParameters{.radius = 1.0F, .totalHeight = 1.0F};
    result = PrimitiveMeshGenerator::Generate(invalid);
    assert(result.HasError() && result.ErrorValue().code.Value() == "primitive.invalid_capsule_height");

    invalid = PrimitiveMeshDescriptor::Defaults(PrimitiveMeshType::Box);
    invalid.version.value = 2;
    result = PrimitiveMeshGenerator::Generate(invalid);
    assert(result.HasError() && result.ErrorValue().code.Value() == "primitive.unsupported_version");
}

void CacheDeduplicatesAndProtectsActiveLeases()
{
    PrimitiveMeshCache cache{{.maximumItems = 2, .maximumBytes = 1024U * 1024U}};
    const PrimitiveMeshDescriptor box = PrimitiveMeshDescriptor::Defaults(PrimitiveMeshType::Box);
    auto first = cache.Acquire(box);
    auto second = cache.Acquire(box);
    assert(first.HasValue() && second.HasValue());
    assert(first.Value().Id() == second.Value().Id());
    assert(cache.ItemCount() == 1);

    PrimitiveMeshCache constrained{{.maximumItems = 1, .maximumBytes = 1024U * 1024U}};
    {
        auto active = constrained.Acquire(box);
        assert(active.HasValue());
        auto blocked = constrained.Acquire(PrimitiveMeshDescriptor::Defaults(PrimitiveMeshType::Quad));
        assert(blocked.HasError() && blocked.ErrorValue().code.Value() == "primitive.cache_capacity_exceeded");
    }
    auto replacement = constrained.Acquire(PrimitiveMeshDescriptor::Defaults(PrimitiveMeshType::Quad));
    assert(replacement.HasValue() && constrained.ItemCount() == 1);

    PrimitiveMeshCache concurrent;
    std::array<std::optional<PrimitiveMeshLease>, 4> leases;
    std::array<std::thread, 4> workers;
    for (std::size_t index = 0; index < workers.size(); ++index)
    {
        workers[index] = std::thread([&, index] {
            auto acquired = concurrent.Acquire(PrimitiveMeshDescriptor::Defaults(PrimitiveMeshType::Sphere));
            assert(acquired.HasValue());
            leases[index] = std::move(acquired).Value();
        });
    }
    for (std::thread &worker : workers)
        worker.join();
    for (const auto &lease : leases)
        assert(lease->Id() == leases.front()->Id());
    assert(concurrent.ItemCount() == 1);
}
} // namespace

int main()
{
    EveryBuiltInGeneratesDeterministicallyWithDocumentedBounds();
    InvalidRecipesReturnStableTypedErrors();
    CacheDeduplicatesAndProtectsActiveLeases();
    return 0;
}
