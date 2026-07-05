/**
 * @file EditorUserSettings.cpp
 * @brief Load/save implementation for `~/.horo/editor_settings.json`.
 */
#include "ui/editor/EditorUserSettings.h"

#include <fstream>
#include <system_error>

#include "mcp/McpSettings.h"

namespace Horo::Editor {
namespace fs = std::filesystem;
using json = nlohmann::json;

// ── Build / Toolchain helpers ───────────────────────────────────────────────

/** @copydoc MakeDefaultBuildToolchainSettings */
BuildToolchainSettings MakeDefaultBuildToolchainSettings() {
    BuildToolchainSettings s;
    // All platforms default to disabled; the host platform is selectively
    // enabled below.  Toolchain strings stay empty (auto-detect).
#if defined(__APPLE__)
    s.macOS.enabled = true;
#elif defined(_WIN32)
    s.windows.enabled = true;
#elif defined(__linux__)
    s.linux_.enabled = true;
#endif
    return s;
}

namespace {

/** @brief Deserialises a single BuildPlatformSettings from a JSON object.
 *  Missing keys leave the target untouched (defaults preserved). */
void FromJson(const json &j, BuildPlatformSettings &out) {
    if (j.is_object()) {
        if (auto it = j.find("enabled"); it != j.end() && it->is_boolean())
            out.enabled = it->get<bool>();
        if (auto it = j.find("toolchain"); it != j.end() && it->is_string())
            out.toolchain = it->get<std::string>();
    }
}

/** @brief Serialises a single BuildPlatformSettings into a JSON object. */
json ToJson(const BuildPlatformSettings &s) {
    return json::object({{"enabled", s.enabled}, {"toolchain", s.toolchain}});
}

/** @brief Deserialises BuildToolchainSettings from a "build" JSON object. */
BuildToolchainSettings ParseBuildToolchainSettings(const json &buildJson) {
    BuildToolchainSettings s;
    if (!buildJson.is_object())
        return s;
    if (auto it = buildJson.find("macOS"); it != buildJson.end())
        FromJson(*it, s.macOS);
    if (auto it = buildJson.find("windows"); it != buildJson.end())
        FromJson(*it, s.windows);
    if (auto it = buildJson.find("linux"); it != buildJson.end())
        FromJson(*it, s.linux_);
    return s;
}

/** @brief Serialises BuildToolchainSettings into a "build" JSON object. */
json SerializeBuildToolchainSettings(const BuildToolchainSettings &s) {
    return json::object({
        {"macOS",   ToJson(s.macOS)},
        {"windows", ToJson(s.windows)},
        {"linux",   ToJson(s.linux_)},
    });
}

} // namespace

// ── Public API ──────────────────────────────────────────────────────────────

/** @copydoc ResolveEditorUserSettingsPath */
fs::path ResolveEditorUserSettingsPath() {
    return Mcp::ResolveMcpSettingsDirectory() / "editor_settings.json";
}

/** @copydoc LoadEditorUserSettingsDocument */
EditorUserSettingsDocument LoadEditorUserSettingsDocument() {
    EditorUserSettingsDocument out;
    std::ifstream in(ResolveEditorUserSettingsPath());
    if (!in.is_open())
        return out;

    out.loadedFromDisk = true;
    try {
        in >> out.rootJson;
    } catch (const json::exception &e) {
        out.rootJson = json::object();
        out.parseError = true;
        out.error = e.what();
        return out;
    }

    if (!out.rootJson.is_object()) {
        out.rootJson = json::object();
        out.parseError = true;
        out.error = "Editor user settings root must be an object.";
        return out;
    }

    const json editorJson = out.rootJson.value("editor", json::object());
    if (!editorJson.is_object())
        return out;

    const std::string presetId =
            editorJson.value("themePreset",
                             std::string(Ui::EditorThemePresetId(
                                     Ui::EditorThemePreset::DarkBlue)));
    bool ok = false;
    const Ui::EditorThemePreset preset = Ui::ParseEditorThemePreset(presetId, &ok);
    out.settings.themePreset = preset;
    out.settings.themePresetId = Ui::IsEditorThemePresetIdKnown(presetId)
                                     ? presetId
                                     : std::string(Ui::EditorThemePresetId(
                                           Ui::EditorThemePreset::DarkBlue));
    if (!ok && out.settings.themePresetId ==
                   Ui::EditorThemePresetId(Ui::EditorThemePreset::DarkBlue))
        out.error = "Unknown editor theme preset; using darkBlue.";

    // Parse build / toolchain settings if present and well-formed.
    if (out.rootJson.contains("build") && out.rootJson.at("build").is_object()) {
        const json buildJson = out.rootJson.at("build");
        out.settings.buildToolchain = ParseBuildToolchainSettings(buildJson);
    }

    return out;
}

/** @copydoc SaveEditorUserSettingsDocument */
bool SaveEditorUserSettingsDocument(EditorUserSettingsDocument *doc,
                                    std::string *outError) {
    if (outError)
        outError->clear();
    if (!doc) {
        if (outError)
            *outError = "Editor user settings document is null.";
        return false;
    }

    json root = doc->rootJson.is_object() ? doc->rootJson : json::object();
    json editorJson = root.value("editor", json::object());
    if (!editorJson.is_object())
        editorJson = json::object();

    std::string presetId = doc->settings.themePresetId;
    if (const Ui::EditorThemePreset idAsBuiltin =
            Ui::ParseEditorThemePreset(presetId, nullptr);
        doc->settings.themePreset != idAsBuiltin)
        presetId = Ui::EditorThemePresetId(doc->settings.themePreset);
    if (!Ui::IsEditorThemePresetIdKnown(presetId))
        presetId = Ui::EditorThemePresetId(doc->settings.themePreset);
    editorJson["themePreset"] = presetId;
    root["editor"] = std::move(editorJson);

    // Persist build / toolchain settings.
    root["build"] = SerializeBuildToolchainSettings(doc->settings.buildToolchain);

    const fs::path dir = Mcp::ResolveMcpSettingsDirectory();
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        if (outError)
            *outError = ec.message();
        return false;
    }

    std::ofstream outFile(ResolveEditorUserSettingsPath());
    if (!outFile.is_open()) {
        if (outError)
            *outError = "Failed to open editor user settings file for writing.";
        return false;
    }

    try {
        outFile << root.dump(2);
    } catch (const json::exception &e) {
        if (outError)
            *outError = e.what();
        return false;
    }

    doc->settings.themePresetId = presetId;
    doc->settings.themePreset = Ui::ParseEditorThemePreset(presetId, nullptr);
    doc->rootJson = std::move(root);
    doc->loadedFromDisk = true;
    doc->parseError = false;
    doc->error.clear();
    return true;
}

} // namespace Horo::Editor
