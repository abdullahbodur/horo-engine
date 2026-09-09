#include "Horo/Editor/EditorIcons.h"
#include "Horo/Editor/EditorSnackbarHost.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"

#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <string>

namespace {
    std::string gClipboardText;

    void CaptureClipboardText(ImGuiContext *, const char *text) {
        gClipboardText = text;
    }
}  // namespace

namespace Horo::Editor {
    struct EditorSnackbarHostTestAccess {
        [[nodiscard]] static const std::deque<ActiveSnackbar> &Queued(const EditorSnackbarHost &host) {
            return host.activeSnackbars_;
        }
    };
}  // namespace Horo::Editor

TEST_CASE("Snackbar host bounds queued notifications while retaining the newest entry", "[unit][editor][gui][design-system]") {
    using namespace Horo::Editor;

    EditorDataBus events;
    EditorSnackbarHost host{events};
    for (std::uint64_t id = 1; id <= 64; ++id) {
        events.Publish(NotificationEvent{.id = id, .message = std::to_string(id), .durationSeconds = 0.0f});
    }

    events.Publish(NotificationEvent{.id = 65, .message = "transient", .durationSeconds = 5.0f});
    const auto &afterTransient = EditorSnackbarHostTestAccess::Queued(host);
    REQUIRE(afterTransient.size() == 64);
    REQUIRE(afterTransient.front().event.id == 2);
    REQUIRE(afterTransient.back().event.id == 65);

    events.Publish(NotificationEvent{.id = 66, .message = "persistent", .durationSeconds = 0.0f});
    const auto &afterPersistent = EditorSnackbarHostTestAccess::Queued(host);
    REQUIRE(afterPersistent.size() == 64);
    REQUIRE(afterPersistent.front().event.id == 2);
    REQUIRE(afterPersistent.back().event.id == 66);
}

TEST_CASE("Editor icon registry resolves canonical and catalog tokens", "[unit][editor][gui][design-system]") {
    using Horo::Editor::Ui::UiIcon;
    using Horo::Editor::Ui::UiIconRegistry;

    REQUIRE(UiIconRegistry::Resolve("action.delete") == UiIcon::Delete);
    REQUIRE(UiIconRegistry::Resolve("action.more_vertical") == UiIcon::MoreVertical);
    REQUIRE(UiIconRegistry::Resolve("action.checkbox_unchecked") == UiIcon::CheckboxUnchecked);
    REQUIRE(UiIconRegistry::Resolve("primitive.light.directional") == UiIcon::DirectionalLight);
    REQUIRE(UiIconRegistry::Resolve("primitive.collider.sphere") == UiIcon::Sphere);
    REQUIRE_FALSE(UiIconRegistry::Resolve("unknown.icon").has_value());
    REQUIRE(std::string(UiIconRegistry::Token(UiIcon::VisibilityOff)) == "action.visibility_off");
    REQUIRE(UiIconRegistry::Token(UiIcon::None).empty());

    const std::span glyphRanges = UiIconRegistry::MaterialSymbolGlyphRanges();
    REQUIRE(glyphRanges.size() >= 3U);
    REQUIRE(glyphRanges.back() == 0);
    const auto containsGlyph = [glyphRanges](const ImWchar glyph) {
        for (std::size_t index = 0; index + 1U < glyphRanges.size() && glyphRanges[index] != 0; index += 2U) {
            if (glyph >= glyphRanges[index] && glyph <= glyphRanges[index + 1U])
                return true;
        }
        return false;
    };
    REQUIRE(containsGlyph(0xE834));
    REQUIRE(containsGlyph(0xE8B8));
    REQUIRE(containsGlyph(0xF053));
}

