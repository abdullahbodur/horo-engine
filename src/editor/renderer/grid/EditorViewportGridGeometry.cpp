#include "EditorViewportGridGeometry.h"

#include <algorithm>
#include <cmath>

namespace Horo::Editor {
    namespace {
        constexpr float RegularGridTone = 0.158F;
        constexpr float AxisGridTone = 0.30F;
        constexpr std::size_t VerticesPerStroke = 6;
        constexpr std::size_t MaximumHalfLineCount = (ViewportGridGeometry::MaxRegularVertices / (VerticesPerStroke * 2) - 1) / 2;

        [[nodiscard]] float RoundedSpacingAtLeast(const float requested) noexcept {
            const float exponent = std::floor(std::log10(requested));
            const float scale = std::pow(10.0F, exponent);
            const float normalized = requested / scale;
            const float step = normalized <= 1.0F ? 1.0F : normalized <= 2.0F ? 2.0F : normalized <= 5.0F ? 5.0F : 10.0F;
            return step * scale;
        }

        [[nodiscard]] float AdaptiveSpacing(const float visibleHeight, const float viewportHeightPixels,
                                            const float targetSpacingPixels) noexcept {
            return RoundedSpacingAtLeast(visibleHeight * targetSpacingPixels / viewportHeightPixels);
        }

        template <std::size_t Capacity>
        [[nodiscard]] bool AppendLine(std::array<Math::Vec3, Capacity> &positions, std::size_t &count, const Math::Vec3 start,
                                      const Math::Vec3 end) noexcept {
            if (count + 2 > positions.size())
                return false;
            positions[count++] = start;
            positions[count++] = end;
            return true;
        }

        [[nodiscard]] bool IsAxisCoordinate(const float coordinate, const float spacing) noexcept {
            return std::fabs(coordinate) <= spacing * 0.0001F;
        }

        [[nodiscard]] bool AppendStroke(std::array<Math::Vec3, ViewportGridGeometry::MaxRegularVertices> &positions, std::size_t &count,
                                        const Math::Vec3 start, const Math::Vec3 end, const Math::Vec3 halfWidth) noexcept {
            if (count + VerticesPerStroke > positions.size())
                return false;
            const Math::Vec3 startMinimum = start - halfWidth;
            const Math::Vec3 startMaximum = start + halfWidth;
            const Math::Vec3 endMinimum = end - halfWidth;
            const Math::Vec3 endMaximum = end + halfWidth;
            positions[count++] = startMinimum;
            positions[count++] = endMinimum;
            positions[count++] = startMaximum;
            positions[count++] = startMaximum;
            positions[count++] = endMinimum;
            positions[count++] = endMaximum;
            return true;
        }
    }  // namespace

    /** @copydoc ViewportGridLineBatch::IsValid */
    bool ViewportGridLineBatch::IsValid() const noexcept {
        const std::size_t verticesPerPrimitive = topology == ViewportGridPrimitiveTopology::Lines ? 2 : 3;
        if (positions.size() % verticesPerPrimitive != 0 || !Math::IsFinite(color))
            return false;
        return std::ranges::all_of(positions, [](const Math::Vec3 position) {
            return Math::IsFinite(position);
        });
    }

    /** @copydoc ViewportGridGeometry::RegularLines */
    ViewportGridLineBatch ViewportGridGeometry::RegularLines() const noexcept {
        return {
            .positions = std::span<const Math::Vec3>{regularPositions.data(), regularVertexCount},
            .color = {RegularGridTone, RegularGridTone, RegularGridTone},
            .topology = ViewportGridPrimitiveTopology::Triangles,
        };
    }

    /** @copydoc ViewportGridGeometry::Axes */
    ViewportGridLineBatch ViewportGridGeometry::Axes() const noexcept {
        return {
            .positions = std::span<const Math::Vec3>{axisPositions.data(), axisVertexCount},
            .color = {AxisGridTone, AxisGridTone, AxisGridTone},
            .topology = ViewportGridPrimitiveTopology::Lines,
        };
    }

    /** @copydoc ViewportGridGeometry::IsValid */
    bool ViewportGridGeometry::IsValid() const noexcept {
        return regularVertexCount <= regularPositions.size() && axisVertexCount <= axisPositions.size() && std::isfinite(minorSpacing) &&
               minorSpacing > 0.0F && RegularLines().IsValid() && Axes().IsValid();
    }

