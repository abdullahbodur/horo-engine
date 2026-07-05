/**
 * @file EditorSettingsModal.cpp
 * @brief Tabbed Settings modal: MCP and Appearance tabs with staged drafts,
 *        Apply/OK/Cancel footer, and central theme application on save.
 */
#include "ui/editor/components/EditorSettingsModal.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "ui/HoroTheme.h"
#include "ui/IconsFontAwesome6.h"
#include "ui/UiComponents.h"
#include "ui/launcher/NativeFolderDialog.h"

namespace Horo::Editor {
namespace {

/** @brief Fixed popup id used for automation and ImGui popup stack identity. */
constexpr const char *kPopupId = "Editor Settings";
constexpr float kModalDefaultWidth = 1180.0f;
constexpr float kModalDefaultHeight = 720.0f;
constexpr float kLeftTabColumnWidth = 190.0f;
constexpr float kFooterHeight = 56.0f;

/** @brief Snapshot of available display size, clamped for the settings modal. */
ImVec2 ComputeModalSize(const ImGuiIO &io) {
    constexpr float kSafeMargin = 48.0f;
    const float width = std::clamp(io.DisplaySize.x - kSafeMargin, 760.0f,
                                   kModalDefaultWidth);
    const float height = std::clamp(io.DisplaySize.y - kSafeMargin, 520.0f,
                                    kModalDefaultHeight);
    return ImVec2(width, height);
}

/** @brief Returns the default architecture shown for each settings platform. */
Horo::Build::BuildArch DefaultArchForOS(Horo::Build::BuildTargetOS os) {
    using enum Horo::Build::BuildTargetOS;
    if (os == MacOS)
        return Horo::Build::BuildArch::Arm64;
    return Horo::Build::BuildArch::x86_64;
}

/** @brief Returns the settings object for a platform target. */
BuildPlatformSettings &PlatformSettingsFor(BuildToolchainSettings &settings,
                                           Horo::Build::BuildTargetOS os) {
    using enum Horo::Build::BuildTargetOS;
    if (os == MacOS)
        return settings.macOS;
    if (os == Windows)
        return settings.windows;
    return settings.linux_;
}

/** @brief Reads the first line of a shell command, trimmed. */
std::string ReadCommandLine(std::string_view command) {
#ifdef _WIN32
    FILE *pipe = _popen(std::string(command).c_str(), "r");
#else
    FILE *pipe = popen(std::string(command).c_str(), "r");
#endif
    if (!pipe)
        return {};

    std::array<char, 512> buffer{};
    std::string output;
    if (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe))
        output = buffer.data();
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
        output.pop_back();
    return output;
}

/** @brief Returns the first executable found by `command -v`. */
std::string FindExecutable(std::initializer_list<std::string_view> names) {
    for (std::string_view name : names) {
#ifdef _WIN32
        const std::string found =
            ReadCommandLine(std::format("where {} 2>nul", name));
#else
        const std::string found = ReadCommandLine(std::format(
            "command -v {} 2>/dev/null", name));
#endif
        if (!found.empty())
            return found;
    }
    return {};
}

/** @brief Quotes one filesystem argument for a POSIX shell command. */
std::string QuoteShellArgument(std::string_view value) {
    std::string quoted = "'";
    for (const char c : value) {
        if (c == '\'')
            quoted += "'\\''";
        else
            quoted += c;
    }
    quoted += "'";
    return quoted;
}

/** @brief Returns true when the path exists and can be executed by the owner. */
bool IsExecutableFile(const std::filesystem::path &path) {
    if (path.empty() || !std::filesystem::exists(path))
        return false;
    const auto status = std::filesystem::status(path);
    if (!std::filesystem::is_regular_file(status) &&
        !std::filesystem::is_symlink(status))
        return false;
    return (status.permissions() & std::filesystem::perms::owner_exec) !=
           std::filesystem::perms::none;
}

/** @brief Reads the first version line from an executable. */
std::string ReadExecutableVersion(const std::string &path) {
    if (!IsExecutableFile(path))
        return {};
    return ReadCommandLine(std::format("{} --version 2>/dev/null",
                                       QuoteShellArgument(path)));
}

/** @brief Finds a compiler/linker executable inside a selected manual path. */
std::string FindExecutableInPath(const std::filesystem::path &path,
                                 std::initializer_list<std::string_view> names) {
    if (IsExecutableFile(path))
        return path.string();
    if (!std::filesystem::is_directory(path))
        return {};

    for (std::string_view name : names) {
        if (const auto direct = path / std::string(name);
            IsExecutableFile(direct))
            return direct.string();
        if (const auto inBin = path / "bin" / std::string(name);
            IsExecutableFile(inBin))
            return inBin.string();
    }
    return {};
}

/** @brief Keeps version text readable inside the compact status column. */
std::string CompactVersionText(std::string text) {
    constexpr std::size_t maxLength = 36;
    if (text.size() <= maxLength)
        return text;
    text.resize(maxLength - 3);
    text += "...";
    return text;
}

/** @brief Builds a stable name for an auto-detected toolchain. */
std::string AutoToolchainName(std::string_view label) {
    return std::string(label);
}

/** @brief Returns the short label shown in toolchain dropdowns. */
std::string ToolchainDisplayName(const Horo::Build::ToolchainConfig &config) {
    if (const std::string prefix =
            std::format("Auto-detected {} ",
                        Horo::Build::GetBuildTargetOSLabel(config.targetTriple.os));
        config.name.rfind(prefix, 0) == 0)
        return config.name.substr(prefix.size());
    return config.name;
}

/** @brief Returns the most useful path to show under a toolchain label. */
std::string ToolchainSubtitle(const Horo::Build::ToolchainConfig &config) {
    if (!config.compilerPath.empty())
        return config.compilerPath;
    if (!config.cmakeToolchainFilePath.empty())
        return config.cmakeToolchainFilePath;
    if (!config.sysrootPath.empty())
        return config.sysrootPath;
    if (!config.cmakePath.empty())
        return config.cmakePath;
    return {};
}

/** @brief Extracts the validated compiler version line from check details. */
std::string ToolchainVersionText(const Horo::Build::ToolchainConfig &config) {
    for (const auto &check : config.lastValidationResult.checks) {
        if (check.checkName == "compiler_version" &&
            check.severity == Horo::Build::ToolchainCheckResult::Severity::Pass)
            return check.message;
    }
    return {};
}

/** @brief Marks an auto-detected toolchain as valid only after path checks pass. */
void ValidateDetectedToolchain(Horo::Build::ToolchainConfig &config) {
    using Horo::Build::ToolchainCheckResult;
    using Horo::Build::ToolchainValidationResult;

    ToolchainValidationResult result;
    result.status = ToolchainValidationResult::Status::Valid;
#if defined(__APPLE__)
    result.hostOSWhenValidated = "macOS";
#elif defined(_WIN32)
    result.hostOSWhenValidated = "Windows";
#else
    result.hostOSWhenValidated = "Linux";
#endif

    auto addCheck = [&](std::string name, bool ok, std::string message) {
        ToolchainCheckResult check;
        check.checkName = std::move(name);
        check.severity = ok ? ToolchainCheckResult::Severity::Pass
                            : ToolchainCheckResult::Severity::Error;
        check.message = std::move(message);
        if (!ok)
            result.status = ToolchainValidationResult::Status::Invalid;
        result.checks.push_back(std::move(check));
    };

    addCheck("compiler_exists", IsExecutableFile(config.compilerPath),
             config.compilerPath.empty() ? "Compiler path is empty"
                                         : config.compilerPath);
    if (!config.linkerPath.empty())
        addCheck("linker_exists", IsExecutableFile(config.linkerPath),
                 config.linkerPath);
    if (!config.sysrootPath.empty())
        addCheck("sysroot_exists", std::filesystem::exists(config.sysrootPath),
                 config.sysrootPath);
    if (!config.cmakePath.empty())
        addCheck("cmake_exists", IsExecutableFile(config.cmakePath),
                 config.cmakePath);

    const std::string version = ReadExecutableVersion(config.compilerPath);
    addCheck("compiler_version", !version.empty(),
             version.empty() ? "Compiler version could not be read" : version);

    result.statusReason =
        result.status == ToolchainValidationResult::Status::Valid
            ? "All detected toolchain checks passed"
            : "Detected toolchain failed validation";
    config.lastValidationTime = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    config.lastValidationResult = std::move(result);
}

/** @brief Tries known local tools for one target OS. */
std::optional<Horo::Build::ToolchainConfig>
AutoDetectToolchain(Horo::Build::BuildTargetOS os) {
    using enum Horo::Build::BuildTargetOS;

    Horo::Build::ToolchainConfig config;
    config.targetTriple = {os, DefaultArchForOS(os)};
    config.enabled = true;
    config.cmakeGenerator = FindExecutable({"ninja"}).empty() ? "Unix Makefiles" : "Ninja";
    config.cmakePath = FindExecutable({"cmake"});

    if (os == MacOS) {
#if defined(__APPLE__)
        config.compilerPath = ReadCommandLine("xcrun --find clang++ 2>/dev/null");
        config.linkerPath = ReadCommandLine("xcrun --find ld 2>/dev/null");
        config.sysrootPath = ReadCommandLine("xcrun --show-sdk-path 2>/dev/null");
        if (!config.compilerPath.empty()) {
            config.name = AutoToolchainName("Xcode clang");
            ValidateDetectedToolchain(config);
            return config;
        }
#endif
        return std::nullopt;
    }

    if (os == Windows) {
        config.compilerPath = FindExecutable({"x86_64-w64-mingw32-g++",
                                              "x86_64-w64-mingw32-clang++"});
        config.linkerPath = FindExecutable({"x86_64-w64-mingw32-ld",
                                            "x86_64-w64-mingw32-ld.lld"});
        if (std::filesystem::is_directory("/usr/x86_64-w64-mingw32"))
            config.sysrootPath = "/usr/x86_64-w64-mingw32";
        else if (std::filesystem::is_directory("/opt/homebrew/opt/mingw-w64"))
            config.sysrootPath = "/opt/homebrew/opt/mingw-w64";
        if (!config.compilerPath.empty()) {
            config.name = AutoToolchainName("MinGW-w64");
            ValidateDetectedToolchain(config);
            return config;
        }
        return std::nullopt;
    }

    config.compilerPath = FindExecutable({"x86_64-linux-gnu-g++", "g++", "clang++"});
    config.linkerPath = FindExecutable({"x86_64-linux-gnu-ld", "ld"});
    if (std::filesystem::is_directory("/usr"))
        config.sysrootPath = "/usr";
    if (!config.compilerPath.empty()) {
        config.name = AutoToolchainName("GCC/Clang");
        ValidateDetectedToolchain(config);
        return config;
    }
    return std::nullopt;
}

/** @brief Adds or updates a detected toolchain and selects it when requested. */
bool AddDetectedToolchain(Horo::Build::ToolchainSettingsStore &store,
                          BuildPlatformSettings &platform,
                          Horo::Build::BuildTargetOS os,
                          bool select) {
    const auto detected = AutoDetectToolchain(os);
    if (!detected)
        return false;

    store.AddToolchain(*detected);
    if (select)
        platform.toolchain = detected->name;
    return true;
}

/** @brief Creates a manual toolchain from a selected folder. */
Horo::Build::ToolchainConfig MakeManualToolchain(
    Horo::Build::BuildTargetOS os, const std::filesystem::path &path) {
    using enum Horo::Build::BuildTargetOS;

    Horo::Build::ToolchainConfig config;
    config.targetTriple = {os, DefaultArchForOS(os)};
    const std::string leaf = path.filename().empty() ? path.string()
                                                     : path.filename().string();
    config.name = std::format("Manual {} {}", Horo::Build::GetBuildTargetOSLabel(os),
                              leaf);
    config.sysrootPath = path.string();
    config.cmakeGenerator = FindExecutable({"ninja"}).empty() ? "Unix Makefiles" : "Ninja";
    config.cmakePath = FindExecutable({"cmake"});
    config.enabled = true;
    if (os == Windows) {
        config.compilerPath = FindExecutableInPath(
            path, {"x86_64-w64-mingw32-g++", "x86_64-w64-mingw32-clang++"});
        config.linkerPath = FindExecutableInPath(
            path, {"x86_64-w64-mingw32-ld", "x86_64-w64-mingw32-ld.lld"});
    } else if (os == MacOS) {
        config.compilerPath = FindExecutableInPath(path, {"clang++", "g++"});
        config.linkerPath = FindExecutableInPath(path, {"ld"});
    } else {
        config.compilerPath =
            FindExecutableInPath(path, {"x86_64-linux-gnu-g++", "g++", "clang++"});
        config.linkerPath = FindExecutableInPath(path, {"x86_64-linux-gnu-ld", "ld"});
    }
    ValidateDetectedToolchain(config);
    return config;
}

/** @brief Static swatch colour used for preset previews. */
void DrawSwatch(ImDrawList *dl, ImVec2 topLeft, float size, const ImVec4 &color,
                float rounding) {
    dl->AddRectFilled(topLeft, ImVec2(topLeft.x + size, topLeft.y + size),
                      ImGui::ColorConvertFloat4ToU32(color), rounding);
}

/** @brief Returns the stable automation marker id for a preset card. */
std::string ThemePresetMarker(std::string_view presetId) {
    if (presetId == "darkBlue")
        return "##settings_test/theme_dark_blue";
    if (presetId == "graphite")
        return "##settings_test/theme_graphite";
    if (presetId == "highContrast")
        return "##settings_test/theme_high_contrast";
    return std::format("##settings_test/theme_custom_{}", presetId);
}

/** @brief Shared layout and mutable state used while drawing toolchain rows. */
struct ToolchainTableContext {
    const Horo::Ui::EditorTheme &theme;
    Horo::Build::ToolchainSettingsStore &store;
    bool &storeDirty;
    ImDrawList &drawList;
    ImVec2 tableTop;
    float verifiedX;
    float toolchainX;
    float toolchainColumnWidth;
    float rowHeight;
    float headerHeight;
};

/** @brief Finds the selected toolchain for a platform row. */
const Horo::Build::ToolchainConfig *SelectedToolchain(
    const ToolchainTableContext &context, Horo::Build::BuildTargetOS os,
    const BuildPlatformSettings &platform) {
    if (platform.toolchain.empty())
        return nullptr;
    return context.store.FindToolchain({os, DefaultArchForOS(os)},
                                       platform.toolchain);
}

/** @brief Draws the validation icon and version text for a toolchain row. */
void DrawToolchainValidation(const ToolchainTableContext &context, float rowY,
                             bool verified,
                             const Horo::Build::ToolchainConfig *selected) {
    const ImVec4 statusColor =
        verified ? ImVec4(0.25f, 0.9f, 0.45f, 1.0f)
                 : context.theme.palette.textMuted;
    const float centerY = rowY + context.rowHeight * 0.5f;
    const ImVec2 center(context.verifiedX + 26.0f, centerY);
    const ImU32 color = ImGui::ColorConvertFloat4ToU32(statusColor);
    context.drawList.AddCircle(center, 9.0f, color, 24, 2.0f);
    if (verified) {
        context.drawList.AddLine(ImVec2(center.x - 4.0f, center.y),
                                 ImVec2(center.x - 1.0f, center.y + 4.0f),
                                 color, 2.0f);
        context.drawList.AddLine(ImVec2(center.x - 1.0f, center.y + 4.0f),
                                 ImVec2(center.x + 5.0f, center.y - 5.0f),
                                 color, 2.0f);
        const std::string version =
            CompactVersionText(ToolchainVersionText(*selected));
        if (!version.empty()) {
            const ImVec2 textSize = ImGui::CalcTextSize(version.c_str());
            ImGui::SetCursorScreenPos(ImVec2(
                context.verifiedX + 50.0f, centerY - textSize.y * 0.5f));
            Horo::Ui::TextMuted(context.theme, version.c_str());
        }
        return;
    }

    context.drawList.AddLine(ImVec2(center.x - 5.0f, center.y),
                             ImVec2(center.x + 5.0f, center.y), color, 2.0f);
    constexpr const char *kNotVerifiedText = "Not verified";
    const ImVec2 textSize = ImGui::CalcTextSize(kNotVerifiedText);
    ImGui::SetCursorScreenPos(
        ImVec2(context.verifiedX + 50.0f, centerY - textSize.y * 0.5f));
    Horo::Ui::TextMuted(context.theme, kNotVerifiedText);
}

/** @brief Returns the toolchain combo preview for a platform row. */
std::string ToolchainPreview(
    const BuildPlatformSettings &platform,
    const Horo::Build::ToolchainConfig *selected,
    const std::vector<Horo::Build::ToolchainConfig> &toolchains) {
    if (!platform.enabled)
        return "No toolchain selected";
    if (selected)
        return ToolchainDisplayName(*selected);
    if (!platform.toolchain.empty())
        return platform.toolchain;
    return toolchains.empty() ? "No toolchains found"
                              : "No toolchain selected";
}

/** @brief Draws the toolchain selector for a platform row. */
void DrawToolchainCombo(
    const ToolchainTableContext &context, BuildPlatformSettings &platform,
    const std::vector<Horo::Build::ToolchainConfig> &toolchains,
    const std::string &preview, float width) {
    Horo::Ui::ScopedComboStyle comboStyle(context.theme);
    ImGui::SetNextItemWidth(width);
    if (!ImGui::BeginCombo("##toolchain_combo", preview.c_str()))
        return;

    if (toolchains.empty()) {
        ImGui::BeginDisabled();
        ImGui::Selectable("No toolchains found", false);
        ImGui::EndDisabled();
    }
    for (const auto &toolchain : toolchains) {
        const bool selectedOption = platform.toolchain == toolchain.name;
        ImGui::PushID(toolchain.name.c_str());
        if (ImGui::Selectable("##toolchain", selectedOption, 0,
                              ImVec2(0.0f, 42.0f)))
            platform.toolchain = toolchain.name;
        const ImVec2 itemMin = ImGui::GetItemRectMin();
        const ImVec2 cursorAfterItem = ImGui::GetCursorScreenPos();
        if (selectedOption)
            ImGui::SetItemDefaultFocus();

        const std::string displayName = ToolchainDisplayName(toolchain);
        ImGui::SetCursorScreenPos(ImVec2(itemMin.x + 6.0f, itemMin.y + 4.0f));
        ImGui::TextUnformatted(displayName.c_str());
        if (const std::string subtitle = ToolchainSubtitle(toolchain);
            !subtitle.empty()) {
            ImGui::SetCursorScreenPos(
                ImVec2(itemMin.x + 6.0f, itemMin.y + 22.0f));
            Horo::Ui::TextMuted(context.theme, subtitle.c_str());
        }
        ImGui::SetCursorScreenPos(cursorAfterItem);
        ImGui::PopID();
    }
    ImGui::EndCombo();
}

/** @brief Draws the manual toolchain picker button. */
void DrawAddToolchainButton(const ToolchainTableContext &context,
                            Horo::Build::BuildTargetOS os,
                            BuildPlatformSettings &platform, float actionSize) {
    if (const std::string label = std::string(ICON_FA_PLUS) + "##add_manual";
        Horo::Ui::Button(context.theme,
                         Horo::Ui::ButtonStyleVariant::Secondary,
                         label.c_str(), ImVec2(actionSize, actionSize))) {
        const std::filesystem::path picked =
            Horo::Launcher::PickFolderPath("Select toolchain path");
        if (!picked.empty()) {
            Horo::Build::ToolchainConfig manual =
                MakeManualToolchain(os, picked);
            context.store.AddToolchain(manual);
            platform.toolchain = manual.name;
            context.storeDirty = true;
        }
    }
}

/** @brief Draws validation, clear, and delete actions for a toolchain row. */
void DrawToolchainActions(
    const ToolchainTableContext &context,
    const Horo::Build::ReleaseTargetTriple &target,
    BuildPlatformSettings &platform,
    const Horo::Build::ToolchainConfig *selected, float actionSize) {
    if (const std::string label =
            std::string(ICON_FA_ELLIPSIS_VERTICAL) + "##more";
        Horo::Ui::Button(context.theme,
                         Horo::Ui::ButtonStyleVariant::Secondary,
                         label.c_str(), ImVec2(actionSize, actionSize)))
        ImGui::OpenPopup("##toolchain_more_popup");

    if (!ImGui::BeginPopup("##toolchain_more_popup"))
        return;
    if (!selected) {
        ImGui::TextUnformatted("No toolchain selected");
        ImGui::EndPopup();
        return;
    }

    if (Horo::Build::ToolchainConfig *live =
            context.store.FindToolchain(target, selected->name);
        ImGui::MenuItem("Validate") && live) {
        ValidateDetectedToolchain(*live);
        context.storeDirty = true;
    }
    if (ImGui::MenuItem("Clear Selection"))
        platform.toolchain.clear();
    if (ImGui::MenuItem("Delete Toolchain")) {
        context.store.RemoveToolchain(target, selected->name);
        platform.toolchain.clear();
        context.storeDirty = true;
    }
    ImGui::EndPopup();
}

/** @brief Draws one platform row in the toolchain settings table. */
void DrawToolchainRow(const ToolchainTableContext &context, int row,
                      const char *id, const char *label,
                      Horo::Build::BuildTargetOS os,
                      BuildPlatformSettings &platform) {
    ImGui::PushID(id);
    const float rowY = context.tableTop.y + context.headerHeight +
                       context.rowHeight * static_cast<float>(row);
    ImGui::SetCursorScreenPos(
        ImVec2(context.tableTop.x + 16.0f, rowY + 23.0f));
    Horo::Ui::RenderEditorToggle(context.theme, "##enabled", nullptr,
                                 platform.enabled);
    ImGui::SetCursorScreenPos(
        ImVec2(context.tableTop.x + 72.0f, rowY + 22.0f));
    ImGui::TextUnformatted(label);

    const Horo::Build::ToolchainConfig *selected =
        SelectedToolchain(context, os, platform);
    const bool verified =
        platform.enabled && selected &&
        selected->lastValidationResult.status ==
            Horo::Build::ToolchainValidationResult::Status::Valid;
    DrawToolchainValidation(context, rowY, verified, selected);

    const Horo::Build::ReleaseTargetTriple target{os, DefaultArchForOS(os)};
    const auto toolchains = context.store.GetToolchainsForTarget(target);
    const std::string preview =
        ToolchainPreview(platform, selected, toolchains);
    constexpr float kActionSize = 34.0f;
    constexpr float kActionGap = 8.0f;
    const float comboWidth =
        std::max(220.0f, context.toolchainColumnWidth - kActionSize * 2.0f -
                             kActionGap * 3.0f - 20.0f);
    ImGui::SetCursorScreenPos(
        ImVec2(context.toolchainX + 16.0f, rowY + 15.0f));
    ImGui::BeginDisabled(!platform.enabled);
    DrawToolchainCombo(context, platform, toolchains, preview, comboWidth);
    ImGui::SameLine(0.0f, kActionGap);
    DrawAddToolchainButton(context, os, platform, kActionSize);
    selected = SelectedToolchain(context, os, platform);
    ImGui::SameLine(0.0f, kActionGap);
    DrawToolchainActions(context, target, platform, selected, kActionSize);
    ImGui::EndDisabled();
    ImGui::PopID();
}

} // namespace