TEST_CASE("Generic card title actions invoke caller-owned callbacks", "[unit][editor][gui][design-system]") {
    using namespace Horo::Editor;
    using namespace Horo::Editor::Ui;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = {640.0F, 480.0F};
    io.DeltaTime = 1.0F / 60.0F;
    io.Fonts->AddFontDefault();
    static_cast<void>(io.Fonts->Build());
    ImFont *defaultFont = io.Fonts->Fonts.front();
    const Theme::Fonts fonts{
        .sans = defaultFont,
        .sansCompact = defaultFont,
        .sansEmphasis = defaultFont,
        .icon = defaultFont,
    };

    bool invoked = false;
    ImVec2 actionCenter{};
    const auto drawFrame = [&] {
        ImGui::SetNextWindowPos({0.0F, 0.0F});
        ImGui::SetNextWindowSize({320.0F, 180.0F});
        ImGui::Begin("CardActionTest", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        {
            Card card(CardProps{.id = "##card"});
            const std::array actions{
                CardTitleBarAction{
                    .id = "reset",
                    .icon = UiIcon::Reset,
                    .title = "Reset",
                    .onInvoke =
                        [&invoked] {
                invoked = true;
            },
                },
            };
            card.DrawTitleBar({.id = "title", .title = "Component", .fonts = fonts, .actions = actions});
            const ImVec2 actionMinimum = ImGui::GetItemRectMin();
            const ImVec2 actionMaximum = ImGui::GetItemRectMax();
            actionCenter = {(actionMinimum.x + actionMaximum.x) * 0.5F, (actionMinimum.y + actionMaximum.y) * 0.5F};
            if (card.BeginBody())
                ImGui::TextUnformatted("Body");
        }
        ImGui::End();
    };

    ImGui::NewFrame();
    drawFrame();
    ImGui::Render();

    io.AddMousePosEvent(actionCenter.x, actionCenter.y);
    ImGui::NewFrame();
    drawFrame();
    ImGui::Render();

    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    ImGui::NewFrame();
    drawFrame();
    ImGui::Render();

    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    ImGui::NewFrame();
    drawFrame();
    ImGui::Render();

    REQUIRE(invoked);
    ImGui::DestroyContext();
}

TEST_CASE("Generic card disclosure persists and suppresses collapsed body content", "[unit][editor][gui][design-system]") {
    using namespace Horo::Editor;
    using namespace Horo::Editor::Ui;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = {400.0F, 240.0F};
    io.DeltaTime = 1.0F / 60.0F;
    io.Fonts->AddFontDefault();
    static_cast<void>(io.Fonts->Build());
    ImFont *defaultFont = io.Fonts->Fonts.front();
    const Theme::Fonts fonts{
        .sans = defaultFont,
        .sansCompact = defaultFont,
        .sansEmphasis = defaultFont,
        .icon = defaultFont,
    };

    bool bodyVisible = false;
    ImVec2 disclosureCenter{};
    const auto drawFrame = [&] {
        bodyVisible = false;
        ImGui::SetNextWindowPos({0.0F, 0.0F});
        ImGui::SetNextWindowSize({320.0F, 180.0F});
        ImGui::Begin("CardDisclosureTest", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        {
            Card card(CardProps{.id = "##card"});
            card.DrawTitleBar({.id = "title", .title = "Camera", .fonts = fonts});
            const ImVec2 disclosureMinimum = ImGui::GetItemRectMin();
            const ImVec2 disclosureMaximum = ImGui::GetItemRectMax();
            disclosureCenter = {(disclosureMinimum.x + disclosureMaximum.x) * 0.5F, (disclosureMinimum.y + disclosureMaximum.y) * 0.5F};
            bodyVisible = card.BeginBody();
            if (bodyVisible)
                ImGui::TextUnformatted("Projection");
        }
        ImGui::End();
    };

    ImGui::NewFrame();
    drawFrame();
    ImGui::Render();
    REQUIRE(bodyVisible);

    io.AddMousePosEvent(disclosureCenter.x, disclosureCenter.y);
    ImGui::NewFrame();
    drawFrame();
    ImGui::Render();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    ImGui::NewFrame();
    drawFrame();
    ImGui::Render();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    ImGui::NewFrame();
    drawFrame();
    ImGui::Render();
    REQUIRE_FALSE(bodyVisible);

    ImGui::NewFrame();
    drawFrame();
    ImGui::Render();
    REQUIRE_FALSE(bodyVisible);

    ImGui::DestroyContext();
}

TEST_CASE("Side dock tabs preserve reference padding height and interaction", "[unit][editor][gui][design-system]") {
    using namespace Horo::Editor;
    using namespace Horo::Editor::Ui;

    Theme::SetUiScalePercent(100);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = {400.0F, 200.0F};
    io.DeltaTime = 1.0F / 60.0F;
    io.Fonts->AddFontDefault();
    static_cast<void>(io.Fonts->Build());
    ImFont *defaultFont = io.Fonts->Fonts.front();
    const Theme::Fonts fonts{
        .sans = defaultFont,
        .sansCompact = defaultFont,
        .sansEmphasis = defaultFont,
        .icon = defaultFont,
    };
    const std::array<const char *, 2> tabs{"Inspector", "Scene"};

    int activeTab = 0;
    float startY = 0.0F;
    float endY = 0.0F;
    ImVec2 sceneTabCenter{};
    const auto drawFrame = [&] {
        ImGui::SetNextWindowPos({0.0F, 0.0F});
        ImGui::SetNextWindowSize({300.0F, 160.0F});
        ImGui::Begin("SideDockTabsTest", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        startY = ImGui::GetCursorScreenPos().y;
        activeTab = DrawSideDockTabs(tabs, activeTab, fonts);
        endY = ImGui::GetCursorScreenPos().y;
        const float inspectorWidth = defaultFont->CalcTextSizeA(12.0F, 100000.0F, 0.0F, tabs.front()).x + 20.0F;
        const float sceneWidth = defaultFont->CalcTextSizeA(12.0F, 100000.0F, 0.0F, tabs.back()).x + 20.0F;
        sceneTabCenter = {ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x + 10.0F + inspectorWidth + sceneWidth * 0.5F,
                          startY + 18.0F};
        ImGui::End();
    };

    ImGui::NewFrame();
    drawFrame();
    ImGui::Render();
    REQUIRE(endY - startY == Catch::Approx(36.0F));

    io.AddMousePosEvent(sceneTabCenter.x, sceneTabCenter.y);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    ImGui::NewFrame();
    drawFrame();
    ImGui::Render();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    ImGui::NewFrame();
    drawFrame();
    ImGui::Render();
    REQUIRE(activeTab == 1);

    ImGui::DestroyContext();
}

TEST_CASE("Workspace popup rows keep the design-system menu geometry", "[unit][editor][gui][design-system]") {
    using namespace Horo::Editor;
    using namespace Horo::Editor::Ui;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = {640.0F, 480.0F};
    io.DeltaTime = 1.0F / 60.0F;
    io.Fonts->AddFontDefault();
    static_cast<void>(io.Fonts->Build());
    ImFont *defaultFont = io.Fonts->Fonts.front();
    const Theme::Fonts fonts{
        .sans = defaultFont,
        .sansCompact = defaultFont,
        .sansEmphasis = defaultFont,
        .icon = defaultFont,
    };

    ImGui::NewFrame();
    ImGui::Begin("PopupGeometryTest");
    ImGui::OpenPopup("##popup");
    REQUIRE(BeginMenuPopup("##popup"));
    static_cast<void>(ContextMenuItem("Create", nullptr, fonts));
    const float rowHeight = ImGui::GetItemRectSize().y;
    const float popupWidth = ImGui::GetWindowWidth();
    ImGui::OpenPopup("##submenu_popup_GameObject###submenu");
    REQUIRE(BeginContextSubmenu("GameObject###submenu", fonts));
    static_cast<void>(ContextMenuItem("Box", nullptr, fonts));
    EndContextSubmenu();
    EndMenuPopup();
    ImGui::End();
    ImGui::Render();

    REQUIRE(rowHeight == Catch::Approx(30.0F));
    REQUIRE(popupWidth >= 176.0F);
    REQUIRE(popupWidth < 224.0F);

    ImGui::DestroyContext();
}

TEST_CASE("Menu-bar dropdowns reuse workspace popup rows", "[unit][editor][gui][design-system]") {
    using namespace Horo::Editor;
    using namespace Horo::Editor::Ui;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = {640.0F, 480.0F};
    io.DeltaTime = 1.0F / 60.0F;
    io.Fonts->AddFontDefault();
    static_cast<void>(io.Fonts->Build());
    ImFont *defaultFont = io.Fonts->Fonts.front();
    const Theme::Fonts fonts{
        .sans = defaultFont,
        .sansCompact = defaultFont,
        .sansEmphasis = defaultFont,
        .icon = defaultFont,
    };

    ImGui::NewFrame();
    ImGui::Begin("MenuBarDropdownTest", nullptr, ImGuiWindowFlags_MenuBar);
    REQUIRE(ImGui::BeginMenuBar());
    ImGui::OpenPopup("Window");
    REQUIRE(BeginMenuDropdown("Window", fonts));
    static_cast<void>(ContextMenuItem("Workspace", nullptr, fonts));
    const float rowHeight = ImGui::GetItemRectSize().y;
    const float popupWidth = ImGui::GetWindowWidth();
    EndMenuDropdown();
    ImGui::EndMenuBar();
    ImGui::End();
    ImGui::Render();

    REQUIRE(rowHeight == Catch::Approx(30.0F));
    REQUIRE(popupWidth >= 176.0F);
    REQUIRE(popupWidth < 224.0F);

    ImGui::DestroyContext();
}

TEST_CASE("Component metrics use theme overrides while global scaling is disabled", "[unit][editor][gui][design-system]") {
    using namespace Horo::Editor;
    using namespace Horo::Editor::DesignSystem;

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "horo-component-token-theme.json";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << R"({
            "name": "Component token test",
            "tokens": {
                "componentSizes": {
                    "xs": {
                        "fontSize": 11,
                        "paddingX": 6,
                        "paddingY": 2,
                        "minimumHeight": 20,
                        "iconSize": 10
                    }
                },
                "styleSpacing": {"xs": 3, "s": 7, "m": 13, "l": 19, "xl": 29}
            }
        })";
    }

    Theme::ThemeEntry entry;
    REQUIRE(Theme::LoadThemeFromJson(path.string().c_str(), entry));
    const ComponentSizeMetrics &xs = MetricsFor(entry.designTokens, ComponentSize::XS);
    REQUIRE(xs.fontSize == 11.0F);
    REQUIRE(xs.minimumHeight == 20.0F);
    REQUIRE(SpacingFor(entry.designTokens, SpacingSize::Medium) == 13.0F);
    std::error_code removeError;
    std::filesystem::remove(path, removeError);

    Theme::SetUiScalePercent(120);
    REQUIRE(MetricsFor(Theme::GetActiveTokens(), ComponentSize::XS).minimumHeight == Catch::Approx(24.0F));
    Theme::SetUiScalePercent(130);
    REQUIRE(MetricsFor(Theme::GetActiveTokens(), ComponentSize::XS).minimumHeight == Catch::Approx(24.0F));
}

