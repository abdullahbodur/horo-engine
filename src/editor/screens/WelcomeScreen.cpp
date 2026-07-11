#include "Horo/Editor/WelcomeScreen.h"
#include "Horo/Editor/EditorSettingsStore.h"
#include "Horo/Foundation/Logging/Logger.h"
#include "GeneratedBuildInfo.h"

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <utility>

namespace Horo::Editor {
namespace {

[[nodiscard]] std::string EscapeRecentJsonString(std::string_view value) {
    std::ostringstream escaped;
    for (const unsigned char character : value) {
        switch (character) {
        case '"': escaped << "\\\""; break;
        case '\\': escaped << "\\\\"; break;
        case '\b': escaped << "\\b"; break;
        case '\f': escaped << "\\f"; break;
        case '\n': escaped << "\\n"; break;
        case '\r': escaped << "\\r"; break;
        case '\t': escaped << "\\t"; break;
        default:
            if (character < 0x20) {
                static constexpr char hex[] = "0123456789abcdef";
                escaped << "\\u00" << hex[character >> 4] << hex[character & 0x0f];
            } else {
                escaped << static_cast<char>(character);
            }
        }
    }
    return escaped.str();
}

void AppendRecentJsonEscape(std::string_view value, std::size_t &index, std::string &out) {
    const char next = value[++index];
    switch (next) {
    case '"': out.push_back('"'); break;
    case '\\': out.push_back('\\'); break;
    case 'b': out.push_back('\b'); break;
    case 'f': out.push_back('\f'); break;
    case 'n': out.push_back('\n'); break;
    case 'r': out.push_back('\r'); break;
    case 't': out.push_back('\t'); break;
    case 'u':
        if (index + 4 < value.size()) index += 4;
        break;
    default: out.push_back(next); break;
    }
}

[[nodiscard]] std::string UnescapeRecentJsonString(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            AppendRecentJsonEscape(value, i, out);
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}

[[nodiscard]] std::optional<std::string> FindJsonStringField(const std::string& objStr, const char* key) {
    const std::string pattern = std::string("\"") + key + "\"\\s*:\\s*\"((?:\\\\.|[^\\\"])*)\"";
    std::regex re(pattern);
    std::smatch match;
    if (std::regex_search(objStr, match, re) && match.size() >= 2) {
        return UnescapeRecentJsonString(match[1].str());
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<RecentProjectEntry> BuildDefaultBootstrapRecentProjects() {
    std::vector<RecentProjectEntry> results;
    std::error_code ec;
    const std::filesystem::path home = ResolveEditorSettingsPath().parent_path().parent_path();
    const std::filesystem::path gamesDir = home / "projects/fun/game/games";
    if (std::filesystem::exists(gamesDir, ec) && std::filesystem::is_directory(gamesDir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(gamesDir, ec)) {
            if (entry.is_directory(ec) && std::filesystem::exists(entry.path() / ".horo/project.json", ec)) {
                results.push_back(RecentProjectEntry{
                    entry.path().filename().string(),
                    entry.path().string(),
                    "Earlier today",
                    "custom"
                });
            }
        }
    }
    results.push_back({"Desert Run", "~/projects/desert-run", "2h ago", "desert-run"});
    results.push_back({"Arena Prototype", "~/projects/arena-proto", "yesterday", "arena-prototype"});
    results.push_back({"Tech Demo", "~/projects/tech-demo", "3 days ago", "tech-demo"});
    return results;
}

} // namespace

/** @copydoc IsRoutePayloadValid */
bool IsRoutePayloadValid(const GuiRoute& route) noexcept {
    switch (route.kind) {
    case GuiRouteKind::Welcome:
        return std::holds_alternative<WelcomeRouteParameters>(route.parameters);
    case GuiRouteKind::ProjectBrowser:
        return std::holds_alternative<ProjectBrowserRouteParameters>(route.parameters);
    case GuiRouteKind::ProjectCreation:
        return std::holds_alternative<ProjectCreationRouteParameters>(route.parameters);
    case GuiRouteKind::ProjectLoading:
        return std::holds_alternative<ProjectLoadingRouteParameters>(route.parameters);
    case GuiRouteKind::EditorWorkspace:
        return std::holds_alternative<EditorWorkspaceRouteParameters>(route.parameters);
    }

    return false;
}

/** @copydoc IsDisplayableRecentProject */
bool IsDisplayableRecentProject(const RecentProjectEntry& entry) noexcept {
    return !entry.name.empty() && !entry.rootPath.empty();
}

/** @copydoc LoadRecentProjectsFromDisk */
std::vector<RecentProjectEntry> LoadRecentProjectsFromDisk() {
    std::error_code error;
    const std::filesystem::path path = ResolveEditorSettingsPath().parent_path() / "recent_projects.json";
    if (!std::filesystem::exists(path, error) || error) {
        LOG_INFO("editor.welcome", "No recent_projects.json found at '%s'; initializing with bootstrap entries.", path.string().c_str());
        auto defaults = BuildDefaultBootstrapRecentProjects();
        SaveRecentProjectsToDisk(defaults);
        return defaults;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("editor.welcome", "Failed to open recent_projects.json at '%s'", path.string().c_str());
        return BuildDefaultBootstrapRecentProjects();
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string content = buffer.str();

    std::vector<RecentProjectEntry> results;
    std::size_t pos = 0;
    while ((pos = content.find('{', pos)) != std::string::npos) {
        const std::size_t endPos = content.find('}', pos);
        if (endPos == std::string::npos) {
            break;
        }
        const std::string objStr = content.substr(pos, endPos - pos + 1);
        pos = endPos + 1;

        auto name = FindJsonStringField(objStr, "name");
        auto rootPath = FindJsonStringField(objStr, "rootPath");
        auto lastOpened = FindJsonStringField(objStr, "lastOpenedLabel");
        auto thumbnailKey = FindJsonStringField(objStr, "thumbnailKey");

        if (name && rootPath) {
            RecentProjectEntry entry{
                std::move(*name),
                std::move(*rootPath),
                lastOpened ? std::move(*lastOpened) : "Recently",
                thumbnailKey ? std::move(*thumbnailKey) : "custom"
            };
            if (IsDisplayableRecentProject(entry)) {
                results.push_back(std::move(entry));
            }
        }
    }

    if (results.empty()) {
        LOG_WARN("editor.welcome", "Loaded recent_projects.json but no valid entries found — falling back to bootstrap defaults.");
        return BuildDefaultBootstrapRecentProjects();
    }

    LOG_INFO("editor.welcome", "Loaded %zu recent projects from '%s'", results.size(), path.string().c_str());
    return results;
}

/** @copydoc SaveRecentProjectsToDisk */
bool SaveRecentProjectsToDisk(const std::vector<RecentProjectEntry>& projects) {
    std::error_code error;
    const std::filesystem::path path = ResolveEditorSettingsPath().parent_path() / "recent_projects.json";
    std::filesystem::create_directories(path.parent_path(), error);
    if (error && !std::filesystem::exists(path.parent_path())) {
        LOG_ERROR("editor.welcome", "Failed to create directory for recent_projects.json at '%s'", path.parent_path().string().c_str());
        return false;
    }

    std::ostringstream out;
    out << "[\n";
    for (std::size_t i = 0; i < projects.size(); ++i) {
        const auto& p = projects[i];
        out << "  {\n";
        out << "    \"name\": \"" << EscapeRecentJsonString(p.name) << "\",\n";
        out << "    \"rootPath\": \"" << EscapeRecentJsonString(p.rootPath) << "\",\n";
        out << "    \"lastOpenedLabel\": \"" << EscapeRecentJsonString(p.lastOpenedLabel) << "\",\n";
        out << "    \"thumbnailKey\": \"" << EscapeRecentJsonString(p.thumbnailKey) << "\"\n";
        out << "  }" << (i + 1 < projects.size() ? "," : "") << "\n";
    }
    out << "]\n";

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        LOG_ERROR("editor.welcome", "Failed to open recent_projects.json for writing at '%s'", path.string().c_str());
        return false;
    }
    const std::string content = out.str();
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    const bool success = file.good();
    file.close();

    if (success) {
        LOG_INFO("editor.welcome", "Saved %zu recent projects to '%s'", projects.size(), path.string().c_str());
    } else {
        LOG_ERROR("editor.welcome", "Error while writing recent_projects.json to '%s'", path.string().c_str());
    }
    return success;
}

/** @copydoc WelcomeScreenController::WelcomeScreenController */
WelcomeScreenController::WelcomeScreenController(std::vector<RecentProjectEntry> recentProjects)
    : recentProjects_(std::move(recentProjects)) {}

/** @copydoc WelcomeScreenController::BuildViewModel */
WelcomeViewModel WelcomeScreenController::BuildViewModel() const {
    WelcomeViewModel model;
    model.productName = "Horo Editor";
    model.statusLabel = "Game Engine";

    model.recentProjects.reserve(recentProjects_.size());
    for (const RecentProjectEntry& entry : recentProjects_) {
        if (IsDisplayableRecentProject(entry)) {
            model.recentProjects.push_back(entry);
        }
    }

    // Populate What's New from build-time generated data (CHANGELOG.md).
    for (int i = 0; i < Generated::kWhatsNewCount; ++i) {
        const auto& src = Generated::kWhatsNewEntries[i];
        model.whatsNew[static_cast<std::size_t>(i)] = WhatsNewEntry{src.tag, src.title, src.body};
    }

    return model;
}

/** @copydoc WelcomeScreenController::RequestCreateProject */
WelcomeAction WelcomeScreenController::RequestCreateProject() const {
    return WelcomeAction{
        WelcomeActionKind::CreateProject,
        GuiRoute{GuiRouteKind::ProjectCreation, ProjectCreationRouteParameters{}},
    };
}

/** @copydoc WelcomeScreenController::RequestOpenProject */
WelcomeAction WelcomeScreenController::RequestOpenProject() const {
    return WelcomeAction{
        WelcomeActionKind::OpenProject,
        GuiRoute{GuiRouteKind::ProjectBrowser, ProjectBrowserRouteParameters{}},
    };
}

/** @copydoc WelcomeScreenController::RequestOpenRecentProject */
std::optional<WelcomeAction> WelcomeScreenController::RequestOpenRecentProject(const std::size_t index) const {
    const WelcomeViewModel model = BuildViewModel();
    if (index >= model.recentProjects.size()) {
        LOG_WARN("editor.welcome",
                      "RequestOpenRecentProject: index %zu is out of range (have %zu projects).",
                      index, model.recentProjects.size());
        return std::nullopt;
    }

    const RecentProjectEntry& project = model.recentProjects[index];
    LOG_DEBUG("editor.welcome", "RequestOpenRecentProject: selected '%s' at '%s'.",
                   project.name.c_str(), project.rootPath.c_str());
    return WelcomeAction{
        WelcomeActionKind::OpenRecentProject,
        GuiRoute{GuiRouteKind::EditorWorkspace, EditorWorkspaceRouteParameters{project.rootPath, std::nullopt}},
    };
}

/** @copydoc RenderWelcomeScreenText */
std::string RenderWelcomeScreenText(const WelcomeViewModel& viewModel) {
    std::ostringstream out;
    out << viewModel.productName << '\n';
    out << viewModel.statusLabel << '\n';
    out << "Actions: New Project | Open Project | Open Recent | Open Settings" << '\n';

    if (viewModel.recentProjects.empty()) {
        out << "Recent Projects: none" << '\n';
        return out.str();
    }

    out << "Recent Projects:" << '\n';
    for (std::size_t i = 0; i < viewModel.recentProjects.size(); ++i) {
        const RecentProjectEntry& project = viewModel.recentProjects[i];
        out << "  [" << i << "] " << project.name << " — " << project.rootPath;
        if (!project.lastOpenedLabel.empty()) {
            out << " (" << project.lastOpenedLabel << ')';
        }
        out << '\n';
    }

    return out.str();
}

} // namespace Horo::Editor
