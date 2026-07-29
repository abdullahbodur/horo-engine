#include "ObjMeshPreviewProvider.h"

#include "../../../AssetErrors.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace Horo::Assets
{
    namespace
    {
        constexpr std::size_t kHeaderBytes = 48;
        constexpr std::uint32_t kMaximumVertices = 1'000'000;
        constexpr std::uint32_t kMaximumTriangleIndices = 6'000'000;
        constexpr std::uint32_t kMaximumRasterizedTriangles = 100'000;

        struct PreviewVertex
        {
            float x{};
            float y{};
            float z{};
        };

        struct ProjectedVertex
        {
            float x{};
            float y{};
            float depth{};
        };

        [[nodiscard]] bool ReadU32(const std::span<const std::uint8_t> bytes, const std::size_t offset,
                                   std::uint32_t& value)
        {
            if (offset > bytes.size() || bytes.size() - offset < sizeof(std::uint32_t))
                return false;
            value = static_cast<std::uint32_t>(bytes[offset]) |
                (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
                (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
                (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
            return true;
        }

        [[nodiscard]] bool ReadFloat(const std::span<const std::uint8_t> bytes, const std::size_t offset,
                                     float& value)
        {
            std::uint32_t raw = 0;
            if (!ReadU32(bytes, offset, raw))
                return false;
            value = std::bit_cast<float>(raw);
            return std::isfinite(value);
        }

        [[nodiscard]] std::vector<ProjectedVertex> ProjectVertices(
            const std::vector<PreviewVertex>& vertices, const std::uint32_t width,
            const std::uint32_t height, const BuiltinMeshPreviewView view)
        {
            std::vector<ProjectedVertex> projected;
            projected.reserve(vertices.size());
            float minX = std::numeric_limits<float>::max();
            float minY = std::numeric_limits<float>::max();
            float maxX = std::numeric_limits<float>::lowest();
            float maxY = std::numeric_limits<float>::lowest();
            for (const PreviewVertex& vertex : vertices)
            {
                const ProjectedVertex point = view == BuiltinMeshPreviewView::NegativeX
                                                  ? ProjectedVertex{
                                                      .x = -vertex.z,
                                                      .y = -vertex.y,
                                                      .depth = vertex.x,
                                                  }
                                                  : ProjectedVertex{
                                                      .x = vertex.x * 0.832F - vertex.z * 0.555F,
                                                      .y = vertex.x * 0.213F - vertex.y * 0.923F +
                                                      vertex.z * 0.320F,
                                                      .depth = -(vertex.x * 0.512F + vertex.y * 0.384F +
                                                          vertex.z * 0.768F),
                                                  };
                minX = std::min(minX, point.x);
                minY = std::min(minY, point.y);
                maxX = std::max(maxX, point.x);
                maxY = std::max(maxY, point.y);
                projected.push_back(point);
            }

            const float extentX = std::max(maxX - minX, 0.0001F);
            const float extentY = std::max(maxY - minY, 0.0001F);
            const float padding = std::max(6.0F, std::min(width, height) * 0.08F);
            const float scale = std::min((static_cast<float>(width) - padding * 2.0F) / extentX,
                                         (static_cast<float>(height) - padding * 2.0F) / extentY);
            const float centerX = static_cast<float>(width) * 0.5F;
            const float centerY = static_cast<float>(height) * 0.5F;
            for (ProjectedVertex& point : projected)
            {
                point.x = centerX + (point.x - (minX + maxX) * 0.5F) * scale;
                point.y = centerY + (point.y - (minY + maxY) * 0.5F) * scale;
            }
            return projected;
        }

        [[nodiscard]] float Edge(const ProjectedVertex& start, const ProjectedVertex& end, const float x,
                                 const float y)
        {
            return (x - start.x) * (end.y - start.y) - (y - start.y) * (end.x - start.x);
        }

        [[nodiscard]] std::uint8_t TriangleShade(const PreviewVertex& a, const PreviewVertex& b,
                                                 const PreviewVertex& c)
        {
            const float abX = b.x - a.x;
            const float abY = b.y - a.y;
            const float abZ = b.z - a.z;
            const float acX = c.x - a.x;
            const float acY = c.y - a.y;
            const float acZ = c.z - a.z;
            const float normalX = abY * acZ - abZ * acY;
            const float normalY = abZ * acX - abX * acZ;
            const float normalZ = abX * acY - abY * acX;
            const float length = std::sqrt(normalX * normalX + normalY * normalY + normalZ * normalZ);
            if (length <= 0.000001F)
                return 176;

            const float light = std::abs((normalX * 0.35F + normalY * 0.82F + normalZ * 0.45F) / length);
            return static_cast<std::uint8_t>(std::clamp(150.0F + light * 90.0F, 0.0F, 255.0F));
        }

        void RasterizeTriangle(AssetPreviewImage& image, std::vector<float>& depthBuffer,
                               const ProjectedVertex& a, const ProjectedVertex& b, const ProjectedVertex& c,
                               const std::uint8_t shade)
        {
            const float area = Edge(a, b, c.x, c.y);
            if (std::abs(area) <= 0.0001F)
                return;

            const int minX = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
            const int minY = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
            const int maxX = std::min(static_cast<int>(image.width) - 1,
                                      static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
            const int maxY = std::min(static_cast<int>(image.height) - 1,
                                      static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
            const bool positive = area > 0.0F;
            for (int y = minY; y <= maxY; ++y)
            {
                for (int x = minX; x <= maxX; ++x)
                {
                    const float sampleX = static_cast<float>(x) + 0.5F;
                    const float sampleY = static_cast<float>(y) + 0.5F;
                    const float edgeA = Edge(b, c, sampleX, sampleY);
                    const float edgeB = Edge(c, a, sampleX, sampleY);
                    const float edgeC = Edge(a, b, sampleX, sampleY);
                    if (positive
                            ? (edgeA < 0.0F || edgeB < 0.0F || edgeC < 0.0F)
                            : (edgeA > 0.0F || edgeB > 0.0F || edgeC > 0.0F))
                    {
                        continue;
                    }

                    const float inverseArea = 1.0F / area;
                    const float depth = (edgeA * a.depth + edgeB * b.depth + edgeC * c.depth) * inverseArea;
                    const std::size_t pixel =
                        static_cast<std::size_t>(y) * image.width + static_cast<std::size_t>(x);
                    if (depth >= depthBuffer[pixel])
                        continue;
                    depthBuffer[pixel] = depth;
                    const std::size_t color = pixel * 4U;
                    image.pixels[color] = shade;
                    image.pixels[color + 1U] =
                        static_cast<std::uint8_t>(std::min<unsigned>(shade + 5U, 255U));
                    image.pixels[color + 2U] =
                        static_cast<std::uint8_t>(std::min<unsigned>(shade + 2U, 255U));
                    image.pixels[color + 3U] = 255U;
                }
            }
        }

        class BuiltinMeshPreviewProvider final : public IAssetPreviewProvider
        {
        public:
            explicit BuiltinMeshPreviewProvider(const BuiltinMeshPreviewView view) : view_(view)
            {
            }

            /** @copydoc IAssetPreviewProvider::GeneratePreview */
            [[nodiscard]] Result<AssetPreviewImage> GeneratePreview(
                const AssetPreviewInput& input, const CancellationToken& cancellation) const override
            {
                if (cancellation.IsCancellationRequested())
                    return Result<AssetPreviewImage>::Failure(MakeError(ImportErrors::ImportCancelled));
                if (input.width < 16 || input.height < 16 || input.width > 512 || input.height > 512)
                    return Result<AssetPreviewImage>::Failure(MakeError(AssetErrors::IndexMalformed));

                std::uint32_t schema = 0;
                std::uint32_t vertexCount = 0;
                std::uint32_t faceCount = 0;
                std::uint32_t positionBytes = 0;
                std::uint32_t texcoordBytes = 0;
                std::uint32_t normalBytes = 0;
                if (input.editorPayload.size() < kHeaderBytes || !ReadU32(input.editorPayload, 0, schema) ||
                    !ReadU32(input.editorPayload, 4, vertexCount) ||
                    !ReadU32(input.editorPayload, 8, faceCount) ||
                    !ReadU32(input.editorPayload, 36, positionBytes) ||
                    !ReadU32(input.editorPayload, 40, texcoordBytes) ||
                    !ReadU32(input.editorPayload, 44, normalBytes) || (schema != 1 && schema != 2) ||
                    vertexCount == 0 || vertexCount > kMaximumVertices ||
                    positionBytes != static_cast<std::uint64_t>(vertexCount) * 3U * sizeof(float) ||
                    kHeaderBytes + static_cast<std::uint64_t>(positionBytes) + texcoordBytes + normalBytes >
                    input.editorPayload.size())
                {
                    return Result<AssetPreviewImage>::Failure(MakeError(AssetErrors::IndexMalformed));
                }
                static_cast<void>(faceCount);

                std::vector<PreviewVertex> vertices;
                vertices.reserve(vertexCount);
                for (std::uint32_t index = 0; index < vertexCount; ++index)
                {
                    const std::size_t offset =
                        kHeaderBytes + static_cast<std::size_t>(index) * 3U * sizeof(float);
                    PreviewVertex vertex;
                    if (!ReadFloat(input.editorPayload, offset, vertex.x) ||
                        !ReadFloat(input.editorPayload, offset + 4U, vertex.y) ||
                        !ReadFloat(input.editorPayload, offset + 8U, vertex.z))
                    {
                        return Result<AssetPreviewImage>::Failure(MakeError(AssetErrors::IndexMalformed));
                    }
                    vertices.push_back(vertex);
                }

                AssetPreviewImage image{
                    .width = input.width,
                    .height = input.height,
                    .pixels = std::vector<std::uint8_t>(
                        static_cast<std::size_t>(input.width) * input.height * 4U, 0),
                };
                if (schema != 2)
                    return Result<AssetPreviewImage>::Failure(MakeError(AssetErrors::IndexMalformed));

                const std::size_t indexHeader =
                    kHeaderBytes + static_cast<std::size_t>(positionBytes) + texcoordBytes + normalBytes;
                std::uint32_t indexCount = 0;
                if (!ReadU32(input.editorPayload, indexHeader, indexCount) || indexCount == 0 ||
                    indexCount % 3U != 0 || indexCount > kMaximumTriangleIndices ||
                    indexHeader + 4U + static_cast<std::uint64_t>(indexCount) * 4U >
                    input.editorPayload.size())
                {
                    return Result<AssetPreviewImage>::Failure(MakeError(AssetErrors::IndexMalformed));
                }

                const std::vector<ProjectedVertex> projected =
                    ProjectVertices(vertices, input.width, input.height, view_);
                std::vector<float> depthBuffer(static_cast<std::size_t>(input.width) * input.height,
                                               std::numeric_limits<float>::infinity());
                const std::uint32_t triangleCount = indexCount / 3U;
                const std::uint32_t rasterizedTriangleCount =
                    std::min(triangleCount, kMaximumRasterizedTriangles);
                for (std::uint32_t triangle = 0; triangle < rasterizedTriangleCount; ++triangle)
                {
                    if ((triangle & 0x0fffU) == 0U && cancellation.IsCancellationRequested())
                        return Result<AssetPreviewImage>::Failure(MakeError(ImportErrors::ImportCancelled));

                    const std::uint32_t sourceTriangle = static_cast<std::uint32_t>(
                        static_cast<std::uint64_t>(triangle) * triangleCount / rasterizedTriangleCount);
                    const std::uint32_t index = sourceTriangle * 3U;
                    std::uint32_t a = 0;
                    std::uint32_t b = 0;
                    std::uint32_t c = 0;
                    if (!ReadU32(input.editorPayload, indexHeader + 4U + index * 4U, a) ||
                        !ReadU32(input.editorPayload, indexHeader + 4U + (index + 1U) * 4U, b) ||
                        !ReadU32(input.editorPayload, indexHeader + 4U + (index + 2U) * 4U, c) ||
                        a >= projected.size() || b >= projected.size() || c >= projected.size())
                    {
                        return Result<AssetPreviewImage>::Failure(MakeError(AssetErrors::IndexMalformed));
                    }
                    RasterizeTriangle(image, depthBuffer, projected[a], projected[b], projected[c],
                                      TriangleShade(vertices[a], vertices[b], vertices[c]));
                }
                return Result<AssetPreviewImage>::Success(std::move(image));
            }

        private:
            BuiltinMeshPreviewView view_;
        };
    } // namespace

    /** @copydoc CreateBuiltinMeshPreviewProvider */
    std::shared_ptr<const IAssetPreviewProvider> CreateBuiltinMeshPreviewProvider(
        const BuiltinMeshPreviewView view)
    {
        return std::make_shared<const BuiltinMeshPreviewProvider>(view);
    }
} // namespace Horo::Assets
