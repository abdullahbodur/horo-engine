#include "Horo/Runtime/Scene/PrimitiveMesh.h"
#include "PrimitiveMeshErrors.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <list>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace Horo::Runtime
{
using namespace Horo::Render;
namespace
{
constexpr std::uint32_t kMaximumSegments = 4096;
constexpr std::size_t kMaximumVertices = 4U * 1024U * 1024U;
constexpr std::size_t kMaximumIndices = 24U * 1024U * 1024U;

[[nodiscard]] Error MakePrimitiveError(const ErrorCodeDescriptor &descriptor, std::string message)
{
    return MakeError(descriptor, std::move(message));
}

[[nodiscard]] bool PositiveFinite(const float value) noexcept
{
    return std::isfinite(value) && value > 0.0F;
}

[[nodiscard]] Math::Aabb ComputeBounds(const std::vector<MeshVertex> &vertices) noexcept
{
    Math::Vec3 minimum = vertices.front().position;
    Math::Vec3 maximum = minimum;
    for (const MeshVertex &vertex : vertices)
    {
        minimum.x = std::min(minimum.x, vertex.position.x);
        minimum.y = std::min(minimum.y, vertex.position.y);
        minimum.z = std::min(minimum.z, vertex.position.z);
        maximum.x = std::max(maximum.x, vertex.position.x);
        maximum.y = std::max(maximum.y, vertex.position.y);
        maximum.z = std::max(maximum.z, vertex.position.z);
    }
    return {minimum, maximum};
}

void AddQuad(MeshData &mesh, const Math::Vec3 center, const Math::Vec3 axisU, const Math::Vec3 axisV,
             const Math::Vec3 normal)
{
    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({center - axisU - axisV, normal, {0.0F, 0.0F}});
    mesh.vertices.push_back({center + axisU - axisV, normal, {1.0F, 0.0F}});
    mesh.vertices.push_back({center + axisU + axisV, normal, {1.0F, 1.0F}});
    mesh.vertices.push_back({center - axisU + axisV, normal, {0.0F, 1.0F}});
    mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

[[nodiscard]] MeshData GenerateBox(const BoxMeshParameters &parameters)
{
    MeshData mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);
    const Math::Vec3 half = parameters.size * 0.5F;
    AddQuad(mesh, {half.x, 0, 0}, {0, 0, -half.z}, {0, half.y, 0}, {1, 0, 0});
    AddQuad(mesh, {-half.x, 0, 0}, {0, 0, half.z}, {0, half.y, 0}, {-1, 0, 0});
    AddQuad(mesh, {0, half.y, 0}, {half.x, 0, 0}, {0, 0, -half.z}, {0, 1, 0});
    AddQuad(mesh, {0, -half.y, 0}, {half.x, 0, 0}, {0, 0, half.z}, {0, -1, 0});
    AddQuad(mesh, {0, 0, half.z}, {half.x, 0, 0}, {0, half.y, 0}, {0, 0, 1});
    AddQuad(mesh, {0, 0, -half.z}, {-half.x, 0, 0}, {0, half.y, 0}, {0, 0, -1});
    mesh.localBounds = ComputeBounds(mesh.vertices);
    return mesh;
}

[[nodiscard]] MeshData GeneratePlanar(const Math::Vec2 size, const bool quad)
{
    MeshData mesh;
    mesh.vertices.reserve(4);
    mesh.indices.reserve(6);
    if (quad)
        AddQuad(mesh, {}, {size.x * 0.5F, 0, 0}, {0, size.y * 0.5F, 0}, {0, 0, 1});
    else
        AddQuad(mesh, {}, {size.x * 0.5F, 0, 0}, {0, 0, -size.y * 0.5F}, {0, 1, 0});
    mesh.localBounds = ComputeBounds(mesh.vertices);
    return mesh;
}

[[nodiscard]] MeshData GenerateSphere(const SphereMeshParameters &parameters)
{
    MeshData mesh;
    const std::uint32_t ringCount = parameters.stacks - 1;
    mesh.vertices.reserve(2 + static_cast<std::size_t>(ringCount) * (parameters.slices + 1));
    mesh.indices.reserve(static_cast<std::size_t>(parameters.slices) * (parameters.stacks - 1) * 6);
    mesh.vertices.push_back({{0, parameters.radius, 0}, {0, 1, 0}, {0.5F, 0}});
    for (std::uint32_t stack = 1; stack < parameters.stacks; ++stack)
    {
        const float v = static_cast<float>(stack) / static_cast<float>(parameters.stacks);
        const float theta = Math::Pi * v;
        const float y = std::cos(theta);
        const float radial = std::sin(theta);
        for (std::uint32_t slice = 0; slice <= parameters.slices; ++slice)
        {
            const float u = static_cast<float>(slice) / static_cast<float>(parameters.slices);
            const float phi = 2.0F * Math::Pi * u;
            const Math::Vec3 normal{radial * std::cos(phi), y, radial * std::sin(phi)};
            mesh.vertices.push_back({normal * parameters.radius, normal, {u, v}});
        }
    }
    const std::uint32_t bottom = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({{0, -parameters.radius, 0}, {0, -1, 0}, {0.5F, 1}});
    const std::uint32_t stride = parameters.slices + 1;
    for (std::uint32_t slice = 0; slice < parameters.slices; ++slice)
        mesh.indices.insert(mesh.indices.end(), {0, 1 + slice + 1, 1 + slice});
    for (std::uint32_t ring = 0; ring + 1 < ringCount; ++ring)
    {
        const std::uint32_t a = 1 + ring * stride;
        const std::uint32_t b = a + stride;
        for (std::uint32_t slice = 0; slice < parameters.slices; ++slice)
            mesh.indices.insert(mesh.indices.end(),
                                {a + slice, a + slice + 1, b + slice + 1, a + slice, b + slice + 1, b + slice});
    }
    const std::uint32_t lastRing = 1 + (ringCount - 1) * stride;
    for (std::uint32_t slice = 0; slice < parameters.slices; ++slice)
        mesh.indices.insert(mesh.indices.end(), {lastRing + slice, lastRing + slice + 1, bottom});
    mesh.localBounds = ComputeBounds(mesh.vertices);
    return mesh;
}

void AddDisc(MeshData &mesh, const float y, const float radius, const std::uint32_t segments, const bool top)
{
    const Math::Vec3 normal = top ? Math::Vec3{0, 1, 0} : Math::Vec3{0, -1, 0};
    const std::uint32_t center = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({{0, y, 0}, normal, {0.5F, 0.5F}});
    const std::uint32_t ring = static_cast<std::uint32_t>(mesh.vertices.size());
    for (std::uint32_t slice = 0; slice <= segments; ++slice)
    {
        const float phi = 2.0F * Math::Pi * static_cast<float>(slice) / static_cast<float>(segments);
        const float x = radius * std::cos(phi);
        const float z = radius * std::sin(phi);
        mesh.vertices.push_back({{x, y, z}, normal, {0.5F + x / (2 * radius), 0.5F + z / (2 * radius)}});
    }
    for (std::uint32_t slice = 0; slice < segments; ++slice)
    {
        if (top)
            mesh.indices.insert(mesh.indices.end(), {center, ring + slice + 1, ring + slice});
        else
            mesh.indices.insert(mesh.indices.end(), {center, ring + slice, ring + slice + 1});
    }
}

[[nodiscard]] MeshData GenerateCylinder(const CylinderMeshParameters &parameters)
{
    MeshData mesh;
    const float half = parameters.height * 0.5F;
    for (std::uint32_t slice = 0; slice <= parameters.radialSegments; ++slice)
    {
        const float u = static_cast<float>(slice) / static_cast<float>(parameters.radialSegments);
        const float phi = 2.0F * Math::Pi * u;
        const Math::Vec3 normal{std::cos(phi), 0, std::sin(phi)};
        mesh.vertices.push_back({{normal.x * parameters.radius, -half, normal.z * parameters.radius}, normal, {u, 0}});
        mesh.vertices.push_back({{normal.x * parameters.radius, half, normal.z * parameters.radius}, normal, {u, 1}});
    }
    for (std::uint32_t slice = 0; slice < parameters.radialSegments; ++slice)
    {
        const std::uint32_t base = slice * 2;
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 3, base, base + 3, base + 2});
    }
    AddDisc(mesh, half, parameters.radius, parameters.radialSegments, true);
    AddDisc(mesh, -half, parameters.radius, parameters.radialSegments, false);
    mesh.localBounds = ComputeBounds(mesh.vertices);
    return mesh;
}