TEST_CASE("Small toolbar primitives share height and fixed action width", "[unit][editor][gui][design-system]") {
    using namespace Horo::Editor;
    using namespace Horo::Editor::Ui;

    Theme::SetUiScalePercent(100);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = {800.0F, 240.0F};
    io.DeltaTime = 1.0F / 60.0F;
    io.Fonts->AddFontDefault();
    static_cast<void>(io.Fonts->Build());
    ImFont *defaultFont = io.Fonts->Fonts.front();
    const Theme::Fonts fonts{.sans = defaultFont, .sansCompact = defaultFont, .sansEmphasis = defaultFont};

    ImVec2 inputSize{};
    ImVec2 buttonSize{};
    ImVec2 comboSize{};
    ImVec2 multiSelectSize{};
    std::array<char, 32> search{};
    int status = 0;
    std::array<bool, 3> visible{true, true, true};
    const std::array<const char *, 4> statuses{"All Status", "OK", "Failed", "Cached"};
    const std::array<const char *, 3> columns{"Status", "Operation", "Message"};

    ImGui::NewFrame();
    ImGui::Begin("ToolbarPrimitiveGeometry");
    static_cast<void>(InputTextControl("##Search", search.data(), search.size(), fonts,
                                       InputTextOptions{.width = 180.0F, .hint = "Filter...", .componentSize = ComponentSize::Small}));
    inputSize = ImGui::GetItemRectSize();
    ImGui::SameLine();
    static_cast<void>(Button({.label = "Clear",
                              .size = {104.0F, 0.0F},
                              .variant = ButtonVariant::Secondary,
                              .font = defaultFont,
                              .baseFontSize = Theme::FontPx::SansCompact,
                              .componentSize = ComponentSize::Small}));
    buttonSize = ImGui::GetItemRectSize();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(104.0F);
    static_cast<void>(ComboControl("##Status", &status, statuses.data(), static_cast<int>(statuses.size()), fonts,
                                   ComboControlOptions{.componentSize = ComponentSize::Small}));
    comboSize = ImGui::GetItemRectSize();
    ImGui::SameLine();
    static_cast<void>(MultiSelectField("##Columns", "Columns", columns, visible, fonts, 104.0F, ComponentSize::Small));
    multiSelectSize = ImGui::GetItemRectSize();
    ImGui::End();
    ImGui::Render();

    REQUIRE(buttonSize.x == Catch::Approx(104.0F));
    REQUIRE(comboSize.x == Catch::Approx(buttonSize.x));
    REQUIRE(multiSelectSize.x == Catch::Approx(buttonSize.x));
    REQUIRE(inputSize.y == Catch::Approx(buttonSize.y).margin(0.1F));
    REQUIRE(comboSize.y == Catch::Approx(buttonSize.y).margin(0.1F));
    REQUIRE(multiSelectSize.y == Catch::Approx(buttonSize.y).margin(0.1F));
    ImGui::DestroyContext();
}