/** @copydoc EditorSettingsModal::Open */
void EditorSettingsModal::Open(const Mcp::McpSettings &mcp,
                               const EditorUserSettings &user,
                               const Horo::Build::ToolchainSettingsStore &toolchains) {
    m_open = true;
    m_openRequested = true;
    m_activeTab = Tab::MCP;
    m_error.clear();
    m_mcpOriginal = mcp;
    m_mcpDraft = mcp;
    m_userOriginal = user;
    m_userDraft = user;
    m_toolchainOriginal = toolchains;
    m_toolchainDraft = toolchains;
    m_toolchainsDirty = false;
    m_toolchainAutoDetectAttempted = false;
}

/** @copydoc EditorSettingsModal::IsDirty */
bool EditorSettingsModal::IsDirty() const {
    if (m_mcpDraft.enabled != m_mcpOriginal.enabled)
        return true;
    if (m_mcpDraft.autoStart != m_mcpOriginal.autoStart)
        return true;
    if (m_mcpDraft.port != m_mcpOriginal.port)
        return true;
    if (m_mcpDraft.host != m_mcpOriginal.host)
        return true;
    if (m_mcpDraft.transport != m_mcpOriginal.transport)
        return true;
    if (m_userDraft.themePresetId != m_userOriginal.themePresetId)
        return true;
    if (m_userDraft.buildToolchain != m_userOriginal.buildToolchain)
        return true;
    if (m_toolchainsDirty)
        return true;
    return false;
}

