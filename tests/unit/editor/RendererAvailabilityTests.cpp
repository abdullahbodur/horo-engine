#include <catch2/catch_test_macros.hpp>

#include "editor/project_model/RendererAvailability.h"

#include <string_view>

namespace
{
    using namespace Horo::Editor;

    TEST_CASE("Keeps Known Unavailable Backends Visible But Not Selectable", "[unit][editor]")
    {
        const RendererAvailabilitySnapshot snapshot{
            {
                RendererBackendAvailability{"opengl", "OpenGL", RendererAvailabilityState::Available, {}},
                RendererBackendAvailability{"metal", "Metal", RendererAvailabilityState::Active, {}},
                RendererBackendAvailability{
                    "vulkan", "Vulkan", RendererAvailabilityState::NotInstalled,
                    "Renderer component is not installed."
                },
            },
            "metal"
        };

        REQUIRE((snapshot.Entries().size() == 3));
        REQUIRE((snapshot.Find("opengl")->IsSelectable()));
        REQUIRE((snapshot.Find("metal")->IsSelectable()));
        REQUIRE((!snapshot.Find("vulkan")->IsSelectable()));
        REQUIRE((snapshot.Find("vulkan")->diagnostic == "Renderer component is not installed."));
        REQUIRE((snapshot.ActiveBackendId() == "metal"));
    }

    TEST_CASE("Selects Active Backend As Project Default", "[unit][editor]")
    {
        const RendererAvailabilitySnapshot snapshot{
            {
                RendererBackendAvailability{"opengl", "OpenGL", RendererAvailabilityState::Available, {}},
                RendererBackendAvailability{"metal", "Metal", RendererAvailabilityState::Active, {}},
                RendererBackendAvailability{"vulkan", "Vulkan", RendererAvailabilityState::NotInstalled, {}},
            },
            "metal"
        };

        REQUIRE((snapshot.DefaultSelectableBackendId() == "metal"));
    }
} // namespace