[[nodiscard]] MeshData GenerateCone(const ConeMeshParameters &parameters)
{
    MeshData mesh;
    for (std::uint32_t slice = 0; slice <= parameters.radialSegments; ++slice)
    {
        const float u = static_cast<float>(slice) / static_cast<float>(parameters.radialSegments);
        const float phi = 2.0F * Math::Pi * u;
        const Math::Vec3 normal = Math::Normalize(
            Math::Vec3{parameters.height * std::cos(phi), parameters.radius, parameters.height * std::sin(phi)});
        mesh.vertices.push_back(
            {{parameters.radius * std::cos(phi), 0, parameters.radius * std::sin(phi)}, normal, {u, 0}});
        mesh.vertices.push_back({{0, parameters.height, 0}, normal, {u, 1}});
    }
    for (std::uint32_t slice = 0; slice < parameters.radialSegments; ++slice)
    {
        const std::uint32_t base = slice * 2;
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2});
    }
    AddDisc(mesh, 0, parameters.radius, parameters.radialSegments, false);
    mesh.localBounds = ComputeBounds(mesh.vertices);
    return mesh;
}

[[nodiscard]] MeshData GenerateCapsule(const CapsuleMeshParameters &parameters)
{
    MeshData mesh;
    const float bodyHalf = (parameters.totalHeight - 2.0F * parameters.radius) * 0.5F;
    struct Ring
    {
        float y;
        float radial;
        float normalY;
        float normalRadial;
        float v;
    };
    std::vector<Ring> rings;
    rings.reserve(parameters.hemisphereRings * 2 + 1);
    for (std::uint32_t index = 1; index <= parameters.hemisphereRings; ++index)
    {
        const float t = static_cast<float>(index) / static_cast<float>(parameters.hemisphereRings);
        const float angle = -Math::Pi * 0.5F + t * Math::Pi * 0.5F;
        rings.push_back({-bodyHalf + std::sin(angle) * parameters.radius, std::cos(angle) * parameters.radius,
                         std::sin(angle), std::cos(angle),
                         (rings.size() + 1.0F) / (2 * parameters.hemisphereRings + 2.0F)});
    }
    rings.push_back(
        {bodyHalf, parameters.radius, 0, 1, (rings.size() + 1.0F) / (2 * parameters.hemisphereRings + 2.0F)});
    for (std::uint32_t index = 1; index < parameters.hemisphereRings; ++index)
    {
        const float t = static_cast<float>(index) / static_cast<float>(parameters.hemisphereRings);
        const float angle = t * Math::Pi * 0.5F;
        rings.push_back({bodyHalf + std::sin(angle) * parameters.radius, std::cos(angle) * parameters.radius,
                         std::sin(angle), std::cos(angle),
                         (rings.size() + 1.0F) / (2 * parameters.hemisphereRings + 2.0F)});
    }
    mesh.vertices.push_back({{0, -parameters.totalHeight * 0.5F, 0}, {0, -1, 0}, {0.5F, 0}});
    const std::uint32_t stride = parameters.radialSegments + 1;
    for (const Ring &ring : rings)
    {
        for (std::uint32_t slice = 0; slice <= parameters.radialSegments; ++slice)
        {
            const float u = static_cast<float>(slice) / static_cast<float>(parameters.radialSegments);
            const float phi = 2.0F * Math::Pi * u;
            const Math::Vec3 radial{std::cos(phi), 0, std::sin(phi)};
            mesh.vertices.push_back({{radial.x * ring.radial, ring.y, radial.z * ring.radial},
                                     {radial.x * ring.normalRadial, ring.normalY, radial.z * ring.normalRadial},
                                     {u, ring.v}});
        }
    }
    const std::uint32_t top = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({{0, parameters.totalHeight * 0.5F, 0}, {0, 1, 0}, {0.5F, 1}});
    for (std::uint32_t slice = 0; slice < parameters.radialSegments; ++slice)
        mesh.indices.insert(mesh.indices.end(), {0, 1 + slice, 1 + slice + 1});
    for (std::uint32_t ring = 0; ring + 1 < rings.size(); ++ring)
    {
        const std::uint32_t a = 1 + ring * stride;
        const std::uint32_t b = a + stride;
        for (std::uint32_t slice = 0; slice < parameters.radialSegments; ++slice)
            mesh.indices.insert(mesh.indices.end(),
                                {a + slice, b + slice, b + slice + 1, a + slice, b + slice + 1, a + slice + 1});
    }
    const std::uint32_t last = 1 + static_cast<std::uint32_t>(rings.size() - 1) * stride;
    for (std::uint32_t slice = 0; slice < parameters.radialSegments; ++slice)
        mesh.indices.insert(mesh.indices.end(), {last + slice, top, last + slice + 1});
    mesh.localBounds = ComputeBounds(mesh.vertices);
    return mesh;
}

