#include "Horo/Editor/EditorSettingsStore.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <optional>
#include <regex>
#include <sstream>
#include <system_error>

namespace Horo::Editor
{
    namespace
    {
        [[nodiscard]] std::string GetEnvVar(const char *name)
        {
            if (!name || !*name)
            {
                return {};
            }
#if defined(_WIN32)
            size_t len = 0;
            if (getenv_s(&len, nullptr, 0, name) != 0 || len <= 1)
            {
                return {};
            }
            std::string value(len, '\0');
            if (getenv_s(&len, value.data(), value.size(), name) != 0 || len <= 1)
            {
                return {};
            }
            value.resize(len - 1);
            return value;
#else
            const char *value = std::getenv(name);
            return value ? std::string(value) : std::string();
#endif
        }

        [[nodiscard]] std::string ReadWholeFile(const std::filesystem::path &path, std::string *outError)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in.is_open())
            {
                if (outError)
                {
                    *outError = "Failed to open editor settings file.";
                }
                return {};
            }
            std::ostringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }

        [[nodiscard]] std::string EscapeJsonString(const std::string &value)
        {
            std::string out;
            out.reserve(value.size() + 8);
            for (const char c : value)
            {
                switch (c)
                {
                case '\\':
                    out += "\\\\";
                    break;
                case '"':
                    out += "\\\"";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    out += c;
                    break;
                }
            }
            return out;
        }

        [[nodiscard]] std::string UnescapeJsonString(const std::string &value)
        {
            std::string out;
            out.reserve(value.size());
            bool escaping = false;
            for (const char c : value)
            {
                if (escaping)
                {
                    switch (c)
                    {
                    case 'n':
                        out += '\n';
                        break;
                    case 'r':
                        out += '\r';
                        break;
                    case 't':
                        out += '\t';
                        break;
                    default:
                        out += c;
                        break;
                    }
                    escaping = false;
                    continue;
                }
                if (c == '\\')
                {
                    escaping = true;
                    continue;
                }
                out += c;
            }
            return out;
        }

        [[nodiscard]] std::optional<std::string> FindStringValue(const std::string &json, const char *key)
        {
            const std::regex re(std::string{"\""} + key + "\\\"\\s*:\\s*\\\"((?:\\\\.|[^\\\"])*)\\\"");
            std::smatch match;
            if (!std::regex_search(json, match, re) || match.size() < 2)
            {
                return std::nullopt;
            }
            return UnescapeJsonString(match[1].str());
        }

        [[nodiscard]] std::optional<bool> FindBoolValue(const std::string &json, const char *key)
        {
            const std::regex re(std::string{"\""} + key + R"("\s*:\s*(true|false))");
            std::smatch match;
            if (!std::regex_search(json, match, re) || match.size() < 2)
            {
                return std::nullopt;
            }
            return match[1].str() == "true";
        }

        [[nodiscard]] std::optional<int> FindIntValue(const std::string &json, const char *key)
        {
            const std::regex re(std::string{"\""} + key + R"("\s*:\s*(-?\d+))");
            std::smatch match;
            if (!std::regex_search(json, match, re) || match.size() < 2)
            {
                return std::nullopt;
            }
            int value = 0;
            const std::string text = match[1].str();
            const auto *begin = text.data();
            const auto *end = begin + text.size();
            if (std::from_chars(begin, end, value).ec != std::errc{})
            {
                return std::nullopt;
            }
            return value;
        }

        [[nodiscard]] std::optional<float> FindFloatValue(const std::string &json, const char *key)
        {
            const std::regex re(std::string{"\""} + key + R"("\s*:\s*(-?\d+(?:\.\d+)?))");
            std::smatch match;
            if (!std::regex_search(json, match, re) || match.size() < 2)
            {
                return std::nullopt;
            }
            try
            {
                return std::stof(match[1].str());
            }
            catch (...)
            {
                return std::nullopt;
            }
        }

        [[nodiscard]] bool IsHexColor(const std::string &value)
        {
            if (value.size() != 7 || value[0] != '#')
            {
                return false;
            }
            return std::all_of(value.begin() + 1, value.end(), [](const char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            });
        }

        [[nodiscard]] EditorStartupBehavior ParseStartup(const std::string &value)
        {
            if (value == "last_project")
                return EditorStartupBehavior::LastProject;
            if (value == "project_browser")
                return EditorStartupBehavior::ProjectBrowser;
            return EditorStartupBehavior::WelcomeScreen;
        }

        [[nodiscard]] const char *ToString(const EditorStartupBehavior value)
        {
            switch (value)
            {
            case EditorStartupBehavior::LastProject:
                return "last_project";
            case EditorStartupBehavior::ProjectBrowser:
                return "project_browser";
            case EditorStartupBehavior::WelcomeScreen:
            default:
                return "welcome_screen";
            }
        }

        [[nodiscard]] EditorThemePreset ParseThemePreset(const std::string &value)
        {
            if (value == "midnight")
                return EditorThemePreset::Midnight;
            if (value == "light")
                return EditorThemePreset::Light;
            return EditorThemePreset::HoroDark;
        }

        [[nodiscard]] const char *ToString(const EditorThemePreset value)
        {
            switch (value)
            {
            case EditorThemePreset::Midnight:
                return "midnight";
            case EditorThemePreset::Light:
                return "light";
            case EditorThemePreset::HoroDark:
            default:
                return "horo_dark";
            }
        }

        [[nodiscard]] EditorViewportMode ParseViewportMode(const std::string &value)
        {
            if (value == "wireframe")
                return EditorViewportMode::Wireframe;
            if (value == "lit")
                return EditorViewportMode::Lit;
            if (value == "unlit")
                return EditorViewportMode::Unlit;
            return EditorViewportMode::Shaded;
        }

        [[nodiscard]] const char *ToString(const EditorViewportMode value)
        {
            switch (value)
            {
            case EditorViewportMode::Wireframe:
                return "wireframe";
            case EditorViewportMode::Lit:
                return "lit";
            case EditorViewportMode::Unlit:
                return "unlit";
            case EditorViewportMode::Shaded:
            default:
                return "shaded";
            }
        }

        [[nodiscard]] EditorRenderingTier ParseRenderingTier(const std::string &value)
        {
            if (value == "dx12_vulkan")
                return EditorRenderingTier::Dx12Vulkan;
            if (value == "dx11")
                return EditorRenderingTier::Dx11;
            if (value == "es3")
                return EditorRenderingTier::Es3;
            return EditorRenderingTier::HighEnd;
        }

        [[nodiscard]] const char *ToString(const EditorRenderingTier value)
        {
            switch (value)
            {
            case EditorRenderingTier::Dx12Vulkan:
                return "dx12_vulkan";
            case EditorRenderingTier::Dx11:
                return "dx11";
            case EditorRenderingTier::Es3:
                return "es3";
            case EditorRenderingTier::HighEnd:
            default:
                return "high_end";
            }
        }

        [[nodiscard]] EditorAudioOutputDevice ParseAudioDevice(const std::string &value)
        {
            if (value == "headphones")
                return EditorAudioOutputDevice::Headphones;
            if (value == "speakers")
                return EditorAudioOutputDevice::Speakers;
            return EditorAudioOutputDevice::SystemDefault;
        }

        [[nodiscard]] const char *ToString(const EditorAudioOutputDevice value)
        {
            switch (value)
            {
            case EditorAudioOutputDevice::Headphones:
                return "headphones";
            case EditorAudioOutputDevice::Speakers:
                return "speakers";
            case EditorAudioOutputDevice::SystemDefault:
            default:
                return "system_default";
            }
        }

        [[nodiscard]] EditorConsoleLogLevel ParseLogLevel(const std::string &value)
        {
            if (value == "debug")
                return EditorConsoleLogLevel::Debug;
            if (value == "info")
                return EditorConsoleLogLevel::Info;
            if (value == "error")
                return EditorConsoleLogLevel::Error;
            return EditorConsoleLogLevel::Warning;
        }

        [[nodiscard]] const char *ToString(const EditorConsoleLogLevel value)
        {
            switch (value)
            {
            case EditorConsoleLogLevel::Debug:
                return "debug";
            case EditorConsoleLogLevel::Info:
                return "info";
            case EditorConsoleLogLevel::Error:
                return "error";
            case EditorConsoleLogLevel::Warning:
            default:
                return "warning";
            }
        }

        void ApplyJsonValues(const std::string &json, EditorSettings &s)
        {
            if (auto v = FindStringValue(json, "startupBehavior")) s.startupBehavior = ParseStartup(*v);
            if (auto v = FindIntValue(json, "autoSaveIntervalMinutes")) s.autoSaveIntervalMinutes = *v;
            if (auto v = FindBoolValue(json, "confirmExitWithUnsavedChanges")) s.confirmExitWithUnsavedChanges = *v;
            if (auto v = FindBoolValue(json, "restoreWorkspaceLayout")) s.restoreWorkspaceLayout = *v;
            if (auto v = FindStringValue(json, "defaultSceneOnProjectOpen")) s.defaultSceneOnProjectOpen = *v;

            if (auto v = FindStringValue(json, "themePreset")) s.themePreset = ParseThemePreset(*v);
            if (auto v = FindStringValue(json, "accentColorHex")) s.accentColorHex = *v;
            if (auto v = FindIntValue(json, "uiScalePercent")) s.uiScalePercent = *v;
            if (auto v = FindIntValue(json, "codeFontSizePx")) s.codeFontSizePx = *v;

            if (auto v = FindIntValue(json, "orbitSensitivity")) s.orbitSensitivity = *v;
            if (auto v = FindIntValue(json, "panSensitivity")) s.panSensitivity = *v;
            if (auto v = FindBoolValue(json, "invertOrbitY")) s.invertOrbitY = *v;

            if (auto v = FindStringValue(json, "viewportMode")) s.viewportMode = ParseViewportMode(*v);
            if (auto v = FindBoolValue(json, "gridOverlay")) s.gridOverlay = *v;
            if (auto v = FindStringValue(json, "renderingTier")) s.renderingTier = ParseRenderingTier(*v);
            if (auto v = FindStringValue(json, "textureStreamingBudget")) s.textureStreamingBudget = *v;

            if (auto v = FindIntValue(json, "masterVolume")) s.masterVolume = *v;
            if (auto v = FindStringValue(json, "audioOutputDevice")) s.audioOutputDevice = ParseAudioDevice(*v);
            if (auto v = FindBoolValue(json, "audioEnabled")) s.audioEnabled = *v;

            if (auto v = FindIntValue(json, "maxPreviewClients")) s.maxPreviewClients = *v;
            if (auto v = FindIntValue(json, "simulatedLatencyMs")) s.simulatedLatencyMs = *v;
            if (auto v = FindIntValue(json, "packageDownloadThreads")) s.packageDownloadThreads = *v;

            if (auto v = FindStringValue(json, "consoleLogLevel")) s.consoleLogLevel = ParseLogLevel(*v);
            if (auto v = FindBoolValue(json, "writeLogToFile")) s.writeLogToFile = *v;
            if (auto v = FindBoolValue(json, "autoCaptureOnStutter")) s.autoCaptureOnStutter = *v;
            if (auto v = FindFloatValue(json, "stutterThresholdMs")) s.stutterThresholdMs = *v;

            if (auto v = FindBoolValue(json, "horoMcpBridgeEnabled")) s.horoMcpBridgeEnabled = *v;
            if (auto v = FindBoolValue(json, "fmodIntegrationEnabled")) s.fmodIntegrationEnabled = *v;
            if (auto v = FindBoolValue(json, "steamworksSdkEnabled")) s.steamworksSdkEnabled = *v;
            if (auto v = FindStringValue(json, "pluginDiscoveryPath")) s.pluginDiscoveryPath = *v;
        }
    } // namespace

    /** @copydoc ResolveEditorSettingsHomeDirectory */
    std::filesystem::path ResolveEditorSettingsHomeDirectory()
    {
#if defined(_WIN32)
        if (const std::string home = GetEnvVar("USERPROFILE"); !home.empty())
        {
            return std::filesystem::path(home);
        }
        const std::string drive = GetEnvVar("HOMEDRIVE");
        const std::string homePath = GetEnvVar("HOMEPATH");
        if (!drive.empty() && !homePath.empty())
        {
            return std::filesystem::path(drive + homePath);
        }
#else
        if (const std::string home = GetEnvVar("HOME"); !home.empty())
        {
            return std::filesystem::path(home);
        }
#endif
        std::error_code ec;
        return std::filesystem::current_path(ec);
    }

    /** @copydoc ResolveEditorSettingsPath */
    std::filesystem::path ResolveEditorSettingsPath()
    {
        return ResolveEditorSettingsHomeDirectory() / ".horo" / "editor_settings.json";
    }

    /** @copydoc DefaultEditorSettings */
    EditorSettings DefaultEditorSettings()
    {
        EditorSettings out{};
        (void)ValidateEditorSettings(out, nullptr);
        return out;
    }

    /** @copydoc ValidateEditorSettings */
    bool ValidateEditorSettings(EditorSettings &settings, std::string *outError)
    {
        if (outError)
        {
            outError->clear();
        }
        bool valid = true;
        auto markInvalid = [&](const char *message) {
            valid = false;
            if (outError && outError->empty())
            {
                *outError = message;
            }
        };

        auto clampInt = [&](int &value, const int minValue, const int maxValue, const char *message) {
            if (value < minValue || value > maxValue)
            {
                markInvalid(message);
                value = std::clamp(value, minValue, maxValue);
            }
        };

        clampInt(settings.autoSaveIntervalMinutes, 0, 30, "Auto-save interval must be between 0 and 30 minutes.");
        clampInt(settings.uiScalePercent, 75, 200, "UI scale must be between 75 and 200 percent.");
        clampInt(settings.codeFontSizePx, 8, 24, "Code font size must be between 8 and 24 px.");
        clampInt(settings.orbitSensitivity, 10, 300, "Orbit sensitivity must be between 10 and 300.");
        clampInt(settings.panSensitivity, 10, 300, "Pan sensitivity must be between 10 and 300.");
        clampInt(settings.masterVolume, 0, 100, "Master volume must be between 0 and 100.");
        clampInt(settings.maxPreviewClients, 1, 16, "Max preview clients must be between 1 and 16.");
        clampInt(settings.simulatedLatencyMs, 0, 500, "Simulated latency must be between 0 and 500 ms.");
        clampInt(settings.packageDownloadThreads, 1, 32, "Package download threads must be between 1 and 32.");

        if (settings.stutterThresholdMs < 1.0F || settings.stutterThresholdMs > 1000.0F)
        {
            markInvalid("Stutter threshold must be between 1 and 1000 ms.");
            settings.stutterThresholdMs = std::clamp(settings.stutterThresholdMs, 1.0F, 1000.0F);
        }

        if (!IsHexColor(settings.accentColorHex))
        {
            markInvalid("Accent color must be a #RRGGBB hex color.");
            settings.accentColorHex = "#04A5FC";
        }

        if (settings.defaultSceneOnProjectOpen.empty())
        {
            markInvalid("Default scene cannot be empty.");
            settings.defaultSceneOnProjectOpen = "Assets/Scenes/Main";
        }
        if (settings.textureStreamingBudget.empty())
        {
            markInvalid("Texture streaming budget cannot be empty.");
            settings.textureStreamingBudget = "2048 MB";
        }
        if (settings.pluginDiscoveryPath.empty())
        {
            markInvalid("Plugin discovery path cannot be empty.");
            settings.pluginDiscoveryPath = "{project}/plugins";
        }

        return valid;
    }

    /** @copydoc LoadEditorSettingsDocument */
    EditorSettingsDocument LoadEditorSettingsDocument()
    {
        EditorSettingsDocument out;
        out.path = ResolveEditorSettingsPath();
        out.settings = DefaultEditorSettings();

        std::error_code ec;
        if (!std::filesystem::exists(out.path, ec))
        {
            return out;
        }

        out.loadedFromDisk = true;
        std::string readError;
        const std::string json = ReadWholeFile(out.path, &readError);
        if (!readError.empty())
        {
            out.parseError = true;
            out.error = readError;
            return out;
        }

        if (json.find('{') == std::string::npos || json.find('}') == std::string::npos)
        {
            out.parseError = true;
            out.error = "Editor settings file must contain a JSON object.";
            return out;
        }

        ApplyJsonValues(json, out.settings);
        std::string validationError;
        if (!ValidateEditorSettings(out.settings, &validationError))
        {
            out.error = validationError;
        }
        return out;
    }

    /** @copydoc SaveEditorSettingsDocument */
    bool SaveEditorSettingsDocument(EditorSettingsDocument *doc, std::string *outError)
    {
        if (outError)
        {
            outError->clear();
        }
        if (!doc)
        {
            if (outError)
            {
                *outError = "Editor settings document is null.";
            }
            return false;
        }

        doc->path = ResolveEditorSettingsPath();
        std::string validationError;
        if (!ValidateEditorSettings(doc->settings, &validationError))
        {
            if (outError)
            {
                *outError = validationError;
            }
            return false;
        }

        const std::filesystem::path dir = doc->path.parent_path();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec)
        {
            if (outError)
            {
                *outError = ec.message();
            }
            return false;
        }

        std::ofstream out(doc->path, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
        {
            if (outError)
            {
                *outError = "Failed to open editor settings file for writing.";
            }
            return false;
        }

        out << "{\n";
        out << "  \"editor\": {\n";
        out << "    \"startupBehavior\": \"" << ToString(doc->settings.startupBehavior) << "\",\n";
        out << "    \"autoSaveIntervalMinutes\": " << doc->settings.autoSaveIntervalMinutes << ",\n";
        out << "    \"confirmExitWithUnsavedChanges\": " << (doc->settings.confirmExitWithUnsavedChanges ? "true" : "false") << ",\n";
        out << "    \"restoreWorkspaceLayout\": " << (doc->settings.restoreWorkspaceLayout ? "true" : "false") << ",\n";
        out << "    \"defaultSceneOnProjectOpen\": \"" << EscapeJsonString(doc->settings.defaultSceneOnProjectOpen) << "\"\n";
        out << "  },\n";
        out << "  \"appearance\": {\n";
        out << "    \"themePreset\": \"" << ToString(doc->settings.themePreset) << "\",\n";
        out << "    \"accentColorHex\": \"" << EscapeJsonString(doc->settings.accentColorHex) << "\",\n";
        out << "    \"uiScalePercent\": " << doc->settings.uiScalePercent << ",\n";
        out << "    \"codeFontSizePx\": " << doc->settings.codeFontSizePx << "\n";
        out << "  },\n";
        out << "  \"input\": {\n";
        out << "    \"orbitSensitivity\": " << doc->settings.orbitSensitivity << ",\n";
        out << "    \"panSensitivity\": " << doc->settings.panSensitivity << ",\n";
        out << "    \"invertOrbitY\": " << (doc->settings.invertOrbitY ? "true" : "false") << "\n";
        out << "  },\n";
        out << "  \"rendering\": {\n";
        out << "    \"viewportMode\": \"" << ToString(doc->settings.viewportMode) << "\",\n";
        out << "    \"gridOverlay\": " << (doc->settings.gridOverlay ? "true" : "false") << ",\n";
        out << "    \"renderingTier\": \"" << ToString(doc->settings.renderingTier) << "\",\n";
        out << "    \"textureStreamingBudget\": \"" << EscapeJsonString(doc->settings.textureStreamingBudget) << "\"\n";
        out << "  },\n";
        out << "  \"audio\": {\n";
        out << "    \"masterVolume\": " << doc->settings.masterVolume << ",\n";
        out << "    \"audioOutputDevice\": \"" << ToString(doc->settings.audioOutputDevice) << "\",\n";
        out << "    \"audioEnabled\": " << (doc->settings.audioEnabled ? "true" : "false") << "\n";
        out << "  },\n";
        out << "  \"network\": {\n";
        out << "    \"maxPreviewClients\": " << doc->settings.maxPreviewClients << ",\n";
        out << "    \"simulatedLatencyMs\": " << doc->settings.simulatedLatencyMs << ",\n";
        out << "    \"packageDownloadThreads\": " << doc->settings.packageDownloadThreads << "\n";
        out << "  },\n";
        out << "  \"diagnostics\": {\n";
        out << "    \"consoleLogLevel\": \"" << ToString(doc->settings.consoleLogLevel) << "\",\n";
        out << "    \"writeLogToFile\": " << (doc->settings.writeLogToFile ? "true" : "false") << ",\n";
        out << "    \"autoCaptureOnStutter\": " << (doc->settings.autoCaptureOnStutter ? "true" : "false") << ",\n";
        out << "    \"stutterThresholdMs\": " << std::fixed << std::setprecision(1) << doc->settings.stutterThresholdMs << "\n";
        out << "  },\n";
        out << "  \"plugins\": {\n";
        out << "    \"horoMcpBridgeEnabled\": " << (doc->settings.horoMcpBridgeEnabled ? "true" : "false") << ",\n";
        out << "    \"fmodIntegrationEnabled\": " << (doc->settings.fmodIntegrationEnabled ? "true" : "false") << ",\n";
        out << "    \"steamworksSdkEnabled\": " << (doc->settings.steamworksSdkEnabled ? "true" : "false") << ",\n";
        out << "    \"pluginDiscoveryPath\": \"" << EscapeJsonString(doc->settings.pluginDiscoveryPath) << "\"\n";
        out << "  }\n";
        out << "}\n";

        if (!out.good())
        {
            if (outError)
            {
                *outError = "Failed while writing editor settings file.";
            }
            return false;
        }

        doc->loadedFromDisk = true;
        doc->parseError = false;
        doc->error.clear();
        return true;
    }

} // namespace Horo::Editor