TEST_CASE("Shared modal shell composes badge split panes and fixed footer", "[unit][editor][gui][design-system]") {
    using namespace Horo::Editor;
    using namespace Horo::Editor::Ui;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0F, 720.0F);
    io.DeltaTime = 1.0F / 60.0F;
    io.Fonts->AddFontDefault();
    static_cast<void>(io.Fonts->Build());
    ImFont *defaultFont = io.Fonts->Fonts.front();
    const Theme::Fonts fonts{
        .sans = defaultFont,
        .sansCompact = defaultFont,
        .sansEmphasis = defaultFont,
    };

    ImGui::NewFrame();
    bool drewLeading = false;
    bool drewContent = false;
    bool preservedContentSpacing = false;
    ImGui::GetStyle().ItemSpacing = {13.0F, 9.0F};
    {
        ScopedModalShell modal(
            {
                .id = "ComponentModalShellTest",
                .title = "Component Modal",
                .requestedSize = {900.0F, 640.0F},
                .headerHeight = 56.0F,
                .footerHeight = 64.0F,
            },
            fonts);
        REQUIRE((modal.BodyHeight() > 0.0F));
        REQUIRE((modal.FooterStartY() > modal.BodyHeight()));
        REQUIRE_FALSE(modal.CloseRequested());

        const BadgeProps smallBadge{
            .label = "Stable",
            .tone = BadgeTone::Success,
        };
        const BadgeProps statusPill{
            .label = "Installing",
            .tone = BadgeTone::Accent,
            .size = BadgeSize::Medium,
            .leadingIndicator = true,
        };
        REQUIRE((BadgeWidth(statusPill, fonts) > BadgeWidth(smallBadge, fonts)));

        ModalSplitPane(
            {
                .id = "ComponentModalSplitTest",
                .size = {0.0F, modal.BodyHeight()},
                .leadingWidth = 280.0F,
            },
            [&]() {
            drewLeading = true;
            Badge(smallBadge, fonts);
        }, [&]() {
            drewContent = true;
            const ImVec2 spacing = ImGui::GetStyle().ItemSpacing;
            preservedContentSpacing = spacing.x == 13.0F && spacing.y == 9.0F;
            Badge(statusPill, fonts);
        });
        modal.BeginFooter({20.0F, 12.0F});
        ImGui::TextUnformatted("Footer");
        modal.EndFooter();
    }
    ImGui::Render();

    REQUIRE(drewLeading);
    REQUIRE(drewContent);
    REQUIRE(preservedContentSpacing);
    ImGui::DestroyContext();
}