[[nodiscard]] std::size_t HashFloat(const float value) noexcept
{
    return std::hash<std::uint32_t>{}(std::bit_cast<std::uint32_t>(value == 0.0F ? 0.0F : value));
}

void HashCombine(std::size_t &seed, const std::size_t value) noexcept
{
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
}

struct DescriptorHash
{
    [[nodiscard]] std::size_t operator()(const PrimitiveMeshDescriptor &descriptor) const noexcept
    {
        std::size_t hash = static_cast<std::size_t>(descriptor.type);
        HashCombine(hash, descriptor.version.value);
        std::visit(
            [&](const auto &parameters) {
                using T = std::decay_t<decltype(parameters)>;
                HashCombine(hash, descriptor.parameters.index());
                if constexpr (std::is_same_v<T, BoxMeshParameters>)
                {
                    HashCombine(hash, HashFloat(parameters.size.x));
                    HashCombine(hash, HashFloat(parameters.size.y));
                    HashCombine(hash, HashFloat(parameters.size.z));
                }
                else if constexpr (std::is_same_v<T, SphereMeshParameters>)
                {
                    HashCombine(hash, HashFloat(parameters.radius));
                    HashCombine(hash, parameters.slices);
                    HashCombine(hash, parameters.stacks);
                }
                else if constexpr (std::is_same_v<T, CapsuleMeshParameters>)
                {
                    HashCombine(hash, HashFloat(parameters.radius));
                    HashCombine(hash, HashFloat(parameters.totalHeight));
                    HashCombine(hash, parameters.radialSegments);
                    HashCombine(hash, parameters.hemisphereRings);
                }
                else if constexpr (std::is_same_v<T, CylinderMeshParameters> || std::is_same_v<T, ConeMeshParameters>)
                {
                    HashCombine(hash, HashFloat(parameters.radius));
                    HashCombine(hash, HashFloat(parameters.height));
                    HashCombine(hash, parameters.radialSegments);
                }
                else
                {
                    HashCombine(hash, HashFloat(parameters.size.x));
                    HashCombine(hash, HashFloat(parameters.size.y));
                }
            },
            descriptor.parameters);
        return hash;
    }
};
} // namespace

