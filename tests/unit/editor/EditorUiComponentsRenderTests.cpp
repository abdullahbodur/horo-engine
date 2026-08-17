#include "Horo/Editor/EditorIcons.h"
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

TEST_CASE("Editor icon registry resolves canonical and catalog tokens", "[unit][editor][gui][design-system]") {
    using Horo::Editor::Ui::UiIcon;
    using Horo::Editor::Ui::UiIconRegistry;

    REQUIRE(UiIconRegistry::Resolve("action.delete") == UiIcon::Delete);
    REQUIRE(UiIconRegistry::Resolve("primitive.light.directional") == UiIcon::DirectionalLight);
    REQUIRE(UiIconRegistry::Resolve("primitive.collider.sphere") == UiIcon::Sphere);
    REQUIRE_FALSE(UiIconRegistry::Resolve("unknown.icon").has_value());
    REQUIRE(std::string(UiIconRegistry::Token(UiIcon::VisibilityOff)) == "action.visibility_off");
    REQUIRE(UiIconRegistry::Token(UiIcon::None).empty());
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
    static_cast<void>(DrawEditableObjTitle("object_name", value, 128U,
                                           EditableObjectTitleBadge{"Mesh", ImVec4{0.2F, 0.7F, 0.4F, 0.15F}, Theme::Ok()}, fonts));
    const ImVec2 inputMinimum = ImGui::GetItemRectMin();
    const ImVec2 inputMaximum = ImGui::GetItemRectMax();
    ImGui::End();
    ImGui::Render();

    constexpr float titleHeight = 38.0F;
    const float topPadding = inputMinimum.y - titleOrigin.y;
    const float bottomPadding = titleOrigin.y + titleHeight - inputMaximum.y;
    INFO("top padding: " << topPadding << ", bottom padding: " << bottomPadding);
    REQUIRE((inputMaximum.y - inputMinimum.y < 28.0F));
    REQUIRE((std::fabs(topPadding - bottomPadding) < 0.6F));

    ImGui::DestroyContext();
}
