#include "LightVisualizerGeometry.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Horo::Editor {
    namespace {
        constexpr std::size_t CircleSegments = 32;

        [[nodiscard]] bool AppendLine(LightVisualizerGeometry &geometry, const Math::Vec3 start, const Math::Vec3 end) noexcept {
            if (geometry.vertexCount + 2 > geometry.positions.size())
                return false;
            geometry.positions[geometry.vertexCount++] = start;
            geometry.positions[geometry.vertexCount++] = end;
            return true;
        }

        [[nodiscard]] Math::Vec3 SafeDisplayColor(const Math::Vec3 color) noexcept {
            return {
                std::clamp(color.x, 0.35F, 1.0F),
                std::clamp(color.y, 0.35F, 1.0F),
                std::clamp(color.z, 0.20F, 1.0F),
            };
        }

        [[nodiscard]] bool BuildBasis(const Math::Vec3 direction, Math::Vec3 &right, Math::Vec3 &up) noexcept {
            const Math::Vec3 reference = std::fabs(direction.y) < 0.95F ? Math::Vec3{0.0F, 1.0F, 0.0F} : Math::Vec3{1.0F, 0.0F, 0.0F};
            const Result<Math::Vec3> resolvedRight = Math::TryNormalize(Math::Cross(direction, reference));
            if (resolvedRight.HasError())
                return false;
            right = resolvedRight.Value();
            up = Math::Normalize(Math::Cross(right, direction));
            return up != Math::Vec3{};
        }

        [[nodiscard]] bool AppendCircle(LightVisualizerGeometry &geometry, const Math::Vec3 center, const Math::Vec3 axisU,
                                        const Math::Vec3 axisV, const float radius) noexcept {
            if (!std::isfinite(radius) || radius <= 0.0F)
                return false;
            for (std::size_t segment = 0; segment < CircleSegments; ++segment) {
                const float startAngle =
                    2.0F * std::numbers::pi_v<float> * static_cast<float>(segment) / static_cast<float>(CircleSegments);
                const float endAngle =
                    2.0F * std::numbers::pi_v<float> * static_cast<float>(segment + 1) / static_cast<float>(CircleSegments);
                const Math::Vec3 start = center + (axisU * std::cos(startAngle) + axisV * std::sin(startAngle)) * radius;
                const Math::Vec3 end = center + (axisU * std::cos(endAngle) + axisV * std::sin(endAngle)) * radius;
                if (!AppendLine(geometry, start, end))
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool AppendArrow(LightVisualizerGeometry &geometry, const Math::Vec3 start, const Math::Vec3 direction,
                                       const float length, const Math::Vec3 side) noexcept {
            const Math::Vec3 end = start + direction * length;
            const float headLength = length * 0.18F;
            const float headWidth = length * 0.08F;
            return AppendLine(geometry, start, end) && AppendLine(geometry, end, end - direction * headLength + side * headWidth) &&
                   AppendLine(geometry, end, end - direction * headLength - side * headWidth);
        }

        [[nodiscard]] bool BuildDirectional(const LightVisualizerGeometryRequest &request, LightVisualizerGeometry &output) noexcept {
            Math::Vec3 right;
            Math::Vec3 up;
            if (!BuildBasis(request.light.direction, right, up))
                return false;
            const float cameraDistance = Math::Length(request.camera.position - request.camera.target);
            const float length = std::clamp(cameraDistance * 0.18F, 1.0F, 8.0F);
            const float spacing = length * 0.28F;
            constexpr std::array offsets{
                std::array{0.0F, 0.0F}, std::array{1.0F, 0.0F}, std::array{-1.0F, 0.0F}, std::array{0.0F, 1.0F}, std::array{0.0F, -1.0F},
            };
            for (const auto &offset : offsets) {
                const Math::Vec3 start = request.light.position + right * (offset[0] * spacing) + up * (offset[1] * spacing) -
                                         request.light.direction * (length * 0.5F);
                if (!AppendArrow(output, start, request.light.direction, length, right))
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool BuildPoint(const LightVisualizerGeometryRequest &request, LightVisualizerGeometry &output) noexcept {
            constexpr float minimumDisplayRange = 0.25F;
            const float displayRange = std::max(request.light.range, minimumDisplayRange);
            const Math::Vec3 x{1.0F, 0.0F, 0.0F};
            const Math::Vec3 y{0.0F, 1.0F, 0.0F};
            const Math::Vec3 z{0.0F, 0.0F, 1.0F};
            if (!AppendCircle(output, request.light.position, x, y, displayRange) ||
                !AppendCircle(output, request.light.position, x, z, displayRange) ||
                !AppendCircle(output, request.light.position, y, z, displayRange)) {
                return false;
            }
            const float rayLength = displayRange * 0.35F;
            for (const Math::Vec3 direction : std::array{x, x * -1.0F, y, y * -1.0F, z, z * -1.0F}) {
                if (!AppendArrow(output, request.light.position, direction, rayLength, std::fabs(direction.y) < 0.9F ? y : x)) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool BuildSpot(const LightVisualizerGeometryRequest &request, LightVisualizerGeometry &output) noexcept {
            constexpr float minimumDisplayRange = 0.25F;
            constexpr float maximumDisplayAngle = std::numbers::pi_v<float> * 0.5F - 0.01F;
            const float displayRange = std::max(request.light.range, minimumDisplayRange);
            Math::Vec3 right;
            Math::Vec3 up;
            if (!BuildBasis(request.light.direction, right, up))
                return false;
            const float outerAngle = std::min(std::acos(std::clamp(request.light.outerConeCosine, -1.0F, 1.0F)), maximumDisplayAngle);
            const float innerAngle = std::min(std::acos(std::clamp(request.light.innerConeCosine, -1.0F, 1.0F)), maximumDisplayAngle);
            const float outerRadius = std::tan(outerAngle) * displayRange;
            const float innerRadius = std::tan(innerAngle) * displayRange;
            const Math::Vec3 endCenter = request.light.position + request.light.direction * displayRange;
            if (!AppendCircle(output, endCenter, right, up, outerRadius))
                return false;
            if (innerRadius > Math::DefaultEpsilon && !AppendCircle(output, endCenter, right, up, innerRadius))
                return false;

            constexpr std::size_t RayCount = 12;
            for (std::size_t ray = 0; ray < RayCount; ++ray) {
                const float angle = 2.0F * std::numbers::pi_v<float> * static_cast<float>(ray) / static_cast<float>(RayCount);
                if (const Math::Vec3 end = endCenter + (right * std::cos(angle) + up * std::sin(angle)) * outerRadius;
                    !AppendLine(output, request.light.position, end))
                    return false;
            }
            return AppendArrow(output, request.light.position, request.light.direction, displayRange, right);
        }
    }  // namespace

    std::span<const Math::Vec3> LightVisualizerGeometry::Lines() const noexcept {
        return {positions.data(), vertexCount};
    }

    bool LightVisualizerGeometry::IsValid() const noexcept {
        return vertexCount > 0 && vertexCount <= positions.size() && vertexCount % 2 == 0 && Math::IsFinite(color) &&
               std::ranges::all_of(Lines(), [](const Math::Vec3 position) {
            return Math::IsFinite(position);
        });
    }

    bool BuildLightVisualizerGeometry(const LightVisualizerGeometryRequest &request, LightVisualizerGeometry &output) noexcept {
        output = {};
        if (!request.camera.IsValid() || !request.light.IsValid())
            return false;
        output.color = SafeDisplayColor(request.light.color);

        bool built = false;
        switch (request.light.kind) {
            case Render::RenderLightKind::Directional:
                built = BuildDirectional(request, output);
                break;
            case Render::RenderLightKind::Point:
                built = BuildPoint(request, output);
                break;
            case Render::RenderLightKind::Spot:
                built = BuildSpot(request, output);
                break;
        }
        return built && output.IsValid();
    }
}  // namespace Horo::Editor