/** @copydoc PrimitiveMeshDescriptor::Defaults */
PrimitiveMeshDescriptor PrimitiveMeshDescriptor::Defaults(const PrimitiveMeshType type) noexcept
{
    PrimitiveMeshDescriptor descriptor{.type = type};
    switch (type)
    {
    case PrimitiveMeshType::Box:
        descriptor.parameters = BoxMeshParameters{};
        break;
    case PrimitiveMeshType::Sphere:
        descriptor.parameters = SphereMeshParameters{};
        break;
    case PrimitiveMeshType::Capsule:
        descriptor.parameters = CapsuleMeshParameters{};
        break;
    case PrimitiveMeshType::Cylinder:
        descriptor.parameters = CylinderMeshParameters{};
        break;
    case PrimitiveMeshType::Cone:
        descriptor.parameters = ConeMeshParameters{};
        break;
    case PrimitiveMeshType::Plane:
        descriptor.parameters = PlaneMeshParameters{};
        break;
    case PrimitiveMeshType::Quad:
        descriptor.parameters = QuadMeshParameters{};
        break;
    }
    return descriptor;
}

/** @copydoc PrimitiveMeshGenerator::Generate */
Result<MeshData> PrimitiveMeshGenerator::Generate(const PrimitiveMeshDescriptor &descriptor)
{
    if (descriptor.version.value != 1)
        return Result<MeshData>::Failure(
            MakePrimitiveError(PrimitiveErrors::UnsupportedVersion, "Primitive mesh version is unsupported."));
    MeshData mesh;
    const auto mismatch = [&] {
        return Result<MeshData>::Failure(
            MakePrimitiveError(PrimitiveErrors::ParameterMismatch, "Primitive type and parameter payload do not match."));
    };
    switch (descriptor.type)
    {
    case PrimitiveMeshType::Box: {
        const auto *p = std::get_if<BoxMeshParameters>(&descriptor.parameters);
        if (!p)
            return mismatch();
        if (!PositiveFinite(p->size.x) || !PositiveFinite(p->size.y) || !PositiveFinite(p->size.z))
            return Result<MeshData>::Failure(
                MakePrimitiveError(PrimitiveErrors::InvalidSize, "Box size must be finite and positive."));
        mesh = GenerateBox(*p);
        break;
    }
    case PrimitiveMeshType::Sphere: {
        const auto *p = std::get_if<SphereMeshParameters>(&descriptor.parameters);
        if (!p)
            return mismatch();
        if (!PositiveFinite(p->radius))
            return Result<MeshData>::Failure(
                MakePrimitiveError(PrimitiveErrors::InvalidRadius, "Sphere radius must be finite and positive."));
        if (p->slices < 3 || p->stacks < 2)
            return Result<MeshData>::Failure(MakePrimitiveError(PrimitiveErrors::InvalidTessellation,
                                                                "Sphere tessellation is below the supported minimum."));
        if (p->slices > kMaximumSegments || p->stacks > kMaximumSegments)
            return Result<MeshData>::Failure(MakePrimitiveError(
                PrimitiveErrors::CapacityExceeded, "Sphere tessellation exceeds the generation capacity."));
        mesh = GenerateSphere(*p);
        break;
    }
    case PrimitiveMeshType::Capsule: {
        const auto *p = std::get_if<CapsuleMeshParameters>(&descriptor.parameters);
        if (!p)
            return mismatch();
        if (!PositiveFinite(p->radius) || !PositiveFinite(p->totalHeight))
            return Result<MeshData>::Failure(
                MakePrimitiveError(PrimitiveErrors::InvalidDimensions, "Capsule dimensions must be finite and positive."));
        if (p->totalHeight < 2.0F * p->radius)
            return Result<MeshData>::Failure(MakePrimitiveError(
                PrimitiveErrors::InvalidCapsuleHeight, "Capsule total height cannot be smaller than its diameter."));
        if (p->radialSegments < 3 || p->hemisphereRings < 1)
            return Result<MeshData>::Failure(MakePrimitiveError(
                PrimitiveErrors::InvalidTessellation, "Capsule tessellation is below the supported minimum."));
        if (p->radialSegments > kMaximumSegments || p->hemisphereRings > kMaximumSegments)
            return Result<MeshData>::Failure(MakePrimitiveError(
                PrimitiveErrors::CapacityExceeded, "Capsule tessellation exceeds the generation capacity."));
        mesh = GenerateCapsule(*p);
        break;
    }
    case PrimitiveMeshType::Cylinder: {
        const auto *p = std::get_if<CylinderMeshParameters>(&descriptor.parameters);
        if (!p)
            return mismatch();
        if (!PositiveFinite(p->radius) || !PositiveFinite(p->height))
            return Result<MeshData>::Failure(
                MakePrimitiveError(PrimitiveErrors::InvalidDimensions, "Cylinder dimensions must be finite and positive."));
        if (p->radialSegments < 3)
            return Result<MeshData>::Failure(MakePrimitiveError(
                PrimitiveErrors::InvalidTessellation, "Cylinder tessellation is below the supported minimum."));
        if (p->radialSegments > kMaximumSegments)
            return Result<MeshData>::Failure(MakePrimitiveError(
                PrimitiveErrors::CapacityExceeded, "Cylinder tessellation exceeds the generation capacity."));
        mesh = GenerateCylinder(*p);
        break;
    }
    case PrimitiveMeshType::Cone: {
        const auto *p = std::get_if<ConeMeshParameters>(&descriptor.parameters);
        if (!p)
            return mismatch();
        if (!PositiveFinite(p->radius) || !PositiveFinite(p->height))
            return Result<MeshData>::Failure(
                MakePrimitiveError(PrimitiveErrors::InvalidDimensions, "Cone dimensions must be finite and positive."));
        if (p->radialSegments < 3)
            return Result<MeshData>::Failure(MakePrimitiveError(PrimitiveErrors::InvalidTessellation,
                                                                "Cone tessellation is below the supported minimum."));
        if (p->radialSegments > kMaximumSegments)
            return Result<MeshData>::Failure(MakePrimitiveError(PrimitiveErrors::CapacityExceeded,
                                                                "Cone tessellation exceeds the generation capacity."));
        mesh = GenerateCone(*p);
        break;
    }
    case PrimitiveMeshType::Plane: {
        const auto *p = std::get_if<PlaneMeshParameters>(&descriptor.parameters);
        if (!p)
            return mismatch();
        if (!PositiveFinite(p->size.x) || !PositiveFinite(p->size.y))
            return Result<MeshData>::Failure(
                MakePrimitiveError(PrimitiveErrors::InvalidSize, "Plane size must be finite and positive."));
        mesh = GeneratePlanar(p->size, false);
        break;
    }
    case PrimitiveMeshType::Quad: {
        const auto *p = std::get_if<QuadMeshParameters>(&descriptor.parameters);
        if (!p)
            return mismatch();
        if (!PositiveFinite(p->size.x) || !PositiveFinite(p->size.y))
            return Result<MeshData>::Failure(
                MakePrimitiveError(PrimitiveErrors::InvalidSize, "Quad size must be finite and positive."));
        mesh = GeneratePlanar(p->size, true);
        break;
    }
    }
    if (mesh.vertices.size() > kMaximumVertices || mesh.indices.size() > kMaximumIndices)
        return Result<MeshData>::Failure(
            MakePrimitiveError(PrimitiveErrors::CapacityExceeded, "Generated mesh exceeds the supported capacity."));
    if (!mesh.IsValid())
        return Result<MeshData>::Failure(
            MakePrimitiveError(PrimitiveErrors::InvalidGeneratedMesh, "Primitive generation produced invalid mesh data."));
    return Result<MeshData>::Success(std::move(mesh));
}