/** @copydoc EditorSettingsModal::ResetDrafts */
void EditorSettingsModal::ResetDrafts() {
    m_mcpDraft = m_mcpOriginal;
    m_userDraft = m_userOriginal;
    m_toolchainDraft = m_toolchainOriginal;
    m_toolchainsDirty = false;
    m_toolchainAutoDetectAttempted = false;
    m_error.clear();
}

/** @copydoc EditorSettingsModal::SaveAll */
bool EditorSettingsModal::SaveAll() {
    m_error.clear();

    // Validate / sanitize MCP draft.
    m_mcpDraft.port = std::clamp(m_mcpDraft.port, 1, 65535);
    m_mcpDraft.host = Mcp::kDefaultMcpHost;

    if (!Horo::Ui::IsEditorThemePresetIdKnown(m_userDraft.themePresetId)) {
        m_error = "Invalid theme preset selection.";
        return false;
    }

    // Apply MCP settings.
    if (!m_mcpController) {
        m_error = "MCP controller unavailable.";
        return false;
    }
    if (std::string mcpError; !m_mcpController->ApplySettings(m_mcpDraft, &mcpError)) {
        m_error = mcpError.empty() ? "Failed to apply MCP settings." : mcpError;
        return false;
    }

    // Persist user settings (theme preset).
    if (!m_userSettingsDocument) {
        m_error = "User settings document unavailable.";
        return false;
    }
    m_userSettingsDocument->settings = m_userDraft;
    m_userSettingsDocument->settings.themePreset =
        Horo::Ui::ParseEditorThemePreset(m_userDraft.themePresetId, nullptr);
    if (std::string userError; !SaveEditorUserSettingsDocument(m_userSettingsDocument, &userError)) {
        m_error = userError.empty() ? "Failed to save editor user settings."
                                    : userError;
        return false;
    }

    // Persist toolchain settings if modified.
    if (m_toolchainsDirty && m_toolchainStore) {
        *m_toolchainStore = m_toolchainDraft;
        if (!m_toolchainStore->SaveToFile(Horo::Build::ToolchainSettingsStore::GetDefaultSettingsPath())) {
            m_error = "Failed to save toolchain settings to disk.";
            return false;
        }
    }

    // Refresh originals from the authoritative post-save state.
    m_mcpOriginal = m_mcpController->GetSettings();
    m_mcpDraft = m_mcpOriginal;
    m_userOriginal = m_userSettingsDocument->settings;
    m_userDraft = m_userOriginal;
    if (m_toolchainStore) {
        m_toolchainOriginal = *m_toolchainStore;
        m_toolchainDraft = m_toolchainOriginal;
        m_toolchainsDirty = false;
    }

    // Apply theme preset into the live editor style.
    if (m_applyThemePreset)
        m_applyThemePreset(m_userDraft.themePresetId);

    return true;
}

