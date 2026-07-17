#include "editor/project_model/RendererAvailability.h"

#include <algorithm>
#include <utility>

namespace Horo::Editor
{
/** @copydoc RendererBackendAvailability::IsSelectable */
bool RendererBackendAvailability::IsSelectable() const noexcept
{
    return state == RendererAvailabilityState::Available || state == RendererAvailabilityState::Selected ||
           state == RendererAvailabilityState::Active;
}

/** @copydoc RendererAvailabilitySnapshot::RendererAvailabilitySnapshot */
RendererAvailabilitySnapshot::RendererAvailabilitySnapshot(std::vector<RendererBackendAvailability> entries,
                                                           std::string activeBackendId)
    : entries_(std::move(entries)), activeBackendId_(std::move(activeBackendId))
{
}

/** @copydoc RendererAvailabilitySnapshot::Entries */
const std::vector<RendererBackendAvailability> &RendererAvailabilitySnapshot::Entries() const noexcept
{
    return entries_;
}

/** @copydoc RendererAvailabilitySnapshot::Find */
const RendererBackendAvailability *RendererAvailabilitySnapshot::Find(const std::string_view backendId) const noexcept
{
    const auto found = std::ranges::find(entries_, backendId, &RendererBackendAvailability::backendId);
    return found == entries_.end() ? nullptr : &*found;
}

/** @copydoc RendererAvailabilitySnapshot::ActiveBackendId */
std::string_view RendererAvailabilitySnapshot::ActiveBackendId() const noexcept
{
    return activeBackendId_;
}

/** @copydoc RendererAvailabilitySnapshot::DefaultSelectableBackendId */
std::string_view RendererAvailabilitySnapshot::DefaultSelectableBackendId() const noexcept
{
    if (const RendererBackendAvailability *active = Find(activeBackendId_); active != nullptr && active->IsSelectable())
    {
        return active->backendId;
    }
    const auto available = std::ranges::find_if(entries_, &RendererBackendAvailability::IsSelectable);
    return available == entries_.end() ? std::string_view{} : std::string_view{available->backendId};
}
} // namespace Horo::Editor
