#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Horo/Editor/EditorStatusBarModel.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace Horo::Editor;

    void Expect(const bool condition, const char* message)
    {
        INFO(message);
        REQUIRE((condition));
    }

    EditorStatusItemDescriptor Descriptor(std::string id, const int priority = 0,
                                          const EditorStatusBarAlignment alignment = EditorStatusBarAlignment::Left,
                                          const std::uint16_t order = 0)
    {
        return EditorStatusItemDescriptor{
            .id = std::move(id),
            .alignment = alignment,
            .visibility = EditorStatusItemVisibility::Always,
            .priority = priority,
            .order = order,
            .maxWidth = 180.0F
        };
    }

    EditorStatusItemContent Content(std::string label, std::string value = {})
    {
        return EditorStatusItemContent{.label = std::move(label), .value = std::move(value)};
    }

    TEST_CASE("Registration rejects invalid status item descriptors", "[unit][editor][status-bar]")
    {
        EditorStatusItemRegistry registry;

        Expect(registry.Register(Descriptor(""), Content("Invalid")).error == EditorStatusItemError::InvalidId,
               "empty contribution IDs must be rejected");
        Expect(
            registry.Register(Descriptor("Vendor Status"), Content("Invalid")).error ==
            EditorStatusItemError::InvalidId,
            "non-canonical contribution IDs must be rejected");
        Expect(registry.Register(Descriptor("horo.status.selection"), Content("Sel", "1 obj")),
               "valid contribution must register");
        Expect(registry.Register(Descriptor("horo.status.selection"), Content("Duplicate")).error ==
               EditorStatusItemError::DuplicateId,
               "duplicate contribution IDs must be rejected instead of replaced by load order");

        auto panelItem = Descriptor("vendor.profiler.status");
        panelItem.visibility = EditorStatusItemVisibility::OnlyWhenPanelActive;
        Expect(registry.Register(panelItem, Content("Profiler")).error == EditorStatusItemError::MissingOwnerPanel,
               "panel-active visibility must name its owning panel");

        auto actionItem = Descriptor("vendor.action.status");
        actionItem.interactive = true;
        Expect(registry.Register(actionItem, Content("Action")).error == EditorStatusItemError::MissingAction,
               "interactive contributions must name a typed action");
    }

    TEST_CASE("Content updates are bounded and transactional", "[unit][editor][status-bar]")
    {
        EditorStatusItemRegistry registry;
        Expect(registry.Register(Descriptor("horo.status.nav"), Content("Nav", "idle")),
               "setup registration must pass");

        const std::string oversized(EditorStatusItemLimits::MaxValueBytes + 1, 'x');
        Expect(
            registry.Update("horo.status.nav", Content("Nav", oversized)).error ==
            EditorStatusItemError::ContentTooLong,
            "oversized updates must be rejected");
        Expect(registry.Find("horo.status.nav")->content.value == "idle",
               "rejected update must preserve the previous snapshot");
        Expect(registry.Update("missing.status", Content("Missing")).error == EditorStatusItemError::UnknownItem,
               "unknown contribution updates must return a typed error");
    }

    TEST_CASE("Panel visibility uses the frame context", "[unit][editor][status-bar]")
    {
        EditorStatusItemRegistry registry;
        auto descriptor = Descriptor("vendor.profiler.status");
        descriptor.visibility = EditorStatusItemVisibility::OnlyWhenPanelActive;
        descriptor.ownerPanelId = "vendor.profiler.panel";
        Expect(registry.Register(std::move(descriptor), Content("GPU", "2.4 ms")), "panel status must register");

        std::vector<std::string_view> noPanels;
        Expect(registry.VisibleItems(EditorStatusBarContext{noPanels}).empty(),
               "panel-only status must be hidden while its panel is inactive");

        const std::vector<std::string_view> activePanels{"vendor.profiler.panel"};
        const auto visible = registry.VisibleItems(EditorStatusBarContext{activePanels});
        Expect(visible.size() == 1 && visible.front()->descriptor.id == "vendor.profiler.status",
               "panel-only status must appear while its panel is active");
    }

    TEST_CASE("Status item ordering does not depend on registration order", "[unit][editor][status-bar]")
    {
        EditorStatusItemRegistry registry;
        Expect(registry.Register(Descriptor("vendor.z", 0, EditorStatusBarAlignment::Right, 20), Content("Z")),
               "register z");
        Expect(registry.Register(Descriptor("vendor.b", 0, EditorStatusBarAlignment::Left, 20), Content("B")),
               "register b");
        Expect(registry.Register(Descriptor("vendor.a", 0, EditorStatusBarAlignment::Left, 20), Content("A")),
               "register a");
        Expect(registry.Register(Descriptor("vendor.y", 0, EditorStatusBarAlignment::Right, 10), Content("Y")),
               "register y");

        const std::vector<std::string_view> activePanels;
        const auto visible = registry.VisibleItems(EditorStatusBarContext{activePanels});
        Expect(visible.size() == 4, "all always-visible entries must resolve");
        Expect(visible[0]->descriptor.id == "vendor.a" && visible[1]->descriptor.id == "vendor.b" &&
               visible[2]->descriptor.id == "vendor.y" && visible[3]->descriptor.id == "vendor.z",
               "alignment, explicit order, and stable ID must determine presentation order");
    }

    TEST_CASE("Width budget keeps higher priority status items", "[unit][editor][status-bar]")
    {
        EditorStatusItemRegistry registry;
        Expect(registry.Register(Descriptor("core.essential", 100, EditorStatusBarAlignment::Left, 0),
                                 Content("Unsaved")),
               "register essential");
        Expect(registry.Register(Descriptor("plugin.medium", 50, EditorStatusBarAlignment::Right, 0), Content("CPU")),
               "register medium");
        Expect(
            registry.Register(Descriptor("plugin.low", 1, EditorStatusBarAlignment::Left, 10),
                              Content("Verbose metric")),
            "register low");

        const std::vector<std::string_view> activePanels;
        const auto visible = registry.VisibleItems(EditorStatusBarContext{activePanels});
        const std::vector<EditorStatusMeasuredItem> measured{
            {visible[0], 70.0F},
            {visible[1], 90.0F},
            {visible[2], 60.0F},
        };

        const EditorStatusBarLayout layout = PlanEditorStatusBarLayout(measured, 170.0F, 8.0F, 20.0F, 12);
        Expect(layout.items.size() == 2, "bounded width must omit one contribution");
        Expect(layout.hiddenCount == 1, "layout must report hidden contribution count");
        Expect(std::ranges::any_of(layout.items,
                                   [](const EditorStatusItem* item)
                                   {
                                       return item->descriptor.id == "core.essential";
                                   }),
               "highest-priority contribution must survive width pressure");
        Expect(std::ranges::none_of(layout.items,
                                    [](const EditorStatusItem* item) { return item->descriptor.id == "plugin.low"; }),
               "lowest-priority contribution must be removed first");
    }

    TEST_CASE("Status item admission is a strict priority prefix", "[unit][editor][status-bar]")
    {
        EditorStatusItemRegistry registry;
        Expect(registry.Register(Descriptor("vendor.high", 100), Content("High")), "register high");
        Expect(registry.Register(Descriptor("vendor.low", 10), Content("Low")), "register low");

        const std::vector<std::string_view> activePanels;
        const auto visible = registry.VisibleItems(EditorStatusBarContext{activePanels});
        const std::vector<EditorStatusMeasuredItem> measured{{visible[0], 110.0F}, {visible[1], 32.0F}};
        const EditorStatusBarLayout layout = PlanEditorStatusBarLayout(measured, 100.0F, 8.0F, 20.0F, 12);
        Expect(layout.items.empty(), "a smaller low-priority item must not bypass an omitted higher-priority item");
        Expect(layout.hiddenCount == 2, "strict-prefix admission must report every omitted item");
    }

    TEST_CASE("Admission tie breaks ignore presentation alignment", "[unit][editor][status-bar]")
    {
        EditorStatusItemRegistry registry;
        Expect(registry.Register(Descriptor("vendor.right", 50, EditorStatusBarAlignment::Right, 0), Content("Right")),
               "register right");
        Expect(registry.Register(Descriptor("vendor.left", 50, EditorStatusBarAlignment::Left, 10), Content("Left")),
               "register left");

        const std::vector<std::string_view> activePanels;
        const auto visible = registry.VisibleItems(EditorStatusBarContext{activePanels});
        const std::vector<EditorStatusMeasuredItem> measured{{visible[0], 70.0F}, {visible[1], 70.0F}};
        const EditorStatusBarLayout layout = PlanEditorStatusBarLayout(measured, 110.0F, 8.0F, 20.0F, 12);
        Expect(layout.items.size() == 1 && layout.items.front()->descriptor.id == "vendor.right",
               "equal-priority admission must use explicit order before presentation alignment");
    }

    TEST_CASE("Status registry capacity preserves stable pointers", "[unit][editor][status-bar]")
    {
        EditorStatusItemRegistry registry;
        Expect(registry.Register(Descriptor("vendor.stable"), Content("Stable")), "register stable item");
        const EditorStatusItem* stable = registry.Find("vendor.stable");
        for (std::size_t index = 1; index < EditorStatusItemLimits::MaxItems; ++index)
        {
            Expect(registry.Register(Descriptor("vendor.item." + std::to_string(index)), Content("Item")),
                   "fill bounded registry");
        }
        Expect(registry.Find("vendor.stable") == stable, "unrelated registrations must not invalidate item pointers");
        Expect(registry.Unregister("vendor.item.1"), "unregister unrelated item");
        Expect(registry.Find("vendor.stable") == stable, "unrelated unregister must not invalidate item pointers");
        Expect(registry.Register(Descriptor("vendor.replacement"), Content("Replacement")), "refill bounded registry");
        Expect(registry.Register(Descriptor("vendor.overflow"), Content("Overflow")).error ==
               EditorStatusItemError::RegistryFull,
               "registry must reject contributions above its hard capacity");
    }

    TEST_CASE("Status layout enforces the maximum visible item count", "[unit][editor][status-bar]")
    {
        EditorStatusItemRegistry registry;
        for (std::size_t index = 0; index < 13; ++index)
        {
            Expect(registry.Register(
                       Descriptor("vendor.visible." + std::to_string(index), 100 - static_cast<int>(index)),
                       Content("Item")),
                   "register visible item");
        }
        const std::vector<std::string_view> activePanels;
        const auto visible = registry.VisibleItems(EditorStatusBarContext{activePanels});
        std::vector<EditorStatusMeasuredItem> measured;
        measured.reserve(visible.size());
        for (const EditorStatusItem* item : visible)
        {
            measured.push_back({item, 32.0F});
        }
        const EditorStatusBarLayout layout = PlanEditorStatusBarLayout(measured, 1000.0F, 8.0F, 20.0F, 12);
        Expect(layout.items.size() == 12 && layout.hiddenCount == 1, "layout must enforce the visible-item cap");
    }

    TEST_CASE("Overflow reservation includes the visual gap", "[unit][editor][status-bar]")
    {
        EditorStatusItemRegistry registry;
        Expect(registry.Register(Descriptor("vendor.high", 100), Content("High")), "register high");
        Expect(registry.Register(Descriptor("vendor.low", 10), Content("Low")), "register low");
        const std::vector<std::string_view> activePanels;
        const auto visible = registry.VisibleItems(EditorStatusBarContext{activePanels});
        const std::vector<EditorStatusMeasuredItem> measured{{visible[0], 108.0F}, {visible[1], 100.0F}};

        constexpr float availableWidth = 136.0F;
        constexpr float itemGap = 8.0F;
        constexpr float overflowWidth = 20.0F;
        const EditorStatusBarLayout layout = PlanEditorStatusBarLayout(measured, availableWidth, itemGap, overflowWidth,
                                                                       1);
        Expect(layout.items.size() == 1 && layout.hiddenCount == 1, "one item and overflow must fit exact budget");
        Expect(108.0F + itemGap + overflowWidth <= availableWidth,
               "admitted item, visual gap, and overflow indicator must remain inside budget");

        const std::vector<EditorStatusMeasuredItem> gapOverflow{{visible[0], 112.0F}, {visible[1], 100.0F}};
        const EditorStatusBarLayout rejected =
            PlanEditorStatusBarLayout(gapOverflow, availableWidth, itemGap, overflowWidth, 1);
        Expect(rejected.items.empty() && rejected.hiddenCount == 2,
               "planner must reject an item that fits only when the overflow gap is omitted");
    }
} // namespace
