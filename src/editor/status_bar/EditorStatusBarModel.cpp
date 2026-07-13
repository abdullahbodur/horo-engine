#include "Horo/Editor/EditorStatusBarModel.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ranges>
#include <utility>

namespace Horo::Editor
{
namespace
{
[[nodiscard]] bool IsCanonicalId(const std::string_view id) noexcept
{
    if (id.empty() || id.size() > EditorStatusItemLimits::MaxIdBytes)
    {
        return false;
    }
    if (!std::isalnum(static_cast<unsigned char>(id.front())))
    {
        return false;
    }
    return std::ranges::all_of(id, [](const char value) {
        const unsigned char character = static_cast<unsigned char>(value);
        return std::islower(character) || std::isdigit(character) || value == '.' || value == '_' || value == '-';
    });
}

[[nodiscard]] bool IsDescriptorBounded(const EditorStatusItemDescriptor &descriptor) noexcept
{
    return descriptor.localizationNamespace.size() <= EditorStatusItemLimits::MaxIdBytes &&
           descriptor.labelKey.size() <= EditorStatusItemLimits::MaxIdBytes &&
           descriptor.ownerPanelId.size() <= EditorStatusItemLimits::MaxIdBytes &&
           descriptor.actionId.size() <= EditorStatusItemLimits::MaxActionIdBytes;
}

[[nodiscard]] bool IsContentBounded(const EditorStatusItemContent &content) noexcept
{
    return content.iconResourceId.size() <= EditorStatusItemLimits::MaxIconIdBytes &&
           content.label.size() <= EditorStatusItemLimits::MaxLabelBytes &&
           content.value.size() <= EditorStatusItemLimits::MaxValueBytes;
}

[[nodiscard]] bool IsVisible(const EditorStatusItem &item, const EditorStatusBarContext &context) noexcept
{
    if (!item.content.available)
    {
        return false;
    }
    if (item.descriptor.visibility == EditorStatusItemVisibility::Always)
    {
        return true;
    }
    return std::ranges::find(context.activePanelIds, std::string_view{item.descriptor.ownerPanelId}) !=
           context.activePanelIds.end();
}

[[nodiscard]] bool PresentationOrder(const EditorStatusItem *left, const EditorStatusItem *right) noexcept
{
    if (left->descriptor.alignment != right->descriptor.alignment)
    {
        return left->descriptor.alignment == EditorStatusBarAlignment::Left;
    }
    if (left->descriptor.order != right->descriptor.order)
    {
        return left->descriptor.order < right->descriptor.order;
    }
    return left->descriptor.id < right->descriptor.id;
}

[[nodiscard]] bool AdmissionOrder(const EditorStatusMeasuredItem &left, const EditorStatusMeasuredItem &right) noexcept
{
    if (left.item->descriptor.priority != right.item->descriptor.priority)
    {
        return left.item->descriptor.priority > right.item->descriptor.priority;
    }
    if (left.item->descriptor.order != right.item->descriptor.order)
    {
        return left.item->descriptor.order < right.item->descriptor.order;
    }
    return left.item->descriptor.id < right.item->descriptor.id;
}
} // namespace

/** @copydoc EditorStatusItemRegistry::Register */
EditorStatusItemResult EditorStatusItemRegistry::Register(EditorStatusItemDescriptor descriptor,
                                                          EditorStatusItemContent content)
{
    if (!IsCanonicalId(descriptor.id))
    {
        return {EditorStatusItemError::InvalidId};
    }
    if (!IsDescriptorBounded(descriptor))
    {
        return {EditorStatusItemError::DescriptorTooLong};
    }
    if (descriptor.visibility == EditorStatusItemVisibility::OnlyWhenPanelActive && descriptor.ownerPanelId.empty())
    {
        return {EditorStatusItemError::MissingOwnerPanel};
    }
    if (descriptor.interactive && descriptor.actionId.empty())
    {
        return {EditorStatusItemError::MissingAction};
    }
    if (!std::isfinite(descriptor.maxWidth) || descriptor.maxWidth < EditorStatusItemLimits::MinWidth ||
        descriptor.maxWidth > EditorStatusItemLimits::MaxWidth)
    {
        return {EditorStatusItemError::InvalidWidth};
    }
    if (!IsContentBounded(content))
    {
        return {EditorStatusItemError::ContentTooLong};
    }
    if (Find(descriptor.id) != nullptr)
    {
        return {EditorStatusItemError::DuplicateId};
    }
    if (items_.size() >= EditorStatusItemLimits::MaxItems)
    {
        return {EditorStatusItemError::RegistryFull};
    }

    items_.push_back(std::make_unique<EditorStatusItem>(EditorStatusItem{std::move(descriptor), std::move(content)}));
    return {};
}

/** @copydoc EditorStatusItemRegistry::Update */
EditorStatusItemResult EditorStatusItemRegistry::Update(const std::string_view id, EditorStatusItemContent content)
{
    if (!IsContentBounded(content))
    {
        return {EditorStatusItemError::ContentTooLong};
    }
    const auto item = std::ranges::find_if(
        items_, [id](const std::unique_ptr<EditorStatusItem> &candidate) { return candidate->descriptor.id == id; });
    if (item == items_.end())
    {
        return {EditorStatusItemError::UnknownItem};
    }
    (*item)->content = std::move(content);
    return {};
}

/** @copydoc EditorStatusItemRegistry::Unregister */
EditorStatusItemResult EditorStatusItemRegistry::Unregister(const std::string_view id)
{
    const auto item = std::ranges::find_if(
        items_, [id](const std::unique_ptr<EditorStatusItem> &candidate) { return candidate->descriptor.id == id; });
    if (item == items_.end())
    {
        return {EditorStatusItemError::UnknownItem};
    }
    items_.erase(item);
    return {};
}

/** @copydoc EditorStatusItemRegistry::Find */
const EditorStatusItem *EditorStatusItemRegistry::Find(const std::string_view id) const noexcept
{
    const auto item = std::ranges::find_if(
        items_, [id](const std::unique_ptr<EditorStatusItem> &candidate) { return candidate->descriptor.id == id; });
    return item == items_.end() ? nullptr : item->get();
}

/** @copydoc EditorStatusItemRegistry::VisibleItems */
std::vector<const EditorStatusItem *> EditorStatusItemRegistry::VisibleItems(
    const EditorStatusBarContext &context) const
{
    std::vector<const EditorStatusItem *> output;
    output.reserve(items_.size());
    CollectVisibleItems(context, output);
    return output;
}

/** @copydoc EditorStatusItemRegistry::CollectVisibleItems */
void EditorStatusItemRegistry::CollectVisibleItems(const EditorStatusBarContext &context,
                                                   std::vector<const EditorStatusItem *> &output) const
{
    output.clear();
    for (const std::unique_ptr<EditorStatusItem> &item : items_)
    {
        if (IsVisible(*item, context))
        {
            output.push_back(item.get());
        }
    }
    std::ranges::sort(output, PresentationOrder);
}

/** @copydoc EditorStatusItemRegistry::Size */
std::size_t EditorStatusItemRegistry::Size() const noexcept
{
    return items_.size();
}

/** @copydoc PlanEditorStatusBarLayout */
EditorStatusBarLayout PlanEditorStatusBarLayout(const std::span<const EditorStatusMeasuredItem> measured,
                                                const float availableWidth, const float itemGap,
                                                const float overflowWidth, const std::size_t maxVisibleItems)
{
    EditorStatusBarLayout result;
    std::vector<EditorStatusMeasuredItem> rankedScratch;
    rankedScratch.reserve(measured.size());
    PlanEditorStatusBarLayoutInto(measured, availableWidth, itemGap, overflowWidth, maxVisibleItems, rankedScratch,
                                  result);
    return result;
}

/** @copydoc PlanEditorStatusBarLayoutInto */
void PlanEditorStatusBarLayoutInto(const std::span<const EditorStatusMeasuredItem> measured, const float availableWidth,
                                   const float itemGap, const float overflowWidth, const std::size_t maxVisibleItems,
                                   std::vector<EditorStatusMeasuredItem> &rankedScratch, EditorStatusBarLayout &output)
{
    rankedScratch.clear();
    output.items.clear();
    output.hiddenCount = 0;
    if (measured.empty())
    {
        return;
    }

    for (const EditorStatusMeasuredItem candidate : measured)
    {
        if (candidate.item != nullptr && std::isfinite(candidate.measuredWidth) && candidate.measuredWidth > 0.0F)
        {
            rankedScratch.push_back(candidate);
        }
    }

    std::ranges::sort(rankedScratch, AdmissionOrder);

    const auto widthFor = [](const EditorStatusMeasuredItem &candidate) {
        return std::min(candidate.measuredWidth, candidate.item->descriptor.maxWidth);
    };
    float unconstrainedWidth = 0.0F;
    for (const EditorStatusMeasuredItem &candidate : rankedScratch)
    {
        unconstrainedWidth += widthFor(candidate);
    }
    if (rankedScratch.size() > 1)
    {
        unconstrainedWidth += itemGap * static_cast<float>(rankedScratch.size() - 1);
    }

    const bool overflowExpected = rankedScratch.size() > maxVisibleItems || unconstrainedWidth > availableWidth;
    const float overflowReservation = overflowExpected ? overflowWidth + itemGap : 0.0F;
    const float itemBudget = std::max(0.0F, availableWidth - overflowReservation);
    float consumed = 0.0F;
    if (output.items.capacity() < maxVisibleItems)
    {
        output.items.reserve(maxVisibleItems);
    }

    for (const EditorStatusMeasuredItem &candidate : rankedScratch)
    {
        if (output.items.size() >= maxVisibleItems)
        {
            continue;
        }
        const float gap = output.items.empty() ? 0.0F : itemGap;
        const float nextWidth = gap + widthFor(candidate);
        if (consumed + nextWidth <= itemBudget)
        {
            output.items.push_back(candidate.item);
            consumed += nextWidth;
        }
        else
        {
            break;
        }
    }

    output.hiddenCount = rankedScratch.size() - output.items.size();
    std::ranges::sort(output.items, PresentationOrder);
}

} // namespace Horo::Editor
