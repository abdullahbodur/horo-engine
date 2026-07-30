#include "EditorViewportGridGeometry.h"

#include <algorithm>
#include <cmath>

namespace Horo::Editor {
    namespace {
        constexpr float MinorGridTone = 0.105F;
        constexpr float MajorGridTone = 0.175F;
        constexpr float AxisGridTone = 0.30F;
        constexpr std::size_t MaximumHalfLineCount = 60;

        [[nodiscard]] float AdaptiveSpacing(const float visibleHeight, const float viewportHeightPixels,
                                            const float targetSpacingPixels) noexcept {
            const float requested = visibleHeight * targetSpacingPixels / viewportHeightPixels;
            const float exponent = std::floor(std::log10(requested));
            const float scale = std::pow(10.0F, exponent);
            const float normalized = requested / scale;
            const float step = normalized <= 1.0F ? 1.0F : normalized <= 2.0F ? 2.0F : normalized <= 5.0F ? 5.0F : 10.0F;
            return step * scale;
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

        [[nodiscard]] bool IsMajorCoordinate(const float coordinate, const float majorSpacing) noexcept {
            return std::fabs(coordinate - std::round(coordinate / majorSpacing) * majorSpacing) <= majorSpacing * 0.0001F;
        }

        [[nodiscard]] bool IsAxisCoordinate(const float coordinate, const float spacing) noexcept {
            return std::fabs(coordinate) <= spacing * 0.0001F;
        }
    }  // namespace

    /** @copydoc ViewportGridLineBatch::IsValid */
    bool ViewportGridLineBatch::IsValid() const noexcept {
        if (positions.size() % 2 != 0 || !Math::IsFinite(color))
            return false;
        return std::ranges::all_of(positions, [](const Math::Vec3 position) {
            return Math::IsFinite(position);
        });
    }

    /** @copydoc ViewportGridGeometry::MinorLines */
    ViewportGridLineBatch ViewportGridGeometry::MinorLines() const noexcept {
        return {
            .positions = std::span<const Math::Vec3>{minorPositions.data(), minorVertexCount},
            .color = {MinorGridTone, MinorGridTone, MinorGridTone},
        };
    }

    /** @copydoc ViewportGridGeometry::MajorLines */
    ViewportGridLineBatch ViewportGridGeometry::MajorLines() const noexcept {
        return {
            .positions = std::span<const Math::Vec3>{majorPositions.data(), majorVertexCount},
            .color = {MajorGridTone, MajorGridTone, MajorGridTone},
        };
    }

    /** @copydoc ViewportGridGeometry::Axes */
    ViewportGridLineBatch ViewportGridGeometry::Axes() const noexcept {
        return {
            .positions = std::span<const Math::Vec3>{axisPositions.data(), axisVertexCount},
            .color = {AxisGridTone, AxisGridTone, AxisGridTone},
        };
    }

    /** @copydoc ViewportGridGeometry::IsValid */
    bool ViewportGridGeometry::IsValid() const noexcept {
        return minorVertexCount <= minorPositions.size() && majorVertexCount <= majorPositions.size() &&
               axisVertexCount <= axisPositions.size() && std::isfinite(minorSpacing) && minorSpacing > 0.0F &&
               std::isfinite(majorSpacing) && majorSpacing >= minorSpacing && MinorLines().IsValid() && MajorLines().IsValid() &&
               Axes().IsValid();
    }

    /** @copydoc BuildViewportGridGeometry */
    bool BuildViewportGridGeometry(const ViewportGridGeometryRequest &request, ViewportGridGeometry &output) noexcept {
        output = {};
        if (!request.camera.IsValid() || !std::isfinite(request.aspect) || request.aspect <= 0.0F ||
            !std::isfinite(request.viewportHeightPixels) || request.viewportHeightPixels <= 0.0F ||
            !std::isfinite(request.targetMinorSpacingPixels) || request.targetMinorSpacingPixels <= 0.0F) {
            return false;
        }

        const float visibleHeight = request.camera.projection.kind == Render::RenderProjectionKind::Perspective
                                        ? 2.0F * Math::Length(request.camera.position - request.camera.target) *
                                              std::tan(request.camera.projection.verticalFovRadians * 0.5F)
                                        : request.camera.projection.orthographicHeight;
        if (!std::isfinite(visibleHeight) || visibleHeight <= Math::DefaultEpsilon)
            return false;

        output.minorSpacing = AdaptiveSpacing(visibleHeight, request.viewportHeightPixels, request.targetMinorSpacingPixels);
        output.majorSpacing = output.minorSpacing * 10.0F;
        const float visibleWidth = visibleHeight * request.aspect;
        const std::size_t halfLineCount =
            std::clamp(static_cast<std::size_t>(std::ceil(std::max(visibleHeight, visibleWidth) / output.minorSpacing * 0.75F)) + 2,
                       std::size_t{2}, MaximumHalfLineCount);
        const float centerX = std::round(request.camera.target.x / output.minorSpacing) * output.minorSpacing;
        const float centerZ = std::round(request.camera.target.z / output.minorSpacing) * output.minorSpacing;
        const float halfExtent = static_cast<float>(halfLineCount) * output.minorSpacing;
        const float minimumX = centerX - halfExtent;
        const float maximumX = centerX + halfExtent;
        const float minimumZ = centerZ - halfExtent;
        const float maximumZ = centerZ + halfExtent;

        for (std::int64_t offset = -static_cast<std::int64_t>(halfLineCount); offset <= static_cast<std::int64_t>(halfLineCount);
             ++offset) {
            const float x = centerX + static_cast<float>(offset) * output.minorSpacing;
            const float z = centerZ + static_cast<float>(offset) * output.minorSpacing;

            const auto appendXLine = [&]() {
                if (IsAxisCoordinate(x, output.minorSpacing))
                    return AppendLine(output.axisPositions, output.axisVertexCount, {x, 0.0F, minimumZ}, {x, 0.0F, maximumZ});
                if (IsMajorCoordinate(x, output.majorSpacing))
                    return AppendLine(output.majorPositions, output.majorVertexCount, {x, 0.0F, minimumZ}, {x, 0.0F, maximumZ});
                return AppendLine(output.minorPositions, output.minorVertexCount, {x, 0.0F, minimumZ}, {x, 0.0F, maximumZ});
            };
            if (!appendXLine())
                return false;

            const auto appendZLine = [&]() {
                if (IsAxisCoordinate(z, output.minorSpacing))
                    return AppendLine(output.axisPositions, output.axisVertexCount, {minimumX, 0.0F, z}, {maximumX, 0.0F, z});
                if (IsMajorCoordinate(z, output.majorSpacing))
                    return AppendLine(output.majorPositions, output.majorVertexCount, {minimumX, 0.0F, z}, {maximumX, 0.0F, z});
                return AppendLine(output.minorPositions, output.minorVertexCount, {minimumX, 0.0F, z}, {maximumX, 0.0F, z});
            };
            if (!appendZLine())
                return false;
        }
        return output.IsValid();
    }
}  // namespace Horo::Editor
