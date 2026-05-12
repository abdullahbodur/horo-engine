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
    if (!ok)
        out.error = "Unknown editor theme preset; using darkBlue.";
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

    editorJson["themePreset"] = Ui::EditorThemePresetId(doc->settings.themePreset);
    root["editor"] = std::move(editorJson);

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

    doc->rootJson = std::move(root);
    doc->loadedFromDisk = true;
    doc->parseError = false;
    doc->error.clear();
    return true;
}

} // namespace Horo::Editor
