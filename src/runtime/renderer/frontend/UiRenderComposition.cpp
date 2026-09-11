#include "Horo/Runtime/Render/UiRenderComposition.h"

#include "Horo/Runtime/Ui/UiErrors.h"

#include <array>

namespace Horo::Render {
    namespace {
        template <typename T = void> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] constexpr bool IsKnown(const UiRenderCompositionSpace value) noexcept {
            return value >= UiRenderCompositionSpace::World && value <= UiRenderCompositionSpace::Screen;
        }

        [[nodiscard]] constexpr bool IsKnown(const UiRenderCompositionPoint value) noexcept {
            return value >= UiRenderCompositionPoint::SceneBeforePostProcess && value <= UiRenderCompositionPoint::DisplayOverlay;
        }

        [[nodiscard]] constexpr bool IsKnown(const UiRenderPresentationBand value) noexcept {
            return value >= UiRenderPresentationBand::World && value <= UiRenderPresentationBand::Debug;
        }

        [[nodiscard]] constexpr bool IsDepthFormat(const RenderTextureFormat format) noexcept {
            return format == RenderTextureFormat::Depth16Unorm || format == RenderTextureFormat::Depth24Stencil8 ||
                   format == RenderTextureFormat::Depth32Float || format == RenderTextureFormat::Depth32FloatStencil8;
        }

        [[nodiscard]] bool MatchesOutput(const RenderTextureDescriptor &texture, const FramebufferExtent extent) noexcept {
            return texture.IsValid() && texture.dimension == RenderTextureDimension::TwoD && texture.extent == extent;
        }

        [[nodiscard]] bool ValidColorTarget(const UiRenderCompositionTarget &target, const FramebufferExtent extent,
                                            const bool requiresSampling) noexcept {
            if (!target.resource.IsValid() || !MatchesOutput(target.texture, extent) || IsDepthFormat(target.texture.format) ||
                !HasTextureUsage(target.texture.usage, RenderTextureUsage::RenderAttachment))
                return false;
            return !requiresSampling || HasTextureUsage(target.texture.usage, RenderTextureUsage::Sampled);
        }

        [[nodiscard]] bool ValidDepthTarget(const UiRenderCompositionTarget &target, const FramebufferExtent extent,
                                            const std::uint32_t sampleCount) noexcept {
            return target.resource.IsValid() && MatchesOutput(target.texture, extent) && IsDepthFormat(target.texture.format) &&
                   target.texture.sampleCount == sampleCount && HasTextureUsage(target.texture.usage, RenderTextureUsage::RenderAttachment);
        }

        [[nodiscard]] bool CompatibleColorTargets(const RenderTextureDescriptor &input, const RenderTextureDescriptor &output) noexcept {
            return input.dimension == output.dimension && input.extent == output.extent && input.format == output.format &&
                   input.mipCount == output.mipCount && input.layerCount == output.layerCount && input.sampleCount == output.sampleCount &&
                   input.depth == output.depth;
        }

        [[nodiscard]] constexpr bool Before(const UiRenderCompositionPass &left, const UiRenderCompositionPass &right) noexcept {
            if (left.point != right.point)
                return left.point < right.point;
            return left.band <= right.band;
        }

        [[nodiscard]] bool ValidSpacePolicy(const UiRenderCompositionPass &entry) noexcept {
            if (entry.space == UiRenderCompositionSpace::World)
                return entry.point == UiRenderCompositionPoint::SceneBeforePostProcess && entry.band == UiRenderPresentationBand::World &&
                       entry.depth.has_value();
            if (entry.space == UiRenderCompositionSpace::Camera)
                return entry.point != UiRenderCompositionPoint::DisplayOverlay && entry.band != UiRenderPresentationBand::World &&
                       !entry.depth.has_value();
            return entry.space == UiRenderCompositionSpace::Screen && entry.band != UiRenderPresentationBand::World &&
                   !entry.depth.has_value();
        }

        struct SnapshotKeySlot final {
            bool occupied{};
            Runtime::Ui::UiCanvasInstanceId canvas;
            Runtime::Ui::UiRenderSnapshotRevision revision;
        };

        inline constexpr std::size_t SnapshotKeySlotCount = MaximumUiRenderCompositionPasses * 2;

