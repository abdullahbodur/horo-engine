#include <catch2/catch_test_macros.hpp>

#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"

#include <imgui.h>

#include <array>
#include <cmath>
#include <string>

namespace
{
    std::string gClipboardText;

    void CaptureClipboardText(ImGuiContext*, const char* text)
    {
        gClipboardText = text;
    }
}

TEST_CASE("Shared modal shell composes badge split panes and fixed footer",
          "[unit][editor][gui][design-system]")
{
    using namespace Horo::Editor;
    using namespace Horo::Editor::Ui;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0F, 720.0F);
    io.DeltaTime = 1.0F / 60.0F;
    io.Fonts->AddFontDefault();
    static_cast<void>(io.Fonts->Build());
    ImFont* defaultFont = io.Fonts->Fonts.front();
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
        REQUIRE(
            (BadgeWidth(statusPill, fonts) >
             BadgeWidth(smallBadge, fonts)));

        ModalSplitPane(
            {
                .id = "ComponentModalSplitTest",
                .size = {0.0F, modal.BodyHeight()},
                .leadingWidth = 280.0F,
            },
            [&]()
            {
                drewLeading = true;
                Badge(smallBadge, fonts);
            },
            [&]()
            {
                drewContent = true;
                const ImVec2 spacing = ImGui::GetStyle().ItemSpacing;
                preservedContentSpacing =
                    spacing.x == 13.0F && spacing.y == 9.0F;
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

TEST_CASE("Selectable text block copies a selection spanning multiple lines",
          "[unit][editor][gui][design-system]")
{
    using namespace Horo::Editor::Ui;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
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
    const auto drawBlock = [&]()
    {
        ImGui::SetNextWindowPos({20.0F, 20.0F});
        ImGui::SetNextWindowSize({500.0F, 240.0F});
        ImGui::Begin("SelectableTextBlockTest");
        textBlockActive = SelectableTextBlock(
            "##LogText", text.data(), text.size() + 1U, lineLayouts, 100.0F);
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

    io.AddMousePosEvent(
        textOrigin.x + ImGui::CalcTextSize("third line").x,
        textOrigin.y + ImGui::GetFontSize() * 2.0F + 3.0F);
    ImGui::NewFrame();
    drawBlock();
    ImGui::Render();

    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    ImGui::NewFrame();
    drawBlock();
    ImGui::Render();

    gClipboardText.clear();
    io.AddKeyEvent(
        io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_C, true);
    ImGui::NewFrame();
    drawBlock();
    ImGui::Render();

    REQUIRE(gClipboardText == text);

    ImGui::DestroyContext();
}

TEST_CASE("Editable object title keeps its compact input vertically centered",
          "[unit][editor][gui][design-system]")
{
    using namespace Horo::Editor;
    using namespace Horo::Editor::Ui;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = {480.0F, 240.0F};
    io.DeltaTime = 1.0F / 60.0F;
    io.Fonts->AddFontDefault();
    static_cast<void>(io.Fonts->Build());
    ImFont* defaultFont = io.Fonts->Fonts.front();
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
    static_cast<void>(DrawEditableObjTitle(
        "object_name", value, 128U, "Mesh",
        ImVec4{0.2F, 0.7F, 0.4F, 0.15F}, Theme::Ok(), fonts));
    const ImVec2 inputMinimum = ImGui::GetItemRectMin();
    const ImVec2 inputMaximum = ImGui::GetItemRectMax();
    ImGui::End();
    ImGui::Render();

    constexpr float titleHeight = 38.0F;
    const float topPadding = inputMinimum.y - titleOrigin.y;
    const float bottomPadding =
        titleOrigin.y + titleHeight - inputMaximum.y;
    INFO("top padding: " << topPadding << ", bottom padding: " << bottomPadding);
    REQUIRE((inputMaximum.y - inputMinimum.y < 28.0F));
    REQUIRE((std::fabs(topPadding - bottomPadding) < 0.6F));

    ImGui::DestroyContext();
}
