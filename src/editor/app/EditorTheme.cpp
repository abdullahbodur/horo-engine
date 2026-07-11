#include "Horo/Editor/EditorTheme.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace Horo::Editor::Theme
{

    namespace
    {
        using namespace DesignSystem;

        void ApplyHoroDark(ImGuiStyle &style);   // forward — defined below
        void ApplyMidnight(ImGuiStyle &style);   // forward — defined below
        void ApplyLight(ImGuiStyle &style);      // forward — defined below

        // ── Active design tokens (runtime-switchable) ──────────────────
        DesignTokens &ActiveTokens()
        {
            static DesignTokens tokens = DefaultDesignTokens();
            return tokens;
        }

        /** @brief Light theme design tokens — surface colors inverted, accents unchanged. */
        void ApplyLightDesignTokens()
        {
            ActiveTokens() = DesignTokens{
                ColorTokens{
                    ::ImVec4{0.941F, 0.937F, 0.929F, 1.0F},  // surfaceRoot
                    ::ImVec4{0.961F, 0.957F, 0.953F, 1.0F},  // surfaceWindow
                    ::ImVec4{0.929F, 0.925F, 0.918F, 1.0F},  // surfacePanel
                    ::ImVec4{0.902F, 0.898F, 0.890F, 1.0F},  // surfaceRaised
                    ::ImVec4{0.867F, 0.863F, 0.855F, 1.0F},  // surfaceHover
                    ::ImVec4{0.784F, 0.780F, 0.773F, 1.0F},  // border
                    ::ImVec4{0.659F, 0.655F, 0.647F, 1.0F},  // borderStrong
                    ::ImVec4{0.125F, 0.129F, 0.137F, 1.0F},  // textPrimary
                    ::ImVec4{0.400F, 0.396F, 0.388F, 1.0F},  // textMuted
                    ::ImVec4{0.529F, 0.525F, 0.518F, 1.0F},  // textDim
                    ::ImVec4{0.016F, 0.647F, 0.988F, 1.0F},  // actionPrimary
                    ::ImVec4{0.180F, 0.706F, 0.992F, 1.0F},  // actionPrimaryHover
                    ::ImVec4{0.000F, 0.500F, 0.820F, 1.0F},  // actionPrimaryActive
                    ::ImVec4{0.016F, 0.647F, 0.988F, 0.15F}, // actionPrimarySoft
                    ::ImVec4{0.373F, 0.722F, 0.541F, 1.0F},  // statusOk
                    ::ImVec4{0.910F, 0.639F, 0.239F, 1.0F},  // statusWarn
                    ::ImVec4{0.831F, 0.322F, 0.290F, 1.0F},  // statusError
                    ::ImVec4{0.071F, 0.082F, 0.102F, 1.0F},  // textOnActionPrimary
                },
                TypographyTokens{16.0F, 14.0F, 16.0F},
                RadiusTokens{4.0F, 6.0F, 8.0F},
                SizeTokens{280.0F, 32.0F, 900.0F, 680.0F, 58.0F, 52.0F, 220.0F, 620.0F, 440.0F},
                SpacingTokens{18.0F, 14.0F, 28.0F, 24.0F, 14.0F, 18.0F},
            };
        }

        /** @brief Midnight theme design tokens — dark surface with purple accents. */
        void ApplyMidnightDesignTokens()
        {
            ActiveTokens() = DesignTokens{
                ColorTokens{
                    ::ImVec4{0.027F, 0.039F, 0.063F, 1.0F},  // surfaceRoot
                    ::ImVec4{0.047F, 0.063F, 0.094F, 1.0F},  // surfaceWindow
                    ::ImVec4{0.059F, 0.078F, 0.110F, 1.0F},  // surfacePanel
                    ::ImVec4{0.086F, 0.106F, 0.149F, 1.0F},  // surfaceRaised
                    ::ImVec4{0.106F, 0.125F, 0.173F, 1.0F},  // surfaceHover
                    ::ImVec4{0.149F, 0.169F, 0.216F, 1.0F},  // border
                    ::ImVec4{0.212F, 0.231F, 0.278F, 1.0F},  // borderStrong
                    ::ImVec4{0.867F, 0.855F, 0.898F, 1.0F},  // textPrimary
                    ::ImVec4{0.600F, 0.580F, 0.640F, 1.0F},  // textMuted
                    ::ImVec4{0.361F, 0.349F, 0.400F, 1.0F},  // textDim
                    ::ImVec4{0.447F, 0.282F, 0.847F, 1.0F},  // actionPrimary
                    ::ImVec4{0.545F, 0.400F, 0.902F, 1.0F},  // actionPrimaryHover
                    ::ImVec4{0.369F, 0.220F, 0.749F, 1.0F},  // actionPrimaryActive
                    ::ImVec4{0.447F, 0.282F, 0.847F, 0.15F}, // actionPrimarySoft
                    ::ImVec4{0.373F, 0.722F, 0.541F, 1.0F},  // statusOk
                    ::ImVec4{0.910F, 0.639F, 0.239F, 1.0F},  // statusWarn
                    ::ImVec4{0.831F, 0.322F, 0.290F, 1.0F},  // statusError
                    ::ImVec4{0.020F, 0.075F, 0.110F, 1.0F},  // textOnActionPrimary
                },
                TypographyTokens{16.0F, 14.0F, 16.0F},
                RadiusTokens{4.0F, 6.0F, 8.0F},
                SizeTokens{280.0F, 32.0F, 900.0F, 680.0F, 58.0F, 52.0F, 220.0F, 620.0F, 440.0F},
                SpacingTokens{18.0F, 14.0F, 28.0F, 24.0F, 14.0F, 18.0F},
            };
        }

        // ── Built-in theme definitions ───────────────────────────────────
        void ApplyColorsToStyle(const std::unordered_map<std::string, ImVec4> &colors, ImGuiStyle &style)
        {
            auto *c = style.Colors;
            auto get = [&](const char *key, const ImVec4 fallback) -> ImVec4 {
                const auto it = colors.find(key);
                return it != colors.end() ? it->second : fallback;
            };

            c[ImGuiCol_WindowBg]         = get("WindowBg",        ImVec4{0.039F, 0.047F, 0.059F, 1.0F});
            c[ImGuiCol_ChildBg]          = get("ChildBg",         ImVec4{0.071F, 0.082F, 0.102F, 1.0F});
            c[ImGuiCol_PopupBg]          = get("PopupBg",         ImVec4{0.071F, 0.082F, 0.102F, 1.0F});
            c[ImGuiCol_FrameBg]          = get("FrameBg",         ImVec4{0.122F, 0.141F, 0.169F, 1.0F});
            c[ImGuiCol_FrameBgHovered]   = get("FrameBgHovered",  ImVec4{0.137F, 0.157F, 0.188F, 1.0F});
            c[ImGuiCol_FrameBgActive]    = get("FrameBgActive",   ImVec4{0.137F, 0.157F, 0.188F, 1.0F});
            c[ImGuiCol_Button]           = get("Button",          ImVec4{0.122F, 0.141F, 0.169F, 1.0F});
            c[ImGuiCol_ButtonHovered]    = get("ButtonHovered",   ImVec4{0.016F, 0.647F, 0.988F, 0.14F});
            c[ImGuiCol_ButtonActive]     = get("ButtonActive",    ImVec4{0.016F, 0.647F, 0.988F, 0.24F});
            c[ImGuiCol_Text]             = get("Text",            ImVec4{0.910F, 0.894F, 0.851F, 1.0F});
            c[ImGuiCol_TextDisabled]     = get("TextDisabled",    ImVec4{0.369F, 0.357F, 0.329F, 1.0F});
            c[ImGuiCol_Border]           = get("Border",          ImVec4{0.165F, 0.184F, 0.216F, 1.0F});
            c[ImGuiCol_BorderShadow]     = get("BorderShadow",    ImVec4{0.0F, 0.0F, 0.0F, 0.0F});
            c[ImGuiCol_ScrollbarBg]      = get("ScrollbarBg",     ImVec4{0.0F, 0.0F, 0.0F, 0.0F});
            c[ImGuiCol_ScrollbarGrab]    = get("ScrollbarGrab",   ImVec4{0.227F, 0.251F, 0.286F, 1.0F});
            c[ImGuiCol_ScrollbarGrabHovered] = get("ScrollbarGrabHovered", ImVec4{0.369F, 0.357F, 0.329F, 1.0F});
            c[ImGuiCol_ScrollbarGrabActive]  = get("ScrollbarGrabActive", ImVec4{0.910F, 0.894F, 0.851F, 1.0F});
            c[ImGuiCol_CheckMark]        = get("CheckMark",       ImVec4{0.016F, 0.647F, 0.988F, 1.0F});
            c[ImGuiCol_SliderGrab]       = get("SliderGrab",      ImVec4{0.016F, 0.647F, 0.988F, 1.0F});
            c[ImGuiCol_SliderGrabActive] = get("SliderGrabActive",ImVec4{0.149F, 0.714F, 0.992F, 1.0F});
            c[ImGuiCol_Header]           = get("Header",          ImVec4{0.016F, 0.647F, 0.988F, 0.14F});
            c[ImGuiCol_HeaderHovered]    = get("HeaderHovered",   ImVec4{0.137F, 0.157F, 0.188F, 1.0F});
            c[ImGuiCol_HeaderActive]     = get("HeaderActive",    ImVec4{0.137F, 0.157F, 0.188F, 1.0F});
            c[ImGuiCol_ResizeGrip]       = get("ResizeGrip",      ImVec4{0.016F, 0.647F, 0.988F, 0.25F});
            c[ImGuiCol_ResizeGripHovered]= get("ResizeGripHovered", ImVec4{0.016F, 0.647F, 0.988F, 0.67F});
            c[ImGuiCol_ResizeGripActive] = get("ResizeGripActive",ImVec4{0.016F, 0.647F, 0.988F, 1.0F});
            c[ImGuiCol_PlotLines]        = get("PlotLines",       ImVec4{0.016F, 0.647F, 0.988F, 1.0F});
            c[ImGuiCol_PlotHistogram]    = get("PlotHistogram",   ImVec4{0.016F, 0.647F, 0.988F, 1.0F});
            c[ImGuiCol_TableBorderStrong]= get("TableBorderStrong", ImVec4{0.165F, 0.184F, 0.216F, 1.0F});
            c[ImGuiCol_TableBorderLight] = get("TableBorderLight", ImVec4{0.165F, 0.184F, 0.216F, 0.5F});
        }

        std::vector<ThemeEntry> &ThemeList()
        {
            static std::vector<ThemeEntry> themes;
            return themes;
        }

        int &ActiveThemeIndex()
        {
            static int index = 0;
            return index;
        }

        [[nodiscard]] bool ParseHexColor(const std::string &hex, ImVec4 &color)
        {
            if (hex.size() < 7 || hex[0] != '#') return false;
            const auto hexDigit = [](const char value) {
                if (value >= '0' && value <= '9') return value - '0';
                if (value >= 'a' && value <= 'f') return value - 'a' + 10;
                return value - 'A' + 10;
            };
            const auto red = hexDigit(hex[1]) * 16 + hexDigit(hex[2]);
            const auto green = hexDigit(hex[3]) * 16 + hexDigit(hex[4]);
            const auto blue = hexDigit(hex[5]) * 16 + hexDigit(hex[6]);
            color = ImVec4{static_cast<float>(red) / 255.0F,
                           static_cast<float>(green) / 255.0F,
                           static_cast<float>(blue) / 255.0F, 1.0F};
            return true;
        }

        void SkipJsonSeparators(const char *&cursor)
        {
            while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r' || *cursor == ':' || *cursor == ',')
                ++cursor;
        }

        [[nodiscard]] bool ReadJsonQuoted(const char *&cursor, std::string &value)
        {
            if (*cursor != '"') return false;
            ++cursor;
            const char *start = cursor;
            while (*cursor != '\0' && *cursor != '"') ++cursor;
            value.assign(start, static_cast<std::size_t>(cursor - start));
            if (*cursor == '"') ++cursor;
            return true;
        }

        /** @brief Tiny JSON key-value reader — dependency free. */
        bool ReadJsonColors(const std::string &json, std::unordered_map<std::string, ImVec4> &out)
        {
            const char *p = json.c_str();
            while (*p)
            {
                SkipJsonSeparators(p);
                if (*p == '\0') break;
                std::string key;
                if (!ReadJsonQuoted(p, key)) { ++p; continue; }
                SkipJsonSeparators(p);
                std::string hex;
                if (!ReadJsonQuoted(p, hex)) { ++p; continue; }

                ImVec4 color{};
                if (ParseHexColor(hex, color)) out[key] = color;
            }
            return !out.empty();
        }

        /** @brief Returns the themes directory path. */
        std::string GetThemesDir()
        {
            const char *home = std::getenv("HOME");
            if (home == nullptr)
                home = std::getenv("USERPROFILE");
            if (home == nullptr)
                return ".horo/themes";

            std::string dir{home};
            dir += "/.horo/themes";
            return dir;
        }

        void AppendCustomThemes(const char *additionalPath)
        {
            std::vector<std::string> scanPaths{GetThemesDir()};
            if (additionalPath != nullptr && additionalPath[0] != '\0')
                scanPaths.emplace_back(additionalPath);

            for (const auto &dir : scanPaths)
            {
                std::error_code ec;
                if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec))
                    continue;
                for (const auto &entry : std::filesystem::directory_iterator(dir, ec))
                {
                    if (ec) break;
                    if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
                    ThemeEntry theme;
                    if (LoadThemeFromJson(entry.path().string().c_str(), theme))
                        ThemeList().push_back(std::move(theme));
                }
            }
        }

    } // namespace

    const std::vector<ThemeEntry> &GetThemeList()
    {
        return ThemeList();
    }

    const DesignTokens &GetActiveTokens()
    {
        return ActiveTokens();
    }

    void RefreshThemeList(const char *additionalPath)
    {
        ThemeList().clear();

        // Built-in themes
        {
            ThemeEntry e;
            e.name = "Horo Dark";
            e.isBuiltIn = true;
            ThemeList().push_back(std::move(e));
        }
        {
            ThemeEntry e;
            e.name = "Midnight";
            e.isBuiltIn = true;
            e.colors["WindowBg"]         = ImVec4{0.027F, 0.039F, 0.063F, 1.0F};
            e.colors["ChildBg"]          = ImVec4{0.047F, 0.063F, 0.094F, 1.0F};
            e.colors["PopupBg"]          = ImVec4{0.047F, 0.063F, 0.094F, 1.0F};
            e.colors["FrameBg"]          = ImVec4{0.086F, 0.106F, 0.149F, 1.0F};
            e.colors["FrameBgHovered"]   = ImVec4{0.106F, 0.125F, 0.173F, 1.0F};
            e.colors["FrameBgActive"]    = ImVec4{0.106F, 0.125F, 0.173F, 1.0F};
            e.colors["Button"]           = ImVec4{0.086F, 0.106F, 0.149F, 1.0F};
            e.colors["ButtonHovered"]    = ImVec4{0.447F, 0.282F, 0.847F, 0.18F};
            e.colors["ButtonActive"]     = ImVec4{0.447F, 0.282F, 0.847F, 0.28F};
            e.colors["Text"]             = ImVec4{0.867F, 0.855F, 0.898F, 1.0F};
            e.colors["TextDisabled"]     = ImVec4{0.361F, 0.349F, 0.400F, 1.0F};
            e.colors["Border"]           = ImVec4{0.149F, 0.169F, 0.216F, 1.0F};
            e.colors["ScrollbarGrab"]    = ImVec4{0.212F, 0.231F, 0.278F, 1.0F};
            e.colors["ScrollbarGrabHovered"] = ImVec4{0.361F, 0.349F, 0.400F, 1.0F};
            e.colors["ScrollbarGrabActive"]  = ImVec4{0.867F, 0.855F, 0.898F, 1.0F};
            e.colors["CheckMark"]        = ImVec4{0.447F, 0.282F, 0.847F, 1.0F};
            e.colors["SliderGrab"]       = ImVec4{0.447F, 0.282F, 0.847F, 1.0F};
            e.colors["SliderGrabActive"] = ImVec4{0.545F, 0.400F, 0.902F, 1.0F};
            e.colors["Header"]           = ImVec4{0.447F, 0.282F, 0.847F, 0.18F};
            e.colors["HeaderHovered"]    = ImVec4{0.106F, 0.125F, 0.173F, 1.0F};
            e.colors["HeaderActive"]     = ImVec4{0.447F, 0.282F, 0.847F, 0.25F};
            e.colors["ResizeGrip"]       = ImVec4{0.447F, 0.282F, 0.847F, 0.25F};
            e.colors["ResizeGripHovered"] = ImVec4{0.447F, 0.282F, 0.847F, 0.67F};
            e.colors["ResizeGripActive"] = ImVec4{0.447F, 0.282F, 0.847F, 1.0F};
            e.colors["PlotLines"]        = ImVec4{0.447F, 0.282F, 0.847F, 1.0F};
            e.colors["PlotHistogram"]    = ImVec4{0.447F, 0.282F, 0.847F, 1.0F};
            e.colors["TableBorderStrong"] = ImVec4{0.149F, 0.169F, 0.216F, 1.0F};
            e.colors["TableBorderLight"]  = ImVec4{0.149F, 0.169F, 0.216F, 0.5F};
            ThemeList().push_back(std::move(e));
        }
        {
            ThemeEntry e;
            e.name = "Light";
            e.isBuiltIn = true;
            e.colors["WindowBg"]         = ImVec4{0.941F, 0.937F, 0.929F, 1.0F};
            e.colors["ChildBg"]          = ImVec4{0.961F, 0.957F, 0.953F, 1.0F};
            e.colors["PopupBg"]          = ImVec4{0.961F, 0.957F, 0.953F, 1.0F};
            e.colors["FrameBg"]          = ImVec4{0.902F, 0.898F, 0.890F, 1.0F};
            e.colors["FrameBgHovered"]   = ImVec4{0.867F, 0.863F, 0.855F, 1.0F};
            e.colors["FrameBgActive"]    = ImVec4{0.867F, 0.863F, 0.855F, 1.0F};
            e.colors["Button"]           = ImVec4{0.902F, 0.898F, 0.890F, 1.0F};
            e.colors["ButtonHovered"]    = ImVec4{0.016F, 0.647F, 0.988F, 0.12F};
            e.colors["ButtonActive"]     = ImVec4{0.016F, 0.647F, 0.988F, 0.22F};
            e.colors["Text"]             = ImVec4{0.125F, 0.129F, 0.137F, 1.0F};
            e.colors["TextDisabled"]     = ImVec4{0.529F, 0.525F, 0.518F, 1.0F};
            e.colors["Border"]           = ImVec4{0.784F, 0.780F, 0.773F, 1.0F};
            e.colors["ScrollbarGrab"]    = ImVec4{0.659F, 0.655F, 0.647F, 1.0F};
            e.colors["ScrollbarGrabHovered"] = ImVec4{0.529F, 0.525F, 0.518F, 1.0F};
            e.colors["ScrollbarGrabActive"]  = ImVec4{0.125F, 0.129F, 0.137F, 1.0F};
            e.colors["CheckMark"]        = ImVec4{0.016F, 0.647F, 0.988F, 1.0F};
            e.colors["SliderGrab"]       = ImVec4{0.016F, 0.647F, 0.988F, 1.0F};
            e.colors["SliderGrabActive"] = ImVec4{0.149F, 0.714F, 0.992F, 1.0F};
            e.colors["Header"]           = ImVec4{0.016F, 0.647F, 0.988F, 0.12F};
            e.colors["HeaderHovered"]    = ImVec4{0.867F, 0.863F, 0.855F, 1.0F};
            e.colors["HeaderActive"]     = ImVec4{0.016F, 0.647F, 0.988F, 0.18F};
            e.colors["ResizeGrip"]       = ImVec4{0.016F, 0.647F, 0.988F, 0.25F};
            e.colors["ResizeGripHovered"] = ImVec4{0.016F, 0.647F, 0.988F, 0.67F};
            e.colors["ResizeGripActive"] = ImVec4{0.016F, 0.647F, 0.988F, 1.0F};
            e.colors["PlotLines"]        = ImVec4{0.016F, 0.647F, 0.988F, 1.0F};
            e.colors["PlotHistogram"]    = ImVec4{0.016F, 0.647F, 0.988F, 1.0F};
            e.colors["TableBorderStrong"] = ImVec4{0.784F, 0.780F, 0.773F, 1.0F};
            e.colors["TableBorderLight"]  = ImVec4{0.784F, 0.780F, 0.773F, 0.5F};
            ThemeList().push_back(std::move(e));
        }

        AppendCustomThemes(additionalPath);
    }

    bool LoadThemeFromJson(const char *path, ThemeEntry &outEntry)
    {
        std::ifstream file(path);
        if (!file.is_open())
            return false;

        std::ostringstream oss;
        oss << file.rdbuf();
        std::string json = oss.str();

        // Extract name from "name" key
        const char *nameKey = "\"name\"";
        auto namePos = json.find(nameKey);
        if (namePos != std::string::npos)
        {
            auto start = json.find('"', namePos + 6);
            if (start != std::string::npos)
            {
                ++start;
                auto end = json.find('"', start);
                if (end != std::string::npos)
                    outEntry.name = json.substr(start, end - start);
            }
        }
        if (outEntry.name.empty())
            outEntry.name = std::filesystem::path(path).stem().string();

        outEntry.sourcePath = path;
        outEntry.isBuiltIn = false;
        return ReadJsonColors(json, outEntry.colors);
    }

    void SelectThemeByIndex(const int index)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= ThemeList().size())
            return;

        ActiveThemeIndex() = index;
        const auto &entry = ThemeList()[index];

        // Always apply ImGui style colors when the entry carries custom values.
        if (!entry.colors.empty())
            ApplyColorsToStyle(entry.colors, ImGui::GetStyle());

        // Select the DesignTokens based on the built-in preset index.
        // Built-in entries (0=HoroDark, 1=Midnight, 2=Light) may or may
        // not carry a colors map, but the token set is always known.
        if (entry.isBuiltIn)
        {
            if (index == 0)
            {
                // Horo Dark — apply style fallback if no colors in entry
                if (entry.colors.empty())
                    ApplyHoroDark(ImGui::GetStyle());
                ActiveTokens() = DefaultDesignTokens();
            }
            else if (index == 1)
            {
                // Midnight — dark surface + purple accent
                if (entry.colors.empty())
                    ApplyMidnight(ImGui::GetStyle());
                ApplyMidnightDesignTokens();
            }
            else if (index == 2)
            {
                // Light — inverted surface + blue accent
                if (entry.colors.empty())
                    ApplyLight(ImGui::GetStyle());
                ApplyLightDesignTokens();
            }
            else
            {
                // Future built-in: fall back to Horo Dark tokens
                ActiveTokens() = DefaultDesignTokens();
            }
        }
        else
        {
            // Custom JSON theme: ImGui style already applied above;
            // derive tokens from ImGui style colors.
            ActiveTokens() = DefaultDesignTokens();
            auto *c = ImGui::GetStyle().Colors;
            ActiveTokens().colors.surfaceRoot = c[ImGuiCol_WindowBg];
            ActiveTokens().colors.surfaceWindow = c[ImGuiCol_ChildBg];
            ActiveTokens().colors.surfacePanel = c[ImGuiCol_PopupBg];
            ActiveTokens().colors.surfaceRaised = c[ImGuiCol_FrameBg];
            ActiveTokens().colors.surfaceHover = c[ImGuiCol_FrameBgHovered];
            ActiveTokens().colors.border = c[ImGuiCol_Border];
            ActiveTokens().colors.borderStrong = c[ImGuiCol_TableBorderStrong];
            ActiveTokens().colors.textPrimary = c[ImGuiCol_Text];
            ActiveTokens().colors.textMuted = c[ImGuiCol_TextDisabled];
            ActiveTokens().colors.textDim = c[ImGuiCol_TextDisabled];
            ActiveTokens().colors.actionPrimary = c[ImGuiCol_CheckMark];
            ActiveTokens().colors.actionPrimaryHover = c[ImGuiCol_SliderGrabActive];
            ActiveTokens().colors.actionPrimaryActive = c[ImGuiCol_SliderGrabActive];
            ActiveTokens().colors.actionPrimarySoft = c[ImGuiCol_Header];
            ActiveTokens().colors.textOnActionPrimary = c[ImGuiCol_WindowBg];
        }
    }

    int GetActiveThemeIndex()
    {
        return ActiveThemeIndex();
    }

    // ── Legacy preset API (for backward compat) ──────────────────────────

    namespace
    {
        void ApplyHoroDark(ImGuiStyle &style)
        {
            auto *c = style.Colors;
            c[ImGuiCol_WindowBg]        = {0.039F, 0.047F, 0.059F, 1.0F};
            c[ImGuiCol_ChildBg]         = {0.071F, 0.082F, 0.102F, 1.0F};
            c[ImGuiCol_PopupBg]         = {0.071F, 0.082F, 0.102F, 1.0F};
            c[ImGuiCol_FrameBg]         = {0.122F, 0.141F, 0.169F, 1.0F};
            c[ImGuiCol_FrameBgHovered]  = {0.137F, 0.157F, 0.188F, 1.0F};
            c[ImGuiCol_FrameBgActive]   = {0.137F, 0.157F, 0.188F, 1.0F};
            c[ImGuiCol_Button]          = {0.122F, 0.141F, 0.169F, 1.0F};
            c[ImGuiCol_ButtonHovered]   = {0.016F, 0.647F, 0.988F, 0.14F};
            c[ImGuiCol_ButtonActive]    = {0.016F, 0.647F, 0.988F, 0.24F};
            c[ImGuiCol_Text]            = {0.910F, 0.894F, 0.851F, 1.0F};
            c[ImGuiCol_TextDisabled]    = {0.369F, 0.357F, 0.329F, 1.0F};
            c[ImGuiCol_Border]          = {0.165F, 0.184F, 0.216F, 1.0F};
            c[ImGuiCol_BorderShadow]    = {0.0F, 0.0F, 0.0F, 0.0F};
            c[ImGuiCol_ScrollbarBg]     = {0.0F, 0.0F, 0.0F, 0.0F};
            c[ImGuiCol_ScrollbarGrab]   = {0.227F, 0.251F, 0.286F, 1.0F};
            c[ImGuiCol_ScrollbarGrabHovered] = {0.369F, 0.357F, 0.329F, 1.0F};
            c[ImGuiCol_ScrollbarGrabActive]  = {0.910F, 0.894F, 0.851F, 1.0F};
            c[ImGuiCol_CheckMark]       = {0.016F, 0.647F, 0.988F, 1.0F};
            c[ImGuiCol_SliderGrab]      = {0.016F, 0.647F, 0.988F, 1.0F};
            c[ImGuiCol_SliderGrabActive]= {0.149F, 0.714F, 0.992F, 1.0F};
            c[ImGuiCol_Header]          = {0.016F, 0.647F, 0.988F, 0.14F};
            c[ImGuiCol_HeaderHovered]   = {0.137F, 0.157F, 0.188F, 1.0F};
            c[ImGuiCol_HeaderActive]    = {0.137F, 0.157F, 0.188F, 1.0F};
            c[ImGuiCol_ResizeGrip]      = {0.016F, 0.647F, 0.988F, 0.25F};
            c[ImGuiCol_ResizeGripHovered]= {0.016F, 0.647F, 0.988F, 0.67F};
            c[ImGuiCol_ResizeGripActive]= {0.016F, 0.647F, 0.988F, 1.0F};
            c[ImGuiCol_PlotLines]       = {0.016F, 0.647F, 0.988F, 1.0F};
            c[ImGuiCol_PlotHistogram]   = {0.016F, 0.647F, 0.988F, 1.0F};
            c[ImGuiCol_TableBorderStrong]= {0.165F, 0.184F, 0.216F, 1.0F};
            c[ImGuiCol_TableBorderLight] = {0.165F, 0.184F, 0.216F, 0.5F};
        }

        void ApplyMidnight(ImGuiStyle &style)
        {
            auto *c = style.Colors;
            c[ImGuiCol_WindowBg]        = {0.027F, 0.039F, 0.063F, 1.0F};
            c[ImGuiCol_ChildBg]         = {0.047F, 0.063F, 0.094F, 1.0F};
            c[ImGuiCol_PopupBg]         = {0.047F, 0.063F, 0.094F, 1.0F};
            c[ImGuiCol_FrameBg]         = {0.086F, 0.106F, 0.149F, 1.0F};
            c[ImGuiCol_FrameBgHovered]  = {0.106F, 0.125F, 0.173F, 1.0F};
            c[ImGuiCol_FrameBgActive]   = {0.106F, 0.125F, 0.173F, 1.0F};
            c[ImGuiCol_Button]          = {0.086F, 0.106F, 0.149F, 1.0F};
            c[ImGuiCol_ButtonHovered]   = {0.447F, 0.282F, 0.847F, 0.18F};
            c[ImGuiCol_ButtonActive]    = {0.447F, 0.282F, 0.847F, 0.28F};
            c[ImGuiCol_Text]            = {0.867F, 0.855F, 0.898F, 1.0F};
            c[ImGuiCol_TextDisabled]    = {0.361F, 0.349F, 0.400F, 1.0F};
            c[ImGuiCol_Border]          = {0.149F, 0.169F, 0.216F, 1.0F};
            c[ImGuiCol_BorderShadow]    = {0.0F, 0.0F, 0.0F, 0.0F};
            c[ImGuiCol_ScrollbarBg]     = {0.0F, 0.0F, 0.0F, 0.0F};
            c[ImGuiCol_ScrollbarGrab]   = {0.212F, 0.231F, 0.278F, 1.0F};
            c[ImGuiCol_ScrollbarGrabHovered] = {0.361F, 0.349F, 0.400F, 1.0F};
            c[ImGuiCol_ScrollbarGrabActive]  = {0.867F, 0.855F, 0.898F, 1.0F};
            c[ImGuiCol_CheckMark]       = {0.447F, 0.282F, 0.847F, 1.0F};
            c[ImGuiCol_SliderGrab]      = {0.447F, 0.282F, 0.847F, 1.0F};
            c[ImGuiCol_SliderGrabActive]= {0.545F, 0.400F, 0.902F, 1.0F};
            c[ImGuiCol_Header]          = {0.447F, 0.282F, 0.847F, 0.18F};
            c[ImGuiCol_HeaderHovered]   = {0.106F, 0.125F, 0.173F, 1.0F};
            c[ImGuiCol_HeaderActive]    = {0.106F, 0.125F, 0.173F, 1.0F};
            c[ImGuiCol_ResizeGrip]      = {0.447F, 0.282F, 0.847F, 0.25F};
            c[ImGuiCol_ResizeGripHovered]= {0.447F, 0.282F, 0.847F, 0.67F};
            c[ImGuiCol_ResizeGripActive]= {0.447F, 0.282F, 0.847F, 1.0F};
            c[ImGuiCol_PlotLines]       = {0.447F, 0.282F, 0.847F, 1.0F};
            c[ImGuiCol_PlotHistogram]   = {0.447F, 0.282F, 0.847F, 1.0F};
            c[ImGuiCol_TableBorderStrong]= {0.149F, 0.169F, 0.216F, 1.0F};
            c[ImGuiCol_TableBorderLight] = {0.149F, 0.169F, 0.216F, 0.5F};
        }

        void ApplyLight(ImGuiStyle &style)
        {
            auto *c = style.Colors;
            c[ImGuiCol_WindowBg]        = {0.941F, 0.937F, 0.929F, 1.0F};
            c[ImGuiCol_ChildBg]         = {0.961F, 0.957F, 0.953F, 1.0F};
            c[ImGuiCol_PopupBg]         = {0.961F, 0.957F, 0.953F, 1.0F};
            c[ImGuiCol_FrameBg]         = {0.902F, 0.898F, 0.890F, 1.0F};
            c[ImGuiCol_FrameBgHovered]  = {0.867F, 0.863F, 0.855F, 1.0F};
            c[ImGuiCol_FrameBgActive]   = {0.867F, 0.863F, 0.855F, 1.0F};
            c[ImGuiCol_Button]          = {0.902F, 0.898F, 0.890F, 1.0F};
            c[ImGuiCol_ButtonHovered]   = {0.016F, 0.647F, 0.988F, 0.12F};
            c[ImGuiCol_ButtonActive]    = {0.016F, 0.647F, 0.988F, 0.22F};
            c[ImGuiCol_Text]            = {0.125F, 0.129F, 0.137F, 1.0F};
            c[ImGuiCol_TextDisabled]    = {0.529F, 0.525F, 0.518F, 1.0F};
            c[ImGuiCol_Border]          = {0.784F, 0.780F, 0.773F, 1.0F};
            c[ImGuiCol_BorderShadow]    = {0.0F, 0.0F, 0.0F, 0.0F};
            c[ImGuiCol_ScrollbarBg]     = {0.0F, 0.0F, 0.0F, 0.0F};
            c[ImGuiCol_ScrollbarGrab]   = {0.659F, 0.655F, 0.647F, 1.0F};
            c[ImGuiCol_ScrollbarGrabHovered] = {0.529F, 0.525F, 0.518F, 1.0F};
            c[ImGuiCol_ScrollbarGrabActive]  = {0.125F, 0.129F, 0.137F, 1.0F};
            c[ImGuiCol_CheckMark]       = {0.016F, 0.647F, 0.988F, 1.0F};
            c[ImGuiCol_SliderGrab]      = {0.016F, 0.647F, 0.988F, 1.0F};
            c[ImGuiCol_SliderGrabActive]= {0.149F, 0.714F, 0.992F, 1.0F};
            c[ImGuiCol_Header]          = {0.016F, 0.647F, 0.988F, 0.12F};
            c[ImGuiCol_HeaderHovered]   = {0.867F, 0.863F, 0.855F, 1.0F};
            c[ImGuiCol_HeaderActive]    = {0.867F, 0.863F, 0.855F, 1.0F};
            c[ImGuiCol_ResizeGrip]      = {0.016F, 0.647F, 0.988F, 0.25F};
            c[ImGuiCol_ResizeGripHovered]= {0.016F, 0.647F, 0.988F, 0.67F};
            c[ImGuiCol_ResizeGripActive]= {0.016F, 0.647F, 0.988F, 1.0F};
            c[ImGuiCol_PlotLines]       = {0.016F, 0.647F, 0.988F, 1.0F};
            c[ImGuiCol_PlotHistogram]   = {0.016F, 0.647F, 0.988F, 1.0F};
            c[ImGuiCol_TableBorderStrong]= {0.784F, 0.780F, 0.773F, 1.0F};
            c[ImGuiCol_TableBorderLight] = {0.784F, 0.780F, 0.773F, 0.5F};
        }

        Preset &CurrentPreset()
        {
            static Preset preset = Preset::HoroDark;
            return preset;
        }
    } // namespace

    void Apply(ImGuiStyle &style)
    {
        ImGui::StyleColorsDark();
        style.WindowRounding = 0;
        style.FrameRounding = Layout::Radius;
        style.ChildRounding = Layout::Radius;
        style.PopupRounding = Layout::Radius;
        style.ScrollbarRounding = Layout::Radius;
        style.GrabRounding = Layout::Radius;
        style.WindowBorderSize = 0;
        style.FrameBorderSize = 1;
        style.ChildBorderSize = 1;
        style.WindowPadding = ImVec2{0, 0};
        style.FramePadding = ImVec2{10, 7};
        style.ItemSpacing = ImVec2{8, 8};
        style.ItemInnerSpacing = ImVec2{8, 4};
        style.ScrollbarSize = 10.0F;

        RefreshThemeList();
        SelectThemeByIndex(0);
    }

    void SetThemePreset(const Preset preset) { CurrentPreset() = preset; }
    Preset GetThemePreset() { return CurrentPreset(); }

    void ApplyCurrentTheme()
    {
        switch (CurrentPreset())
        {
        case Preset::Midnight:
            ApplyMidnight(ImGui::GetStyle());
            ApplyMidnightDesignTokens();
            break;
        case Preset::Light:
            ApplyLight(ImGui::GetStyle());
            ApplyLightDesignTokens();
            break;
        default:
            ApplyHoroDark(ImGui::GetStyle());
            ActiveTokens() = DefaultDesignTokens();
            break;
        }
    }

} // namespace Horo::Editor::Theme