TEST_CASE("Selectable text block copies a selection spanning multiple lines", "[unit][editor][gui][design-system]") {
    using namespace Horo::Editor::Ui;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = {640.0F, 360.0F};
    io.DeltaTime = 1.0F / 60.0F;
    io.Fonts->AddFontDefault();
    static_cast<void>(io.Fonts->Build());
    ImGui::GetPlatformIO().Platform_SetClipboardTextFn = CaptureClipboardText;

    std::string text{"first line\nsecond line\nthird line"};
    const std::array lineLayouts{
        SelectableTextLineLayout{
            .color = {1.0F, 0.2F, 0.2F, 1.0F},
            .alignedColumnByteOffset = 6U,
        },
        SelectableTextLineLayout{
            .color = {0.2F, 1.0F, 0.2F, 1.0F},
            .alignedColumnByteOffset = 7U,
        },
        SelectableTextLineLayout{
            .color = {0.2F, 0.2F, 1.0F, 1.0F},
            .alignedColumnByteOffset = 6U,
        },
    };
    ImVec2 textOrigin{};
    bool textBlockActive = false;
    const auto drawBlock = [&]() {
        ImGui::SetNextWindowPos({20.0F, 20.0F});
        ImGui::SetNextWindowSize({500.0F, 240.0F});
        ImGui::Begin("SelectableTextBlockTest");
        textBlockActive = SelectableTextBlock("##LogText", text.data(), text.size() + 1U, lineLayouts, 100.0F);
        textOrigin = ImGui::GetItemRectMin();
        ImGui::End();
    };

    ImGui::NewFrame();
    drawBlock();
    ImGui::Render();

    io.AddMousePosEvent(textOrigin.x + 1.0F, textOrigin.y + 3.0F);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    ImGui::NewFrame();
    drawBlock();
    ImGui::Render();
    REQUIRE(textBlockActive);

    io.AddMousePosEvent(textOrigin.x + ImGui::CalcTextSize("third line").x, textOrigin.y + ImGui::GetFontSize() * 2.0F + 3.0F);
    ImGui::NewFrame();
    drawBlock();
    ImGui::Render();

    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    ImGui::NewFrame();
    drawBlock();
    ImGui::Render();

    gClipboardText.clear();
    io.AddKeyEvent(io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_C, true);
    ImGui::NewFrame();
    drawBlock();
    ImGui::Render();

    REQUIRE(gClipboardText == text);

    ImGui::DestroyContext();
}

