#include "Horo/Runtime/Ui/UiRenderSnapshot.h"

#include "Horo/Runtime/Ui/UiErrors.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace Horo::Runtime::Ui {
    namespace {
        template <typename T = void> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        bool UnitInterval(const float value) noexcept {
            return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
        }

        bool ValidRole(const UiRenderResourceRole role) noexcept {
            return role >= UiRenderResourceRole::Image && role <= UiRenderResourceRole::Mask;
        }

        bool PresentIndex(const std::uint32_t index, const std::size_t size) noexcept {
            return index < size;
        }

        bool OptionalIndex(const std::uint32_t index, const std::size_t size) noexcept {
            return index == NoUiRenderIndex || PresentIndex(index, size);
        }

        bool ValidPaintLimits(const UiRenderSnapshotLimits &limits) noexcept {
            return limits.commands <= MaximumUiRenderCommands && limits.textRuns <= MaximumUiRenderTextRuns &&
                   limits.glyphs <= MaximumUiRenderGlyphs && limits.resources <= MaximumUiRenderResources;
        }

        bool ValidProjectionLimits(const UiRenderSnapshotLimits &limits) noexcept {
            return limits.clips <= MaximumUiRenderClips && limits.masks <= MaximumUiRenderMasks && limits.transforms > 0 &&
                   limits.transforms <= MaximumUiRenderTransforms;
        }

        Result<void> ValidateDescriptor(const UiElementTree &tree, const UiRenderSnapshotDescriptor &descriptor) {
            if (tree.State() != UiElementTreeState::Active)
                return Failure(UiErrors::ElementTreeLifecycleUnavailable);
            if (descriptor.instance != tree.Instance() || descriptor.canvas != tree.Canvas())
                return Failure(UiErrors::HandleOwnerMismatch);
            if (descriptor.document != tree.SourceDocument() || descriptor.documentRevision != tree.SourceDocumentRevision() ||
                descriptor.treeRevision != tree.Revision())
                return Failure(UiErrors::RevisionStale);
            if (!descriptor.interactionRevision.IsValid() || !descriptor.snapshotRevision.IsValid())
                return Failure(UiErrors::RevisionInvalid);
            if (!descriptor.view.IsValid() || descriptor.view.ownership != descriptor.instance.ownership)
                return Failure(UiErrors::HandleOwnerMismatch);
            return descriptor.limits.IsValid() ? Result<void>::Success() : Failure(UiErrors::CapacityExceeded);
        }

        Result<void> ValidateCounts(const UiRenderSnapshotDescriptor &descriptor, const std::size_t commandCount,
                                    const std::size_t textRunCount, const std::size_t glyphCount, const std::size_t clipCount,
                                    const std::size_t maskCount, const std::size_t transformCount, const std::size_t resourceCount) {
            const auto &limits = descriptor.limits;
            if (commandCount > limits.commands || textRunCount > limits.textRuns || glyphCount > limits.glyphs)
                return Failure(UiErrors::CapacityExceeded);
            if (clipCount > limits.clips || maskCount > limits.masks || transformCount > limits.transforms)
                return Failure(UiErrors::CapacityExceeded);
            return resourceCount <= limits.resources ? Result<void>::Success() : Failure(UiErrors::CapacityExceeded);
        }

        Result<void> ValidateResources(const std::span<const UiRenderResourceReference> resources) {
            for (const auto &resource : resources)
                if (!resource.asset.IsValid() || !resource.revision.IsValid() || !ValidRole(resource.role))
                    return Failure(UiErrors::RenderResourceReferenceInvalid);
            return Result<void>::Success();
        }

        Result<void> ValidateText(const std::span<const UiTextRun> runs, const std::span<const UiPositionedGlyph> glyphs,
                                  const std::span<const UiRenderResourceReference> resources) {
            for (const auto &run : runs) {
                if (!PresentIndex(run.fontResource, resources.size()) || resources[run.fontResource].role != UiRenderResourceRole::FontFace)
                    return Failure(UiErrors::RenderResourceReferenceInvalid);
                if (run.firstGlyph > glyphs.size() || run.glyphCount > glyphs.size() - run.firstGlyph || !run.color.IsValid())
                    return Failure(UiErrors::RenderSnapshotInvalid);
            }
            return Result<void>::Success();
        }

        Result<void> ValidateClips(const std::span<const UiClip> clips) {
            for (std::uint32_t index = 0; index < clips.size(); ++index) {
                if (!clips[index].rect.extent.IsValid() || (clips[index].parent != NoUiRenderIndex && clips[index].parent >= index))
                    return Failure(UiErrors::RenderSnapshotInvalid);
            }
            return Result<void>::Success();
        }

        Result<void> ValidateMasks(const std::span<const UiMask> masks, const std::span<const UiLogicalTransform> transforms,
                                   const std::span<const UiRenderResourceReference> resources) {
            for (const auto &mask : masks) {
                if (!mask.rect.extent.IsValid() || !PresentIndex(mask.transform, transforms.size()))
                    return Failure(UiErrors::RenderSnapshotInvalid);
                if (!PresentIndex(mask.resource, resources.size()) || resources[mask.resource].role != UiRenderResourceRole::Mask)
                    return Failure(UiErrors::RenderResourceReferenceInvalid);
            }
            return Result<void>::Success();
        }

        Result<void> ValidatePayload(const UiSolidDraw &draw, const std::span<const UiTextRun>,
                                     const std::span<const UiRenderResourceReference>) {
            return draw.color.IsValid() ? Result<void>::Success() : Failure(UiErrors::RenderCommandInvalid);
        }

        Result<void> ValidatePayload(const UiBorderDraw &draw, const std::span<const UiTextRun>,
                                     const std::span<const UiRenderResourceReference>) {
            return draw.width >= 0 && draw.color.IsValid() ? Result<void>::Success() : Failure(UiErrors::RenderCommandInvalid);
        }

        Result<void> ValidatePayload(const UiImageDraw &draw, const std::span<const UiTextRun>,
                                     const std::span<const UiRenderResourceReference> resources) {
            if (!PresentIndex(draw.resource, resources.size()) || resources[draw.resource].role != UiRenderResourceRole::Image)
                return Failure(UiErrors::RenderResourceReferenceInvalid);
            return draw.tint.IsValid() ? Result<void>::Success() : Failure(UiErrors::RenderCommandInvalid);
        }

        Result<void> ValidatePayload(const UiTextDraw &draw, const std::span<const UiTextRun> runs,
                                     const std::span<const UiRenderResourceReference>) {
            return PresentIndex(draw.run, runs.size()) ? Result<void>::Success() : Failure(UiErrors::RenderCommandInvalid);
        }

        Result<void> ValidateCommands(const UiElementTree &tree, const std::span<const UiDrawCommand> commands,
                                      const std::span<const UiTextRun> runs, const std::span<const UiClip> clips,
                                      const std::span<const UiMask> masks, const std::span<const UiLogicalTransform> transforms,
                                      const std::span<const UiRenderResourceReference> resources) {
            for (const auto &command : commands) {
                if (tree.Get(command.element).HasError() || !command.rect.extent.IsValid() || !UnitInterval(command.opacity))
                    return Failure(UiErrors::RenderCommandInvalid);
                if (!PresentIndex(command.transform, transforms.size()) || !OptionalIndex(command.clip, clips.size()) ||
                    !OptionalIndex(command.mask, masks.size()))
                    return Failure(UiErrors::RenderCommandInvalid);
                const auto payload = std::visit([runs, resources]<typename Draw>(const Draw &draw) {
                    return ValidatePayload(draw, runs, resources);
                }, command.payload);
                if (payload.HasError())
                    return payload;
            }
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc UiLogicalExtent::IsValid */
    bool UiLogicalExtent::IsValid() const noexcept {
        return width >= 0 && height >= 0;
    }

    /** @copydoc UiLogicalTransform::IsValid */
    bool UiLogicalTransform::IsValid() const noexcept {
        return std::ranges::all_of(values, [](const float value) {
            return std::isfinite(value);
        });
    }

    /** @copydoc UiLinearColor::IsValid */
    bool UiLinearColor::IsValid() const noexcept {
        return UnitInterval(red) && UnitInterval(green) && UnitInterval(blue) && UnitInterval(alpha);
    }

    /** @copydoc UiRenderSnapshotLimits::IsValid */
    bool UiRenderSnapshotLimits::IsValid() const noexcept {
        return ValidPaintLimits(*this) && ValidProjectionLimits(*this);
    }

    /** @brief Private immutable frame-owned storage. */
    struct UiRenderSnapshot::Storage final {
        UiRenderSnapshotDescriptor descriptor;
        std::vector<UiDrawCommand> commands;
        std::vector<UiTextRun> textRuns;
        std::vector<UiPositionedGlyph> glyphs;
        std::vector<UiClip> clips;
        std::vector<UiMask> masks;
        std::vector<UiLogicalTransform> transforms;
        std::vector<UiRenderResourceReference> resources;
    };

    /** @copydoc UiRenderSnapshot::Extract */
    Result<UiRenderSnapshot> UiRenderSnapshot::Extract(const UiElementTree &tree, const UiRenderSnapshotDescriptor &descriptor,
                                                       const std::span<const UiDrawCommand> commands,
                                                       const std::span<const UiTextRun> textRuns,
                                                       const std::span<const UiPositionedGlyph> glyphs, const std::span<const UiClip> clips,
                                                       const std::span<const UiMask> masks,
                                                       const std::span<const UiLogicalTransform> transforms,
                                                       const std::span<const UiRenderResourceReference> resources) {
        if (const auto validated = ValidateDescriptor(tree, descriptor); validated.HasError())
            return Result<UiRenderSnapshot>::Failure(validated.ErrorValue());
        if (const auto bounded = ValidateCounts(descriptor, commands.size(), textRuns.size(), glyphs.size(), clips.size(), masks.size(),
                                                transforms.size(), resources.size());
            bounded.HasError())
            return Result<UiRenderSnapshot>::Failure(bounded.ErrorValue());
        if (const auto validResources = ValidateResources(resources); validResources.HasError())
            return Result<UiRenderSnapshot>::Failure(validResources.ErrorValue());
        if (!std::ranges::all_of(transforms, &UiLogicalTransform::IsValid))
            return Failure<UiRenderSnapshot>(UiErrors::RenderSnapshotInvalid);
        if (const auto validText = ValidateText(textRuns, glyphs, resources); validText.HasError())
            return Result<UiRenderSnapshot>::Failure(validText.ErrorValue());
        if (const auto validClips = ValidateClips(clips); validClips.HasError())
            return Result<UiRenderSnapshot>::Failure(validClips.ErrorValue());
        if (const auto validMasks = ValidateMasks(masks, transforms, resources); validMasks.HasError())
            return Result<UiRenderSnapshot>::Failure(validMasks.ErrorValue());
        if (const auto validCommands = ValidateCommands(tree, commands, textRuns, clips, masks, transforms, resources);
            validCommands.HasError())
            return Result<UiRenderSnapshot>::Failure(validCommands.ErrorValue());
        try {
            auto storage = std::make_shared<Storage>(Storage{descriptor,
                                                             {commands.begin(), commands.end()},
                                                             {textRuns.begin(), textRuns.end()},
                                                             {glyphs.begin(), glyphs.end()},
                                                             {clips.begin(), clips.end()},
                                                             {masks.begin(), masks.end()},
                                                             {transforms.begin(), transforms.end()},
                                                             {resources.begin(), resources.end()}});
            return Result<UiRenderSnapshot>::Success(UiRenderSnapshot{std::move(storage)});
        } catch (const std::bad_alloc &) {
            return Failure<UiRenderSnapshot>(UiErrors::CapacityExceeded);
        }
    }

    /** @copydoc UiRenderSnapshot::UiRenderSnapshot */
    UiRenderSnapshot::UiRenderSnapshot(std::shared_ptr<const Storage> storage) noexcept : storage_(std::move(storage)) {}

    /** @copydoc UiRenderSnapshot::Descriptor */
    const UiRenderSnapshotDescriptor &UiRenderSnapshot::Descriptor() const noexcept {
        return storage_->descriptor;
    }

    /** @copydoc UiRenderSnapshot::Commands */
    std::span<const UiDrawCommand> UiRenderSnapshot::Commands() const noexcept {
        return storage_->commands;
    }

    /** @copydoc UiRenderSnapshot::TextRuns */
    std::span<const UiTextRun> UiRenderSnapshot::TextRuns() const noexcept {
        return storage_->textRuns;
    }

    /** @copydoc UiRenderSnapshot::Glyphs */
    std::span<const UiPositionedGlyph> UiRenderSnapshot::Glyphs() const noexcept {
        return storage_->glyphs;
    }

    /** @copydoc UiRenderSnapshot::Clips */
    std::span<const UiClip> UiRenderSnapshot::Clips() const noexcept {
        return storage_->clips;
    }

    /** @copydoc UiRenderSnapshot::Masks */
    std::span<const UiMask> UiRenderSnapshot::Masks() const noexcept {
        return storage_->masks;
    }

    /** @copydoc UiRenderSnapshot::Transforms */
    std::span<const UiLogicalTransform> UiRenderSnapshot::Transforms() const noexcept {
        return storage_->transforms;
    }

    /** @copydoc UiRenderSnapshot::Resources */
    std::span<const UiRenderResourceReference> UiRenderSnapshot::Resources() const noexcept {
        return storage_->resources;
    }
}  // namespace Horo::Runtime::Ui
