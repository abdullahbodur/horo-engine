#include "editor/project_model/RendererAvailability.h"

#include <cassert>
#include <string_view>

namespace
{
using namespace Horo::Editor;

void KeepsKnownUnavailableBackendsVisibleButNotSelectable()
{
    const RendererAvailabilitySnapshot snapshot{
        {
            RendererBackendAvailability{"opengl", "OpenGL", RendererAvailabilityState::Available, {}},
            RendererBackendAvailability{"metal", "Metal", RendererAvailabilityState::Active, {}},
            RendererBackendAvailability{"vulkan", "Vulkan", RendererAvailabilityState::NotInstalled,
                                        "Renderer component is not installed."},
        },
        "metal"};

    assert(snapshot.Entries().size() == 3);
    assert(snapshot.Find("opengl")->IsSelectable());
    assert(snapshot.Find("metal")->IsSelectable());
    assert(!snapshot.Find("vulkan")->IsSelectable());
    assert(snapshot.Find("vulkan")->diagnostic == "Renderer component is not installed.");
    assert(snapshot.ActiveBackendId() == "metal");
}

void SelectsActiveBackendAsProjectDefault()
{
    const RendererAvailabilitySnapshot snapshot{
        {
            RendererBackendAvailability{"opengl", "OpenGL", RendererAvailabilityState::Available, {}},
            RendererBackendAvailability{"metal", "Metal", RendererAvailabilityState::Active, {}},
            RendererBackendAvailability{"vulkan", "Vulkan", RendererAvailabilityState::NotInstalled, {}},
        },
        "metal"};

    assert(snapshot.DefaultSelectableBackendId() == "metal");
}
} // namespace

int main()
{
    KeepsKnownUnavailableBackendsVisibleButNotSelectable();
    SelectsActiveBackendAsProjectDefault();
    return 0;
}