/** @copydoc EditorSettingsModal::Draw */
void EditorSettingsModal::Draw() {
    if (!m_open)
        return;

    const auto &theme = Horo::Ui::GetEditorTheme();
    const ImGuiIO &io = ImGui::GetIO();

    if (m_openRequested) {
        ImGui::OpenPopup(kPopupId);
        m_openRequested = false;
    }

    const ImVec2 modalSize = ComputeModalSize(io);
    ImGui::SetNextWindowSize(modalSize, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    if (constexpr ImGuiWindowFlags kFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
        !ImGui::BeginPopupModal(kPopupId, nullptr, kFlags))
        return;

    // Treat Escape as Cancel.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ResetDrafts();
        m_open = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    // ── Header ───────────────────────────────────────────────────────────────
    {
        ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
        ImGui::PushFont(ImGui::GetFont());
        ImGui::TextUnformatted("Settings");
        ImGui::PopFont();
        ImGui::PopStyleColor();

        // Right-aligned close affordance: "X" button acts like Cancel.
        const float closeSize = ImGui::GetFrameHeight();
        const float closeX = ImGui::GetWindowContentRegionMax().x - closeSize;
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), closeX));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.palette.cardHover);
        ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.textMuted);
        if (ImGui::Button("X##settings_test/header_close",
                          ImVec2(closeSize, closeSize))) {
            ResetDrafts();
            m_open = false;
            ImGui::CloseCurrentPopup();
            ImGui::PopStyleColor(3);
            ImGui::EndPopup();
            return;
        }
        ImGui::PopStyleColor(3);

        ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.textMuted);
        ImGui::TextUnformatted("Editor preferences");
        ImGui::PopStyleColor();
        ImGui::Separator();
    }

    // ── Body: left tabs + right content ──────────────────────────────────────
    const float bodyHeight =
        std::max(120.0f, ImGui::GetContentRegionAvail().y - kFooterHeight);
    ImGui::BeginChild("##settings_body", ImVec2(0.0f, bodyHeight),
                      ImGuiChildFlags_None);

    const std::array<Horo::Ui::EditorVerticalTabItem, 3> tabs = {{
        {
            .id = "##settings_test/tab_mcp",
            .icon = nullptr,
            .label = "MCP",
            .description = "Built-in server",
            .selected = m_activeTab == Tab::MCP,
        },
        {
            .id = "##settings_test/tab_appearance",
            .icon = nullptr,
            .label = "Appearance",
            .description = "Theme presets",
            .selected = m_activeTab == Tab::Appearance,
        },
        {
            .id = "##settings_test/tab_build",
            .icon = nullptr,
            .label = "Build",
            .description = "Platform & toolchain",
            .selected = m_activeTab == Tab::Build,
        },
    }};

    if (const auto tabResult = Horo::Ui::RenderEditorVerticalTabs(
            theme, "##settings_vtabs", tabs, kLeftTabColumnWidth);
        tabResult.clickedIndex == 0)
        m_activeTab = Tab::MCP;
    else if (tabResult.clickedIndex == 1)
        m_activeTab = Tab::Appearance;
    else if (tabResult.clickedIndex == 2)
        m_activeTab = Tab::Build;

    ImGui::SameLine();

    ImGui::BeginChild("##settings_content", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_None,
                      ImGuiWindowFlags_AlwaysUseWindowPadding);

    using enum Tab;
    switch (m_activeTab) {
    case MCP:        DrawMcpTab(theme);        break;
    case Appearance: DrawAppearanceTab(theme); break;
    case Build:      DrawBuildTab(theme);      break;
    }

    ImGui::EndChild();
    ImGui::EndChild();

    // ── Footer: Cancel / Apply / OK ──────────────────────────────────────────
    if (!m_error.empty()) {
        Horo::Ui::ErrorText(theme, m_error.c_str());
    }

    const bool dirty = IsDirty();
    if (const auto footer =
            Horo::Ui::RenderEditorSettingsFooter(theme, /*canApply=*/dirty);
        footer.cancelled) {
        ResetDrafts();
        m_open = false;
        ImGui::CloseCurrentPopup();
    } else if (footer.applied) {
        SaveAll();
        // On failure, m_error already set; modal remains open.
    } else if (footer.accepted && (!dirty || SaveAll())) {
        m_open = false;
        ImGui::CloseCurrentPopup();
        // On save failure the modal remains open with m_error populated.
    }

    ImGui::EndPopup();
}