struct PrimitiveMeshCache::Impl
{
    struct Entry
    {
        PrimitiveMeshDescriptor descriptor;
        MeshResourceId id;
        std::shared_ptr<const MeshData> data;
        std::uint64_t lastUse;
    };
    PrimitiveMeshCacheLimits limits;
    mutable std::mutex mutex;
    std::list<Entry> entries;
    std::unordered_map<PrimitiveMeshDescriptor, std::list<Entry>::iterator, DescriptorHash> byDescriptor;
    std::size_t bytes{0};
    std::uint64_t nextId{1};
    std::uint64_t useClock{0};
};

PrimitiveMeshCache::PrimitiveMeshCache(const PrimitiveMeshCacheLimits limits) : m_impl(std::make_unique<Impl>())
{
    m_impl->limits = limits;
}
PrimitiveMeshCache::~PrimitiveMeshCache() = default;
PrimitiveMeshCache::PrimitiveMeshCache(PrimitiveMeshCache &&) noexcept = default;
PrimitiveMeshCache &PrimitiveMeshCache::operator=(PrimitiveMeshCache &&) noexcept = default;

/** @copydoc PrimitiveMeshCache::Acquire */
Result<PrimitiveMeshLease> PrimitiveMeshCache::Acquire(const PrimitiveMeshDescriptor &descriptor)
{
    std::scoped_lock lock{m_impl->mutex};
    if (const auto found = m_impl->byDescriptor.find(descriptor); found != m_impl->byDescriptor.end())
    {
        found->second->lastUse = ++m_impl->useClock;
        return Result<PrimitiveMeshLease>::Success(PrimitiveMeshLease{found->second->id, found->second->data});
    }
    Result<MeshData> generated = PrimitiveMeshGenerator::Generate(descriptor);
    if (generated.HasError())
        return Result<PrimitiveMeshLease>::Failure(generated.ErrorValue());
    auto data = std::make_shared<const MeshData>(std::move(generated).Value());
    const std::size_t bytes = data->ByteSize();
    if (m_impl->limits.maximumItems == 0 || bytes > m_impl->limits.maximumBytes)
        return Result<PrimitiveMeshLease>::Failure(MakePrimitiveError(
            PrimitiveErrors::CacheCapacityExceeded, "Mesh cannot fit within the configured cache budget."));
    while (m_impl->entries.size() >= m_impl->limits.maximumItems || m_impl->bytes + bytes > m_impl->limits.maximumBytes)
    {
        auto victim = m_impl->entries.end();
        for (auto candidate = m_impl->entries.begin(); candidate != m_impl->entries.end(); ++candidate)
            if (candidate->data.use_count() == 1 &&
                (victim == m_impl->entries.end() || candidate->lastUse < victim->lastUse))
                victim = candidate;
        if (victim == m_impl->entries.end())
            return Result<PrimitiveMeshLease>::Failure(
                MakePrimitiveError(PrimitiveErrors::CacheCapacityExceeded, "Active mesh leases prevent cache eviction."));
        m_impl->bytes -= victim->data->ByteSize();
        m_impl->byDescriptor.erase(victim->descriptor);
        m_impl->entries.erase(victim);
    }
    const MeshResourceId id{m_impl->nextId++};
    m_impl->entries.push_back({descriptor, id, data, ++m_impl->useClock});
    auto inserted = std::prev(m_impl->entries.end());
    m_impl->byDescriptor.emplace(descriptor, inserted);
    m_impl->bytes += bytes;
    return Result<PrimitiveMeshLease>::Success(PrimitiveMeshLease{id, std::move(data)});
}

std::size_t PrimitiveMeshCache::ItemCount() const noexcept
{
    std::scoped_lock lock{m_impl->mutex};
    return m_impl->entries.size();
}
std::size_t PrimitiveMeshCache::ByteCount() const noexcept
{
    std::scoped_lock lock{m_impl->mutex};
    return m_impl->bytes;
}
} // namespace Horo::Runtime