TEST_CASE("Editable object title keeps its compact input vertically centered", "[unit][editor][gui][design-system]") {
    using namespace Horo::Editor;
    using namespace Horo::Editor::Ui;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = {480.0F, 240.0F};
    io.DeltaTime = 1.0F / 60.0F;
    io.Fonts->AddFontDefault();
    static_cast<void>(io.Fonts->Build());
    ImFont *defaultFont = io.Fonts->Fonts.front();
    const Theme::Fonts fonts{
        .sans = defaultFont,
        .sansCompact = defaultFont,
        .sansEmphasis = defaultFont,
    };

    ImGui::NewFrame();
    ImGui::SetNextWindowPos({20.0F, 20.0F});
    ImGui::SetNextWindowSize({320.0F, 160.0F});
    ImGui::Begin("EditableObjectTitleTest");
    const ImVec2 titleOrigin = ImGui::GetCursorScreenPos();
    std::string value{"Box"};
    static_cast<void>(DrawEditableTitle("object_name", value, 128U, fonts, {.leadingIcon = UiIcon::HierarchyMesh, .trailingWidth = 88.0F}));
    const ImVec2 inputMinimum = ImGui::GetItemRectMin();
    const ImVec2 inputMaximum = ImGui::GetItemRectMax();
    ImGui::End();
    ImGui::Render();

    constexpr float titleHeight = 38.0F;
    const float topPadding = inputMinimum.y - titleOrigin.y;
    const float bottomPadding = titleOrigin.y + titleHeight - inputMaximum.y;
    INFO("top padding: " << topPadding << ", bottom padding: " << bottomPadding);
    REQUIRE((inputMaximum.y - inputMinimum.y == Catch::Approx(30.0F).margin(1.0F)));
    REQUIRE((std::fabs(topPadding - bottomPadding) <= 1.0F));

    ImGui::DestroyContext();
}
