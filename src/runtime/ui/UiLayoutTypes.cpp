#include "Horo/Runtime/Ui/UiLayout.h"

namespace Horo::Runtime::Ui {
    namespace {
        [[nodiscard]] bool SameOwner(const RuntimeUiInstanceId instance, const UiCanvasInstanceId canvas) noexcept {
            return instance.IsValid() && canvas.IsValid() && instance.ownership == canvas.ownership;
        }
    }  // namespace

    /** @copydoc UiLogicalExtent::IsValid */
    bool UiLogicalExtent::IsValid() const noexcept {
        return width >= 0 && height >= 0;
    }

    /** @copydoc UiLogicalRect::IsValid */
    bool UiLogicalRect::IsValid() const noexcept {
        return extent.IsValid();
    }

    /** @copydoc UiLayoutSourceRevisions::IsValid */
    bool UiLayoutSourceRevisions::IsValid() const noexcept {
        return document.IsValid() && tree.IsValid() && content.IsValid() && style.IsValid() && intrinsic.IsValid() && canvas.IsValid() &&
               policy.IsValid();
    }

    /** @copydoc UiLayoutConstraints::IsValid */
    bool UiLayoutConstraints::IsValid() const noexcept {
        return minimum.IsValid() && maximum.IsValid() && minimum.width <= maximum.width && minimum.height <= maximum.height;
    }

    /** @copydoc UiLayoutMeasurement::IsValid */
    bool UiLayoutMeasurement::IsValid(const UiLayoutConstraints &constraints) const noexcept {
        return constraints.IsValid() && desired.IsValid() && desired.width >= constraints.minimum.width &&
               desired.width <= constraints.maximum.width && desired.height >= constraints.minimum.height &&
               desired.height <= constraints.maximum.height;
    }

    /** @copydoc UiLayoutArrangement::IsValid */
    bool UiLayoutArrangement::IsValid() const noexcept {
        return marginBox.IsValid() && borderBox.IsValid() && paddingBox.IsValid() && contentBox.IsValid() && overflow.IsValid() &&
               hitTest.IsValid() && baseline >= NoUiBaseline;
    }

    /** @copydoc UiLayoutEngineDescriptor::IsValid */
    bool UiLayoutEngineDescriptor::IsValid() const noexcept {
        return SameOwner(instance, canvas) && document.IsValid() && elementCapacity > 0 && elementCapacity <= MaximumUiTreeElements &&
               invalidationCapacity > 0 && invalidationCapacity <= MaximumUiStructuralCommands && concurrentSnapshots >= 2 &&
               concurrentSnapshots <= MaximumUiLayoutSnapshotsInFlight && initialInteractionRevision.IsValid();
    }
}  // namespace Horo::Runtime::Ui