void EditorSettingsModal::DrawMcpTab(const Horo::Ui::EditorTheme& theme) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
    ImGui::TextUnformatted("MCP");
    ImGui::PopStyleColor();
    Horo::Ui::TextMuted(theme,
                         "Built-in Model Context Protocol server settings.");
    ImGui::Spacing();

    if (Horo::Ui::BeginEditorSettingsCard(theme, "##settings_mcp_server_card",
                                          "Server")) {
        Horo::Ui::RenderEditorToggle(theme, "##mcp_toggle",
                                     "Enable built-in MCP",
                                     m_mcpDraft.enabled);
        Horo::Ui::RenderEditorCheckbox(theme, "Auto-start when editor opens",
                                       m_mcpDraft.autoStart);

        int port = m_mcpDraft.port;
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::InputInt("Port", &port))
            m_mcpDraft.port = std::clamp(port, 1, 65535);

        ImGui::Text("Host: %s", Mcp::kDefaultMcpHost);
        m_mcpDraft.host = Mcp::kDefaultMcpHost;

        const auto endpoint =
            std::format("{}://{}:{}/mcp", Mcp::kMcpUrlScheme,
                        m_mcpDraft.host, m_mcpDraft.port);
        ImGui::TextWrapped("Endpoint: %s##settings_test/mcp_endpoint",
                           endpoint.c_str());
    }
    Horo::Ui::EndEditorSettingsCard();
}

