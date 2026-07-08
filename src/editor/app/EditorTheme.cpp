#include "Horo/Editor/EditorTheme.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
        void ApplyHoroDark(ImGuiStyle &style);  // forward — defined below

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

        std::vector<ThemeEntry> g_themeList;
        int g_activeThemeIndex = 0;

        /** @brief Tiny JSON key-value reader — dependency free. */
        bool ReadJsonColors(const std::string &json, std::unordered_map<std::string, ImVec4> &out)
        {
            const char *p = json.c_str();
            while (*p)
            {
                // Skip whitespace
                while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
                if (*p == '\0') break;

                // Expect "key"
                if (*p != '"') { ++p; continue; }
                ++p;
                const char *keyStart = p;
                while (*p && *p != '"') ++p;
                std::string key(keyStart, static_cast<std::size_t>(p - keyStart));
                if (*p == '"') ++p;

                // Skip :
                while (*p && (*p == ' ' || *p == ':')) ++p;

                // Expect "value" (hex string)
                if (*p != '"') { ++p; continue; }
                ++p;
                const char *valStart = p;
                while (*p && *p != '"') ++p;
                std::string hex(valStart, static_cast<std::size_t>(p - valStart));
                if (*p == '"') ++p;

                // Parse #rrggbb
                if (hex.size() >= 7 && hex[0] == '#')
                {
                    unsigned int r = 0, g = 0, b = 0;
                    char rr[3] = {hex[1], hex[2], '\0'};
                    char gg[3] = {hex[3], hex[4], '\0'};
                    char bb[3] = {hex[5], hex[6], '\0'};
                    r = static_cast<unsigned int>(std::strtoul(rr, nullptr, 16));
                    g = static_cast<unsigned int>(std::strtoul(gg, nullptr, 16));
                    b = static_cast<unsigned int>(std::strtoul(bb, nullptr, 16));
                    out[key] = ImVec4{static_cast<float>(r) / 255.0F,
                                      static_cast<float>(g) / 255.0F,
                                      static_cast<float>(b) / 255.0F, 1.0F};
                }

                // Skip comma
                while (*p && (*p == ' ' || *p == ',')) ++p;
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

    } // namespace

    const std::vector<ThemeEntry> &GetThemeList()
    {
        return g_themeList;
    }

    void RefreshThemeList(const char *additionalPath)
    {
        g_themeList.clear();

        // Built-in themes
        {
            ThemeEntry e;
            e.name = "Horo Dark";
            e.isBuiltIn = true;
            g_themeList.push_back(std::move(e));
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
            e.colors["ResizeGrip"]       = ImVec4{0.447F, 0.282F, 0.847F, 0.25F};
            e.colors["ResizeGripHovered"] = ImVec4{0.447F, 0.282F, 0.847F, 0.67F};
            e.colors["ResizeGripActive"] = ImVec4{0.447F, 0.282F, 0.847F, 1.0F};
            e.colors["PlotLines"]        = ImVec4{0.447F, 0.282F, 0.847F, 1.0F};
            e.colors["PlotHistogram"]    = ImVec4{0.447F, 0.282F, 0.847F, 1.0F};
            e.colors["TableBorderStrong"] = ImVec4{0.149F, 0.169F, 0.216F, 1.0F};
            e.colors["TableBorderLight"]  = ImVec4{0.149F, 0.169F, 0.216F, 0.5F};
            g_themeList.push_back(std::move(e));
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
            e.colors["ResizeGrip"]       = ImVec4{0.016F, 0.647F, 0.988F, 0.25F};
            e.colors["ResizeGripHovered"] = ImVec4{0.016F, 0.647F, 0.988F, 0.67F};
            e.colors["ResizeGripActive"] = ImVec4{0.016F, 0.647F, 0.988F, 1.0F};
            e.colors["PlotLines"]        = ImVec4{0.016F, 0.647F, 0.988F, 1.0F};
            e.colors["PlotHistogram"]    = ImVec4{0.016F, 0.647F, 0.988F, 1.0F};
            e.colors["TableBorderStrong"] = ImVec4{0.784F, 0.780F, 0.773F, 1.0F};
            e.colors["TableBorderLight"]  = ImVec4{0.784F, 0.780F, 0.773F, 0.5F};
            g_themeList.push_back(std::move(e));
        }

        // Scan custom themes
        std::vector<std::string> scanPaths;
        scanPaths.push_back(GetThemesDir());
        if (additionalPath && additionalPath[0] != '\0')
            scanPaths.emplace_back(additionalPath);

        for (const auto &dir : scanPaths)
        {
            std::error_code ec;
            if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec))
                continue;

            for (const auto &entry : std::filesystem::directory_iterator(dir, ec))
            {
                if (ec) break;
                if (!entry.is_regular_file()) continue;
                const auto &path = entry.path();
                if (path.extension() != ".json") continue;

                ThemeEntry te;
                if (LoadThemeFromJson(path.string().c_str(), te))
                    g_themeList.push_back(std::move(te));
            }
        }
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
        if (index < 0 || static_cast<std::size_t>(index) >= g_themeList.size())
            return;

        g_activeThemeIndex = index;
        const auto &entry = g_themeList[index];

        if (!entry.colors.empty())
        {
            ApplyColorsToStyle(entry.colors, ImGui::GetStyle());
        }
        else if (index == 0)
        {
            // Horo Dark — default (no custom colors, uses constexpr palette)
            ApplyHoroDark(ImGui::GetStyle());
        }
    }

    int GetActiveThemeIndex()
    {
        return g_activeThemeIndex;
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

        Preset g_preset = Preset::HoroDark;
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

    void SetThemePreset(const Preset preset) { g_preset = preset; }
    Preset GetThemePreset() { return g_preset; }

    void ApplyCurrentTheme()
    {
        switch (g_preset)
        {
        case Preset::Midnight: ApplyMidnight(ImGui::GetStyle()); break;
        case Preset::Light:    ApplyLight(ImGui::GetStyle()); break;
        default:               ApplyHoroDark(ImGui::GetStyle()); break;
        }
    }

} // namespace Horo::Editor::Theme
