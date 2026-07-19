#include "Horo/Editor/WelcomeController.h"
#include "Horo/Editor/EditorSettingsStore.h"
#include "Horo/Foundation/Logging/Logger.h"
#include "Horo/Foundation/Platform.h"
#include "GeneratedBuildInfo.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <utility>

namespace Horo::Editor
{
    namespace
    {
        [[nodiscard]] std::string EscapeRecentJsonString(std::string_view value)
        {
            std::ostringstream escaped;
            for (const unsigned char character : value)
            {
                switch (character)
                {
                case '"': escaped << R"(\")";
                    break;
                case '\\': escaped << R"(\\)";
                    break;
                case '\b': escaped << "\\b";
                    break;
                case '\f': escaped << "\\f";
                    break;
                case '\n': escaped << "\\n";
                    break;
                case '\r': escaped << "\\r";
                    break;
                case '\t': escaped << "\\t";
                    break;
                default:
                    if (character < 0x20)
                    {
                        static constexpr char hex[] = "0123456789abcdef";
                        const auto byte = static_cast<std::byte>(character);
                        escaped << "\\u00" << hex[std::to_integer<unsigned>(byte >> 4)] << hex[std::to_integer<
                            unsigned>(byte) & 0x0fU];
                    }
                    else
                    {
                        escaped << static_cast<char>(character);
                    }
                }
            }
            return escaped.str();
        }

        void AppendRecentJsonEscape(const char*& pos, const char* end, std::string& out)
        {
            if (pos + 1 >= end) return;
            ++pos;
            const char next = *pos;
            switch (next)
            {
            case '"': out.push_back('"');
                break;
            case '\\': out.push_back('\\');
                break;
            case 'b': out.push_back('\b');
                break;
            case 'f': out.push_back('\f');
                break;
            case 'n': out.push_back('\n');
                break;
            case 'r': out.push_back('\r');
                break;
            case 't': out.push_back('\t');
                break;
            case 'u':
                // Skip 4 hex digits if available
                for (int k = 0; k < 4 && pos + 1 < end; ++k, ++pos)
                {
                }
                break;
            default: out.push_back(next);
                break;
            }
        }

        [[nodiscard]] std::string UnescapeRecentJsonString(std::string_view value)
        {
            std::string out;
            out.reserve(value.size());
            const char* pos = value.data();
            const char* const end = pos + value.size();
            while (pos < end)
            {
                if (*pos == '\\')
                {
                    AppendRecentJsonEscape(pos, end, out);
                }
                else
                {
                    out.push_back(*pos);
                }
                ++pos;
            }
            return out;
        }