        [[nodiscard]] std::size_t SnapshotKeyHash(const Runtime::Ui::UiCanvasInstanceId canvas,
                                                  const Runtime::Ui::UiRenderSnapshotRevision revision) noexcept {
            std::uint64_t value = canvas.ownership.Value() ^ (static_cast<std::uint64_t>(canvas.slot) << 32U) ^
                                  (static_cast<std::uint64_t>(canvas.generation) << 1U) ^ revision.Value();
            value ^= value >> 33U;
            value *= 0xff51afd7ed558ccdULL;
            value ^= value >> 33U;
            return static_cast<std::size_t>(value) & (SnapshotKeySlotCount - 1U);
        }

        [[nodiscard]] bool InsertSnapshotKey(std::array<SnapshotKeySlot, SnapshotKeySlotCount> &slots,
                                             const UiRenderCompositionPass &entry) noexcept {
            const auto &descriptor = entry.snapshot->Descriptor();
            std::size_t index = SnapshotKeyHash(descriptor.canvas, descriptor.snapshotRevision);
            for (std::size_t probe = 0; probe < slots.size(); ++probe) {
                auto &slot = slots[index];
                if (!slot.occupied) {
                    slot = {true, descriptor.canvas, descriptor.snapshotRevision};
                    return true;
                }
                if (slot.canvas == descriptor.canvas && slot.revision == descriptor.snapshotRevision)
                    return false;
                index = (index + 1U) & (slots.size() - 1U);
            }
            return false;
        }

        [[nodiscard]] Result<void> ValidateEntry(const UiRenderCompositionRequest &request, const UiRenderCompositionPass &entry) {
            if (entry.snapshot == nullptr || !entry.pass.IsValid() || !IsKnown(entry.space) || !IsKnown(entry.point) ||
                !IsKnown(entry.band) || !ValidSpacePolicy(entry))
                return Failure(Runtime::Ui::UiErrors::RenderCompositionInvalid);

            const auto &snapshot = entry.snapshot->Descriptor();
            if (snapshot.view != request.view)
                return Failure(Runtime::Ui::UiErrors::HandleOwnerMismatch);
            if (entry.pass.owner != request.graphOwner || entry.colorInput.resource.owner != request.graphOwner ||
                entry.colorOutput.resource.owner != request.graphOwner)
                return Failure(Runtime::Ui::UiErrors::HandleOwnerMismatch);

            const bool distinctColorTargets = entry.colorInput.resource != entry.colorOutput.resource;
            if (!ValidColorTarget(entry.colorInput, request.outputExtent, distinctColorTargets) ||
                !ValidColorTarget(entry.colorOutput, request.outputExtent, false) ||
                !CompatibleColorTargets(entry.colorInput.texture, entry.colorOutput.texture))
                return Failure(Runtime::Ui::UiErrors::RenderCompositionInvalid);

            if (entry.depth.has_value()) {
                if (entry.depth->resource.owner != request.graphOwner)
                    return Failure(Runtime::Ui::UiErrors::HandleOwnerMismatch);
                if (!ValidDepthTarget(*entry.depth, request.outputExtent, entry.colorOutput.texture.sampleCount))
                    return Failure(Runtime::Ui::UiErrors::RenderCompositionInvalid);
            }
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc ValidateUiRenderComposition */
    Result<void> ValidateUiRenderComposition(const UiRenderCompositionRequest &request) {
        if (!request.limits.IsValid() || request.passes.size() > request.limits.maximumPasses)
            return Failure(Runtime::Ui::UiErrors::RenderCompositionCapacityExceeded);
        if (!request.view.IsValid() || !request.graphOwner.IsValid() || !request.outputExtent.IsValid())
            return Failure(Runtime::Ui::UiErrors::RenderCompositionInvalid);

        std::array<SnapshotKeySlot, SnapshotKeySlotCount> snapshotKeys{};
        for (std::size_t index = 0; index < request.passes.size(); ++index) {
            if (const auto valid = ValidateEntry(request, request.passes[index]); valid.HasError())
                return valid;
            if (index > 0 && !Before(request.passes[index - 1], request.passes[index]))
                return Failure(Runtime::Ui::UiErrors::RenderCompositionInvalid);
            if (!InsertSnapshotKey(snapshotKeys, request.passes[index]))
                return Failure(Runtime::Ui::UiErrors::RenderCompositionInvalid);
        }
        return Result<void>::Success();
    }
}  // namespace Horo::Render