void EditorSettingsModal::DrawBuildTab(const Horo::Ui::EditorTheme& theme) {
    if (!m_toolchainAutoDetectAttempted) {
        bool detectedSelected = false;
        detectedSelected |= AddDetectedToolchain(
            m_toolchainDraft, m_userDraft.buildToolchain.macOS,
            Horo::Build::BuildTargetOS::MacOS,
            m_userDraft.buildToolchain.macOS.enabled &&
                m_userDraft.buildToolchain.macOS.toolchain.empty());
        detectedSelected |= AddDetectedToolchain(
            m_toolchainDraft, m_userDraft.buildToolchain.windows,
            Horo::Build::BuildTargetOS::Windows,
            m_userDraft.buildToolchain.windows.enabled &&
                m_userDraft.buildToolchain.windows.toolchain.empty());
        detectedSelected |= AddDetectedToolchain(
            m_toolchainDraft, m_userDraft.buildToolchain.linux_,
            Horo::Build::BuildTargetOS::Linux,
            m_userDraft.buildToolchain.linux_.enabled &&
                m_userDraft.buildToolchain.linux_.toolchain.empty());
        if (detectedSelected)
            m_toolchainsDirty = true;
        m_toolchainAutoDetectAttempted = true;
    }

    if (!Horo::Ui::BeginEditorSettingsCard(theme,
                                           "##settings_toolchains_card",
                                           "Toolchains")) {
        Horo::Ui::EndEditorSettingsCard();
        return;
    }

    Horo::Ui::TextMuted(theme,
                        "Select or configure toolchains used to build for each platform.");
    ImGui::Spacing();

    const float fullWidth = ImGui::GetContentRegionAvail().x;
    const float platformCol = std::max(160.0f, fullWidth * 0.16f);
    const float verifiedCol = std::max(260.0f, fullWidth * 0.24f);
    const float toolchainCol =
        std::max(360.0f, fullWidth - platformCol - verifiedCol);
    const float rowHeight = 64.0f;
    const float headerHeight = 34.0f;
    const ImVec2 tableTop = ImGui::GetCursorScreenPos();
    const float tableHeight = headerHeight + rowHeight * 3.0f;
    ImDrawList *dl = ImGui::GetWindowDrawList();

    const ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(theme.palette.border);
    const ImU32 rowFill = ImGui::ColorConvertFloat4ToU32(theme.palette.panel);
    const ImU32 headerFill = ImGui::ColorConvertFloat4ToU32(theme.palette.panelSoft);
    dl->AddRectFilled(tableTop, ImVec2(tableTop.x + fullWidth, tableTop.y + tableHeight),
                      rowFill, theme.rounding.card);
    dl->AddRect(tableTop, ImVec2(tableTop.x + fullWidth, tableTop.y + tableHeight),
                borderColor, theme.rounding.card, 0, 1.0f);
    dl->AddRectFilled(tableTop, ImVec2(tableTop.x + fullWidth, tableTop.y + headerHeight),
                      headerFill, theme.rounding.card,
                      ImDrawFlags_RoundCornersTop);

    const float verifiedX = tableTop.x + platformCol;
    const float toolchainX = tableTop.x + platformCol + verifiedCol;
    dl->AddLine(ImVec2(tableTop.x, tableTop.y + headerHeight),
                ImVec2(tableTop.x + fullWidth, tableTop.y + headerHeight),
                borderColor, 1.0f);
    for (int i = 1; i < 3; ++i) {
        const float y =
            tableTop.y + headerHeight + rowHeight * static_cast<float>(i);
        dl->AddLine(ImVec2(tableTop.x, y), ImVec2(tableTop.x + fullWidth, y),
                    borderColor, 1.0f);
    }

    ImGui::SetCursorScreenPos(ImVec2(tableTop.x + 16.0f, tableTop.y + 9.0f));
    Horo::Ui::TextMuted(theme, "Platform");
    ImGui::SetCursorScreenPos(ImVec2(verifiedX + 16.0f, tableTop.y + 9.0f));
    Horo::Ui::TextMuted(theme, "Verified");
    ImGui::SetCursorScreenPos(ImVec2(toolchainX + 16.0f, tableTop.y + 9.0f));
    Horo::Ui::TextMuted(theme, "Toolchain");

    const ToolchainTableContext tableContext{
        theme,       m_toolchainDraft, m_toolchainsDirty, *dl,
        tableTop,    verifiedX,        toolchainX,        toolchainCol,
        rowHeight,   headerHeight,
    };
    DrawToolchainRow(tableContext, 0, "macos", "macOS",
                     Horo::Build::BuildTargetOS::MacOS,
                     m_userDraft.buildToolchain.macOS);
    DrawToolchainRow(tableContext, 1, "windows", "Windows",
                     Horo::Build::BuildTargetOS::Windows,
                     m_userDraft.buildToolchain.windows);
    DrawToolchainRow(tableContext, 2, "linux", "Linux",
                     Horo::Build::BuildTargetOS::Linux,
                     m_userDraft.buildToolchain.linux_);

    const ImVec2 hintMin(tableTop.x, tableTop.y + tableHeight + 18.0f);
    const ImVec2 hintMax(tableTop.x + fullWidth, hintMin.y + 48.0f);
    dl->AddRectFilled(hintMin, hintMax,
                      ImGui::ColorConvertFloat4ToU32(theme.palette.panel),
                      theme.rounding.card);
    dl->AddRect(hintMin, hintMax, borderColor, theme.rounding.card, 0, 1.0f);
    ImGui::SetCursorScreenPos(ImVec2(hintMin.x + 18.0f, hintMin.y + 15.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.accent);
    ImGui::TextUnformatted("i");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    Horo::Ui::TextMuted(
        theme,
        "The selected toolchain will be used when building for the corresponding platform.");
    ImGui::Dummy(ImVec2(1.0f, 58.0f));

    Horo::Ui::EndEditorSettingsCard();
}

void EditorSettingsModal::DrawAppearanceTab(const Horo::Ui::EditorTheme& theme) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
    ImGui::TextUnformatted("Appearance");
    ImGui::PopStyleColor();
    Horo::Ui::TextMuted(theme, "Editor theme and color preferences.");
    ImGui::Spacing();

    if (!Horo::Ui::BeginEditorSettingsCard(theme,
                                          "##settings_appearance_theme_card",
                                          "Theme")) {
        Horo::Ui::EndEditorSettingsCard();
        return;
    }

    const std::vector<Horo::Ui::EditorThemePresetDescriptor> presets =
        Horo::Ui::EditorThemePresetOptions();
    for (const Horo::Ui::EditorThemePresetDescriptor &preset : presets) {
        const bool selected = m_userDraft.themePresetId == preset.id;
        const std::string marker = ThemePresetMarker(preset.id);

        ImGui::PushID(marker.c_str());
        const ImVec2 rowStart = ImGui::GetCursorScreenPos();
        const float rowHeight = 62.0f;
        const float rowWidth = ImGui::GetContentRegionAvail().x;

        if (const std::string hitId = std::string("##preset_hit") + marker;
            ImGui::InvisibleButton(hitId.c_str(),
                                   ImVec2(rowWidth, rowHeight)))
            m_userDraft.themePresetId = preset.id;
        const bool hovered = ImGui::IsItemHovered();

        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec4 bg;
        if (selected)
            bg = theme.palette.selection;
        else if (hovered)
            bg = theme.palette.cardHover;
        else
            bg = theme.palette.card;
        dl->AddRectFilled(
            rowStart,
            ImVec2(rowStart.x + rowWidth, rowStart.y + rowHeight),
            ImGui::ColorConvertFloat4ToU32(bg),
            theme.rounding.card);
        dl->AddRect(
            rowStart,
            ImVec2(rowStart.x + rowWidth, rowStart.y + rowHeight),
            ImGui::ColorConvertFloat4ToU32(theme.palette.border),
            theme.rounding.card, 0, 1.0f);

        const float radioCx = rowStart.x + 18.0f;
        const float radioCy = rowStart.y + rowHeight * 0.5f;
        dl->AddCircle(ImVec2(radioCx, radioCy), 7.0f,
                      ImGui::ColorConvertFloat4ToU32(
                          selected ? theme.palette.accent
                                   : theme.palette.border),
                      0, 1.5f);
        if (selected) {
            dl->AddCircleFilled(
                ImVec2(radioCx, radioCy), 3.5f,
                ImGui::ColorConvertFloat4ToU32(theme.palette.accent));
        }

        const float textX = rowStart.x + 40.0f;
        dl->AddText(
            ImVec2(textX, rowStart.y + 12.0f),
            ImGui::ColorConvertFloat4ToU32(theme.palette.text),
            preset.label.c_str());
        dl->AddText(
            ImVec2(textX, rowStart.y + 12.0f + ImGui::GetFontSize() + 2.0f),
            ImGui::ColorConvertFloat4ToU32(theme.palette.textMuted),
            preset.description.c_str());

        if (selected) {
            constexpr float kSwatchSize = 14.0f;
            constexpr float kSwatchGap = 4.0f;
            const float swatchY = rowStart.y + (rowHeight - kSwatchSize) * 0.5f;
            float swatchX = rowStart.x + rowWidth - (kSwatchSize * 4.0f + kSwatchGap * 3.0f + 16.0f);
            DrawSwatch(dl, ImVec2(swatchX, swatchY), kSwatchSize,
                       preset.palette.panel, theme.rounding.input);
            swatchX += kSwatchSize + kSwatchGap;
            DrawSwatch(dl, ImVec2(swatchX, swatchY), kSwatchSize,
                       preset.palette.card, theme.rounding.input);
            swatchX += kSwatchSize + kSwatchGap;
            DrawSwatch(dl, ImVec2(swatchX, swatchY), kSwatchSize,
                       preset.palette.accent, theme.rounding.input);
            swatchX += kSwatchSize + kSwatchGap;
            DrawSwatch(dl, ImVec2(swatchX, swatchY), kSwatchSize,
                       preset.palette.textMuted, theme.rounding.input);
        }

        ImGui::PopID();
        ImGui::Spacing();
    }
    Horo::Ui::EndEditorSettingsCard();
}

} // namespace Horo::Editor