    /** @copydoc BuildViewportGridGeometry */
    bool BuildViewportGridGeometry(const ViewportGridGeometryRequest &request, ViewportGridGeometry &output) noexcept {
        output = {};
        if (!request.camera.IsValid() || !std::isfinite(request.aspect) || request.aspect <= 0.0F ||
            !std::isfinite(request.viewportHeightPixels) || request.viewportHeightPixels <= 0.0F ||
            !std::isfinite(request.targetMinorSpacingPixels) || request.targetMinorSpacingPixels <= 0.0F ||
            !std::isfinite(request.targetLineWidthPixels) || request.targetLineWidthPixels <= 0.0F) {
            return false;
        }

        const float visibleHeight = request.camera.projection.kind == Render::RenderProjectionKind::Perspective
                                        ? 2.0F * Math::Length(request.camera.position - request.camera.target) *
                                              std::tan(request.camera.projection.verticalFovRadians * 0.5F)
                                        : request.camera.projection.orthographicHeight;
        if (!std::isfinite(visibleHeight) || visibleHeight <= Math::DefaultEpsilon)
            return false;

        const float farHalfHeight = request.camera.projection.kind == Render::RenderProjectionKind::Perspective
                                        ? request.camera.projection.farPlane * std::tan(request.camera.projection.verticalFovRadians * 0.5F)
                                        : request.camera.projection.orthographicHeight * 0.5F;
        const float farHalfWidth = farHalfHeight * request.aspect;
        const float coverageRadius = std::sqrt(request.camera.projection.farPlane * request.camera.projection.farPlane +
                                               farHalfWidth * farHalfWidth + farHalfHeight * farHalfHeight);
        if (!std::isfinite(coverageRadius))
            return false;
        output.minorSpacing = std::max(AdaptiveSpacing(visibleHeight, request.viewportHeightPixels, request.targetMinorSpacingPixels),
                                       RoundedSpacingAtLeast(coverageRadius / static_cast<float>(MaximumHalfLineCount)));
        const std::size_t halfLineCount =
            std::clamp(static_cast<std::size_t>(std::ceil(coverageRadius / output.minorSpacing)) + 1, std::size_t{2}, MaximumHalfLineCount);
        const float centerX = std::round(request.camera.position.x / output.minorSpacing) * output.minorSpacing;
        const float centerZ = std::round(request.camera.position.z / output.minorSpacing) * output.minorSpacing;
        const float halfExtent = static_cast<float>(halfLineCount) * output.minorSpacing;
        const float minimumX = centerX - halfExtent;
        const float maximumX = centerX + halfExtent;
        const float minimumZ = centerZ - halfExtent;
        const float maximumZ = centerZ + halfExtent;
        const float strokeWidth = output.minorSpacing * request.targetLineWidthPixels / request.targetMinorSpacingPixels;
        const float halfStrokeWidth = strokeWidth * 0.5F;

        for (std::int64_t offset = -static_cast<std::int64_t>(halfLineCount); offset <= static_cast<std::int64_t>(halfLineCount);
             ++offset) {
            const float x = centerX + static_cast<float>(offset) * output.minorSpacing;
            const float z = centerZ + static_cast<float>(offset) * output.minorSpacing;

            const auto appendXLine = [&]() {
                if (IsAxisCoordinate(x, output.minorSpacing))
                    return AppendLine(output.axisPositions, output.axisVertexCount, {x, 0.0F, minimumZ}, {x, 0.0F, maximumZ});
                return AppendStroke(output.regularPositions, output.regularVertexCount, {x, 0.0F, minimumZ}, {x, 0.0F, maximumZ},
                                    {halfStrokeWidth, 0.0F, 0.0F});
            };
            if (!appendXLine())
                return false;

            const auto appendZLine = [&]() {
                if (IsAxisCoordinate(z, output.minorSpacing))
                    return AppendLine(output.axisPositions, output.axisVertexCount, {minimumX, 0.0F, z}, {maximumX, 0.0F, z});
                return AppendStroke(output.regularPositions, output.regularVertexCount, {minimumX, 0.0F, z}, {maximumX, 0.0F, z},
                                    {0.0F, 0.0F, halfStrokeWidth});
            };
            if (!appendZLine())
                return false;
        }
        return output.IsValid();
    }
}  // namespace Horo::Editor
