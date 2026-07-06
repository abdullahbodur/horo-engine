#include "Horo/Editor/EditorGuiApp.h"

#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/WelcomeScreen.h"

#include <SDL.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <SDL_opengl.h>
#endif

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Horo::Editor
{
    namespace
    {

        using Theme::Fonts;
        using Theme::ScopedFont;
        using Theme::ScopedFontScale;
        using Theme::ScopedTextStyle;

        // ── uygulama durumu ──────────────────────────────────────────────────────

        struct EditorGuiOptions
        {
            bool textPreview = false;
            bool exitAfterFirstFrame = false;
        };

        struct EditorTextures
        {
            ImTextureID logo = 0;
        };

        enum class GuiAction
        {
            None,
            NewProject,
            OpenSettings,
            RecentProject,
        };

        struct NewProjectState
        {
            bool open = false;
            int step = 1;
            int selectedTemplate = 1; // varsayılan: 3D Starter
            char name[128] = "DesertRun";
            char path[512] = "/Users/bodur/projects/games/DesertRun";
            char version[32] = "0.1.0";
            char defaultScene[128] = "assets/scenes/main.horo";
            int renderBackend = 0;
            char targetFps[8] = "60";
            int physics = 0;
            int buildProfile = 0;
            int assetCompression = 0;
            int textureCompression = 0;
            int targetPlatform = 0;
            int compilerFamily = 0;
            int cppStandard = 0;
            bool initGit = true;
            bool restorePackages = true;
            bool includeStarter = true;
            bool generateCMake = false;
        };

        struct SettingsState
        {
            bool open = false;
            int startupAction = 0;
            int renderBackend = 0;
            bool autoSave = true;
            int autoSaveInterval = 5;
            char editorFontSize[8] = "15";
            bool showFps = false;
            bool anonymousTelemetry = false;
        };

        // ── asset yükleme (fontlar / logo dokusu) ───────────────────────────────

        [[nodiscard]] std::string AssetPath(const char *rel)
        {
            return std::string{HORO_EDITOR_ASSET_ROOT} + "/" + rel;
        }

        [[nodiscard]] const ImWchar *BuildEditorGlyphRanges(ImGuiIO &io)
        {
            // ImGui default ranges do not always include the small UI glyphs used
            // by the HTML mockups (arrows, multiplication sign, square icon,
            // middle dot). Without these, they render as '?' in the editor.
            static ImVector<ImWchar> ranges;
            if (ranges.empty())
            {
                ImFontGlyphRangesBuilder builder;
                builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
                builder.AddChar(0x00B7); // ·
                builder.AddChar(0x00D7); // ×
                builder.AddChar(0x2013); // –
                builder.AddChar(0x2014); // —
                builder.AddChar(0x2026); // …
                builder.AddChar(0x2190); // ←
                builder.AddChar(0x2191); // ↑
                builder.AddChar(0x2192); // →
                builder.AddChar(0x2193); // ↓
                builder.AddChar(0x25A1); // □
                builder.AddChar(0x25AA); // ▪
                builder.AddChar(0x25AB); // ▫
                builder.AddChar(0x2713); // ✓
                builder.BuildRanges(&ranges);
            }
            return ranges.Data;
        }

        [[nodiscard]] Fonts LoadEditorFonts(ImGuiIO &io)
        {
            Fonts f;
            const ImWchar *ranges = BuildEditorGlyphRanges(io);
            f.sans = io.Fonts->AddFontFromFileTTF(
                AssetPath("fonts/inter/InterVariable.ttf").c_str(), Theme::FontPx::Sans, nullptr, ranges);
            f.mono = io.Fonts->AddFontFromFileTTF(
                AssetPath("fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf").c_str(), Theme::FontPx::Mono, nullptr, ranges);
            f.monoSemiBold = io.Fonts->AddFontFromFileTTF(
                AssetPath("fonts/ibm-plex-mono/IBMPlexMono-SemiBold.ttf").c_str(), Theme::FontPx::MonoSemiBold, nullptr, ranges);
            if (f.sans)
                io.FontDefault = f.sans;
            return f;
        }

        [[nodiscard]] EditorTextures LoadEditorTextures()
        {
            EditorTextures t;
            auto path = AssetPath("launcher/logo.png");
            int w = 0, h = 0, c = 0;
            auto *px = stbi_load(path.c_str(), &w, &h, &c, 4);
            if (!px)
            {
                std::fprintf(stderr, "logo fail: %s\n", path.c_str());
                return t;
            }
            GLuint tex = 0;
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
            stbi_image_free(px);
            t.logo = (ImTextureID)(intptr_t)tex;
            return t;
        }

        // ── küçük, tekrar kullanılabilir UI parçaları ───────────────────────────
        // (Bunlar Theme'i KULLANIR ama kendisi Theme değildir; widget-assembly
        // yardımcılarıdır, .card / .section-title gibi CSS sınıflarının karşılığı.)

        // RAII: `.card { background: var(--bg2); border:1px solid var(--bd);
        //                border-radius:4px; padding:18px }` karşılığı.
        struct ScopedCard
        {
            explicit ScopedCard(const char *id, ImVec2 size,
                                float padX = Theme::Layout::CardPad,
                                float padY = Theme::Layout::CardPad,
                                ImVec4 bg = Theme::Bg2())
            {
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{padX, padY});
                ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
                ImGui::BeginChild(id, size, true, ImGuiWindowFlags_NoScrollbar);
            }
            ~ScopedCard()
            {
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            }
            ScopedCard(const ScopedCard &) = delete;
            ScopedCard &operator=(const ScopedCard &) = delete;
        };

        // `.section-title { font:700 12px var(--mono); text-transform:uppercase;
        //                    letter-spacing:.8px; color:var(--mut) }`
        void SectionTitle(const char *upperCaseLabel, const Fonts &f)
        {
            ScopedTextStyle ts(f.monoSemiBold, 12.0F, Theme::FontPx::MonoSemiBold);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
            ImGui::TextUnformatted(upperCaseLabel);
            ImGui::PopStyleColor();
        }

        // `.field label / .full-field label { font:11px var(--mono);
        //   text-transform:uppercase; letter-spacing:.5px; color:var(--mut) }`
        void FieldLabel(const char *upperCaseLabel, const Fonts &f)
        {
            ScopedTextStyle ts(f.mono, 11.0F, Theme::FontPx::Mono);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
            ImGui::TextUnformatted(upperCaseLabel);
            ImGui::PopStyleColor();
        }

        // `.hint { font:10.5px var(--mono); color:var(--dim) }`
        void Hint(const char *text, const Fonts &f)
        {
            ScopedTextStyle ts(f.mono, 10.5F, Theme::FontPx::Mono);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
            ImGui::TextWrapped("%s", text);
            ImGui::PopStyleColor();
        }

        // `.summary-row { border-bottom: 1px dashed var(--bd) }` karşılığı.
        void DashedSeparator(float dash = 4.0F, float gap = 3.0F)
        {
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            auto *dl = ImGui::GetWindowDrawList();
            const ImU32 col = Theme::U32(Theme::Border());
            for (float x = 0.0F; x < w; x += dash + gap)
            {
                const float xEnd = std::min(x + dash, w);
                dl->AddLine({p0.x + x, p0.y}, {p0.x + xEnd, p0.y}, col, 1.0F);
            }
            ImGui::Dummy({w, 1.0F});
        }

        [[nodiscard]] bool DrawActionButton(const char *label, bool primary, const Fonts &f)
        {
            using namespace Theme;
            if (primary)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, Accent());
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentHover());
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentActive());
                ImGui::PushStyleColor(ImGuiCol_Text, DarkText());
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Button, Bg3());
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentSoft());
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{Accent().x, Accent().y, Accent().z, 0.24F});
                ImGui::PushStyleColor(ImGuiCol_Text, Text());
            }
            bool clicked;
            { // .action-btn { font-size:13px }
                ScopedTextStyle ts(f.sans, 13.0F, FontPx::Sans);
                clicked = ImGui::Button(label, ImVec2{-1, 42});
            }
            ImGui::PopStyleColor(4);
            return clicked;
        }

        // `.project-item` karşılığı — bkz. welcome-screen.html
        void DrawProjectCard(const RecentProjectEntry &prj, int idx, const Fonts &f)
        {
            using namespace Theme;
            ImGui::PushID(idx);
            {
                ScopedCard card("PC", ImVec2{-1, 64}, /*padX=*/14.0F, /*padY=*/12.0F);

                auto *dl = ImGui::GetWindowDrawList();
                const ImVec2 t0 = ImGui::GetCursorScreenPos();
                dl->AddRectFilled(t0, {t0.x + 40, t0.y + 40}, U32(Bg3()), Layout::Radius);
                ImGui::Dummy({52, 40}); // 40px thumb + 12px gap
                ImGui::SameLine(0, 0);

                ImGui::BeginGroup();
                {
                    ScopedTextStyle ts(f.sans, 14.0F, FontPx::Sans); // .project-name
                    ImGui::TextUnformatted(prj.name.c_str());
                }
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4);
                {
                    ScopedTextStyle ts(f.mono, 11.0F, FontPx::Mono); // .project-path
                    ImGui::TextDisabled("%s", prj.rootPath.c_str());
                }
                ImGui::EndGroup();

                float metaW = 0.0F;
                {
                    ScopedTextStyle ts(f.mono, 10.0F, FontPx::Mono); // .project-meta
                    metaW = ImGui::CalcTextSize(prj.lastOpenedLabel.c_str()).x;
                }
                ImGui::SameLine(ImGui::GetWindowWidth() - metaW - 14);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8);
                {
                    ScopedTextStyle ts(f.mono, 10.0F, FontPx::Mono);
                    ImGui::TextDisabled("%s", prj.lastOpenedLabel.c_str());
                }
            }
            ImGui::PopID();
        }

        // `.news-card` karşılığı — bkz. welcome-screen.html
        void DrawNewsCard(const char *tag, const char *title, const char *desc, ImVec2 size, const Fonts &f)
        {
            using namespace Theme;
            ScopedCard card(title, size, /*padX=*/14.0F, /*padY=*/14.0F, ImVec4{0, 0, 0, 0});
            {
                ScopedTextStyle ts(f.mono, 10.0F, FontPx::Mono); // .news-card .tag
                ImGui::PushStyleColor(ImGuiCol_Text, Accent());
                ImGui::TextUnformatted(tag);
                ImGui::PopStyleColor();
            }
            {
                ScopedTextStyle ts(f.sans, 13.0F, FontPx::Sans); // .news-card .title
                ImGui::TextUnformatted(title);
            }
            ImGui::Dummy({0, 2});
            {
                ScopedTextStyle ts(f.sans, 11.5F, FontPx::Sans); // .news-card .desc
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + size.x - 28);
                ImGui::TextDisabled("%s", desc);
                ImGui::PopTextWrapPos();
            }
        }

        // ── welcome screen ───────────────────────────────────────────────────────
        // bkz. welcome-screen.html

        void DrawWelcomeScreen(const WelcomeViewModel &vm, const Fonts &f, const EditorTextures &tx, GuiAction &act)
        {
            using namespace Theme;

            auto *vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->WorkPos);
            ImGui::SetNextWindowSize(vp->WorkSize);
            constexpr auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDecoration |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
            ImGui::PushStyleColor(ImGuiCol_WindowBg, Bg1());
            ImGui::Begin("Welcome", nullptr, flags);

            // WelcomeCard fills the entire welcome window — it IS the window content.
            const ImVec2 avail = ImGui::GetContentRegionAvail();

            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Layout::RadiusModal);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Bg1());
            ImGui::BeginChild("WelcomeCard", avail, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::PopStyleVar(2);

            auto *cardDrawList = ImGui::GetWindowDrawList();
            const ImVec2 cardMin = ImGui::GetWindowPos();

            // ── side rail (.side) ──
            // HTML: background panel + sadece sağ border. BeginChild border=false,
            // dikey ayırıcı çizgi elle çiziliyor.
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{Layout::WelcomePad, Layout::WelcomePad});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Bg2());
            ImGui::BeginChild("Side", {Layout::WelcomeSideW, 0}, false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                  ImGuiWindowFlags_AlwaysUseWindowPadding);

            if (tx.logo)
                ImGui::Image(tx.logo, {64, 64}); // .logo-mark img { height:64px }
            ImGui::Dummy({0, 18});               // .logo-mark { margin-bottom:18px }

            {
                ScopedTextStyle ts(f.monoSemiBold, 24.0F, FontPx::MonoSemiBold); // .logo-text
                ImGui::TextUnformatted("HORO");
            }
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6); // .logo-sub margin-top:6px
            {
                ScopedTextStyle ts(f.sans, 12.0F, FontPx::Sans); // .logo-sub
                ImGui::TextDisabled("Game Engine");
            }
            ImGui::Dummy({0, 28}); // .logo-block { margin-bottom:28px }

            if (DrawActionButton("+  New Project", true, f))
                act = GuiAction::NewProject;
            (void)DrawActionButton("   Open Project", false, f);
            (void)DrawActionButton("\xe2\x86\x93  Open Recent", false, f);
            if (DrawActionButton("   Open Settings", false, f))
                act = GuiAction::OpenSettings;

            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();

            cardDrawList->AddLine({cardMin.x + Layout::WelcomeSideW, cardMin.y},
                                  {cardMin.x + Layout::WelcomeSideW, cardMin.y + avail.y},
                                  U32(Border()), 1.0F);

            ImGui::SameLine(0, 0);

            // ── main content (.main) ──
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{Layout::WelcomePad, Layout::WelcomePad});
            ImGui::BeginChild("Main", {0, 0}, false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                  ImGuiWindowFlags_AlwaysUseWindowPadding);

            {
                ScopedTextStyle ts(f.monoSemiBold, 12.0F, FontPx::MonoSemiBold); // .section-title
                ImGui::TextDisabled("RECENT PROJECTS");
                ImGui::SameLine(ImGui::GetWindowWidth() - 92);
                ImGui::PushStyleColor(ImGuiCol_Text, Accent());
                ImGui::TextUnformatted("BROWSE ALL");
                ImGui::PopStyleColor();
            }
            ImGui::Dummy({0, 14});

            for (size_t i = 0; i < vm.recentProjects.size(); ++i)
            {
                DrawProjectCard(vm.recentProjects[i], static_cast<int>(i), f);
            }

            ImGui::Dummy({0, 28});

            {
                ScopedTextStyle ts(f.monoSemiBold, 12.0F, FontPx::MonoSemiBold);
                ImGui::TextDisabled("WHAT'S NEW");
            }
            ImGui::Dummy({0, 14});

            const float newsW = (ImGui::GetContentRegionAvail().x - 12) * 0.5F;
            DrawNewsCard("Release Notes", "GPU-driven rendering preview",
                         "Experimental render graph and bindless resource backend now available.",
                         {newsW, 104}, f);
            ImGui::SameLine(0, 12);
            DrawNewsCard("Documentation", "MCP workflow guide",
                         "Author scenes and assets through the Model Context Protocol.",
                         {newsW, 104}, f);

            ImGui::EndChild();
            ImGui::PopStyleVar();

            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::End();
            ImGui::PopStyleColor();
        }

        // ── new project wizard ───────────────────────────────────────────────────
        // bkz. new-project-wizard.html — her adım kendi fonksiyonunda.
        // HTML karşılığı:
        //   .modal 900x680, header 58, body 220px + 1fr, footer 52
        //   .main padding 24px 28px, .steps padding 18px 14px

        constexpr const char *kTemplateNames[] = {
            "Empty", "3D Starter", "First Person", "Package Based", "Tech Demo", "Custom"};

        namespace WizardLayout
        {
            // Values copied from new-project-wizard.html. Keep these local to the
            // wizard so the modal can match the mockup without changing unrelated
            // editor surfaces.
            constexpr float ModalW = 900.0F;
            constexpr float ModalH = 680.0F;
            constexpr float ViewportPad = 56.0F; // body padding:28px on both sides

            constexpr float HeaderH = 58.0F;
            constexpr float FooterH = 52.0F;
            constexpr float SidebarW = 220.0F;

            constexpr float HeaderPadX = 22.0F;
            constexpr float SidebarPadX = 14.0F;
            constexpr float SidebarPadY = 18.0F;
            constexpr float MainPadX = 28.0F;
            constexpr float MainPadY = 24.0F;

            constexpr float StepH = 58.0F; // 11px top/bottom padding + 2 text rows
            constexpr float StepGap = 6.0F;

            constexpr float TemplateGap = 10.0F;
            constexpr float TemplateH = 108.0F; // CSS auto height rendered with 14px padding + wrapped desc
            constexpr float TemplatePad = 14.0F;
            constexpr float TemplateIconPx = 24.0F;
            constexpr float TemplateNamePx = 13.0F;
            constexpr float TemplateDescPx = 11.0F;

            constexpr float GridGap = 14.0F;
            constexpr float FieldLabelGap = 4.0F;
            constexpr float HintGap = 3.0F;
            constexpr float CardPad = 18.0F;
            constexpr float CardGap = 16.0F;
            constexpr float CheckGap = 10.0F;

            constexpr float Radius = 4.0F;
            constexpr float TemplateRadius = 6.0F;
            constexpr float ModalRadius = 8.0F;
        } // namespace WizardLayout

        namespace WizardCss
        {
            // :root from new-project-wizard.html
            [[nodiscard]] inline ImVec4 Rgba(float r, float g, float b, float a = 1.0F)
            {
                return ImVec4{r / 255.0F, g / 255.0F, b / 255.0F, a};
            }

            [[nodiscard]] inline ImVec4 Bg0() { return Rgba(0x0a, 0x0c, 0x0f); }
            [[nodiscard]] inline ImVec4 Bg1() { return Rgba(0x12, 0x15, 0x1a); }
            [[nodiscard]] inline ImVec4 Bg2() { return Rgba(0x18, 0x1c, 0x21); }
            [[nodiscard]] inline ImVec4 Bg3() { return Rgba(0x1f, 0x24, 0x2b); }
            [[nodiscard]] inline ImVec4 Hover() { return Rgba(0x23, 0x28, 0x30); }
            [[nodiscard]] inline ImVec4 Border() { return Rgba(0x2a, 0x2f, 0x37); }
            [[nodiscard]] inline ImVec4 Border2() { return Rgba(0x3a, 0x40, 0x49); }
            [[nodiscard]] inline ImVec4 Text() { return Rgba(0xe8, 0xe4, 0xd9); }
            [[nodiscard]] inline ImVec4 Muted() { return Rgba(0x9a, 0x95, 0x8a); }
            [[nodiscard]] inline ImVec4 Dim() { return Rgba(0x5e, 0x5b, 0x54); }
            [[nodiscard]] inline ImVec4 Accent() { return Rgba(0x04, 0xa5, 0xfc); }
            [[nodiscard]] inline ImVec4 AccentSoft() { return Rgba(0x04, 0xa5, 0xfc, 0.15F); }
            [[nodiscard]] inline ImVec4 AccentHover() { return Rgba(0x04, 0xa5, 0xfc, 0.22F); }
            [[nodiscard]] inline ImVec4 AccentActive() { return Rgba(0x04, 0xa5, 0xfc, 0.30F); }
            [[nodiscard]] inline ImVec4 Ok() { return Rgba(0x5f, 0xb8, 0x8a); }
            [[nodiscard]] inline ImVec4 Warn() { return Rgba(0xe8, 0xa3, 0x3d); }
            [[nodiscard]] inline ImVec4 Err() { return Rgba(0xd4, 0x52, 0x4a); }
            [[nodiscard]] inline ImVec4 ErrSoft() { return Rgba(0xd4, 0x52, 0x4a, 0.12F); }
            [[nodiscard]] inline ImVec4 DarkText() { return Rgba(0x05, 0x13, 0x1c); }
            [[nodiscard]] inline ImVec4 Shadow() { return Rgba(0x00, 0x00, 0x00, 0.55F); }
        } // namespace WizardCss

        // std::filesystem::exists / is_empty için kaba bir yer tutucu.
        [[nodiscard]] bool PathLooksOccupied(const char *path)
        {
            // TODO: gerçek bir dosya sistemi kontrolüyle değiştirilecek. HTML mockup
            // varsayılan "DesertRun" yolunu her zaman dolu/var olan bir klasör
            // olarak gösteriyor; davranış burada da aynen taklit ediliyor.
            return std::string_view{path}.find("DesertRun") != std::string_view::npos;
        }

        void DrawCssBorderForLastItem(const ImVec4 &color, float rounding, float thickness = 1.0F, float inflate = 0.0F)
        {
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(
                {min.x - inflate, min.y - inflate},
                {max.x + inflate, max.y + inflate},
                Theme::U32(color),
                rounding,
                0,
                thickness);
        }

        void DrawCssFocusRingForLastItem(const bool error = false)
        {
            if (ImGui::IsItemActive())
            {
                DrawCssBorderForLastItem(error ? WizardCss::ErrSoft() : WizardCss::AccentSoft(),
                                         WizardLayout::Radius + 2.0F,
                                         2.0F,
                                         2.0F);
            }
            else if (ImGui::IsItemHovered())
            {
                DrawCssBorderForLastItem(WizardCss::Border2(), WizardLayout::Radius, 1.0F, 0.0F);
            }
        }

        // SVG-style X close button — two diagonal lines, no text.
        [[nodiscard]] bool DrawCloseButton(const ImVec2 size)
        {
            const ImVec2 pos = ImGui::GetCursorScreenPos();

            ImGui::InvisibleButton("##close", size);
            const bool clicked = ImGui::IsItemClicked();
            const bool hovered = ImGui::IsItemHovered();

            auto *dl = ImGui::GetWindowDrawList();
            const ImVec2 center{pos.x + size.x * 0.5F, pos.y + size.y * 0.5F};
            const float arm = std::min(size.x, size.y) * 0.28F;

            if (hovered)
            {
                dl->AddRectFilled(pos, {pos.x + size.x, pos.y + size.y},
                                  Theme::U32(WizardCss::Bg3()), WizardLayout::Radius);
                dl->AddRect(pos, {pos.x + size.x, pos.y + size.y},
                            Theme::U32(WizardCss::Border2()), WizardLayout::Radius, 0, 1.0F);
            }

            const ImU32 color = Theme::U32(hovered ? WizardCss::Text() : WizardCss::Dim());
            constexpr float kThickness = 1.5F;
            dl->AddLine({center.x - arm, center.y - arm}, {center.x + arm, center.y + arm}, color, kThickness);
            dl->AddLine({center.x + arm, center.y - arm}, {center.x - arm, center.y + arm}, color, kThickness);

            return clicked;
        }

        [[nodiscard]] bool DrawWizardButton(const char *label,
                                            const ImVec2 size,
                                            const bool primary,
                                            const bool enabled,
                                            const Fonts &f)
        {
            // button { padding:8px 14px; border:1px solid var(--bd); border-radius:4px;
            //          background:var(--bg3); color:var(--txt); font:500 12px var(--mono) }
            if (!enabled)
            {
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.45F);
            }

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{14.0F, 8.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, WizardLayout::Radius);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);

            if (primary)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, WizardCss::Accent());
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, WizardCss::Accent());
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, WizardCss::Accent());
                ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::DarkText());
                ImGui::PushStyleColor(ImGuiCol_Border, WizardCss::Accent());
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Button, WizardCss::Bg3());
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, WizardCss::Bg3());
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, WizardCss::Bg3());
                ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Text());
                ImGui::PushStyleColor(ImGuiCol_Border, WizardCss::Border());
            }

            bool clicked = false;
            {
                ScopedTextStyle ts(f.mono, 12.0F, Theme::FontPx::Mono);
                clicked = ImGui::Button(label, size);
            }

            if (enabled && !primary && ImGui::IsItemHovered())
            {
                DrawCssBorderForLastItem(WizardCss::Border2(), WizardLayout::Radius, 1.0F, 0.0F);
            }

            ImGui::PopStyleColor(5);
            ImGui::PopStyleVar(3);
            if (!enabled)
            {
                ImGui::PopStyleVar();
                clicked = false;
            }

            return clicked;
        }

        [[nodiscard]] bool InputTextCss(const char *id,
                                        char *buffer,
                                        const size_t bufferSize,
                                        const Fonts &f,
                                        const bool error = false)
        {
            // input { background:var(--bg3); border:1px solid var(--bd); border-radius:4px;
            //         color:var(--txt); padding:7px 10px; font:12px var(--mono) }
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{10.0F, 7.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, WizardLayout::Radius);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, error ? WizardCss::ErrSoft() : WizardCss::Bg3());
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, error ? WizardCss::ErrSoft() : WizardCss::Hover());
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, error ? WizardCss::ErrSoft() : WizardCss::Hover());
            ImGui::PushStyleColor(ImGuiCol_Border, error ? WizardCss::Err() : WizardCss::Border());
            ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Text());

            bool changed = false;
            {
                ScopedTextStyle ts(f.mono, 12.0F, Theme::FontPx::Mono);
                changed = ImGui::InputText(id, buffer, bufferSize);
            }
            DrawCssFocusRingForLastItem(error);

            ImGui::PopStyleColor(5);
            ImGui::PopStyleVar(3);
            return changed;
        }

        [[nodiscard]] bool ComboCss(const char *id,
                                    int *value,
                                    const char *const items[],
                                    const int itemCount,
                                    const Fonts &f)
        {
            using namespace WizardLayout;

            ImGui::PushID(id);

            // ── kapalı alan: input/select ile aynı CSS sözleşmesi ──
            // select { padding:7px 10px; border:1px solid var(--bd); border-radius:4px;
            //          background:var(--bg3); font:12px var(--mono) }
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{10.0F, 7.0F});
            const float fieldW = ImGui::CalcItemWidth();
            const float fieldH = ImGui::GetFrameHeight();
            ImGui::PopStyleVar();

            const ImVec2 fieldPos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##field", ImVec2{fieldW, fieldH});
            const bool fieldHovered = ImGui::IsItemHovered();
            const bool fieldClicked = ImGui::IsItemClicked();

            const std::string popupId = std::string("##popup_") + id;
            const bool popupOpen = ImGui::IsPopupOpen(popupId.c_str());

            auto *dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(fieldPos, {fieldPos.x + fieldW, fieldPos.y + fieldH},
                              Theme::U32(fieldHovered ? WizardCss::Hover() : WizardCss::Bg3()), Radius);
            dl->AddRect(fieldPos, {fieldPos.x + fieldW, fieldPos.y + fieldH},
                        Theme::U32(popupOpen ? WizardCss::Accent() : WizardCss::Border()),
                        Radius, 0, popupOpen ? 1.5F : 1.0F);

            // seçili değerin etiketi
            {
                ImFont *font = f.mono ? f.mono : ImGui::GetFont();
                const char *label = (*value >= 0 && *value < itemCount) ? items[*value] : "";
                dl->AddText(font, 12.0F,
                            {fieldPos.x + 10.0F, fieldPos.y + (fieldH - 12.0F) * 0.5F},
                            Theme::U32(WizardCss::Text()), label);
            }

            // sağdaki ok — küçük dolgulu üçgen (glyph fallback riskine karşı)
            {
                const float cx = fieldPos.x + fieldW - 18.0F;
                const float cy = fieldPos.y + fieldH * 0.5F;
                const ImU32 arrowCol = Theme::U32(fieldHovered ? WizardCss::Text() : WizardCss::Muted());
                dl->AddTriangleFilled({cx - 4.0F, cy - 2.0F}, {cx + 4.0F, cy - 2.0F}, {cx, cy + 3.0F}, arrowCol);
            }

            if (fieldClicked)
                ImGui::OpenPopup(popupId.c_str());

            bool changed = false;

            // ── popup: .dropdown { padding:5px 0; border:1px solid var(--bd2);
            //           border-radius:6px; background:var(--bg2); box-shadow } ──
            ImGui::SetNextWindowPos({fieldPos.x, fieldPos.y + fieldH + 4.0F});
            ImGui::SetNextWindowSize({fieldW, 0.0F});

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 5.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.0F, 0.0F});
            ImGui::PushStyleColor(ImGuiCol_PopupBg, WizardCss::Bg2());
            ImGui::PushStyleColor(ImGuiCol_Border, WizardCss::Border2());

            if (ImGui::BeginPopup(popupId.c_str(), ImGuiWindowFlags_NoMove))
            {
                const ImVec2 pMin = ImGui::GetWindowPos();
                const ImVec2 pMax = {pMin.x + ImGui::GetWindowWidth(), pMin.y + ImGui::GetWindowHeight()};

                // Yumuşak gölge: modal backdrop ile aynı yöntem — katmanlı, yarı saydam
                // dikdörtgenler, GetBackgroundDrawList() üzerinde (popup'ın ARKASINDA
                // çizilir). Önceki sürümde tek kalın AddRect + foreground draw list
                // kullanılmıştı; bu popup'ın üstüne binen kalın siyah bir çerçeve gibi
                // görünüyor ve altta ayrı bir liste varmış hissi veriyordu.
                auto *bgdl = ImGui::GetBackgroundDrawList();
                constexpr int shadowLayers = 12;
                for (int i = shadowLayers; i >= 1; --i)
                {
                    const float t = static_cast<float>(i) / static_cast<float>(shadowLayers);
                    const float spread = 16.0F * t;
                    const float alpha = 0.45F * (1.0F - t) * 0.11F;
                    bgdl->AddRectFilled({pMin.x - spread, pMin.y + 3.0F - spread * 0.25F},
                                        {pMax.x + spread, pMax.y + 3.0F + spread},
                                        Theme::U32(ImVec4{0.0F, 0.0F, 0.0F, alpha}),
                                        6.0F + spread);
                }

                for (int i = 0; i < itemCount; ++i)
                {
                    ImGui::PushID(i);
                    const bool isSelected = (*value == i);
                    const ImVec2 rowMin = ImGui::GetCursorScreenPos();
                    const float rowW = ImGui::GetContentRegionAvail().x;
                    constexpr float rowH = 28.0F;

                    ImGui::InvisibleButton("##row", {rowW, rowH});
                    const bool rowHovered = ImGui::IsItemHovered();
                    if (ImGui::IsItemClicked())
                    {
                        *value = i;
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }

                    auto *pdl = ImGui::GetWindowDrawList();
                    if (rowHovered || isSelected)
                    {
                        pdl->AddRectFilled(rowMin, {rowMin.x + rowW, rowMin.y + rowH},
                                           Theme::U32(isSelected ? WizardCss::AccentSoft() : WizardCss::Hover()));
                    }
                    if (isSelected)
                    {
                        pdl->AddRectFilled({rowMin.x, rowMin.y + 4.0F},
                                           {rowMin.x + 2.5F, rowMin.y + rowH - 4.0F},
                                           Theme::U32(WizardCss::Accent()));
                    }

                    ImFont *font = f.mono ? f.mono : ImGui::GetFont();
                    pdl->AddText(font, 12.0F,
                                 {rowMin.x + 14.0F, rowMin.y + (rowH - 12.0F) * 0.5F},
                                 Theme::U32(isSelected ? WizardCss::Text() : WizardCss::Muted()),
                                 items[i]);

                    if (isSelected)
                    {
                        const char *check = "\xE2\x9C\x93";
                        const ImVec2 cs = font->CalcTextSizeA(12.0F, FLT_MAX, 0.0F, check);
                        pdl->AddText(font, 12.0F,
                                     {rowMin.x + rowW - cs.x - 14.0F, rowMin.y + (rowH - 12.0F) * 0.5F},
                                     Theme::U32(WizardCss::Accent()), check);
                    }

                    ImGui::PopID();
                }
                ImGui::EndPopup();
            }
            
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(4);

            ImGui::PopID();
            return changed;
        }

        void WizardSectionTitle(const char *upperCaseLabel, const Fonts &f)
        {
            // .section-title { font:700 12px var(--mono); letter-spacing:.8px; color:var(--mut); margin:0 0 14px }
            ScopedTextStyle ts(f.monoSemiBold, 12.0F, Theme::FontPx::MonoSemiBold);
            ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Muted());
            ImGui::TextUnformatted(upperCaseLabel);
            ImGui::PopStyleColor();
        }

        void WizardFieldLabel(const char *upperCaseLabel, const Fonts &f)
        {
            // .field label { font:11px var(--mono); color:var(--mut); margin-bottom:4px; letter-spacing:.5px }
            ScopedTextStyle ts(f.mono, 11.0F, Theme::FontPx::Mono);
            ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Muted());
            ImGui::TextUnformatted(upperCaseLabel);
            ImGui::PopStyleColor();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (ImGui::GetStyle().ItemSpacing.y - WizardLayout::FieldLabelGap));
        }

        void WizardHint(const char *text, const Fonts &f)
        {
            // .hint { color:var(--dim); font:10.5px var(--mono); margin-top:3px }
            ScopedTextStyle ts(f.mono, 10.5F, Theme::FontPx::Mono);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (ImGui::GetStyle().ItemSpacing.y - WizardLayout::HintGap));
            ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Dim());
            ImGui::TextWrapped("%s", text);
            ImGui::PopStyleColor();
        }

        void ErrorText(const char *text, const Fonts &f)
        {
            ScopedTextStyle ts(f.mono, 11.0F, Theme::FontPx::Mono);
            ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Err());
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (ImGui::GetStyle().ItemSpacing.y - WizardLayout::HintGap));
            ImGui::TextWrapped("%s", text);
            ImGui::PopStyleColor();
        }

        void DrawInputField(const char *label,
                            char *buffer,
                            const size_t bufferSize,
                            const float width,
                            const Fonts &f,
                            const char *hint = nullptr,
                            const bool error = false,
                            const char *errorText = nullptr)
        {
            ImGui::PushID(label);
            ImGui::BeginGroup();
            WizardFieldLabel(label, f);
            ImGui::PushItemWidth(width);
            [[maybe_unused]] const bool changed = InputTextCss("##value", buffer, bufferSize, f, error);
            ImGui::PopItemWidth();
            if (error && errorText)
            {
                ErrorText(errorText, f);
            }
            else if (hint)
            {
                WizardHint(hint, f);
            }
            ImGui::EndGroup();
            ImGui::PopID();
        }

        void DrawComboField(const char *label,
                            int *value,
                            const char *const items[],
                            const int itemCount,
                            const float width,
                            const Fonts &f,
                            const char *hint = nullptr)
        {
            ImGui::PushID(label);
            ImGui::BeginGroup();
            WizardFieldLabel(label, f);
            ImGui::PushItemWidth(width);
            [[maybe_unused]] const bool changed = ComboCss("##value", value, items, itemCount, f);
            ImGui::PopItemWidth();
            if (hint)
            {
                WizardHint(hint, f);
            }
            ImGui::EndGroup();
            ImGui::PopID();
        }

        void CheckboxCss(const char *label, bool *value, const Fonts &f)
        {
            // .check { display:flex; align-items:center; gap:8px; font:12px var(--sans); color:var(--mut) }
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{0.0F, 0.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2{8.0F, 0.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, WizardLayout::Radius);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, WizardCss::Bg3());
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, WizardCss::Hover());
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, WizardCss::Hover());
            ImGui::PushStyleColor(ImGuiCol_Border, WizardCss::Border());
            ImGui::PushStyleColor(ImGuiCol_CheckMark, WizardCss::Accent());
            ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Muted());
            {
                ScopedTextStyle ts(f.sans, 12.0F, Theme::FontPx::Sans);
                ImGui::Checkbox(label, value);
            }
            ImGui::PopStyleColor(6);
            ImGui::PopStyleVar(4);
        }

        void DrawNewProjectBackdrop(const ImGuiViewport *vp, const ImVec2 modalPos, const ImVec2 modalSize)
        {
            auto *dl = ImGui::GetBackgroundDrawList();

            // Remove the decorative radial glow behind the modal.
            // Keep only a subtle dark overlay + the modal shadow so the dialog
            // feels focused without adding extra background styling.
            dl->AddRectFilled(vp->WorkPos,
                              {vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y},
                              IM_COL32(0, 0, 0, 90));

            // box-shadow: 0 28px 80px rgba(0,0,0,.55). ImDrawList has no blur,
            // so approximate with layered rounded rects behind the modal.
            constexpr int shadowLayers = 18;
            for (int i = shadowLayers; i >= 1; --i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(shadowLayers);
                const float spread = 80.0F * t;
                const float alpha = 0.55F * (1.0F - t) * 0.075F;
                const ImVec4 col{0.0F, 0.0F, 0.0F, alpha};
                dl->AddRectFilled({modalPos.x - spread, modalPos.y + 28.0F - spread},
                                  {modalPos.x + modalSize.x + spread, modalPos.y + modalSize.y + 28.0F + spread},
                                  Theme::U32(col),
                                  WizardLayout::ModalRadius + spread);
            }
        }

        // .header { height:58px; padding:0 22px; background:var(--bg0); border-bottom:1px solid var(--bd) }
        [[nodiscard]] bool DrawWizardHeader(NewProjectState &st, const Fonts &f, const EditorTextures &tx)
        {
            using namespace Theme;
            using namespace WizardLayout;

            bool closeRequested = false;

            ImGui::PushStyleColor(ImGuiCol_ChildBg, WizardCss::Bg0());
            ImGui::BeginChild("WizHdr", {0, HeaderH}, false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            const ImVec2 headerPos = ImGui::GetWindowPos();
            const float headerW = ImGui::GetWindowWidth();

            // .title { font:700 14px mono; gap:9px; align-items:center }
            ImGui::SetCursorPos({HeaderPadX, 12.0F});
            if (tx.logo)
            {
                ImGui::Image(tx.logo, {20.0F, 20.0F});
                ImGui::SameLine(0.0F, 9.0F);
            }
            {
                ScopedTextStyle ts(f.monoSemiBold, 14.0F, FontPx::MonoSemiBold);
                ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Text());
                ImGui::TextUnformatted("NEW PROJECT");
                ImGui::PopStyleColor();
            }

            // .subtitle { font:11px mono; color:var(--dim); margin-top:3px }
            ImGui::SetCursorPos({HeaderPadX, 36.0F});
            {
                ScopedTextStyle ts(f.mono, 11.0F, FontPx::Mono);
                ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Dim());
                ImGui::TextUnformatted("Create portable .horo metadata and starter content");
                ImGui::PopStyleColor();
            }

            // close button: SVG-style X, dark bg on hover
            const ImVec2 closeSize{38.0F, 36.0F};
            ImGui::SetCursorPos({headerW - HeaderPadX - closeSize.x, 11.0F});
            if (DrawCloseButton(closeSize))
            {
                st.open = false;
                closeRequested = true;
            }

            // header bottom border, without turning the header into a bordered child
            ImGui::GetWindowDrawList()->AddLine(
                {headerPos.x, headerPos.y + HeaderH - 1.0F},
                {headerPos.x + headerW, headerPos.y + HeaderH - 1.0F},
                Theme::U32(WizardCss::Border()),
                1.0F);

            ImGui::EndChild();
            ImGui::PopStyleColor();

            return closeRequested;
        }

        // .steps { width:220px; background:var(--bg2); border-right:1px solid var(--bd); padding:18px 14px }
        void DrawWizardSidebar(NewProjectState &st, const Fonts &f, const float bodyH)
        {
            using namespace Theme;
            using namespace WizardLayout;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{SidebarPadX, SidebarPadY});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, WizardCss::Bg2());
            ImGui::BeginChild("WizSide", {SidebarW, bodyH}, false,
                              ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoScrollWithMouse |
                                  ImGuiWindowFlags_AlwaysUseWindowPadding);

            const ImVec2 sidePos = ImGui::GetWindowPos();
            const float sideH = ImGui::GetWindowHeight();
            auto *dl = ImGui::GetWindowDrawList();

            static constexpr const char *kStepLabels[] = {"Template", "Identity", "Settings", "Review"};
            static constexpr const char *kStepDescs[] = {"Choose starter", "Name & location", "Runtime defaults", "Validate & create"};

            for (int s = 1; s <= 4; ++s)
            {
                ImGui::PushID(s);

                const bool active = (st.step == s);
                const ImVec2 rowMin = ImGui::GetCursorScreenPos();
                const float rowW = ImGui::GetContentRegionAvail().x;
                const ImVec2 rowSize{rowW, StepH};

                if (active)
                {
                    dl->AddRectFilled(rowMin,
                                      {rowMin.x + rowSize.x, rowMin.y + rowSize.y},
                                      Theme::U32(WizardCss::AccentSoft()),
                                      Radius);
                }

                ImGui::InvisibleButton("##step", rowSize);
                if (ImGui::IsItemClicked())
                {
                    st.step = s;
                }

                // .step .n { width:22px; height:22px; border-radius:50%; margin/padding alignment }
                const ImVec2 circleCenter{rowMin.x + 10.0F + 11.0F, rowMin.y + 11.0F + 11.0F};
                dl->AddCircleFilled(circleCenter, 11.0F, Theme::U32(active ? WizardCss::Accent() : WizardCss::Bg3()), 24);
                dl->AddCircle(circleCenter, 11.0F, Theme::U32(active ? WizardCss::Accent() : WizardCss::Border()), 24, 1.0F);

                const char *number = (s == 1) ? "1" : (s == 2) ? "2"
                                                  : (s == 3)   ? "3"
                                                               : "4";
                ImFont *numberFont = f.mono ? f.mono : ImGui::GetFont();
                const float numberFontSize = 11.0F;
                const ImVec2 numberSize = numberFont->CalcTextSizeA(numberFontSize, FLT_MAX, 0.0F, number);
                dl->AddText(numberFont,
                            numberFontSize,
                            {circleCenter.x - numberSize.x * 0.5F, circleCenter.y - numberSize.y * 0.5F},
                            Theme::U32(active ? WizardCss::DarkText() : WizardCss::Dim()),
                            number);

                ImGui::SetCursorScreenPos({rowMin.x + 42.0F, rowMin.y + 10.0F});
                {
                    ScopedTextStyle ts(f.sans, 13.0F, FontPx::Sans);
                    ImGui::PushStyleColor(ImGuiCol_Text, active ? WizardCss::Text() : WizardCss::Muted());
                    ImGui::TextUnformatted(kStepLabels[s - 1]);
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({rowMin.x + 42.0F, rowMin.y + 31.0F});
                {
                    ScopedTextStyle ts(f.mono, 10.0F, FontPx::Mono);
                    ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Dim());
                    ImGui::TextUnformatted(kStepDescs[s - 1]);
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({rowMin.x, rowMin.y + StepH + StepGap});
                ImGui::PopID();
            }

            // only right separator, no boxed sidebar border
            dl->AddLine({sidePos.x + SidebarW - 1.0F, sidePos.y},
                        {sidePos.x + SidebarW - 1.0F, sidePos.y + sideH},
                        Theme::U32(WizardCss::Border()),
                        1.0F);

            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }

        // Step 1 — .template-grid { grid-template-columns:repeat(3,1fr); gap:10px }
        void DrawStepTemplate(NewProjectState &st, const Fonts &f)
        {
            using namespace Theme;
            using namespace WizardLayout;

            static constexpr const char *kDescs[] = {
                "No starter scene. Minimal asset tree and project.json.",
                "Scene, camera, directional light, floor, material defaults.",
                "Character controller, input map, capsule, and test level.",
                "Create from a verified template package and lockfile.",
                "Rendering samples, observability overlays, benchmark scene.",
                "Pick systems manually before project generation."};

            WizardSectionTitle("CHOOSE A TEMPLATE", f);
            ImGui::Dummy({0.0F, 14.0F});

            const float cardW = (ImGui::GetContentRegionAvail().x - TemplateGap * 2.0F) / 3.0F;

            for (int i = 0; i < 6; ++i)
            {
                if (i % 3 != 0)
                {
                    ImGui::SameLine(0.0F, TemplateGap);
                }

                ImGui::PushID(i);
                const bool selected = (st.selectedTemplate == i);

                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, TemplateRadius);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{TemplatePad, TemplatePad});
                ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0F);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, selected ? WizardCss::AccentSoft() : WizardCss::Bg2());
                ImGui::PushStyleColor(ImGuiCol_Border, selected ? WizardCss::Accent() : WizardCss::Border());

                ImGui::BeginChild("TemplateCard",
                                  {cardW, TemplateH},
                                  true,
                                  ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoScrollWithMouse |
                                      ImGuiWindowFlags_AlwaysUseWindowPadding);

                // HTML: .template-icon { font-size:24px; margin-bottom:8px }
                // Draw the square manually instead of relying on the □ glyph;
                // this avoids fallback '?' boxes when the runtime font atlas is incomplete.
                if (i == 0)
                {
                    const ImVec2 iconPos = ImGui::GetCursorScreenPos();
                    const float squareSize = 18.0F;
                    const float squareOffset = (TemplateIconPx - squareSize) * 0.5F;
                    ImGui::GetWindowDrawList()->AddRect(
                        {iconPos.x + squareOffset, iconPos.y + squareOffset},
                        {iconPos.x + squareOffset + squareSize, iconPos.y + squareOffset + squareSize},
                        Theme::U32(WizardCss::Text()),
                        3.0F,
                        0,
                        2.0F);
                    ImGui::Dummy({TemplateIconPx, TemplateIconPx});
                    ImGui::Dummy({0.0F, 8.0F});
                }
                else
                {
                    // Empty .template-icon div still contributes its 8px bottom margin in the HTML.
                    ImGui::Dummy({0.0F, 8.0F});
                }

                {
                    // HTML: .template-name { font:600 13px var(--sans); margin-bottom:4px }
                    // Draw with ImDrawList::AddText(font, pixel_size, ...) so the card uses
                    // exact CSS-sized text instead of inheriting a smaller window font scale.
                    ImFont *nameFont = f.sans ? f.sans : ImGui::GetFont();
                    const ImVec2 namePos = ImGui::GetCursorScreenPos();
                    const char *name = kTemplateNames[i];
                    ImGui::GetWindowDrawList()->AddText(nameFont, TemplateNamePx, namePos, Theme::U32(WizardCss::Text()), name);
                    const ImVec2 nameSize = nameFont->CalcTextSizeA(TemplateNamePx, FLT_MAX, 0.0F, name);
                    ImGui::Dummy({nameSize.x, nameSize.y});
                }

                ImGui::Dummy({0.0F, 4.0F});

                {
                    // HTML: .template-desc { font:11px var(--mono); color:var(--mut) }
                    ImFont *descFont = f.mono ? f.mono : ImGui::GetFont();
                    const ImVec2 descPos = ImGui::GetCursorScreenPos();
                    const float wrapW = cardW - TemplatePad * 2.0F;
                    const char *desc = kDescs[i];
                    ImGui::GetWindowDrawList()->AddText(descFont, TemplateDescPx, descPos, Theme::U32(WizardCss::Muted()), desc, nullptr, wrapW);
                    const ImVec2 descSize = descFont->CalcTextSizeA(TemplateDescPx, FLT_MAX, wrapW, desc);
                    ImGui::Dummy({wrapW, descSize.y});
                }

                ImGui::EndChild();

                const bool hovered = ImGui::IsItemHovered();
                if (hovered || selected)
                {
                    DrawCssBorderForLastItem(hovered && !selected ? WizardCss::Border2() : WizardCss::Accent(),
                                             TemplateRadius,
                                             selected ? 1.5F : 1.0F,
                                             0.0F);
                }

                if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    st.selectedTemplate = i;
                }

                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(3);
                ImGui::PopID();

                if (i == 2)
                {
                    ImGui::Dummy({0.0F, TemplateGap});
                }
            }
        }

        // Step 2 — .full-field* + Project Directory card
        void DrawStepIdentity(NewProjectState &st, const Fonts &f, const bool pathOccupied)
        {
            using namespace WizardLayout;

            WizardSectionTitle("PROJECT IDENTITY", f);
            ImGui::Dummy({0.0F, 14.0F});

            // HTML: <div style="display:flex;flex-direction:column;gap:14px">
            DrawInputField("PROJECT NAME",
                           st.name,
                           sizeof(st.name),
                           -1.0F,
                           f,
                           "Stored as project.json name; projectId is generated once.");
            ImGui::Dummy({0.0F, GridGap});

            DrawInputField("PROJECT PATH",
                           st.path,
                           sizeof(st.path),
                           -1.0F,
                           f,
                           nullptr,
                           pathOccupied,
                           "Directory already exists; choose an empty folder or import existing project.");
            ImGui::Dummy({0.0F, GridGap});

            DrawInputField("PROJECT VERSION",
                           st.version,
                           sizeof(st.version),
                           -1.0F,
                           f,
                           "Game/product version. Does not select project-format migrations.");
            ImGui::Dummy({0.0F, GridGap});

            DrawInputField("DEFAULT SCENE", st.defaultScene, sizeof(st.defaultScene), -1.0F, f);
            ImGui::Dummy({0.0F, CardGap});

            {
                ScopedCard card("DirCard", {0.0F, 220.0F}, CardPad, CardPad, WizardCss::Bg2());
                WizardSectionTitle("PROJECT DIRECTORY", f);
                ImGui::Dummy({0.0F, 8.0F});
                ScopedTextStyle ts(f.mono, 11.0F, Theme::FontPx::Mono);
                ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Muted());
                ImGui::TextUnformatted(
                    "MyGame/\n"
                    "  .horo/\n"
                    "    project.json          \xe2\x86\x90 identity & settings\n"
                    "    plugins.json          \xe2\x86\x90 portable plugin deps\n"
                    "    editor workspace.json \xe2\x86\x90 local UI state (not committed)\n"
                    "    asset index.json      \xe2\x86\x90 derived lookup (not committed)\n"
                    "    local/                \xe2\x86\x90 machine overrides (not committed)\n"
                    "  assets/                 \xe2\x86\x90 source assets\n"
                    "    models/ textures/ materials/ shaders/ scenes/\n"
                    "  src/                    \xe2\x86\x90 optional game code\n"
                    "  build/                  \xe2\x86\x90 generated output (not committed)");
                ImGui::PopStyleColor();
            }
        }

        // Step 3 — Runtime Defaults / Required Toolchain / Optional cards
        void DrawStepSettings(NewProjectState &st, const Fonts &f)
        {
            using namespace WizardLayout;
            static constexpr const char *kRenderBackend[] = {"opengl", "vulkan", "auto detect"};
            static constexpr const char *kPhysics[] = {"Enabled", "Disabled"};
            static constexpr const char *kBuildProfile[] = {"desktop-debug", "desktop-profile", "desktop-release"};
            static constexpr const char *kAssetCompression[] = {"lz4", "none", "zstd"};
            static constexpr const char *kTextureCompression[] = {"bc7", "bc5", "astc", "none"};
            static constexpr const char *kPlatform[] = {"host", "windows", "linux", "macos"};
            static constexpr const char *kCompiler[] = {"default", "clang", "gcc", "msvc"};
            static constexpr const char *kCppStd[] = {"C++20", "C++17"};

            {
                ScopedCard card("RtCard", {0.0F, 262.0F}, CardPad, CardPad, WizardCss::Bg2());
                WizardSectionTitle("RUNTIME DEFAULTS", f);
                ImGui::Dummy({0.0F, 10.0F});

                const float colW = (ImGui::GetContentRegionAvail().x - GridGap) * 0.5F;

                DrawComboField("RENDER BACKEND", &st.renderBackend, kRenderBackend, 3, colW, f,
                               "Default: opengl. Override per host profile.");
                ImGui::SameLine(0.0F, GridGap);
                DrawInputField("TARGET FRAME RATE", st.targetFps, sizeof(st.targetFps), colW, f);

                ImGui::Dummy({0.0F, GridGap});

                DrawComboField("PHYSICS", &st.physics, kPhysics, 2, colW, f);
                ImGui::SameLine(0.0F, GridGap);
                DrawComboField("BUILD PROFILE", &st.buildProfile, kBuildProfile, 3, colW, f);

                ImGui::Dummy({0.0F, GridGap});

                DrawComboField("ASSET COMPRESSION", &st.assetCompression, kAssetCompression, 3, colW, f);
                ImGui::SameLine(0.0F, GridGap);
                DrawComboField("TEXTURE COMPRESSION", &st.textureCompression, kTextureCompression, 4, colW, f);
            }

            ImGui::Dummy({0.0F, CardGap});

            {
                ScopedCard card("TcCard", {0.0F, 212.0F}, CardPad, CardPad, WizardCss::Bg2());
                WizardSectionTitle("REQUIRED TOOLCHAIN", f);
                ImGui::Dummy({0.0F, 10.0F});

                const float colW = (ImGui::GetContentRegionAvail().x - GridGap) * 0.5F;

                DrawComboField("TARGET PLATFORM", &st.targetPlatform, kPlatform, 4, colW, f);
                ImGui::SameLine(0.0F, GridGap);
                DrawComboField("COMPILER FAMILY", &st.compilerFamily, kCompiler, 4, colW, f);

                ImGui::Dummy({0.0F, GridGap});

                DrawComboField("MINIMUM C++ STANDARD", &st.cppStandard, kCppStd, 2, colW, f);

                ImGui::Dummy({0.0F, 12.0F});
                WizardHint("Portable project settings describe build intent. Machine-specific paths and SDK "
                           "locations are resolved by user-level toolchain profiles, never stored in project.json.",
                           f);
            }

            ImGui::Dummy({0.0F, CardGap});

            {
                ScopedCard card("OptCard", {0.0F, 180.0F}, CardPad, CardPad, WizardCss::Bg2());
                WizardSectionTitle("OPTIONAL", f);
                ImGui::Dummy({0.0F, 14.0F});

                CheckboxCss("Initialize git repository", &st.initGit, f);
                ImGui::Dummy({0.0F, CheckGap});
                CheckboxCss("Restore packages after creation", &st.restorePackages, f);
                ImGui::Dummy({0.0F, CheckGap});
                CheckboxCss("Include starter content", &st.includeStarter, f);
                ImGui::Dummy({0.0F, CheckGap});
                CheckboxCss("Generate CMake project files", &st.generateCMake, f);
            }
        }

        // .summary-row { display:flex; justify-content:space-between; padding:8px 0; border-bottom:1px dashed var(--bd) }
        void SummaryRow(const char *label, const std::string &value, const Fonts &f, const bool warn, const bool last = false)
        {

            const ImVec2 rowStart = ImGui::GetCursorScreenPos();
            const float rowW = ImGui::GetContentRegionAvail().x;
            constexpr float rowH = 26.0F;

            {
                ScopedTextStyle ts(f.sans, 12.0F, Theme::FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Muted());
                ImGui::TextUnformatted(label);
                ImGui::PopStyleColor();
            }

            float valueW = 0.0F;
            {
                ScopedTextStyle ts(f.mono, 11.0F, Theme::FontPx::Mono);
                valueW = ImGui::CalcTextSize(value.c_str()).x;
            }

            ImGui::SameLine(std::max(0.0F, rowW - valueW));
            {
                ScopedTextStyle ts(f.mono, 11.0F, Theme::FontPx::Mono);
                if (warn)
                    ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Warn());
                else
                    ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Text());
                ImGui::TextUnformatted(value.c_str());
                ImGui::PopStyleColor();
            }

            ImGui::SetCursorScreenPos({rowStart.x, rowStart.y + rowH});
            if (!last)
            {
                auto *dl = ImGui::GetWindowDrawList();
                const ImU32 col = Theme::U32(WizardCss::Border());
                for (float x = 0.0F; x < rowW; x += 7.0F)
                {
                    dl->AddLine({rowStart.x + x, rowStart.y + rowH - 1.0F},
                                {rowStart.x + std::min(x + 4.0F, rowW), rowStart.y + rowH - 1.0F},
                                col,
                                1.0F);
                }
            }
        }

        // Step 4 — Review cards
        void DrawStepReview(const NewProjectState &st, const Fonts &f, const bool pathOccupied)
        {
            using namespace WizardLayout;
            static constexpr const char *kBuildProfile[] = {"desktop-debug", "desktop-profile", "desktop-release"};
            static constexpr const char *kRenderBackend[] = {"opengl", "vulkan", "auto detect"};
            static constexpr const char *kPhysics[] = {"Enabled", "Disabled"};

            WizardSectionTitle("REVIEW & CREATE", f);
            ImGui::Dummy({0.0F, 14.0F});

            {
                ScopedCard card("RevCard1", {0.0F, 250.0F}, CardPad, CardPad, WizardCss::Bg2());
                SummaryRow("Template", kTemplateNames[st.selectedTemplate], f, false);
                SummaryRow("Project Name", st.name, f, false);
                SummaryRow("Project Path", st.path, f, pathOccupied);
                SummaryRow("Project Version", st.version, f, false);
                SummaryRow("Default Scene", st.defaultScene, f, false);
                SummaryRow("Render Backend", kRenderBackend[st.renderBackend], f, false);
                SummaryRow("Physics", kPhysics[st.physics], f, false);
                SummaryRow("Build Profile", kBuildProfile[st.buildProfile], f, false, true);
            }

            ImGui::Dummy({0.0F, CardGap});

            {
                ScopedCard card("RevCard2", {0.0F, 232.0F}, CardPad, CardPad, WizardCss::Bg2());
                WizardSectionTitle("WHAT WILL BE CREATED", f);
                ImGui::Dummy({0.0F, 6.0F});
                SummaryRow("Portable metadata (commit)", ".horo/project.json, .horo/plugins.json, asset sidecars", f, false);
                SummaryRow("Local / derived (ignore)", ".horo/editor workspace.json, .horo/asset index.json, .horo/local/", f, false);
                SummaryRow("Build output (ignore)", "build/", f, false);
                SummaryRow("Project schema", "formatVersion 1 · projectId generated", f, false);
                SummaryRow("Validation mode",
                           pathOccupied ? "Edit — 1 blocking issue (path exists)" : "Ready to create",
                           f,
                           pathOccupied);
                SummaryRow("Recommended .gitignore", ".horo/{editor workspace,asset index}.json .horo/local/ build/", f, false, true);
            }
        }

        // .footer { height:52px; padding:0 22px; background:var(--bg0); border-top:1px solid var(--bd) }
        void DrawWizardFooter(NewProjectState &st, const Fonts &f, const bool pathOccupied)
        {
            using namespace WizardLayout;

            ImGui::PushStyleColor(ImGuiCol_ChildBg, WizardCss::Bg0());
            ImGui::BeginChild("WizFtr", {0, FooterH}, false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            const ImVec2 footerPos = ImGui::GetWindowPos();
            const float footerW = ImGui::GetWindowWidth();
            auto *dl = ImGui::GetWindowDrawList();

            dl->AddLine({footerPos.x, footerPos.y},
                        {footerPos.x + footerW, footerPos.y},
                        Theme::U32(WizardCss::Border()),
                        1.0F);

            // HTML status: green dot even when text says validation failed.
            const ImVec2 dotCenter{footerPos.x + 22.0F + 4.0F, footerPos.y + 26.0F};
            dl->AddCircleFilled(dotCenter, 4.0F, Theme::U32(WizardCss::Ok()), 16);

            ImGui::SetCursorPos({38.0F, 18.0F});
            {
                ScopedTextStyle ts(f.mono, 12.0F, Theme::FontPx::Mono);
                ImGui::PushStyleColor(ImGuiCol_Text, WizardCss::Muted());
                if (pathOccupied)
                {
                    ImGui::Text("Template: %s \xC2\xB7 path validation failed", kTemplateNames[st.selectedTemplate]);
                }
                else
                {
                    ImGui::Text("Template: %s", kTemplateNames[st.selectedTemplate]);
                }
                ImGui::PopStyleColor();
            }

            const bool isReview = (st.step == 4);
            const float backW = 80.0F;
            const float nextW = 80.0F;
            const float createW = 120.0F;
            const float btnH = 32.0F;
            const float gap = 8.0F;
            const float actionsW = isReview ? (backW + gap + createW) : (backW + gap + nextW);
            ImGui::SetCursorPos({footerW - 22.0F - actionsW, 10.0F});

            if (DrawWizardButton("\xE2\x86\x90 Back", {backW, btnH}, false, st.step > 1, f))
            {
                st.step--;
            }

            ImGui::SameLine(0.0F, gap);

            if (!isReview)
            {
                if (DrawWizardButton("Next \xE2\x86\x92", {nextW, btnH}, true, true, f))
                {
                    st.step++;
                }
            }
            else
            {
                if (DrawWizardButton("Create Project", {createW, btnH}, true, !pathOccupied, f))
                {
                    st.open = false;
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        // .modal { width:min(900px, calc(100vw - 56px)); height:min(680px, calc(100vh - 56px)); border-radius:8px; overflow:hidden }
        void DrawNewProjectModal(NewProjectState &st, const Fonts &f, const EditorTextures &tx)
        {
            using namespace WizardLayout;

            if (!st.open)
                return;

            const ImGuiViewport *vp = ImGui::GetMainViewport();
            const float modalW = std::min(ModalW, std::max(320.0F, vp->WorkSize.x - ViewportPad));
            const float modalH = std::min(ModalH, std::max(320.0F, vp->WorkSize.y - ViewportPad));
            const ImVec2 modalSize{modalW, modalH};
            const ImVec2 modalPos{
                vp->WorkPos.x + (vp->WorkSize.x - modalW) * 0.5F,
                vp->WorkPos.y + (vp->WorkSize.y - modalH) * 0.5F};

            DrawNewProjectBackdrop(vp, modalPos, modalSize);

            ImGui::SetNextWindowPos(modalPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(modalSize, ImGuiCond_Always);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ModalRadius);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.0F, 0.0F});
            ImGui::PushStyleColor(ImGuiCol_WindowBg, WizardCss::Bg1());
            ImGui::PushStyleColor(ImGuiCol_Border, WizardCss::Border());

            const bool isOpen = ImGui::BeginPopupModal(
                "New Project",
                &st.open,
                ImGuiWindowFlags_NoTitleBar |
                    ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse);

            if (!isOpen)
            {
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(4);
                return;
            }

            const bool pathOccupied = PathLooksOccupied(st.path);

            if (DrawWizardHeader(st, f, tx))
            {
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(4);
                return;
            }

            const float bodyH = ImGui::GetWindowHeight() - HeaderH - FooterH;

            // .body { flex:1; display:grid; grid-template-columns:220px 1fr; overflow:hidden }
            DrawWizardSidebar(st, f, bodyH);
            ImGui::SameLine(0.0F, 0.0F);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{MainPadX, MainPadY});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, WizardCss::Bg1());
            ImGui::BeginChild("WizMain",
                              {0.0F, bodyH},
                              false,
                              ImGuiWindowFlags_AlwaysUseWindowPadding);

            switch (st.step)
            {
            case 1:
                DrawStepTemplate(st, f);
                break;
            case 2:
                DrawStepIdentity(st, f, pathOccupied);
                break;
            case 3:
                DrawStepSettings(st, f);
                break;
            case 4:
                DrawStepReview(st, f, pathOccupied);
                break;
            default:
                st.step = 1;
                DrawStepTemplate(st, f);
                break;
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();

            DrawWizardFooter(st, f, pathOccupied);

            ImGui::EndPopup();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(4);
        }

        // ── settings modal ───────────────────────────────────────────────────────

        void DrawSettingsModal(SettingsState &st, const Fonts &f)
        {
            using namespace Theme;
            if (!st.open)
                return;

            ImGui::SetNextWindowSize({Layout::SettingsW, Layout::SettingsH}, ImGuiCond_Appearing);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Layout::RadiusModal);
            const bool isOpen = ImGui::BeginPopupModal(
                "Settings", &st.open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
            ImGui::PopStyleVar();
            if (!isOpen)
                return;

            if (ImGui::BeginTabBar("ST"))
            {
                if (ImGui::BeginTabItem("General"))
                {
                    ImGui::Spacing();
                    ImGui::TextUnformatted("Startup");
                    ImGui::Separator();
                    ImGui::RadioButton("Welcome screen", &st.startupAction, 0);
                    ImGui::RadioButton("Last project", &st.startupAction, 1);
                    ImGui::RadioButton("Project browser", &st.startupAction, 2);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Editor"))
                {
                    ImGui::Spacing();
                    ImGui::TextUnformatted("Editor Font Size");
                    ImGui::PushItemWidth(80);
                    ImGui::InputText("##fs", st.editorFontSize, sizeof(st.editorFontSize));
                    ImGui::PopItemWidth();
                    ImGui::Checkbox("Show FPS overlay", &st.showFps);
                    ImGui::Checkbox("Auto-save", &st.autoSave);
                    if (st.autoSave)
                    {
                        ImGui::SameLine();
                        ImGui::PushItemWidth(60);
                        ImGui::InputInt("min", &st.autoSaveInterval);
                        ImGui::PopItemWidth();
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Renderer"))
                {
                    ImGui::Spacing();
                    ImGui::TextUnformatted("Render Backend");
                    ImGui::Separator();
                    ImGui::RadioButton("OpenGL", &st.renderBackend, 0);
                    ImGui::RadioButton("Vulkan", &st.renderBackend, 1);
                    ImGui::RadioButton("Metal", &st.renderBackend, 2);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Privacy"))
                {
                    ImGui::Spacing();
                    ImGui::Checkbox("Anonymous telemetry", &st.anonymousTelemetry);
                    {
                        ScopedTextStyle ts(f.mono, 11.0F, FontPx::Mono);
                        ImGui::TextDisabled("No personal data or project content is collected.");
                    }
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }

            ImGui::Spacing();
            ImGui::Separator();
            if (ImGui::Button("Done", {100, 36}))
            {
                st.open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

    } // namespace

    // ── yardımcılar (Horo::Editor içinde, anonim namespace dışında) ────────

    [[nodiscard]] static EditorGuiOptions ParseOptions(int argc, char **argv) noexcept
    {
        EditorGuiOptions opts;
        for (int i = 1; i < argc; ++i)
        {
            std::string_view a{argv[i]};
            if (a == "--text-preview")
                opts.textPreview = true;
            else if (a == "--exit-after-first-frame")
                opts.exitAfterFirstFrame = true;
        }
        return opts;
    }

    [[nodiscard]] static std::vector<RecentProjectEntry> BuildBootstrapRecentProjects()
    {
        return {{{"Desert Run", "~/projects/desert-run", "2h ago", "desert-run"},
                 {"Arena Prototype", "~/projects/arena-proto", "yesterday", "arena-prototype"},
                 {"Tech Demo", "~/projects/tech-demo", "3 days ago", "tech-demo"}}};
    }

    // ── public entry ─────────────────────────────────────────────────────────

    /** @copydoc RunEditorGuiApp */
    int RunEditorGuiApp(const int argc, char **argv)
    {
        auto opts = ParseOptions(argc, argv);
        WelcomeScreenController ctrl{BuildBootstrapRecentProjects()};
        auto vm = ctrl.BuildViewModel();

        if (opts.textPreview)
        {
            std::fputs(RenderWelcomeScreenText(vm).c_str(), stdout);
            return 0;
        }

        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER))
        {
            std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
            return 1;
        }
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

        auto *w = SDL_CreateWindow("Horo Editor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   1000, 760, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!w)
        {
            std::fprintf(stderr, "Window: %s\n", SDL_GetError());
            SDL_Quit();
            return 1;
        }
        auto *gl = SDL_GL_CreateContext(w);
        if (!gl)
        {
            std::fprintf(stderr, "GL: %s\n", SDL_GetError());
            SDL_DestroyWindow(w);
            SDL_Quit();
            return 1;
        }
        SDL_GL_MakeCurrent(w, gl);
        SDL_GL_SetSwapInterval(1);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        auto fonts = LoadEditorFonts(io);
        auto textures = LoadEditorTextures();
        Theme::Apply(ImGui::GetStyle());

        constexpr auto *glsl = "#version 150";
        ImGui_ImplSDL2_InitForOpenGL(w, gl);
        ImGui_ImplOpenGL3_Init(glsl);

        bool run = true;
        GuiAction act = GuiAction::None;
        NewProjectState np;
        SettingsState sets;
        while (run)
        {
            SDL_Event ev;
            while (SDL_PollEvent(&ev))
            {
                ImGui_ImplSDL2_ProcessEvent(&ev);
                if (ev.type == SDL_QUIT)
                    run = false;
                if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE &&
                    ev.window.windowID == SDL_GetWindowID(w))
                    run = false;
            }
            act = GuiAction::None;
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();

            DrawWelcomeScreen(vm, fonts, textures, act);

            if (act == GuiAction::NewProject)
            {
                np.open = true;
                ImGui::OpenPopup("New Project");
            }
            else if (act == GuiAction::OpenSettings)
            {
                sets.open = true;
                ImGui::OpenPopup("Settings");
            }

            DrawNewProjectModal(np, fonts, textures);
            DrawSettingsModal(sets, fonts);

            ImGui::Render();
            glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
            glClearColor(Theme::Bg0().x, Theme::Bg0().y, Theme::Bg0().z, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            SDL_GL_SwapWindow(w);
            if (opts.exitAfterFirstFrame)
                run = false;
        }

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        if (textures.logo)
        {
            GLuint t = (GLuint)(intptr_t)textures.logo;
            glDeleteTextures(1, &t);
        }
        SDL_GL_DeleteContext(gl);
        SDL_DestroyWindow(w);
        SDL_Quit();
        return 0;
    }

} // namespace Horo::Editor