        [[nodiscard]] std::optional<std::string> FindJsonStringField(const std::string& objStr, const char* key)
        {
            const std::string pattern = std::string(R"(")") + key + R"re("\s*:\s*"((?:\\.|[^"])*)")re";
            std::regex re(pattern);
            if (std::smatch match; std::regex_search(objStr, match, re) && match.size() >= 2)
            {
                return UnescapeRecentJsonString(match[1].str());
            }
            return std::nullopt;
        }

        [[nodiscard]] std::vector<RecentProjectEntry> BuildDefaultBootstrapRecentProjects()
        {
            std::vector<RecentProjectEntry> results;
            std::error_code ec;
            const std::filesystem::path home = ResolveEditorSettingsPath().parent_path().parent_path();
            if (const std::filesystem::path gamesDir = home / "projects/fun/game/games";
                std::filesystem::exists(gamesDir, ec) && std::filesystem::is_directory(gamesDir, ec))
            {
                for (const auto& entry : std::filesystem::directory_iterator(gamesDir, ec))
                {
                    if (entry.is_directory(ec) && std::filesystem::exists(entry.path() / ".horo/project.json", ec))
                    {
                        results.emplace_back(
                            entry.path().filename().string(),
                            entry.path().string(),
                            "Earlier today",
                            "custom"
                        );
                    }
                }
            }
            return results;
        }
    } // namespace

    /** @copydoc IsRoutePayloadValid */
    bool IsRoutePayloadValid(const GuiRoute& route) noexcept
    {
        using enum GuiRouteKind;
        switch (route.kind)
        {
        case Welcome:
            return std::holds_alternative<WelcomeRouteParameters>(route.parameters);
        case ProjectBrowser:
            return std::holds_alternative<ProjectBrowserRouteParameters>(route.parameters);
        case ProjectCreation:
            return std::holds_alternative<ProjectCreationRouteParameters>(route.parameters);
        case ProjectLoading:
            return std::holds_alternative<ProjectLoadingRouteParameters>(route.parameters);
        case EditorWorkspace:
            if (const auto* parameters = std::get_if<EditorWorkspaceRouteParameters>(&route.parameters))
                return parameters->session.value != 0;
            return false;
        }

        return false;
    }

    /** @copydoc AreRoutesIdentical */
    bool AreRoutesIdentical(const GuiRoute& lhs, const GuiRoute& rhs) noexcept
    {
        if (lhs.kind != rhs.kind) return false;
        if (lhs.parameters.index() != rhs.parameters.index()) return false;
        return lhs.parameters == rhs.parameters;
    }

    /** @copydoc IsDisplayableRecentProject */
    bool IsDisplayableRecentProject(const RecentProjectEntry& entry) noexcept
    {
        return !entry.name.empty() && std::filesystem::path{entry.rootPath}.is_absolute();
    }

    namespace
    {
        using Json = nlohmann::json;

        [[nodiscard]] std::string_view CompatibilityStatusName(
            const Application::ProjectCompatibilityStatus status) noexcept
        {
            using enum Application::ProjectCompatibilityStatus;
            switch (status)
            {
            case Current: return "current";
            case CompatibleReleaseLine: return "compatible";
            case AutomaticMigrationRequired: return "automatic_migration";
            case RecoveryRequired: return "recovery";
            case FutureVersion: return "future";
            case MigrationPathMissing: return "migration_missing";
            case RequiredProviderUnavailable: return "provider_missing";
            case Corrupt: return "corrupt";
            case Inaccessible: return "inaccessible";
            }
            return "inaccessible";
        }

        [[nodiscard]] std::optional<Application::ProjectCompatibilityStatus> ParseCompatibilityStatus(
            const std::string_view text) noexcept
        {
            using enum Application::ProjectCompatibilityStatus;
            if (text == "current") return Current;
            if (text == "compatible") return CompatibleReleaseLine;
            if (text == "automatic_migration") return AutomaticMigrationRequired;
            if (text == "recovery") return RecoveryRequired;
            if (text == "future") return FutureVersion;
            if (text == "migration_missing") return MigrationPathMissing;
            if (text == "provider_missing") return RequiredProviderUnavailable;
            if (text == "corrupt") return Corrupt;
            if (text == "inaccessible") return Inaccessible;
            return std::nullopt;
        }
    } // namespace

    /** @copydoc LoadRecentProjectsFromDisk */
    std::vector<RecentProjectEntry> LoadRecentProjectsFromDisk()
    {
        std::error_code error;
        const std::filesystem::path path = ResolveEditorSettingsPath().parent_path() / "recent_projects.json";
        if (!std::filesystem::exists(path, error) || error)
        {
            LOG_INFO("editor.welcome", "No recent_projects.json found at '%s'; initializing with bootstrap entries.",
                     path.string().c_str());
            auto defaults = BuildDefaultBootstrapRecentProjects();
            SaveRecentProjectsToDisk(defaults);
            return defaults;
        }

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            LOG_ERROR("editor.welcome", "Failed to open recent_projects.json at '%s'", path.string().c_str());
            return BuildDefaultBootstrapRecentProjects();
        }

        file.seekg(0, std::ios::end);
        const std::streamoff size = file.tellg();
        if (size <= 0 || size > 512 * 1024)
            return BuildDefaultBootstrapRecentProjects();
        file.seekg(0, std::ios::beg);
        std::string content(static_cast<std::size_t>(size), '\0');
        if (!file.read(content.data(), size))
            return BuildDefaultBootstrapRecentProjects();
        std::vector<RecentProjectEntry> results;
        Json root;
        try
        {
            root = Json::parse(content);
        }
        catch (...)
        {
            return BuildDefaultBootstrapRecentProjects();
        }
        if (!root.is_array() || root.size() > 128)
            return BuildDefaultBootstrapRecentProjects();
        for (const Json &value : root)
        {
            if (!value.is_object() || !value.contains("name") || !value["name"].is_string() ||
                !value.contains("rootPath") || !value["rootPath"].is_string())
                continue;
            RecentProjectEntry entry{value["name"].get<std::string>(), value["rootPath"].get<std::string>(),
                                     value.value("lastOpenedLabel", "Recently"),
                                     value.value("thumbnailKey", "custom")};
            if (value.contains("compatibility") && value["compatibility"].is_object())
            {
                const Json &cached = value["compatibility"];
                if (cached.contains("status") && cached["status"].is_string() &&
                    cached.contains("targetVersion") && cached["targetVersion"].is_string())
                {
                    auto status = ParseCompatibilityStatus(cached["status"].get_ref<const std::string &>());
                    auto target = Application::ParseHoroVersion(cached["targetVersion"].get_ref<const std::string &>());
                    if (status.has_value() && target.HasValue())
                    {
                        RecentProjectCompatibilityProjection projection{
                            .status = *status,
                            .targetVersion = Application::EngineReleaseVersion{target.Value()},
                            .inspectionState = RecentProjectInspectionState::Cached};
                        if (cached.contains("projectVersion") && cached["projectVersion"].is_string())
                        {
                            auto projectVersion =
                                Application::ParseHoroVersion(cached["projectVersion"].get_ref<const std::string &>());
                            if (projectVersion.HasValue())
                                projection.projectVersion = Application::EngineReleaseVersion{projectVersion.Value()};
                        }
                        entry.compatibility = std::move(projection);
                    }
                }
            }
            if (IsDisplayableRecentProject(entry))
                results.push_back(std::move(entry));
        }

        if (results.empty())
        {
            LOG_WARN("editor.welcome",
                     "Loaded recent_projects.json but no valid entries found — falling back to bootstrap defaults.");
            return BuildDefaultBootstrapRecentProjects();
        }

        LOG_INFO("editor.welcome", "Loaded %zu recent projects from '%s'", results.size(), path.string().c_str());
        return results;
    }

    /** @copydoc SaveRecentProjectsToDisk */
    bool SaveRecentProjectsToDisk(const std::vector<RecentProjectEntry>& projects)
    {
        std::error_code error;
        const std::filesystem::path path = ResolveEditorSettingsPath().parent_path() / "recent_projects.json";
        std::filesystem::create_directories(path.parent_path(), error);
        if (error && !std::filesystem::exists(path.parent_path()))
        {
            LOG_ERROR("editor.welcome", "Failed to create directory for recent_projects.json at '%s'",
                      path.parent_path().string().c_str());
            return false;
        }

        Json root = Json::array();
        for (const RecentProjectEntry &project : projects)
        {
            Json value{{"name", project.name}, {"rootPath", project.rootPath},
                       {"lastOpenedLabel", project.lastOpenedLabel}, {"thumbnailKey", project.thumbnailKey}};
            if (project.compatibility.has_value())
            {
                Json compatibility{{"status", CompatibilityStatusName(project.compatibility->status)},
                                   {"targetVersion", Application::FormatHoroVersion(
                                                         project.compatibility->targetVersion.value)}};
                if (project.compatibility->projectVersion.has_value())
                    compatibility["projectVersion"] =
                        Application::FormatHoroVersion(project.compatibility->projectVersion->value);
                value["compatibility"] = std::move(compatibility);
            }
            root.push_back(std::move(value));
        }
        const std::string content = root.dump(2) + "\n";
        const std::filesystem::path temporary = path.string() + ".tmp";
        const std::span bytes{reinterpret_cast<const std::byte *>(content.data()), content.size()};
        NativeDurableFileSystem files;
        if (const auto written = files.WriteDurable(temporary, bytes); written.HasError())
        {
            LOG_ERROR("editor.welcome", "Failed to open recent_projects.json for writing at '%s'",
                      path.string().c_str());
            return false;
        }
        if (const auto published = files.AtomicReplace(temporary, path); published.HasError())
        {
            std::filesystem::remove(temporary, error);
            LOG_ERROR("editor.welcome", "Failed to publish recent_projects.json at '%s'", path.string().c_str());
            return false;
        }
        LOG_INFO("editor.welcome", "Saved %zu recent projects to '%s'", projects.size(), path.string().c_str());
        return true;
    }

    /** @copydoc WelcomeScreenController::WelcomeScreenController */
    WelcomeScreenController::WelcomeScreenController(std::vector<RecentProjectEntry> recentProjects)
        : recentProjects_(std::move(recentProjects))
    {
    }

    /** @copydoc WelcomeScreenController::BuildViewModel */
    WelcomeViewModel WelcomeScreenController::BuildViewModel() const
    {
        WelcomeViewModel model;
        model.productName = "Horo Editor";
        model.statusLabel = "Game Engine";

        model.recentProjects.reserve(recentProjects_.size());
        for (const RecentProjectEntry& entry : recentProjects_)
        {
            if (IsDisplayableRecentProject(entry))
            {
                model.recentProjects.push_back(entry);
            }
        }

        // Populate What's New from build-time generated data (CHANGELOG.md).
        for (int i = 0; i < Generated::kWhatsNewCount; ++i)
        {
            const auto& src = Generated::kWhatsNewEntries[i];
            model.whatsNew[static_cast<std::size_t>(i)] = WhatsNewEntry{src.tag, src.title, src.body};
        }

        return model;
    }

    /** @copydoc WelcomeScreenController::RequestCreateProject */
    WelcomeAction WelcomeScreenController::RequestCreateProject() const
    {
        return WelcomeAction{
            WelcomeActionKind::CreateProject,
            GuiRoute{GuiRouteKind::ProjectCreation, ProjectCreationRouteParameters{}},
        };
    }

    /** @copydoc WelcomeScreenController::RequestOpenProject */
    WelcomeAction WelcomeScreenController::RequestOpenProject() const
    {
        return WelcomeAction{
            WelcomeActionKind::OpenProject,
            GuiRoute{GuiRouteKind::ProjectBrowser, ProjectBrowserRouteParameters{}},
        };
    }

    /** @copydoc WelcomeScreenController::RequestOpenRecentProject */
    std::optional<WelcomeAction> WelcomeScreenController::RequestOpenRecentProject(const std::size_t index) const
    {
        const WelcomeViewModel model = BuildViewModel();
        if (index >= model.recentProjects.size())
        {
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
            GuiRoute{GuiRouteKind::ProjectLoading, ProjectLoadingRouteParameters{project.rootPath, project.name}},
        };
    }

    /** @copydoc RenderWelcomeScreenText */
    std::string RenderWelcomeScreenText(const WelcomeViewModel& viewModel)
    {
        std::ostringstream out;
        out << viewModel.productName << '\n';
        out << viewModel.statusLabel << '\n';
        out << "Actions: New Project | Open Project | Open Recent | Open Settings" << '\n';

        if (viewModel.recentProjects.empty())
        {
            out << "Recent Projects: none" << '\n';
            return out.str();
        }

        out << "Recent Projects:" << '\n';
        for (std::size_t i = 0; i < viewModel.recentProjects.size(); ++i)
        {
            const RecentProjectEntry& project = viewModel.recentProjects[i];
            out << "  [" << i << "] " << project.name << " — " << project.rootPath;
            if (!project.lastOpenedLabel.empty())
            {
                out << " (" << project.lastOpenedLabel << ')';
            }
            out << '\n';
        }

        return out.str();
    }
} // namespace Horo::Editor
