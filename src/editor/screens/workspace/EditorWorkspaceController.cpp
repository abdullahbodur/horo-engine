#include "editor/screens/workspace/EditorWorkspaceController.h"

#include "Horo/Assets/AssetReimport.h"
#include "Horo/Editor/EditorWorkspaceEvents.h"
#include "Horo/Editor/WorkspacePanelRegistry.h"
#include "Horo/Foundation/Logging/Logger.h"
#include "editor/document/EditorViewportPicking.h"
#include "editor/document/RuntimeSceneConversion.h"
#include "editor/document/SceneDocumentComparison.h"
#include "editor/document/SceneDocumentPersistence.h"
#include "editor/menu/EditorMenuPlatform.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>
#include <span>
#include <vector>

namespace Horo::Editor {
    namespace {
        const ErrorDomainId SceneComparisonCaptureDomain{"horo.editor.scene_comparison_capture"};
        const ErrorCodeDescriptor SceneComparisonUnavailable{
            .domain = SceneComparisonCaptureDomain,
            .code = ErrorCode{"scene_comparison_capture.unavailable"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "The active scene is unavailable for comparison.",
        };

        [[nodiscard]] bool TryGetDockArea(const int value, WorkspaceDockArea &area) noexcept {
            switch (value) {
                case 0:
                    area = WorkspaceDockArea::Left;
                    return true;
                case 1:
                    area = WorkspaceDockArea::Right;
                    return true;
                case 2:
                    area = WorkspaceDockArea::Bottom;
                    return true;
                case 3:
                    area = WorkspaceDockArea::Document;
                    return true;
                default:
                    return false;
            }
        }

        void NormalizeSideDock(SideDockMode &mode, std::string &fullPanel, std::string &topPanel, std::string &bottomPanel) {
            if (mode != SideDockMode::Split || (!topPanel.empty() && !bottomPanel.empty())) {
                return;
            }

            fullPanel = topPanel.empty() ? std::move(bottomPanel) : std::move(topPanel);
            topPanel.clear();
            bottomPanel.clear();
            mode = SideDockMode::Full;
        }

        [[nodiscard]] bool HasPathPrefix(const std::filesystem::path &root, const std::filesystem::path &candidate) {
            auto rootPart = root.begin();
            auto candidatePart = candidate.begin();
            while (rootPart != root.end() && candidatePart != candidate.end()) {
                if (*rootPart != *candidatePart)
                    return false;
                ++rootPart;
                ++candidatePart;
            }
            return rootPart == root.end();
        }

        [[nodiscard]] std::filesystem::path NormalizeAbsolute(const std::filesystem::path &path) {
            std::error_code error;
            const std::filesystem::path absolute = std::filesystem::absolute(path, error).lexically_normal();
            if (error)
                return {};
            const std::filesystem::path canonical = std::filesystem::weakly_canonical(absolute, error);
            return error ? absolute : canonical;
        }

        [[nodiscard]] bool IsDirectContentBrowserEntry(const ContentBrowserDirectory &directory, const std::filesystem::path &candidate) {
            if (!candidate.is_absolute())
                return false;
            const std::filesystem::path normalized = NormalizeAbsolute(candidate);
            const std::filesystem::path root = NormalizeAbsolute(directory.absoluteRootPath);
            const std::filesystem::path current = NormalizeAbsolute(directory.absoluteCurrentPath);
            if (normalized.empty() || root.empty() || current.empty() || normalized.parent_path() != current ||
                !HasPathPrefix(root, normalized)) {
                return false;
            }
            std::error_code error;
            const auto status = std::filesystem::symlink_status(normalized, error);
            return !error && !std::filesystem::is_symlink(status) &&
                   (std::filesystem::is_directory(status) || std::filesystem::is_regular_file(status));
        }

        [[nodiscard]] std::optional<std::vector<std::filesystem::path>> ValidatedAssetCompanions(const std::filesystem::path &source,
                                                                                                 const bool requireIdentitySidecar) {
            std::error_code error;
            const std::filesystem::file_status sourceStatus = std::filesystem::symlink_status(source, error);
            if (error || std::filesystem::is_symlink(sourceStatus) || !std::filesystem::is_regular_file(sourceStatus)) {
                return std::nullopt;
            }

            std::vector<std::filesystem::path> paths{source};
            for (const char *suffix : {".horo", ".meta"}) {
                std::filesystem::path sidecar = source;
                sidecar += suffix;
                error.clear();
                const std::filesystem::file_status status = std::filesystem::symlink_status(sidecar, error);
                if (error) {
                    if (error != std::errc::no_such_file_or_directory) {
                        return std::nullopt;
                    }
                    error.clear();
                    if (requireIdentitySidecar && std::string_view{suffix} == ".horo") {
                        return std::nullopt;
                    }
                    continue;
                }
                if (!std::filesystem::exists(status)) {
                    if (requireIdentitySidecar && std::string_view{suffix} == ".horo") {
                        return std::nullopt;
                    }
                    continue;
                }
                if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
                    return std::nullopt;
                }
                paths.push_back(std::move(sidecar));
            }
            return paths;
        }

        [[nodiscard]] std::string PortableFold(const std::string_view value) {
            std::string folded{value};
            std::ranges::transform(folded, folded.begin(), [](const unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return folded;
        }

        [[nodiscard]] bool DirectoryContainsPortableName(const std::filesystem::path &directory, const std::string_view name,
                                                         const std::filesystem::path &ignoredEntry = {}) {
            const std::string foldedName = PortableFold(name);
            const std::filesystem::path normalizedIgnored =
                ignoredEntry.empty() ? std::filesystem::path{} : ignoredEntry.lexically_normal();
            std::error_code error;
            std::filesystem::directory_iterator iterator{directory, std::filesystem::directory_options::skip_permission_denied, error};
            const std::filesystem::directory_iterator end;
            while (!error && iterator != end) {
                if (iterator->path().lexically_normal() != normalizedIgnored &&
                    PortableFold(iterator->path().filename().string()) == foldedName) {
                    return true;
                }
                iterator.increment(error);
            }
            return error || iterator != end;
        }

        [[nodiscard]] bool IsPortableEntryName(const std::string_view name) {
            if (name.empty() || name == "." || name == ".." || name.ends_with(' ') || name.ends_with('.')) {
                return false;
            }
            for (const unsigned char character : name) {
                if (character < 32U || std::string_view{"<>:\"/\\|?*"}.find(static_cast<char>(character)) != std::string_view::npos) {
                    return false;
                }
            }

            const std::size_t dot = name.find('.');
            const std::string stem = PortableFold(name.substr(0, dot));
            if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul") {
                return false;
            }
            if (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1' && stem[3] <= '9') {
                return false;
            }
            return true;
        }

        [[nodiscard]] bool RollbackPathMoves(const std::vector<std::pair<std::filesystem::path, std::filesystem::path>> &moved) {
            bool complete = true;
            for (auto item = moved.rbegin(); item != moved.rend(); ++item) {
                std::error_code error;
                std::filesystem::rename(item->second, item->first, error);
                if (error) {
                    complete = false;
                    LOG_ERROR("editor.content_browser", "Rollback rename failed: %s -> %s (%s)", item->second.string().c_str(),
                              item->first.string().c_str(), error.message().c_str());
                }
            }
            return complete;
        }

        [[nodiscard]] bool RemoveCreatedPaths(const std::vector<std::filesystem::path> &created) {
            bool complete = true;
            for (auto item = created.rbegin(); item != created.rend(); ++item) {
                std::error_code error;
                if (!std::filesystem::remove(*item, error) || error) {
                    complete = false;
                    LOG_ERROR("editor.content_browser", "Copy rollback removal failed: %s (%s)", item->string().c_str(),
                              error.message().c_str());
                }
            }
            return complete;
        }

        [[nodiscard]] std::filesystem::path CompanionDestination(const std::filesystem::path &item, const std::filesystem::path &source,
                                                                 const std::filesystem::path &destination) {
            if (item == source)
                return destination;
            std::filesystem::path target = destination;
            target += item.extension().string();
            return target;
        }

        [[nodiscard]] bool AssetDestinationAvailable(const std::filesystem::path &source, const std::filesystem::path &destination,
                                                     const std::vector<std::filesystem::path> &companions) {
            return std::ranges::all_of(companions, [&source, &destination](const std::filesystem::path &item) {
                const std::filesystem::path target = CompanionDestination(item, source, destination);
                return !DirectoryContainsPortableName(target.parent_path(), target.filename().string());
            });
        }

        [[nodiscard]] bool PathDoesNotExist(const std::filesystem::path &path) {
            std::error_code error;
            const std::filesystem::file_status status = std::filesystem::symlink_status(path, error);
            if (error == std::errc::no_such_file_or_directory) {
                return true;
            }
            return !error && !std::filesystem::exists(status);
        }

        [[nodiscard]] std::filesystem::path ResolveDuplicateDestination(const std::filesystem::path &source,
                                                                        const std::filesystem::path &destinationDirectory,
                                                                        const std::vector<std::filesystem::path> &companions) {
            const std::string extension = source.extension().string();
            const std::string stem = source.stem().string();
            for (std::uint32_t index = 1; index < 10000; ++index) {
                const std::filesystem::path candidate = destinationDirectory / std::format("{} ({}){}", stem, index, extension);
                if (AssetDestinationAvailable(source, candidate, companions)) {
                    return candidate;
                }
            }
            return {};
        }

        [[nodiscard]] Assets::AssetId GenerateRandomAssetId(const Assets::AssetRegistrySnapshot &snapshot) {
            std::random_device random;
            for (std::uint32_t attempt = 0; attempt < 32; ++attempt) {
                std::array<std::uint8_t, 16> bytes{};
                for (std::uint8_t &byte : bytes)
                    byte = static_cast<std::uint8_t>(random());
                bytes[6] = static_cast<std::uint8_t>(bytes[6] & 0x0fU | 0x40U);
                bytes[8] = static_cast<std::uint8_t>(bytes[8] & 0x3fU | 0x80U);
                const Assets::AssetId candidate = Assets::AssetId::FromBytes(bytes);
                if (candidate.IsValid() && snapshot.Find(candidate) == nullptr)
                    return candidate;
            }
            return {};
        }

        [[nodiscard]] std::optional<nlohmann::json> ReadSidecarJson(const std::filesystem::path &path) {
            std::error_code error;
            const std::uintmax_t size = std::filesystem::file_size(path, error);
            if (error || size == 0 || size > 1024U * 1024U)
                return std::nullopt;
            std::ifstream input(path, std::ios::binary);
            if (!input)
                return std::nullopt;
            const nlohmann::json parsed = nlohmann::json::parse(input, nullptr, false, true);
            return parsed.is_object() ? std::optional<nlohmann::json>{parsed} : std::nullopt;
        }

        [[nodiscard]] std::vector<std::byte> JsonBytes(const nlohmann::json &value) {
            const std::string serialized = value.dump(2) + '\n';
            const auto *begin = reinterpret_cast<const std::byte *>(serialized.data());
            return {begin, begin + serialized.size()};
        }

        void ActivateSideDock(SideDockMode &mode, std::string &fullPanel, std::string &topPanel, std::string &bottomPanel,
                              const std::optional<SideDockSlot> targetSlot, const std::string &panelId,
                              std::vector<std::string> &displacedPanelIds) {
            if (targetSlot.has_value() && !panelId.empty()) {
                const std::string previousFull = fullPanel;
                if (mode == SideDockMode::Full) {
                    fullPanel.clear();
                    topPanel.clear();
                    bottomPanel.clear();
                    mode = SideDockMode::Split;
                    if (*targetSlot == SideDockSlot::Top) {
                        topPanel = panelId;
                        if (previousFull != panelId)
                            bottomPanel = previousFull;
                    } else {
                        if (previousFull != panelId)
                            topPanel = previousFull;
                        bottomPanel = panelId;
                    }
                    return;
                }

                if (topPanel == panelId)
                    topPanel.clear();
                if (bottomPanel == panelId)
                    bottomPanel.clear();
                std::string &targetPanel = *targetSlot == SideDockSlot::Top ? topPanel : bottomPanel;
                displacedPanelIds.push_back(targetPanel);
                targetPanel = panelId;
                return;
            }

            displacedPanelIds.push_back(fullPanel);
            displacedPanelIds.push_back(topPanel);
            displacedPanelIds.push_back(bottomPanel);
            mode = SideDockMode::Full;
            topPanel.clear();
            bottomPanel.clear();
            fullPanel = panelId;
        }

        void NormalizeBottomDock(EditorWorkspaceViewModel &viewModel) {
            if (viewModel.bottomDockMode != BottomDockMode::Split ||
                (!viewModel.activeBottomLeftPanelId.empty() && !viewModel.activeBottomRightPanelId.empty())) {
                return;
            }

            viewModel.activeBottomPanelId = viewModel.activeBottomLeftPanelId.empty() ? std::move(viewModel.activeBottomRightPanelId)
                                                                                      : std::move(viewModel.activeBottomLeftPanelId);
            viewModel.activeBottomLeftPanelId.clear();
            viewModel.activeBottomRightPanelId.clear();
            viewModel.bottomDockMode = BottomDockMode::Full;
        }

        void NormalizeDocks(EditorWorkspaceViewModel &viewModel) {
            NormalizeSideDock(viewModel.leftDockMode, viewModel.activeLeftPanelId, viewModel.activeLeftTopPanelId,
                              viewModel.activeLeftBottomPanelId);
            NormalizeSideDock(viewModel.rightDockMode, viewModel.activeRightPanelId, viewModel.activeRightTopPanelId,
                              viewModel.activeRightBottomPanelId);
            NormalizeBottomDock(viewModel);
        }

        struct ActivityLayoutRegion {
            WorkspaceDockArea area = WorkspaceDockArea::Document;
            std::optional<SideDockSlot> sideSlot;
            std::optional<BottomDockSlot> bottomSlot;

            friend bool operator==(const ActivityLayoutRegion &, const ActivityLayoutRegion &) = default;
        };

        [[nodiscard]] std::optional<ActivityLayoutRegion> RegionForActivitySlot(const ActivityBarSlot slot) {
            if (slot.rail == ActivityBarRail::DocumentTop && slot.groupIndex == 0) {
                return ActivityLayoutRegion{WorkspaceDockArea::Document, std::nullopt, std::nullopt};
            }
            if (slot.rail == ActivityBarRail::Left) {
                switch (slot.groupIndex) {
                    case 0:
                        return ActivityLayoutRegion{WorkspaceDockArea::Left, SideDockSlot::Top, std::nullopt};
                    case 1:
                        return ActivityLayoutRegion{WorkspaceDockArea::Left, SideDockSlot::Bottom, std::nullopt};
                    case 2:
                        return ActivityLayoutRegion{WorkspaceDockArea::Bottom, std::nullopt, BottomDockSlot::Left};
                    default:
                        return std::nullopt;
                }
            }
            if (slot.rail == ActivityBarRail::Right) {
                switch (slot.groupIndex) {
                    case 0:
                        return ActivityLayoutRegion{WorkspaceDockArea::Right, SideDockSlot::Top, std::nullopt};
                    case 1:
                        return ActivityLayoutRegion{WorkspaceDockArea::Right, SideDockSlot::Bottom, std::nullopt};
                    case 2:
                        return ActivityLayoutRegion{WorkspaceDockArea::Bottom, std::nullopt, BottomDockSlot::Right};
                    default:
                        return std::nullopt;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] bool IsPanelActiveInRegion(const EditorWorkspaceViewModel &viewModel, const std::string_view panelId,
                                                 const ActivityLayoutRegion &region) {
            switch (region.area) {
                case WorkspaceDockArea::Left:
                    if (viewModel.leftDockMode == SideDockMode::Full) {
                        return viewModel.activeLeftPanelId == panelId;
                    }
                    return region.sideSlot == SideDockSlot::Top ? viewModel.activeLeftTopPanelId == panelId
                                                                : viewModel.activeLeftBottomPanelId == panelId;
                case WorkspaceDockArea::Right:
                    if (viewModel.rightDockMode == SideDockMode::Full) {
                        return viewModel.activeRightPanelId == panelId;
                    }
                    return region.sideSlot == SideDockSlot::Top ? viewModel.activeRightTopPanelId == panelId
                                                                : viewModel.activeRightBottomPanelId == panelId;
                case WorkspaceDockArea::Bottom:
                    if (viewModel.bottomDockMode == BottomDockMode::Full) {
                        return viewModel.activeBottomPanelId == panelId;
                    }
                    return region.bottomSlot == BottomDockSlot::Left ? viewModel.activeBottomLeftPanelId == panelId
                                                                     : viewModel.activeBottomRightPanelId == panelId;
                case WorkspaceDockArea::Document:
                    return viewModel.activeDocumentPanelId == panelId;
            }
            return false;
        }

        [[nodiscard]] EditorWorkspaceViewCommandData MakeRegionActivationCommand(const std::string_view panelId,
                                                                                 const ActivityLayoutRegion &region) {
            EditorWorkspaceViewCommandData command;
            command.command = EditorWorkspaceViewCommand::ChangeActivePanel;
            command.targetIndex = static_cast<int>(region.area);
            command.stringPayload = std::string(panelId);
            command.sideDockSlot = region.sideSlot;
            command.bottomDockSlot = region.bottomSlot;
            return command;
        }
    }  // namespace

    EditorWorkspaceController::EditorWorkspaceController(std::string projectRoot, Runtime::RuntimeSceneService &runtimeScene,
                                                         const Assets::AssetRegistrySnapshot &assetRegistry,
                                                         Assets::AssetRegistry *mutableAssetRegistry, ProjectMutationCoordinator *mutations,
                                                         DurableFileSystem *durableFiles,
                                                         const Assets::AssetImporterCatalogSnapshot *importerCatalog, JobSystem *jobs)
        : m_runtimeScene(runtimeScene), m_assetRegistry(assetRegistry), m_mutableAssetRegistry(mutableAssetRegistry),
          m_mutations(mutations), m_durableFiles(durableFiles), m_importerCatalog(importerCatalog),
          m_sceneFileWatch(jobs != nullptr ? std::make_unique<SceneFileWatchService>(*jobs) : nullptr) {
        std::error_code pathError;
        std::filesystem::path absoluteProjectRoot =
            std::filesystem::absolute(std::filesystem::path{projectRoot}, pathError).lexically_normal();
        if (pathError) {
            pathError.clear();
            absoluteProjectRoot = (std::filesystem::current_path(pathError) / std::filesystem::path{projectRoot}).lexically_normal();
        }
        if (!pathError) {
            std::error_code canonicalError;
            const std::filesystem::path canonicalProjectRoot = std::filesystem::weakly_canonical(absoluteProjectRoot, canonicalError);
            if (!canonicalError)
                absoluteProjectRoot = canonicalProjectRoot;
        }
        m_viewModel.projectRoot = absoluteProjectRoot.string();
        m_viewModel.assetRegistryRevision = assetRegistry.Revision();
        m_viewModel.contentBrowser = BuildContentBrowserDirectory(m_viewModel.projectRoot, {}, assetRegistry, m_importerCatalog);
        m_viewModel.panelDockAreas = {{"horo.hierarchy", WorkspaceDockArea::Left},
                                      {"horo.viewport", WorkspaceDockArea::Document},
                                      {"horo.global_dock", WorkspaceDockArea::Bottom},
                                      {"horo.inspector", WorkspaceDockArea::Right},
                                      {"horo.input_mapping", WorkspaceDockArea::Right}};
        static_cast<void>(m_viewModel.activityBarLayout.Insert("horo.hierarchy", ActivityBarSlot{ActivityBarRail::Left, 0, 0}));
        static_cast<void>(m_viewModel.activityBarLayout.Insert("horo.viewport", ActivityBarSlot{ActivityBarRail::DocumentTop, 0, 0}));
        static_cast<void>(m_viewModel.activityBarLayout.Insert("horo.global_dock", ActivityBarSlot{ActivityBarRail::Left, 2, 0}));
        static_cast<void>(m_viewModel.activityBarLayout.Insert("horo.inspector", ActivityBarSlot{ActivityBarRail::Right, 0, 0}));
        static_cast<void>(m_viewModel.activityBarLayout.Insert("horo.input_mapping", ActivityBarSlot{ActivityBarRail::Right, 1, 0}));

        bool loadedProjectScene = false;
        const Result<std::optional<LoadedProjectScene>> loaded = LoadProjectDefaultScene(absoluteProjectRoot);
        if (loaded.HasError()) {
            m_initializationError = loaded.ErrorValue();
            LOG_ERROR("editor.scene_document", "Default scene load failed: %s", loaded.ErrorValue().message.c_str());
        } else if (loaded.Value().has_value()) {
            LoadedProjectScene projectScene = *loaded.Value();
            const Result<void> installed = m_document.LoadSaved(std::move(projectScene.objects));
            if (installed.HasError()) {
                m_initializationError = installed.ErrorValue();
                LOG_ERROR("editor.scene_document", "Default scene validation failed: %s", installed.ErrorValue().message.c_str());
            } else {
                m_defaultScenePath = std::move(projectScene.absolutePath);
                m_sceneFingerprint = std::move(projectScene.fingerprint);
                loadedProjectScene = true;
                m_history.Clear();
                LOG_INFO("editor.scene_document", "Loaded default scene '%s'.", m_defaultScenePath->string().c_str());
            }
        }

        if (!loadedProjectScene && !m_initializationError.has_value()) {
            const Math::Quaternion pitch = Math::Quaternion::FromAxisAngle({1.0F, 0.0F, 0.0F}, -0.42F);
            const Math::Quaternion yaw = Math::Quaternion::FromAxisAngle({0.0F, 1.0F, 0.0F}, 0.55F);
            const Result<SceneCommandResult> created = m_documentCommands.Execute(CreateSceneObjectCommand{
                .name = "Box",
                .localTransform = Math::Transform{.rotation = pitch * yaw},
                .primitiveMesh = PrimitiveMeshDescriptor{},
            });
            if (created.HasError()) {
                LOG_ERROR("editor.scene_document", "Bootstrap scene creation failed: %s", created.ErrorValue().message.c_str());
            } else {
                static_cast<void>(m_document.MarkSaved(m_document.Revision(), m_document.State()));
                m_history.Clear();
            }
        }
        if (m_defaultScenePath.has_value()) {
            const Result<std::optional<ProjectSceneRecoveryRecord>> recovery =
                InspectProjectSceneRecovery(absoluteProjectRoot, *m_defaultScenePath);
            if (recovery.HasError()) {
                LOG_ERROR("editor.scene_recovery", "Recovery inspection failed: %s", recovery.ErrorValue().message.c_str());
            } else if (recovery.Value().has_value()) {
                m_viewModel.recoveryAvailable = true;
                LOG_WARN("editor.scene_recovery", "Validated recovery is available for '%s'; canonical scene was not modified.",
                         m_defaultScenePath->string().c_str());
            }
        }
        RefreshSceneProjections();
    }

    /** @copydoc EditorWorkspaceController::UpdateExternalSceneWatch */
    void EditorWorkspaceController::UpdateExternalSceneWatch(const float elapsedSeconds) {
        if (m_sceneFileWatch == nullptr || !m_defaultScenePath.has_value() || !m_sceneFingerprint.has_value())
            return;

        for (SceneFileWatchUpdate &update : m_sceneFileWatch->DrainUpdates()) {
            if (update.error.has_value()) {
                if (!m_sceneFileWatchErrorPresented) {
                    LOG_WARN("editor.scene_document", "Background scene inspection failed for '%s': %s",
                             m_defaultScenePath->string().c_str(), update.error->message.c_str());
                    m_sceneFileWatchErrorPresented = true;
                }
                continue;
            }
            if (!update.fingerprint.has_value())
                continue;
            m_sceneFileWatchErrorPresented = false;
            const bool conflict = *update.fingerprint != *m_sceneFingerprint;
            if (conflict && !m_viewModel.sceneExternalConflict) {
                LOG_WARN("editor.scene_document", "Canonical scene changed outside this document session: '%s'.",
                         m_defaultScenePath->string().c_str());
            }
            m_viewModel.sceneExternalConflict = conflict;
        }

        if (!std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.0F || m_sceneFileWatch->HasPendingInspection())
            return;
        m_sceneFileWatchElapsedSeconds += elapsedSeconds;
        if (m_sceneFileWatchElapsedSeconds < 1.0F)
            return;
        m_sceneFileWatchElapsedSeconds = 0.0F;
        const Result<std::uint64_t> requested =
            m_sceneFileWatch->Request(std::filesystem::path{m_viewModel.projectRoot}, *m_defaultScenePath);
        if (requested.HasError() && !m_sceneFileWatchErrorPresented) {
            LOG_WARN("editor.scene_document", "Background scene inspection could not be scheduled: %s",
                     requested.ErrorValue().message.c_str());
            m_sceneFileWatchErrorPresented = true;
        }
    }

    /** @copydoc EditorWorkspaceController::CaptureExternalSceneComparison */
    Result<SceneDocumentComparisonRequest> EditorWorkspaceController::CaptureExternalSceneComparison() const {
        if (!m_defaultScenePath.has_value())
            return Result<SceneDocumentComparisonRequest>::Failure(MakeError(SceneComparisonUnavailable));
        return Result<SceneDocumentComparisonRequest>::Success({
            .absoluteProjectRoot = std::filesystem::path{m_viewModel.projectRoot},
            .absoluteScenePath = *m_defaultScenePath,
            .document = m_document.Snapshot(),
        });
    }

    void EditorWorkspaceController::UpdateFps(const float fps) {
        m_viewModel.fps = fps;
    }

    /** @copydoc EditorWorkspaceController::UpdateAutosave */
    void EditorWorkspaceController::UpdateAutosave(const float elapsedSeconds, const int intervalMinutes) {
        if (intervalMinutes <= 0 || !std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.0F || !m_document.IsDirty() ||
            m_viewModel.recoveryAvailable || m_autosaveSuppressedForDiscard) {
            if (!m_document.IsDirty())
                m_autosaveElapsedSeconds = 0.0F;
            return;
        }
        if (m_document.State() == m_lastAutosavedState)
            return;
        if (m_autosaveRetryDelaySeconds > 0.0F) {
            m_autosaveRetryDelaySeconds = std::max(0.0F, m_autosaveRetryDelaySeconds - elapsedSeconds);
            if (m_autosaveRetryDelaySeconds > 0.0F)
                return;
        }

        m_autosaveElapsedSeconds += elapsedSeconds;
        const float intervalSeconds = static_cast<float>(intervalMinutes) * 60.0F;
        if (m_autosaveElapsedSeconds >= intervalSeconds)
            WriteAutosaveRecovery();
    }

    /** @copydoc EditorWorkspaceController::FlushAutosave */
    void EditorWorkspaceController::FlushAutosave() {
        if (m_document.IsDirty() && !m_viewModel.recoveryAvailable && !m_autosaveSuppressedForDiscard &&
            m_document.State() != m_lastAutosavedState)
            WriteAutosaveRecovery();
    }

    /** @copydoc EditorWorkspaceController::WriteAutosaveRecovery */
    void EditorWorkspaceController::WriteAutosaveRecovery() {
        if (!m_defaultScenePath.has_value() || m_mutations == nullptr || m_durableFiles == nullptr)
            return;
        const SceneDocumentSnapshot snapshot = m_document.Snapshot();
        const Result<void> written =
            WriteProjectSceneRecovery(std::filesystem::path{m_viewModel.projectRoot}, *m_defaultScenePath, snapshot,
                                      m_document.SavedRevision(), m_document.SavedState(), *m_mutations, *m_durableFiles);
        if (written.HasError()) {
            m_autosaveRetryDelaySeconds = 30.0F;
            LOG_ERROR("editor.scene_recovery", "Autosave recovery write failed: %s", written.ErrorValue().message.c_str());
            return;
        }
        m_lastAutosavedState = snapshot.state;
        m_autosaveElapsedSeconds = 0.0F;
        m_autosaveRetryDelaySeconds = 0.0F;
        LOG_INFO("editor.scene_recovery", "Autosaved recovery revision %llu for '%s'.",
                 static_cast<unsigned long long>(snapshot.revision.value), m_defaultScenePath->string().c_str());
    }

    /** @copydoc EditorWorkspaceController::SaveScene */
    void EditorWorkspaceController::SaveScene(const bool overwriteConflict) {
        if (!m_defaultScenePath.has_value() || m_mutations == nullptr || m_durableFiles == nullptr) {
            LOG_ERROR("editor.scene_document", "Save rejected because the project scene path or "
                                               "durable writer services are unavailable.");
            return;
        }

        const SceneDocumentSnapshot snapshot = m_document.Snapshot();
        if (!m_sceneFingerprint.has_value()) {
            LOG_ERROR("editor.scene_document", "Save rejected because the canonical scene identity is unavailable.");
            return;
        }
        const Result<ProjectSceneSaveResult> saved =
            SaveProjectScene(std::filesystem::path{m_viewModel.projectRoot}, *m_defaultScenePath, snapshot, *m_sceneFingerprint,
                             overwriteConflict, *m_mutations, *m_durableFiles);
        if (saved.HasError()) {
            LOG_ERROR("editor.scene_document", "Scene save failed for '%s': %s", m_defaultScenePath->string().c_str(),
                      saved.ErrorValue().message.c_str());
            return;
        }
        if (saved.Value().status == ProjectSceneSaveStatus::Conflict) {
            m_viewModel.sceneExternalConflict = true;
            LOG_WARN("editor.scene_document", "Save paused because '%s' changed outside this document session.",
                     m_defaultScenePath->string().c_str());
            return;
        }

        const Result<void> marked = m_document.MarkSaved(snapshot.revision, snapshot.state);
        if (marked.HasError()) {
            LOG_ERROR("editor.scene_document", "Saved scene state could not be acknowledged: %s", marked.ErrorValue().message.c_str());
            return;
        }
        m_sceneFingerprint = saved.Value().fingerprint;
        if (m_sceneFileWatch != nullptr)
            m_sceneFileWatch->Reset();
        m_viewModel.sceneExternalConflict = false;
        m_sceneFileWatchErrorPresented = false;
        m_sceneFileWatchElapsedSeconds = 0.0F;
        const Result<void> recoveryDiscarded =
            DiscardProjectSceneRecovery(std::filesystem::path{m_viewModel.projectRoot}, *m_mutations, *m_durableFiles);
        if (recoveryDiscarded.HasError()) {
            LOG_WARN("editor.scene_recovery", "Saved scene but could not clean recovery state: %s",
                     recoveryDiscarded.ErrorValue().message.c_str());
        } else {
            m_viewModel.recoveryAvailable = false;
            m_lastAutosavedState = {};
        }
        m_autosaveSuppressedForDiscard = false;
        m_dataBus.Publish(SceneDocumentChangedEvent{m_document.Revision(),
                                                    m_document.State(),
                                                    DocumentChangeKind::SaveStateChanged,
                                                    m_document.IsDirty(),
                                                    {}});
        RefreshSceneProjections();
        LOG_INFO("editor.scene_document", "Saved scene revision %llu to '%s'.", static_cast<unsigned long long>(snapshot.revision.value),
                 m_defaultScenePath->string().c_str());
    }

    /** @copydoc EditorWorkspaceController::SaveSceneToPath */
    void EditorWorkspaceController::SaveSceneToPath(const std::filesystem::path &absolutePath, const bool copyOnly) {
        if (!m_defaultScenePath.has_value() || m_mutations == nullptr || m_durableFiles == nullptr) {
            LOG_ERROR("editor.scene_document", "Destination save rejected because the active scene or "
                                               "durable writer services are unavailable.");
            return;
        }

        std::error_code canonicalError;
        const std::filesystem::path destination = std::filesystem::weakly_canonical(absolutePath, canonicalError);
        if (canonicalError) {
            LOG_ERROR("editor.scene_document", "Destination save rejected because '%s' could not be normalized: %s",
                      absolutePath.string().c_str(), canonicalError.message().c_str());
            return;
        }
        const std::filesystem::path activePath = m_defaultScenePath->lexically_normal();
        if (!destination.is_absolute()) {
            LOG_ERROR("editor.scene_document", "Destination save rejected because '%s' is not absolute.", destination.string().c_str());
            return;
        }
        if (copyOnly && destination == activePath) {
            LOG_ERROR("editor.scene_document", "Save Copy As rejected because the destination is the active scene path '%s'.",
                      activePath.string().c_str());
            return;
        }
        if (!copyOnly && destination == activePath) {
            SaveScene(true);
            return;
        }

        const SceneDocumentSnapshot snapshot = m_document.Snapshot();
        auto saved = SaveProjectSceneToPath(std::filesystem::path{m_viewModel.projectRoot}, destination, snapshot, true, *m_mutations,
                                            *m_durableFiles);
        if (saved.HasError()) {
            LOG_ERROR("editor.scene_document", "%s failed for '%s': %s", copyOnly ? "Save Copy As" : "Save As",
                      destination.string().c_str(), saved.ErrorValue().message.c_str());
            return;
        }
        if (saved.Value().status != ProjectSceneDestinationSaveStatus::Saved) {
            LOG_WARN("editor.scene_document", "%s paused because destination '%s' changed during the save.",
                     copyOnly ? "Save Copy As" : "Save As", destination.string().c_str());
            return;
        }

        if (copyOnly) {
            LOG_INFO("editor.scene_document", "Saved scene copy revision %llu to '%s'.",
                     static_cast<unsigned long long>(snapshot.revision.value), destination.string().c_str());
            return;
        }

        const Result<void> marked = m_document.MarkSaved(snapshot.revision, snapshot.state);
        if (marked.HasError()) {
            LOG_ERROR("editor.scene_document",
                      "Saved As destination is durable but the active document state could not be "
                      "acknowledged: %s",
                      marked.ErrorValue().message.c_str());
            return;
        }

        m_defaultScenePath = destination;
        m_sceneFingerprint = saved.Value().fingerprint;
        if (m_sceneFileWatch != nullptr)
            m_sceneFileWatch->Reset();
        m_viewModel.sceneExternalConflict = false;
        m_sceneFileWatchErrorPresented = false;
        m_sceneFileWatchElapsedSeconds = 0.0F;
        const Result<void> recoveryDiscarded =
            DiscardProjectSceneRecovery(std::filesystem::path{m_viewModel.projectRoot}, *m_mutations, *m_durableFiles);
        if (recoveryDiscarded.HasError()) {
            LOG_WARN("editor.scene_recovery", "Saved scene to a new path but could not clean recovery state: %s",
                     recoveryDiscarded.ErrorValue().message.c_str());
        } else {
            m_viewModel.recoveryAvailable = false;
            m_lastAutosavedState = {};
        }
        m_autosaveSuppressedForDiscard = false;
        m_dataBus.Publish(SceneDocumentChangedEvent{m_document.Revision(),
                                                    m_document.State(),
                                                    DocumentChangeKind::SaveStateChanged,
                                                    m_document.IsDirty(),
                                                    {}});
        RefreshSceneProjections();
        LOG_INFO("editor.scene_document", "Saved scene revision %llu as '%s'; active document identity was updated.",
                 static_cast<unsigned long long>(snapshot.revision.value), destination.string().c_str());
    }

    /** @copydoc EditorWorkspaceController::ReloadExternalScene */
    void EditorWorkspaceController::ReloadExternalScene() {
        if (!m_defaultScenePath.has_value() || m_mutations == nullptr || m_durableFiles == nullptr)
            return;

        const Result<LoadedProjectScene> loaded = LoadProjectScene(std::filesystem::path{m_viewModel.projectRoot}, *m_defaultScenePath);
        if (loaded.HasError() || !loaded.Value().existed) {
            LOG_ERROR("editor.scene_document", "External scene reload failed because the canonical document is unavailable.");
            return;
        }

        LoadedProjectScene external = loaded.Value();
        SceneDocument validatedExternal;
        const Result<void> validated = validatedExternal.LoadSaved(external.objects);
        if (validated.HasError()) {
            LOG_ERROR("editor.scene_document", "External scene validation failed: %s", validated.ErrorValue().message.c_str());
            return;
        }
        const Result<void> recoveryDiscarded =
            DiscardProjectSceneRecovery(std::filesystem::path{m_viewModel.projectRoot}, *m_mutations, *m_durableFiles);
        if (recoveryDiscarded.HasError()) {
            LOG_ERROR("editor.scene_recovery", "External reload could not discard superseded recovery state: %s",
                      recoveryDiscarded.ErrorValue().message.c_str());
            return;
        }
        const Result<void> installed = m_document.LoadSaved(std::move(external.objects));
        if (installed.HasError()) {
            LOG_ERROR("editor.scene_document", "External scene validation failed: %s", installed.ErrorValue().message.c_str());
            return;
        }

        m_sceneFingerprint = std::move(external.fingerprint);
        if (m_sceneFileWatch != nullptr)
            m_sceneFileWatch->Reset();
        m_history.Clear();
        m_selection.Clear();
        m_deferredRuntimeSnapshot.reset();
        m_activeRuntimeRevision = {};
        m_queuedDefinitionRevision = {};
        m_viewModel.sceneExternalConflict = false;
        m_sceneFileWatchErrorPresented = false;
        m_sceneFileWatchElapsedSeconds = 0.0F;
        m_viewModel.recoveryAvailable = false;
        m_lastAutosavedState = {};
        m_autosaveElapsedSeconds = 0.0F;
        m_autosaveRetryDelaySeconds = 0.0F;
        m_autosaveSuppressedForDiscard = false;
        RefreshSceneProjections();
        LOG_INFO("editor.scene_document", "Reloaded externally changed scene '%s'.", m_defaultScenePath->string().c_str());
    }

    /** @copydoc EditorWorkspaceController::RestoreSceneRecovery */
    void EditorWorkspaceController::RestoreSceneRecovery() {
        if (!m_defaultScenePath.has_value())
            return;
        const Result<std::optional<ProjectSceneRecoveryRecord>> recovery =
            InspectProjectSceneRecovery(std::filesystem::path{m_viewModel.projectRoot}, *m_defaultScenePath);
        if (recovery.HasError() || !recovery.Value().has_value()) {
            LOG_ERROR("editor.scene_recovery", "Recovery restore failed because no valid record is available.");
            return;
        }
        const Result<void> restored = m_document.LoadRecovered(std::move(recovery.Value()->objects));
        if (restored.HasError()) {
            LOG_ERROR("editor.scene_recovery", "Recovery restore validation failed: %s", restored.ErrorValue().message.c_str());
            return;
        }
        m_history.Clear();
        m_selection.Clear();
        m_viewModel.recoveryAvailable = false;
        m_lastAutosavedState = m_document.State();
        m_autosaveSuppressedForDiscard = false;
        RefreshSceneProjections();
        LOG_INFO("editor.scene_recovery", "Recovery restored into a new dirty document session.");
    }

    /** @copydoc EditorWorkspaceController::DiscardSceneRecovery */
    void EditorWorkspaceController::DiscardSceneRecovery() {
        if (m_mutations == nullptr || m_durableFiles == nullptr)
            return;
        const Result<void> discarded =
            DiscardProjectSceneRecovery(std::filesystem::path{m_viewModel.projectRoot}, *m_mutations, *m_durableFiles);
        if (discarded.HasError()) {
            LOG_ERROR("editor.scene_recovery", "Recovery discard failed: %s", discarded.ErrorValue().message.c_str());
            return;
        }
        m_viewModel.recoveryAvailable = false;
        m_lastAutosavedState = {};
        m_autosaveSuppressedForDiscard = true;
        LOG_INFO("editor.scene_recovery", "Recovery state discarded explicitly.");
    }

    /** @copydoc EditorWorkspaceController::RefreshAssets */
    void EditorWorkspaceController::RefreshAssets(const Assets::AssetRegistrySnapshot &assetRegistry) {
        if (assetRegistry.Revision() == m_viewModel.assetRegistryRevision)
            return;
        m_assetRegistry = assetRegistry;
        m_viewModel.assetRegistryRevision = assetRegistry.Revision();
        m_contentBrowserRefreshPending = false;
        m_contentBrowserLoadingPresented = false;
        m_viewModel.contentBrowser = BuildContentBrowserDirectory(m_viewModel.projectRoot, m_viewModel.contentBrowser.absoluteCurrentPath,
                                                                  m_assetRegistry, m_importerCatalog);
        ReconcileContentBrowserNavigation();
    }

    /** @copydoc EditorWorkspaceController::UpdateContentBrowser */
    void EditorWorkspaceController::UpdateContentBrowser() {
        if (!m_contentBrowserRefreshPending)
            return;
        if (!m_contentBrowserLoadingPresented) {
            m_contentBrowserLoadingPresented = true;
            return;
        }

        m_contentBrowserRefreshPending = false;
        m_contentBrowserLoadingPresented = false;
        RefreshContentBrowserAfterMutation();
    }

    void EditorWorkspaceController::ProcessCommand(const EditorWorkspaceViewCommandData &cmd) {
        // A transient overlay must not survive the interaction or workspace action that owns it.
        if (m_viewport.Current().transformPreview.has_value() && cmd.command != EditorWorkspaceViewCommand::PreviewObjectTransform &&
            cmd.command != EditorWorkspaceViewCommand::CommitObjectTransform &&
            cmd.command != EditorWorkspaceViewCommand::CancelObjectTransformPreview) {
            CancelObjectTransformPreview();
        }
        switch (cmd.command) {
            case EditorWorkspaceViewCommand::None:
            case EditorWorkspaceViewCommand::ReturnToWelcome:
            case EditorWorkspaceViewCommand::CompareExternalScene:
                break;
            case EditorWorkspaceViewCommand::SaveScene:
                SaveScene();
                break;
            case EditorWorkspaceViewCommand::SaveSceneAs:
                if (cmd.stringPayload.has_value())
                    SaveSceneToPath(std::filesystem::path{*cmd.stringPayload}, false);
                break;
            case EditorWorkspaceViewCommand::SaveSceneCopyAs:
                if (cmd.stringPayload.has_value())
                    SaveSceneToPath(std::filesystem::path{*cmd.stringPayload}, true);
                break;
            case EditorWorkspaceViewCommand::ReloadExternalScene:
                ReloadExternalScene();
                break;
            case EditorWorkspaceViewCommand::OverwriteExternalScene:
                SaveScene(true);
                break;
            case EditorWorkspaceViewCommand::RestoreSceneRecovery:
                RestoreSceneRecovery();
                break;
            case EditorWorkspaceViewCommand::DiscardSceneRecovery:
                DiscardSceneRecovery();
                break;
            case EditorWorkspaceViewCommand::UndoScene:
                HandleDocumentCommandResult(m_documentCommands.Undo(), "Undo");
                break;
            case EditorWorkspaceViewCommand::RedoScene:
                HandleDocumentCommandResult(m_documentCommands.Redo(), "Redo");
                break;
            case EditorWorkspaceViewCommand::CreatePrimitive:
                if (cmd.primitivePayload.has_value()) {
                    HandleCreatePrimitive(*cmd.primitivePayload, cmd.objectPayload);
                }
                break;
            case EditorWorkspaceViewCommand::DuplicateObject:
                if (cmd.objectPayload.has_value())
                    HandleDuplicateObject(*cmd.objectPayload);
                break;
            case EditorWorkspaceViewCommand::DeleteObject:
                if (cmd.objectPayload.has_value())
                    HandleDeleteObject(*cmd.objectPayload);
                break;
            case EditorWorkspaceViewCommand::SelectObject:
                if (cmd.objectPayload.has_value()) {
                    const Result<void> selected = m_selection.SetObjects({*cmd.objectPayload}, *cmd.objectPayload);
                    if (selected.HasError()) {
                        LOG_ERROR("editor.selection", "Select object failed: %s", selected.ErrorValue().message.c_str());
                    }
                    RefreshSelectionProjection();
                }
                break;
            case EditorWorkspaceViewCommand::PickViewport:
                if (cmd.viewportPickPayload.has_value()) {
                    const ViewportPickRequest &request = *cmd.viewportPickPayload;
                    const Result<EditorViewportPickResult> picked =
                        PickEditorViewportScene(m_viewportScene,
                                                EditorViewportPickQuery{request.normalizedX, request.normalizedY, request.aspect});
                    if (picked.HasError()) {
                        LOG_ERROR("editor.viewport_picking", "Viewport pick failed: %s", picked.ErrorValue().message.c_str());
                        break;
                    }
                    const std::optional<Runtime::RuntimeSceneView> active = m_runtimeScene.ActiveScene();
                    if (!active || picked.Value().runtimeScene != active->RuntimeId()) {
                        LOG_WARN("editor.viewport_picking", "Discarded a stale runtime-scene pick result.");
                        break;
                    }
                    if (picked.Value().object) {
                        const SceneObjectId object = *picked.Value().object;
                        const Result<void> selected = m_selection.SetObjects({object}, object);
                        if (selected.HasError())
                            LOG_ERROR("editor.selection", "Viewport selection failed: %s", selected.ErrorValue().message.c_str());
                    } else
                        m_selection.Clear();
                    RefreshSelectionProjection();
                }
                break;
            case EditorWorkspaceViewCommand::NavigateViewport:
                if (cmd.viewportNavigationPayload.has_value()) {
                    const Result<void> navigated = m_viewport.Navigate(*cmd.viewportNavigationPayload);
                    if (navigated.HasError()) {
                        LOG_ERROR("editor.viewport", "Viewport navigation failed: %s", navigated.ErrorValue().message.c_str());
                    } else {
                        m_viewportScene.camera = m_viewport.Current().camera;
                        m_viewModel.viewportCamera = m_viewport.Current().camera;
                    }
                }
                break;
            case EditorWorkspaceViewCommand::ChangeViewportProjection:
                if (cmd.viewportProjectionPayload.has_value()) {
                    const Result<void> changed = m_viewport.SetProjection(*cmd.viewportProjectionPayload);
                    if (changed.HasError())
                        LOG_ERROR("editor.viewport", "Viewport projection change failed: %s", changed.ErrorValue().message.c_str());
                    else {
                        m_viewportScene.camera = m_viewport.Current().camera;
                        m_viewModel.viewportCamera = m_viewport.Current().camera;
                    }
                }
                break;
            case EditorWorkspaceViewCommand::FocusViewportSelection:
                if (m_viewModel.primarySelectionWorldBounds.has_value() && cmd.floatPayload.has_value()) {
                    const Result<void> focused = m_viewport.Focus(*m_viewModel.primarySelectionWorldBounds, *cmd.floatPayload);
                    if (focused.HasError())
                        LOG_ERROR("editor.viewport", "Viewport focus failed: %s", focused.ErrorValue().message.c_str());
                    else {
                        m_viewportScene.camera = m_viewport.Current().camera;
                        m_viewModel.viewportCamera = m_viewport.Current().camera;
                    }
                }
                break;
            case EditorWorkspaceViewCommand::ChangeTransformTool:
                if (cmd.transformToolPayload.has_value()) {
                    m_viewModel.activeTransformTool = *cmd.transformToolPayload;
                }
                break;
            case EditorWorkspaceViewCommand::ChangeTransformSpace:
                if (cmd.transformSpacePayload.has_value()) {
                    m_viewModel.activeTransformSpace = *cmd.transformSpacePayload;
                }
                break;
            case EditorWorkspaceViewCommand::PreviewObjectTransform:
                if (cmd.objectPayload.has_value() && cmd.transformPayload.has_value()) {
                    PreviewObjectTransform(*cmd.objectPayload, *cmd.transformPayload);
                }
                break;
            case EditorWorkspaceViewCommand::CommitObjectTransform:
                if (cmd.objectPayload.has_value() && cmd.transformPayload.has_value()) {
                    HandleDocumentCommandResult(m_documentCommands.Execute(
                                                    SetSceneObjectTransformCommand{*cmd.objectPayload, *cmd.transformPayload}),
                                                "Transform object");
                }
                break;
            case EditorWorkspaceViewCommand::CancelObjectTransformPreview:
                CancelObjectTransformPreview();
                break;
            case EditorWorkspaceViewCommand::UpdateObjectName:
                if (cmd.objectPayload.has_value() && cmd.stringPayload.has_value()) {
                    HandleDocumentCommandResult(m_documentCommands.Execute(
                                                    RenameSceneObjectCommand{*cmd.objectPayload, *cmd.stringPayload}),
                                                "Rename object");
                }
                break;
            case EditorWorkspaceViewCommand::UpdateCameraComponent:
                if (cmd.objectPayload.has_value() && cmd.cameraPayload.has_value()) {
                    HandleDocumentCommandResult(m_documentCommands.Execute(
                                                    SetSceneObjectCameraCommand{*cmd.objectPayload, *cmd.cameraPayload}),
                                                "Update camera");
                }
                break;
            case EditorWorkspaceViewCommand::NavigateContentBrowser:
                if (cmd.stringPayload.has_value())
                    NavigateContentBrowser(*cmd.stringPayload, true);
                break;
            case EditorWorkspaceViewCommand::NavigateContentBrowserBack:
                NavigateContentBrowserBack();
                break;
            case EditorWorkspaceViewCommand::NavigateContentBrowserForward:
                NavigateContentBrowserForward();
                break;
            case EditorWorkspaceViewCommand::NavigateContentBrowserUp:
                NavigateContentBrowserUp();
                break;
            case EditorWorkspaceViewCommand::RefreshContentBrowser:
                RequestContentBrowserRefresh();
                break;
            case EditorWorkspaceViewCommand::RenameContentBrowserEntry:
                if (cmd.stringPayload.has_value() && cmd.secondaryStringPayload.has_value())
                    RenameContentBrowserEntry(*cmd.stringPayload, *cmd.secondaryStringPayload);
                break;
            case EditorWorkspaceViewCommand::DeleteContentBrowserEntry:
                if (cmd.stringPayload.has_value())
                    DeleteContentBrowserEntry(*cmd.stringPayload);
                break;
            case EditorWorkspaceViewCommand::DuplicateContentBrowserAsset:
                if (cmd.stringPayload.has_value())
                    DuplicateContentBrowserAsset(*cmd.stringPayload);
                break;
            case EditorWorkspaceViewCommand::CopyContentBrowserAsset:
                if (cmd.stringPayload.has_value())
                    SetContentBrowserClipboard(*cmd.stringPayload, ContentBrowserClipboardMode::Copy);
                break;
            case EditorWorkspaceViewCommand::CutContentBrowserAsset:
                if (cmd.stringPayload.has_value())
                    SetContentBrowserClipboard(*cmd.stringPayload, ContentBrowserClipboardMode::Move);
                break;
            case EditorWorkspaceViewCommand::PasteContentBrowserAsset:
                PasteContentBrowserAsset(cmd.stringPayload.value_or(m_viewModel.contentBrowser.absoluteCurrentPath));
                break;
            case EditorWorkspaceViewCommand::TransferContentBrowserAsset:
                if (cmd.contentBrowserTransfer.has_value())
                    TransferContentBrowserAsset(*cmd.contentBrowserTransfer);
                break;
            case EditorWorkspaceViewCommand::CancelContentBrowserClipboard:
                ClearContentBrowserClipboard();
                break;
            case EditorWorkspaceViewCommand::CreateContentBrowserFolder:
                if (cmd.stringPayload.has_value() && cmd.secondaryStringPayload.has_value())
                    CreateContentBrowserFolder(*cmd.stringPayload, *cmd.secondaryStringPayload);
                break;
            case EditorWorkspaceViewCommand::ReimportContentBrowserAsset:
                if (cmd.stringPayload.has_value())
                    ReimportContentBrowserAsset(*cmd.stringPayload);
                break;
            case EditorWorkspaceViewCommand::RevealContentBrowserEntry:
                if (cmd.stringPayload.has_value())
                    RevealContentBrowserEntry(*cmd.stringPayload);
                break;
            case EditorWorkspaceViewCommand::ChangeActivePanel:
                if (cmd.targetIndex.has_value() && cmd.stringPayload.has_value()) {
                    WorkspaceDockArea area{};
                    if (!TryGetDockArea(*cmd.targetIndex, area))
                        break;
                    const bool panelWasActive =
                        !cmd.stringPayload->empty() &&
                        (*cmd.stringPayload == m_viewModel.activeLeftPanelId || *cmd.stringPayload == m_viewModel.activeRightPanelId ||
                         *cmd.stringPayload == m_viewModel.activeLeftTopPanelId ||
                         *cmd.stringPayload == m_viewModel.activeLeftBottomPanelId ||
                         *cmd.stringPayload == m_viewModel.activeRightTopPanelId ||
                         *cmd.stringPayload == m_viewModel.activeRightBottomPanelId ||
                         *cmd.stringPayload == m_viewModel.activeBottomPanelId ||
                         *cmd.stringPayload == m_viewModel.activeBottomLeftPanelId ||
                         *cmd.stringPayload == m_viewModel.activeBottomRightPanelId ||
                         *cmd.stringPayload == m_viewModel.activeDocumentPanelId);
                    std::vector<std::string> displacedPanelIds;
                    if (!cmd.stringPayload->empty()) {
                        const auto previousPlacement = m_viewModel.panelDockAreas.find(*cmd.stringPayload);
                        if (previousPlacement != m_viewModel.panelDockAreas.end() && previousPlacement->second != area) {
                            switch (previousPlacement->second) {
                                case WorkspaceDockArea::Left:
                                    if (m_viewModel.activeLeftPanelId == *cmd.stringPayload)
                                        m_viewModel.activeLeftPanelId.clear();
                                    if (m_viewModel.activeLeftTopPanelId == *cmd.stringPayload)
                                        m_viewModel.activeLeftTopPanelId.clear();
                                    if (m_viewModel.activeLeftBottomPanelId == *cmd.stringPayload)
                                        m_viewModel.activeLeftBottomPanelId.clear();
                                    break;
                                case WorkspaceDockArea::Right:
                                    if (m_viewModel.activeRightPanelId == *cmd.stringPayload)
                                        m_viewModel.activeRightPanelId.clear();
                                    if (m_viewModel.activeRightTopPanelId == *cmd.stringPayload)
                                        m_viewModel.activeRightTopPanelId.clear();
                                    if (m_viewModel.activeRightBottomPanelId == *cmd.stringPayload)
                                        m_viewModel.activeRightBottomPanelId.clear();
                                    break;
                                case WorkspaceDockArea::Bottom:
                                    if (m_viewModel.activeBottomPanelId == *cmd.stringPayload)
                                        m_viewModel.activeBottomPanelId.clear();
                                    if (m_viewModel.activeBottomLeftPanelId == *cmd.stringPayload)
                                        m_viewModel.activeBottomLeftPanelId.clear();
                                    if (m_viewModel.activeBottomRightPanelId == *cmd.stringPayload)
                                        m_viewModel.activeBottomRightPanelId.clear();
                                    break;
                                case WorkspaceDockArea::Document:
                                    if (m_viewModel.activeDocumentPanelId == *cmd.stringPayload)
                                        m_viewModel.activeDocumentPanelId.clear();
                                    break;
                            }
                            NormalizeDocks(m_viewModel);
                        }
                        m_viewModel.panelDockAreas[*cmd.stringPayload] = area;
                    }

                    switch (area) {
                        case WorkspaceDockArea::Left:
                            ActivateSideDock(m_viewModel.leftDockMode, m_viewModel.activeLeftPanelId, m_viewModel.activeLeftTopPanelId,
                                             m_viewModel.activeLeftBottomPanelId, cmd.sideDockSlot, *cmd.stringPayload, displacedPanelIds);
                            break;
                        case WorkspaceDockArea::Right:
                            ActivateSideDock(m_viewModel.rightDockMode, m_viewModel.activeRightPanelId, m_viewModel.activeRightTopPanelId,
                                             m_viewModel.activeRightBottomPanelId, cmd.sideDockSlot, *cmd.stringPayload, displacedPanelIds);
                            break;
                        case WorkspaceDockArea::Bottom:
                            if (cmd.bottomDockSlot.has_value() && !cmd.stringPayload->empty()) {
                                const std::string previousFull = m_viewModel.activeBottomPanelId;
                                if (m_viewModel.bottomDockMode == BottomDockMode::Full) {
                                    m_viewModel.activeBottomPanelId.clear();
                                    m_viewModel.activeBottomLeftPanelId.clear();
                                    m_viewModel.activeBottomRightPanelId.clear();
                                    m_viewModel.bottomDockMode = BottomDockMode::Split;
                                    if (*cmd.bottomDockSlot == BottomDockSlot::Left) {
                                        m_viewModel.activeBottomLeftPanelId = *cmd.stringPayload;
                                        if (previousFull != *cmd.stringPayload)
                                            m_viewModel.activeBottomRightPanelId = previousFull;
                                    } else {
                                        if (previousFull != *cmd.stringPayload)
                                            m_viewModel.activeBottomLeftPanelId = previousFull;
                                        m_viewModel.activeBottomRightPanelId = *cmd.stringPayload;
                                    }
                                } else {
                                    if (m_viewModel.activeBottomLeftPanelId == *cmd.stringPayload)
                                        m_viewModel.activeBottomLeftPanelId.clear();
                                    if (m_viewModel.activeBottomRightPanelId == *cmd.stringPayload)
                                        m_viewModel.activeBottomRightPanelId.clear();

                                    std::string &targetPanel = *cmd.bottomDockSlot == BottomDockSlot::Left
                                                                   ? m_viewModel.activeBottomLeftPanelId
                                                                   : m_viewModel.activeBottomRightPanelId;
                                    displacedPanelIds.push_back(targetPanel);
                                    targetPanel = *cmd.stringPayload;
                                }
                            } else {
                                displacedPanelIds.push_back(m_viewModel.activeBottomPanelId);
                                displacedPanelIds.push_back(m_viewModel.activeBottomLeftPanelId);
                                displacedPanelIds.push_back(m_viewModel.activeBottomRightPanelId);
                                m_viewModel.bottomDockMode = BottomDockMode::Full;
                                m_viewModel.activeBottomLeftPanelId.clear();
                                m_viewModel.activeBottomRightPanelId.clear();
                                m_viewModel.activeBottomPanelId = *cmd.stringPayload;
                            }
                            break;
                        case WorkspaceDockArea::Document:
                            displacedPanelIds.push_back(m_viewModel.activeDocumentPanelId);
                            m_viewModel.activeDocumentPanelId = *cmd.stringPayload;
                            break;
                    }

                    if (!cmd.stringPayload->empty()) {
                        const char *stackId = nullptr;
                        switch (area) {
                            case WorkspaceDockArea::Left:
                                stackId = "workspace.left";
                                break;
                            case WorkspaceDockArea::Document:
                                stackId = "workspace.document";
                                break;
                            case WorkspaceDockArea::Right:
                                stackId = "workspace.right";
                                break;
                            case WorkspaceDockArea::Bottom:
                                break;
                        }
                        if (stackId != nullptr) {
                            const auto activateResult = m_viewModel.workspacePanelHost.SetActiveTab(stackId, *cmd.stringPayload);
                            if (!activateResult.Succeeded()) {
                                static_cast<void>(m_viewModel.workspacePanelHost.DockPanel(*cmd.stringPayload, stackId,
                                                                                           WorkspacePanelHost::DropKind::TabCenter));
                            }
                        }
                    }

                    for (const std::string &displacedPanelId : displacedPanelIds) {
                        if (displacedPanelId.empty() || displacedPanelId == *cmd.stringPayload) {
                            continue;
                        }
                        LOG_INFO("editor.workspace", "Panel closed: '%s'", displacedPanelId.c_str());
                        m_dataBus.Publish(WorkspacePanelClosedEvent{displacedPanelId, area});
                    }

                    if (!panelWasActive && !cmd.stringPayload->empty()) {
                        LOG_INFO("editor.workspace", "Panel opened: '%s'", cmd.stringPayload->c_str());
                        m_dataBus.Publish(WorkspacePanelOpenedEvent{*cmd.stringPayload, area});
                    }

                    // Workspace allocation drops carry an append slot. Activity Bar slot drops use the
                    // dedicated reorder command and therefore preserve their exact insertion index.
                    if (!cmd.stringPayload->empty() && cmd.activityBarSlot.has_value()) {
                        const auto previousSlot = m_viewModel.activityBarLayout.FindSlot(*cmd.stringPayload);
                        if (previousSlot.has_value()) {
                            const auto result = m_viewModel.activityBarLayout.Move(*cmd.stringPayload, *cmd.activityBarSlot);
                            const auto resultingSlot = m_viewModel.activityBarLayout.FindSlot(*cmd.stringPayload);
                            if (result.Succeeded() && result.code != ActivityBarLayoutOperationCode::NoOp && resultingSlot.has_value()) {
                                m_dataBus.Publish(ActivityBarItemReorderedEvent{*cmd.stringPayload, *previousSlot, *resultingSlot});
                            }
                        }
                    }
                }
                break;
            case EditorWorkspaceViewCommand::ReorderActivityBarItem:
                if (cmd.stringPayload.has_value() && cmd.activityBarSlot.has_value()) {
                    const auto previousSlot = m_viewModel.activityBarLayout.FindSlot(*cmd.stringPayload);
                    if (!previousSlot.has_value()) {
                        break;
                    }

                    const auto previousRegion = RegionForActivitySlot(*previousSlot);
                    const auto targetRegion = RegionForActivitySlot(*cmd.activityBarSlot);
                    const bool activePanelChangesRegion = previousRegion.has_value() && targetRegion.has_value() &&
                                                          *previousRegion != *targetRegion &&
                                                          IsPanelActiveInRegion(m_viewModel, *cmd.stringPayload, *previousRegion);

                    const auto result = m_viewModel.activityBarLayout.Move(*cmd.stringPayload, *cmd.activityBarSlot);
                    if (result.Succeeded() && result.code != ActivityBarLayoutOperationCode::NoOp) {
                        const auto resultingSlot = m_viewModel.activityBarLayout.FindSlot(*cmd.stringPayload);
                        if (!resultingSlot.has_value()) {
                            break;
                        }

                        if (activePanelChangesRegion) {
                            const std::string sourceFallback{
                                m_viewModel.activityBarLayout.ItemAt(previousSlot->rail, previousSlot->groupIndex, 0)};
                            // The activation path needs the source placement to remove the panel from
                            // its old runtime region before assigning its destination.
                            ProcessCommand(MakeRegionActivationCommand(*cmd.stringPayload, *targetRegion));
                            if (!sourceFallback.empty()) {
                                ProcessCommand(MakeRegionActivationCommand(sourceFallback, *previousRegion));
                            }
                            NormalizeDocks(m_viewModel);
                        } else if (targetRegion.has_value()) {
                            m_viewModel.panelDockAreas[*cmd.stringPayload] = targetRegion->area;
                        }

                        m_dataBus.Publish(ActivityBarItemReorderedEvent{*cmd.stringPayload, *previousSlot, *resultingSlot});
                    }
                }
                break;
            case EditorWorkspaceViewCommand::DockWorkspacePanel:
                if (cmd.stringPayload.has_value() && cmd.workspaceDropTarget.has_value()) {
                    const auto &target = *cmd.workspaceDropTarget;
                    const auto result = m_viewModel.workspacePanelHost.DockPanel(*cmd.stringPayload, target.targetNodeId, target.kind);
                    if (result.Succeeded()) {
                        m_dataBus.Publish(WorkspacePanelDockedEvent{*cmd.stringPayload, target.targetNodeId, target.kind});
                    }
                }
                break;
            case EditorWorkspaceViewCommand::ResizePanel:
                if (cmd.targetIndex.has_value() && cmd.floatPayload.has_value()) {
                    WorkspaceDockArea area{};
                    if (!TryGetDockArea(*cmd.targetIndex, area)) {
                        break;
                    }

                    switch (area) {
                        case WorkspaceDockArea::Left:
                            m_viewModel.leftPanelWidth = *cmd.floatPayload;
                            break;
                        case WorkspaceDockArea::Right:
                            m_viewModel.rightPanelWidth = *cmd.floatPayload;
                            break;
                        case WorkspaceDockArea::Bottom:
                            m_viewModel.bottomPanelHeight = *cmd.floatPayload;
                            break;
                        case WorkspaceDockArea::Document:
                            break;
                    }

                    if (cmd.layoutPayload.has_value()) {
                        m_dataBus.Publish(WorkspaceLayoutChangedEvent{area, *cmd.layoutPayload});
                    }
                }
                break;
        }
    }

    void EditorWorkspaceController::HandleCreatePrimitive(const Runtime::PrimitiveId primitive, const std::optional<SceneObjectId> parent) {
        Result<SceneCommandResult> result = m_createSceneObject.Execute(PrimitiveCreationRequest{primitive, parent});
        if (result.HasError()) {
            HandleDocumentCommandResult(std::move(result), "Create object");
            return;
        }
        const SceneObjectId created = result.Value().object;
        const bool committed = result.Value().committed;
        HandleDocumentCommandResult(std::move(result), "Create object");
        if (committed) {
            m_viewModel.hierarchyRevealObject = created;
            m_viewModel.hierarchyRevealRevision = m_document.Revision();
            const Result<void> selected = m_selection.SetObjects({created}, created);
            if (selected.HasError()) {
                LOG_ERROR("editor.selection", "Select created object failed: %s", selected.ErrorValue().message.c_str());
            }
            RefreshSelectionProjection();
        }
    }

    void EditorWorkspaceController::HandleDuplicateObject(const SceneObjectId object) {
        const auto source = std::ranges::find(m_viewModel.objects, object, &SceneObject::id);
        if (source != m_viewModel.objects.end()) {
            HandleDocumentCommandResult(m_documentCommands.Execute(DuplicateSceneObjectCommand{source->id, source->name + " Copy"}),
                                        "Duplicate object");
        }
    }

    void EditorWorkspaceController::HandleDeleteObject(const SceneObjectId object) {
        if (m_document.Contains(object)) {
            HandleDocumentCommandResult(m_documentCommands.Execute(DeleteSceneObjectCommand{object}), "Delete object");
        }
    }

    void EditorWorkspaceController::HandleDocumentCommandResult(Result<SceneCommandResult> result, const char *operation) {
        if (result.HasError()) {
            LOG_ERROR("editor.scene_document", "%s failed: %s", operation, result.ErrorValue().message.c_str());
            return;
        }
        const SceneCommandResult &committed = result.Value();
        if (!committed.committed) {
            CancelObjectTransformPreview();
            return;
        }
        m_viewport.ClearTransformPreview();
        m_dataBus.Publish(SceneDocumentChangedEvent{committed.revision, committed.state, committed.kind, m_document.IsDirty(),
                                                    committed.affectedObjects});
        m_selection.Reconcile();
        RefreshSceneProjections();
    }

    void EditorWorkspaceController::PreviewObjectTransform(const SceneObjectId object, const Math::Transform &transform) {
        const std::optional<Runtime::RuntimeSceneView> active = m_runtimeScene.ActiveScene();
        if (!active || m_viewportScene.runtimeSceneId != active->RuntimeId())
            return;
        const SceneObjectTransformPreview preview{object, transform};
        const std::optional<SceneObjectTransformPreview> previousPreview = m_viewport.Current().transformPreview;
        Result<void> applied = ApplyEditorViewportTransformPreview(*active, preview, m_viewportScene);
        if (applied.HasError()) {
            LOG_ERROR("editor.viewport", "Transform preview failed: %s", applied.ErrorValue().message.c_str());
            return;
        }
        Result<void> committed = m_viewport.SetTransformPreview(preview);
        if (committed.HasError()) {
            const Result<void> restored = ApplyEditorViewportTransformPreview(*active, previousPreview, m_viewportScene);
            if (restored.HasError()) {
                LOG_ERROR("editor.viewport", "Transform preview rollback failed: %s", restored.ErrorValue().message.c_str());
            }
            LOG_ERROR("editor.viewport", "Transform preview state failed: %s", committed.ErrorValue().message.c_str());
            return;
        }
        m_viewModel.primarySelectionPreviewWorldTransform.reset();
        if (m_viewModel.primarySelection == object && m_viewModel.primarySelectionParentWorldTransform.has_value()) {
            m_viewModel.primarySelectionPreviewWorldTransform =
                Math::Multiply(*m_viewModel.primarySelectionParentWorldTransform, transform.ToMatrix());
        }
    }

    void EditorWorkspaceController::CancelObjectTransformPreview() {
        if (!m_viewport.Current().transformPreview.has_value()) {
            return;
        }
        const std::optional<Runtime::RuntimeSceneView> active = m_runtimeScene.ActiveScene();
        if (!active || m_viewportScene.runtimeSceneId != active->RuntimeId())
            return;
        const Result<void> restored = ApplyEditorViewportTransformPreview(*active, {}, m_viewportScene);
        if (restored.HasError()) {
            LOG_ERROR("editor.viewport", "Transform preview cancellation failed: %s", restored.ErrorValue().message.c_str());
            return;
        }
        m_viewport.ClearTransformPreview();
        m_viewModel.primarySelectionPreviewWorldTransform.reset();
    }

    void EditorWorkspaceController::RefreshSceneProjections() {
        const SceneDocumentSnapshot documentSnapshot = m_document.Snapshot();
        m_viewModel.documentRevision = documentSnapshot.revision;
        m_viewModel.objects.clear();
        m_viewModel.objects.reserve(documentSnapshot.objects.size());
        for (const SceneObjectSnapshot &object : documentSnapshot.objects) {
            SceneObjectKind kind = SceneObjectKind::Empty;
            if (object.primitiveMesh.has_value()) {
                kind = SceneObjectKind::Mesh;
            } else if (object.components.camera.has_value()) {
                kind = SceneObjectKind::Camera;
            } else if (object.components.light.has_value()) {
                kind = SceneObjectKind::Light;
            } else if (object.components.triggerVolume.has_value()) {
                kind = SceneObjectKind::TriggerVolume;
            } else if (object.components.audioSource.has_value()) {
                kind = SceneObjectKind::AudioSource;
            }
            m_viewModel.objects.push_back(SceneObject{.id = object.id,
                                                      .parent = object.parent,
                                                      .name = object.name,
                                                      .kind = kind,
                                                      .localTransform = object.localTransform,
                                                      .components = object.components});
        }
        m_viewModel.isDirty = m_document.IsDirty();
        m_viewModel.canUndo = m_history.CanUndo();
        m_viewModel.canRedo = m_history.CanRedo();
        QueueRuntimeScene(documentSnapshot);
        RefreshSelectionProjection();
    }

    void EditorWorkspaceController::QueueRuntimeScene(SceneDocumentSnapshot snapshot) {
        if (snapshot.state.value == m_activeRuntimeRevision.value || snapshot.state.value == m_queuedDefinitionRevision.value ||
            (m_deferredRuntimeSnapshot && m_deferredRuntimeSnapshot->state == snapshot.state))
            return;

        Result<Runtime::RuntimeSceneDefinition> definition = ConvertSceneDocumentToRuntime(snapshot, m_previewSceneId);
        if (definition.HasError()) {
            LOG_ERROR("editor.runtime_scene", "Scene conversion failed: %s", definition.ErrorValue().message.c_str());
            return;
        }
        const Result<void> queued = m_runtimeScene.QueuePreparation(definition.Value());
        if (queued.HasError()) {
            m_deferredRuntimeSnapshot = std::move(snapshot);
            return;
        }
        m_queuedRuntimeRevision = snapshot.revision;
        m_queuedDefinitionRevision = Runtime::SceneDefinitionRevision{snapshot.state.value};
    }

    void EditorWorkspaceController::SynchronizeRuntimeScenePreview() {
        if (std::optional<Error> operationError = m_runtimeScene.TakeOperationError())
            LOG_ERROR("editor.runtime_scene", "Runtime scene operation failed: %s", operationError->message.c_str());

        const std::optional<Runtime::RuntimeSceneView> active = m_runtimeScene.ActiveScene();
        if (!active || active->DefinitionRevision() == m_activeRuntimeRevision)
            return;

        Result<EditorViewportSceneSnapshot> extracted =
            ExtractEditorViewportScene(*active, m_queuedRuntimeRevision, m_viewport.Current().camera, m_primitiveMeshCache);
        if (extracted.HasError()) {
            LOG_ERROR("editor.viewport", "Runtime scene extraction failed: %s", extracted.ErrorValue().message.c_str());
            return;
        }
        m_viewportScene = std::move(extracted).Value();
        m_activeRuntimeRevision = active->DefinitionRevision();
        if (m_queuedDefinitionRevision == m_activeRuntimeRevision)
            m_queuedDefinitionRevision = {};
        m_selection.Reconcile();
        RefreshSelectionProjection();

        if (m_deferredRuntimeSnapshot && m_deferredRuntimeSnapshot->state.value != m_activeRuntimeRevision.value) {
            SceneDocumentSnapshot deferred = std::move(*m_deferredRuntimeSnapshot);
            m_deferredRuntimeSnapshot.reset();
            QueueRuntimeScene(std::move(deferred));
        }
    }

    void EditorWorkspaceController::RefreshContentBrowserAfterMutation() {
        m_contentBrowserRefreshPending = false;
        m_contentBrowserLoadingPresented = false;
        if (m_mutableAssetRegistry != nullptr) {
            auto rebuilt =
                Assets::RebuildAssetRegistry(*m_mutableAssetRegistry, m_viewModel.projectRoot, Assets::AssetRegistryOpenMode::Edit);
            if (rebuilt.HasError() || rebuilt.Value().status == Assets::AssetRegistryBuildStatus::Failed) {
                m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.registry_failed";
                m_viewModel.contentBrowser =
                    BuildContentBrowserDirectory(m_viewModel.projectRoot, m_viewModel.contentBrowser.absoluteCurrentPath, m_assetRegistry,
                                                 m_importerCatalog);
                ReconcileContentBrowserNavigation();
                return;
            }
            m_assetRegistry = m_mutableAssetRegistry->Snapshot();
            m_viewModel.assetRegistryRevision = m_assetRegistry.Revision();
        }
        m_viewModel.contentBrowser = BuildContentBrowserDirectory(m_viewModel.projectRoot, m_viewModel.contentBrowser.absoluteCurrentPath,
                                                                  m_assetRegistry, m_importerCatalog);
        ReconcileContentBrowserNavigation();
    }

    void EditorWorkspaceController::RequestContentBrowserRefresh() {
        if (m_contentBrowserRefreshPending)
            return;
        m_contentBrowserRefreshPending = true;
        m_contentBrowserLoadingPresented = false;
        m_viewModel.contentBrowser.loadState = ContentBrowserLoadState::Loading;
        m_viewModel.contentBrowserOperationError.clear();
    }

    void EditorWorkspaceController::ReconcileContentBrowserNavigation() {
        const std::filesystem::path root = NormalizeAbsolute(m_viewModel.contentBrowser.absoluteRootPath);
        const std::filesystem::path current = NormalizeAbsolute(m_viewModel.contentBrowser.absoluteCurrentPath);
        const auto isValidHistoryEntry = [&root, &current](const std::filesystem::path &entry) {
            const std::filesystem::path normalized = NormalizeAbsolute(entry);
            return !normalized.empty() && normalized != current && IsContentBrowserDirectoryTargetAllowed(root, normalized);
        };
        std::erase_if(m_contentBrowserBackHistory, [&isValidHistoryEntry](const std::filesystem::path &entry) {
            return !isValidHistoryEntry(entry);
        });
        std::erase_if(m_contentBrowserForwardHistory, [&isValidHistoryEntry](const std::filesystem::path &entry) {
            return !isValidHistoryEntry(entry);
        });
        m_viewModel.contentBrowserCanNavigateBack = !m_contentBrowserBackHistory.empty();
        m_viewModel.contentBrowserCanNavigateForward = !m_contentBrowserForwardHistory.empty();
    }

    void EditorWorkspaceController::NavigateContentBrowser(const std::filesystem::path &absoluteDirectory, const bool recordHistory) {
        const std::filesystem::path root = NormalizeAbsolute(m_viewModel.contentBrowser.absoluteRootPath);
        const std::filesystem::path destination = NormalizeAbsolute(absoluteDirectory);
        if (!IsContentBrowserDirectoryTargetAllowed(root, destination)) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.invalid_target";
            return;
        }

        const std::filesystem::path current = NormalizeAbsolute(m_viewModel.contentBrowser.absoluteCurrentPath);
        if (destination == current)
            return;

        m_contentBrowserRefreshPending = false;
        m_contentBrowserLoadingPresented = false;
        if (recordHistory && !current.empty()) {
            m_contentBrowserBackHistory.push_back(current);
            m_contentBrowserForwardHistory.clear();
        }
        m_viewModel.contentBrowser = BuildContentBrowserDirectory(m_viewModel.projectRoot, destination, m_assetRegistry, m_importerCatalog);
        m_viewModel.contentBrowserOperationError.clear();
        m_viewModel.contentBrowserCanNavigateBack = !m_contentBrowserBackHistory.empty();
        m_viewModel.contentBrowserCanNavigateForward = !m_contentBrowserForwardHistory.empty();
    }

    void EditorWorkspaceController::NavigateContentBrowserBack() {
        if (m_contentBrowserBackHistory.empty())
            return;
        const std::filesystem::path current = NormalizeAbsolute(m_viewModel.contentBrowser.absoluteCurrentPath);
        while (!m_contentBrowserBackHistory.empty()) {
            const std::filesystem::path destination = m_contentBrowserBackHistory.back();
            m_contentBrowserBackHistory.pop_back();
            if (!IsContentBrowserDirectoryTargetAllowed(m_viewModel.contentBrowser.absoluteRootPath, destination)) {
                continue;
            }
            if (!current.empty())
                m_contentBrowserForwardHistory.push_back(current);
            NavigateContentBrowser(destination, false);
            break;
        }
        m_viewModel.contentBrowserCanNavigateBack = !m_contentBrowserBackHistory.empty();
        m_viewModel.contentBrowserCanNavigateForward = !m_contentBrowserForwardHistory.empty();
    }

    void EditorWorkspaceController::NavigateContentBrowserForward() {
        if (m_contentBrowserForwardHistory.empty())
            return;
        const std::filesystem::path current = NormalizeAbsolute(m_viewModel.contentBrowser.absoluteCurrentPath);
        while (!m_contentBrowserForwardHistory.empty()) {
            const std::filesystem::path destination = m_contentBrowserForwardHistory.back();
            m_contentBrowserForwardHistory.pop_back();
            if (!IsContentBrowserDirectoryTargetAllowed(m_viewModel.contentBrowser.absoluteRootPath, destination)) {
                continue;
            }
            if (!current.empty())
                m_contentBrowserBackHistory.push_back(current);
            NavigateContentBrowser(destination, false);
            break;
        }
        m_viewModel.contentBrowserCanNavigateBack = !m_contentBrowserBackHistory.empty();
        m_viewModel.contentBrowserCanNavigateForward = !m_contentBrowserForwardHistory.empty();
    }

    void EditorWorkspaceController::NavigateContentBrowserUp() {
        const std::filesystem::path root = NormalizeAbsolute(m_viewModel.contentBrowser.absoluteRootPath);
        const std::filesystem::path current = NormalizeAbsolute(m_viewModel.contentBrowser.absoluteCurrentPath);
        if (!root.empty() && current != root)
            NavigateContentBrowser(current.parent_path(), true);
    }

    void EditorWorkspaceController::DuplicateContentBrowserAsset(const std::string &absolutePath) {
        m_viewModel.contentBrowserOperationError.clear();
        static_cast<void>(CopyContentBrowserAssetTo(NormalizeAbsolute(absolutePath), NormalizeAbsolute(absolutePath).parent_path()));
    }

    void EditorWorkspaceController::SetContentBrowserClipboard(const std::string &absolutePath, const ContentBrowserClipboardMode mode) {
        m_viewModel.contentBrowserOperationError.clear();
        const std::filesystem::path source = NormalizeAbsolute(absolutePath);
        std::error_code error;
        const auto status = std::filesystem::symlink_status(source, error);
        if (!IsDirectContentBrowserEntry(m_viewModel.contentBrowser, source) || error || !std::filesystem::is_regular_file(status)) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.invalid_target";
            return;
        }
        m_viewModel.contentBrowserClipboard = {
            .mode = mode,
            .absoluteSourcePath = source.string(),
        };
    }

    void EditorWorkspaceController::PasteContentBrowserAsset(const std::string &absoluteDirectory) {
        m_viewModel.contentBrowserOperationError.clear();
        const ContentBrowserClipboardState clipboard = m_viewModel.contentBrowserClipboard;
        if (clipboard.mode == ContentBrowserClipboardMode::None || clipboard.absoluteSourcePath.empty()) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.clipboard_empty";
            return;
        }

        const std::filesystem::path source = NormalizeAbsolute(clipboard.absoluteSourcePath);
        const std::filesystem::path destination = NormalizeAbsolute(absoluteDirectory);
        const bool succeeded = clipboard.mode == ContentBrowserClipboardMode::Copy ? CopyContentBrowserAssetTo(source, destination)
                                                                                   : MoveContentBrowserAssetTo(source, destination);
        if (succeeded && clipboard.mode == ContentBrowserClipboardMode::Move)
            ClearContentBrowserClipboard();
    }

    void EditorWorkspaceController::TransferContentBrowserAsset(const ContentBrowserAssetTransferRequest &request) {
        m_viewModel.contentBrowserOperationError.clear();
        if (!std::filesystem::path{request.absoluteSourcePath}.is_absolute() ||
            !std::filesystem::path{request.absoluteDestinationDirectory}.is_absolute()) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.invalid_target";
            return;
        }
        const std::filesystem::path source = NormalizeAbsolute(request.absoluteSourcePath);
        const std::filesystem::path destination = NormalizeAbsolute(request.absoluteDestinationDirectory);
        if (request.mode == ContentBrowserTransferMode::Copy)
            static_cast<void>(CopyContentBrowserAssetTo(source, destination));
        else
            static_cast<void>(MoveContentBrowserAssetTo(source, destination));
    }

    void EditorWorkspaceController::CreateContentBrowserFolder(const std::string &absoluteDirectory, const std::string &name) {
        m_viewModel.contentBrowserOperationError.clear();
        if (m_mutations == nullptr || m_durableFiles == nullptr) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.unavailable";
            return;
        }
        const std::filesystem::path directory = NormalizeAbsolute(absoluteDirectory);
        const std::filesystem::path requestedName{name};
        if (!IsContentBrowserDirectoryTargetAllowed(m_viewModel.contentBrowser.absoluteRootPath, directory) ||
            requestedName != requestedName.filename() || !IsPortableEntryName(name)) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.invalid_name";
            return;
        }
        if (DirectoryContainsPortableName(directory, name)) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.name_exists";
            return;
        }

        auto lease = m_mutations->TryAcquire(ProjectMutationRequest{
            .projectRoot = m_viewModel.projectRoot,
            .owner = ProjectMutationOwner::Asset,
            .operationId = "content-browser-create-folder",
        });
        if (lease.HasError()) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.busy";
            return;
        }
        std::error_code error;
        if (!std::filesystem::create_directory(directory / requestedName, error) || error) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.create_folder_failed";
            return;
        }
        static_cast<void>(m_durableFiles->SyncDirectory(directory));
        RefreshContentBrowserAfterMutation();
    }

    bool EditorWorkspaceController::CopyContentBrowserAssetTo(const std::filesystem::path &absoluteSource,
                                                              const std::filesystem::path &absoluteDestinationDirectory) {
        if (m_mutations == nullptr || m_durableFiles == nullptr || m_mutableAssetRegistry == nullptr) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.unavailable";
            return false;
        }

        const std::filesystem::path root = NormalizeAbsolute(m_viewModel.contentBrowser.absoluteRootPath);
        const std::filesystem::path source = NormalizeAbsolute(absoluteSource);
        const std::filesystem::path destinationDirectory = NormalizeAbsolute(absoluteDestinationDirectory);
        std::error_code error;
        const auto sourceStatus = std::filesystem::symlink_status(source, error);
        if (error || std::filesystem::is_symlink(sourceStatus) || !std::filesystem::is_regular_file(sourceStatus) ||
            !HasPathPrefix(root, source) || !IsContentBrowserDirectoryTargetAllowed(root, destinationDirectory)) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.invalid_target";
            return false;
        }

        const std::filesystem::path projectRoot = NormalizeAbsolute(m_viewModel.projectRoot);
        const std::string projectPath = source.lexically_relative(projectRoot).generic_string();
        const Assets::AssetRecord *sourceRecord = m_assetRegistry.FindByPath(projectPath);
        std::filesystem::path sourceSidecar = source;
        sourceSidecar += ".horo";
        const auto companions = ValidatedAssetCompanions(source, true);
        if (sourceRecord == nullptr || !companions.has_value()) {
            m_viewModel.contentBrowserOperationError = sourceRecord == nullptr ? "workspace.content_browser.operation.asset_required"
                                                                               : "workspace.content_browser.operation.companion_invalid";
            return false;
        }

        std::filesystem::path destination = destinationDirectory / source.filename();
        if (destinationDirectory == source.parent_path() || !AssetDestinationAvailable(source, destination, *companions)) {
            destination = ResolveDuplicateDestination(source, destinationDirectory, *companions);
        }
        if (destination.empty()) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.name_exists";
            return false;
        }

        auto sidecar = ReadSidecarJson(sourceSidecar);
        const Assets::AssetId newId = GenerateRandomAssetId(m_assetRegistry);
        if (!sidecar.has_value() || !newId.IsValid()) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.copy_failed";
            return false;
        }
        (*sidecar)["assetId"] = newId.ToString();

        auto lease = m_mutations->TryAcquire(ProjectMutationRequest{
            .projectRoot = m_viewModel.projectRoot,
            .owner = ProjectMutationOwner::Asset,
            .operationId = "content-browser-copy",
        });
        if (lease.HasError()) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.busy";
            return false;
        }

        std::vector<std::filesystem::path> created;
        for (const std::filesystem::path &item : *companions) {
            const std::filesystem::path target = CompanionDestination(item, source, destination);
            if (!PathDoesNotExist(target)) {
                const bool rollbackComplete = RemoveCreatedPaths(created);
                m_viewModel.contentBrowserOperationError = rollbackComplete ? "workspace.content_browser.operation.name_exists"
                                                                            : "workspace.content_browser.operation.rollback_failed";
                return false;
            }
            if (item == sourceSidecar) {
                const std::vector<std::byte> bytes = JsonBytes(*sidecar);
                if (m_durableFiles->WriteDurable(target, bytes).HasError()) {
                    std::error_code cleanupError;
                    std::filesystem::remove(target, cleanupError);
                    const bool rollbackComplete = RemoveCreatedPaths(created);
                    m_viewModel.contentBrowserOperationError = rollbackComplete ? "workspace.content_browser.operation.copy_failed"
                                                                                : "workspace.content_browser.operation.rollback_failed";
                    return false;
                }
            } else {
                if (m_durableFiles->CopyDurable(item, target).HasError()) {
                    std::error_code cleanupError;
                    std::filesystem::remove(target, cleanupError);
                    const bool rollbackComplete = RemoveCreatedPaths(created);
                    m_viewModel.contentBrowserOperationError = rollbackComplete ? "workspace.content_browser.operation.copy_failed"
                                                                                : "workspace.content_browser.operation.rollback_failed";
                    return false;
                }
            }
            created.push_back(target);
        }
        if (m_durableFiles->SyncDirectory(destinationDirectory).HasError()) {
            const bool rollbackComplete = RemoveCreatedPaths(created);
            m_viewModel.contentBrowserOperationError = rollbackComplete ? "workspace.content_browser.operation.copy_failed"
                                                                        : "workspace.content_browser.operation.rollback_failed";
            return false;
        }

        auto rebuilt = Assets::RebuildAssetRegistry(*m_mutableAssetRegistry, projectRoot, Assets::AssetRegistryOpenMode::Edit);
        const std::string destinationProjectPath = destination.lexically_relative(projectRoot).generic_string();
        const Assets::AssetRegistrySnapshot rebuiltSnapshot = m_mutableAssetRegistry->Snapshot();
        const Assets::AssetRecord *copiedRecord = rebuiltSnapshot.Find(newId);
        if (rebuilt.HasError() || rebuilt.Value().status != Assets::AssetRegistryBuildStatus::Complete || copiedRecord == nullptr ||
            copiedRecord->sourcePath.String() != destinationProjectPath) {
            const bool rollbackComplete = RemoveCreatedPaths(created);
            static_cast<void>(Assets::RebuildAssetRegistry(*m_mutableAssetRegistry, projectRoot, Assets::AssetRegistryOpenMode::Edit));
            m_viewModel.contentBrowserOperationError = rollbackComplete ? "workspace.content_browser.operation.registry_failed"
                                                                        : "workspace.content_browser.operation.rollback_failed";
            return false;
        }
        m_assetRegistry = m_mutableAssetRegistry->Snapshot();
        m_viewModel.assetRegistryRevision = m_assetRegistry.Revision();
        m_viewModel.contentBrowser =
            BuildContentBrowserDirectory(projectRoot, m_viewModel.contentBrowser.absoluteCurrentPath, m_assetRegistry, m_importerCatalog);
        return true;
    }

    bool EditorWorkspaceController::MoveContentBrowserAssetTo(const std::filesystem::path &absoluteSource,
                                                              const std::filesystem::path &absoluteDestinationDirectory) {
        if (m_mutations == nullptr || m_durableFiles == nullptr || m_mutableAssetRegistry == nullptr) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.unavailable";
            return false;
        }
        const std::filesystem::path root = NormalizeAbsolute(m_viewModel.contentBrowser.absoluteRootPath);
        const std::filesystem::path source = NormalizeAbsolute(absoluteSource);
        const std::filesystem::path destinationDirectory = NormalizeAbsolute(absoluteDestinationDirectory);
        std::error_code error;
        const auto sourceStatus = std::filesystem::symlink_status(source, error);
        if (error || std::filesystem::is_symlink(sourceStatus) || !std::filesystem::is_regular_file(sourceStatus) ||
            !HasPathPrefix(root, source) || !IsContentBrowserDirectoryTargetAllowed(root, destinationDirectory)) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.invalid_target";
            return false;
        }
        if (source.parent_path() == destinationDirectory) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.same_folder";
            return false;
        }
        const std::filesystem::path projectRoot = NormalizeAbsolute(m_viewModel.projectRoot);
        const std::string oldProjectPath = source.lexically_relative(projectRoot).generic_string();
        const Assets::AssetRecord *record = m_assetRegistry.FindByPath(oldProjectPath);
        if (record == nullptr) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.asset_required";
            return false;
        }
        const auto companions = ValidatedAssetCompanions(source, true);
        if (!companions.has_value()) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.companion_invalid";
            return false;
        }
        const Assets::AssetId originalId = record->id;
        const std::filesystem::path destination = destinationDirectory / source.filename();
        if (!AssetDestinationAvailable(source, destination, *companions)) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.name_exists";
            return false;
        }

        auto lease = m_mutations->TryAcquire(ProjectMutationRequest{
            .projectRoot = m_viewModel.projectRoot,
            .owner = ProjectMutationOwner::Asset,
            .operationId = "content-browser-move",
        });
        if (lease.HasError()) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.busy";
            return false;
        }

        std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moved;
        for (const std::filesystem::path &item : *companions) {
            const std::filesystem::path target = CompanionDestination(item, source, destination);
            if (!PathDoesNotExist(target)) {
                const bool rollbackComplete = RollbackPathMoves(moved);
                m_viewModel.contentBrowserOperationError = rollbackComplete ? "workspace.content_browser.operation.name_exists"
                                                                            : "workspace.content_browser.operation.rollback_failed";
                return false;
            }
            std::filesystem::rename(item, target, error);
            if (error) {
                const bool rollbackComplete = RollbackPathMoves(moved);
                m_viewModel.contentBrowserOperationError = rollbackComplete ? "workspace.content_browser.operation.move_failed"
                                                                            : "workspace.content_browser.operation.rollback_failed";
                return false;
            }
            moved.emplace_back(item, target);
        }
        if (m_durableFiles->SyncDirectory(source.parent_path()).HasError() ||
            m_durableFiles->SyncDirectory(destinationDirectory).HasError()) {
            const bool rollbackComplete = RollbackPathMoves(moved);
            m_viewModel.contentBrowserOperationError = rollbackComplete ? "workspace.content_browser.operation.move_failed"
                                                                        : "workspace.content_browser.operation.rollback_failed";
            return false;
        }

        auto rebuilt = Assets::RebuildAssetRegistry(*m_mutableAssetRegistry, projectRoot, Assets::AssetRegistryOpenMode::Edit);
        const std::string newProjectPath = destination.lexically_relative(projectRoot).generic_string();
        const bool registryValid = rebuilt.HasValue() && rebuilt.Value().status == Assets::AssetRegistryBuildStatus::Complete &&
                                   m_mutableAssetRegistry->Snapshot().Find(originalId) != nullptr &&
                                   m_mutableAssetRegistry->Snapshot().Find(originalId)->sourcePath.String() == newProjectPath;
        if (!registryValid) {
            const bool rollbackComplete = RollbackPathMoves(moved);
            static_cast<void>(Assets::RebuildAssetRegistry(*m_mutableAssetRegistry, projectRoot, Assets::AssetRegistryOpenMode::Edit));
            m_viewModel.contentBrowserOperationError = rollbackComplete ? "workspace.content_browser.operation.registry_failed"
                                                                        : "workspace.content_browser.operation.rollback_failed";
            return false;
        }
        m_assetRegistry = m_mutableAssetRegistry->Snapshot();
        m_viewModel.assetRegistryRevision = m_assetRegistry.Revision();
        m_viewModel.contentBrowser =
            BuildContentBrowserDirectory(projectRoot, m_viewModel.contentBrowser.absoluteCurrentPath, m_assetRegistry, m_importerCatalog);
        return true;
    }

    void EditorWorkspaceController::ClearContentBrowserClipboard() noexcept {
        m_viewModel.contentBrowserClipboard = {};
    }

    void EditorWorkspaceController::ReimportContentBrowserAsset(const std::string &absolutePath) {
        m_viewModel.contentBrowserOperationError.clear();
        if (m_mutations == nullptr || m_durableFiles == nullptr || m_mutableAssetRegistry == nullptr || m_importerCatalog == nullptr) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.unavailable";
            return;
        }
        const std::filesystem::path target = NormalizeAbsolute(absolutePath);
        if (!IsDirectContentBrowserEntry(m_viewModel.contentBrowser, target)) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.invalid_target";
            return;
        }

        auto lease = m_mutations->TryAcquire(ProjectMutationRequest{
            .projectRoot = m_viewModel.projectRoot,
            .owner = ProjectMutationOwner::Asset,
            .operationId = "content-browser-reimport",
        });
        if (lease.HasError()) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.busy";
            return;
        }

        auto reimported = Assets::ReimportProjectAsset(
            Assets::AssetReimportRequest{
                .absoluteProjectRoot = NormalizeAbsolute(m_viewModel.projectRoot),
                .absoluteAssetPath = target,
                .importerCatalog = m_importerCatalog,
                .registry = m_mutableAssetRegistry,
                .files = m_durableFiles,
            },
            CancellationToken{});
        if (reimported.HasError()) {
            LOG_ERROR("editor.content_browser", "Reimport failed for %s: %s", target.string().c_str(),
                      reimported.ErrorValue().message.c_str());
            m_viewModel.contentBrowserOperationError = reimported.ErrorValue().code.Value() == "asset.import.no_importer"
                                                           ? "workspace.content_browser.operation.reimport_importer_missing"
                                                       : reimported.ErrorValue().code.Value() == "asset.registry.source_missing"
                                                           ? "workspace.content_browser.operation.reimport_unavailable"
                                                           : "workspace.content_browser.operation.reimport_failed";
            return;
        }
        m_assetRegistry = m_mutableAssetRegistry->Snapshot();
        m_viewModel.assetRegistryRevision = m_assetRegistry.Revision();
        m_viewModel.contentBrowser = BuildContentBrowserDirectory(m_viewModel.projectRoot, m_viewModel.contentBrowser.absoluteCurrentPath,
                                                                  m_assetRegistry, m_importerCatalog);
    }

    void EditorWorkspaceController::RevealContentBrowserEntry(const std::string &absolutePath) {
        m_viewModel.contentBrowserOperationError.clear();
        const std::filesystem::path target = NormalizeAbsolute(absolutePath);
        const std::filesystem::path root = NormalizeAbsolute(m_viewModel.contentBrowser.absoluteRootPath);
        std::error_code error;
        const auto status = std::filesystem::symlink_status(target, error);
        if (error || std::filesystem::is_symlink(status) ||
            (!std::filesystem::is_regular_file(status) && !std::filesystem::is_directory(status)) || !HasPathPrefix(root, target)) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.invalid_target";
            return;
        }
        if (!RevealInNativeFileManager(target)) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.reveal_unavailable";
        }
    }

    void EditorWorkspaceController::RenameContentBrowserEntry(const std::string &absolutePath, const std::string &newName) {
        m_viewModel.contentBrowserOperationError.clear();
        if (m_mutations == nullptr || m_durableFiles == nullptr || m_mutableAssetRegistry == nullptr) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.unavailable";
            return;
        }

        if (!std::filesystem::path{absolutePath}.is_absolute()) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.invalid_target";
            return;
        }
        const std::filesystem::path source = NormalizeAbsolute(absolutePath);
        std::filesystem::path requestedName{newName};
        if (!IsDirectContentBrowserEntry(m_viewModel.contentBrowser, source) || requestedName != requestedName.filename() ||
            !IsPortableEntryName(newName)) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.invalid_name";
            return;
        }
        std::error_code error;
        const bool regularFile = std::filesystem::is_regular_file(source, error);
        if (regularFile && requestedName.extension().empty())
            requestedName += source.extension().string();
        if (regularFile && requestedName.extension() != source.extension()) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.invalid_name";
            return;
        }
        const std::filesystem::path destination = source.parent_path() / requestedName;
        if (destination == source)
            return;
        if (DirectoryContainsPortableName(source.parent_path(), requestedName.string(), source)) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.name_exists";
            return;
        }

        const bool directory = std::filesystem::is_directory(source, error);
        if (error) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.invalid_target";
            return;
        }
        std::vector<std::filesystem::path> sources;
        if (directory) {
            sources.push_back(source);
        } else {
            const std::filesystem::path projectRoot = NormalizeAbsolute(m_viewModel.projectRoot);
            const Assets::AssetRecord *record = m_assetRegistry.FindByPath(source.lexically_relative(projectRoot).generic_string());
            const auto companions = ValidatedAssetCompanions(source, record != nullptr);
            if (!companions.has_value()) {
                m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.companion_invalid";
                return;
            }
            sources = *companions;
            for (const std::filesystem::path &item : sources) {
                const std::filesystem::path target = CompanionDestination(item, source, destination);
                if (DirectoryContainsPortableName(target.parent_path(), target.filename().string(), item)) {
                    m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.name_exists";
                    return;
                }
            }
        }

        auto lease = m_mutations->TryAcquire(ProjectMutationRequest{
            .projectRoot = m_viewModel.projectRoot,
            .owner = ProjectMutationOwner::Asset,
            .operationId = "content-browser-rename",
        });
        if (lease.HasError()) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.busy";
            return;
        }

        std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moved;
        moved.reserve(sources.size());
        for (const std::filesystem::path &item : sources) {
            const std::filesystem::path target = CompanionDestination(item, source, destination);
            if (!PathDoesNotExist(target)) {
                const bool rollbackComplete = RollbackPathMoves(moved);
                m_viewModel.contentBrowserOperationError = rollbackComplete ? "workspace.content_browser.operation.name_exists"
                                                                            : "workspace.content_browser.operation.rollback_failed";
                return;
            }
            std::filesystem::rename(item, target, error);
            if (error) {
                const bool rollbackComplete = RollbackPathMoves(moved);
                m_viewModel.contentBrowserOperationError = rollbackComplete ? "workspace.content_browser.operation.rename_failed"
                                                                            : "workspace.content_browser.operation.rollback_failed";
                return;
            }
            moved.emplace_back(item, target);
        }
        if (m_durableFiles->SyncDirectory(source.parent_path()).HasError()) {
            const bool rollbackComplete = RollbackPathMoves(moved);
            m_viewModel.contentBrowserOperationError = rollbackComplete ? "workspace.content_browser.operation.rename_failed"
                                                                        : "workspace.content_browser.operation.rollback_failed";
            return;
        }

        const std::filesystem::path projectRoot = NormalizeAbsolute(m_viewModel.projectRoot);
        auto rebuilt = Assets::RebuildAssetRegistry(*m_mutableAssetRegistry, projectRoot, Assets::AssetRegistryOpenMode::Edit);
        const Assets::AssetRegistrySnapshot rebuiltSnapshot = m_mutableAssetRegistry->Snapshot();
        const bool registryValid = rebuilt.HasValue() && rebuilt.Value().status == Assets::AssetRegistryBuildStatus::Complete &&
                                   rebuiltSnapshot.Records().size() == m_assetRegistry.Records().size() &&
                                   std::ranges::all_of(m_assetRegistry.Records(), [&rebuiltSnapshot](const Assets::AssetRecord &record) {
            return rebuiltSnapshot.Find(record.id) != nullptr;
        });
        if (!registryValid) {
            const bool rollbackComplete = RollbackPathMoves(moved);
            static_cast<void>(Assets::RebuildAssetRegistry(*m_mutableAssetRegistry, projectRoot, Assets::AssetRegistryOpenMode::Edit));
            m_viewModel.contentBrowserOperationError = rollbackComplete ? "workspace.content_browser.operation.registry_failed"
                                                                        : "workspace.content_browser.operation.rollback_failed";
            return;
        }
        m_assetRegistry = rebuiltSnapshot;
        m_viewModel.assetRegistryRevision = m_assetRegistry.Revision();
        m_viewModel.contentBrowser =
            BuildContentBrowserDirectory(projectRoot, m_viewModel.contentBrowser.absoluteCurrentPath, m_assetRegistry, m_importerCatalog);
    }

    void EditorWorkspaceController::DeleteContentBrowserEntry(const std::string &absolutePath) {
        m_viewModel.contentBrowserOperationError.clear();
        if (m_mutations == nullptr || m_durableFiles == nullptr || m_mutableAssetRegistry == nullptr) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.unavailable";
            return;
        }

        if (!std::filesystem::path{absolutePath}.is_absolute()) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.invalid_target";
            return;
        }
        const std::filesystem::path source = NormalizeAbsolute(absolutePath);
        if (!IsDirectContentBrowserEntry(m_viewModel.contentBrowser, source)) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.invalid_target";
            return;
        }

        auto lease = m_mutations->TryAcquire(ProjectMutationRequest{
            .projectRoot = m_viewModel.projectRoot,
            .owner = ProjectMutationOwner::Asset,
            .operationId = "content-browser-delete",
        });
        if (lease.HasError()) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.busy";
            return;
        }

        std::error_code error;
        const bool directory = std::filesystem::is_directory(source, error);
        if (error) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.delete_failed";
            return;
        }

        std::vector<std::filesystem::path> sources;
        if (directory) {
            sources.push_back(source);
        } else {
            const std::filesystem::path projectRoot = NormalizeAbsolute(m_viewModel.projectRoot);
            const Assets::AssetRecord *record = m_assetRegistry.FindByPath(source.lexically_relative(projectRoot).generic_string());
            const auto companions = ValidatedAssetCompanions(source, record != nullptr);
            if (!companions.has_value()) {
                m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.companion_invalid";
                return;
            }
            sources = *companions;
        }

        const auto stamp =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        const std::filesystem::path trashRoot = NormalizeAbsolute(m_viewModel.projectRoot) / ".horo" / "local" / "trash";
        std::filesystem::create_directories(trashRoot, error);
        if (error) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.delete_failed";
            return;
        }
        std::filesystem::path trashDirectory;
        for (std::uint32_t attempt = 0; attempt < 1000; ++attempt) {
            trashDirectory = trashRoot / std::format("asset-{}-{}", stamp, attempt);
            error.clear();
            if (std::filesystem::create_directory(trashDirectory, error)) {
                break;
            }
            if (error) {
                trashDirectory.clear();
                break;
            }
            trashDirectory.clear();
        }
        if (trashDirectory.empty()) {
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.delete_failed";
            return;
        }

        nlohmann::json manifest{
            {"schemaVersion", 1},
            {"originalAbsolutePath", source.string()},
            {"deletedAtUnixMicroseconds", stamp},
            {"entries", nlohmann::json::array()},
        };
        for (const std::filesystem::path &item : sources) {
            manifest["entries"].push_back({
                {"originalAbsolutePath", item.string()},
                {"trashFileName", item.filename().string()},
            });
        }
        std::filesystem::path manifestPath;
        for (std::uint32_t attempt = 0; attempt < 1000; ++attempt) {
            const std::string fileName = attempt == 0 ? "trash.json" : std::format("trash-{}.json", attempt);
            const bool collidesWithMovedEntry = std::ranges::any_of(sources, [&fileName](const std::filesystem::path &item) {
                return PortableFold(item.filename().string()) == PortableFold(fileName);
            });
            if (!collidesWithMovedEntry) {
                manifestPath = trashDirectory / fileName;
                break;
            }
        }
        if (manifestPath.empty()) {
            std::filesystem::remove_all(trashDirectory, error);
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.delete_failed";
            return;
        }
        if (m_durableFiles->WriteDurable(manifestPath, JsonBytes(manifest)).HasError()) {
            std::filesystem::remove_all(trashDirectory, error);
            m_viewModel.contentBrowserOperationError = "workspace.content_browser.operation.delete_failed";
            return;
        }

        std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moved;
        moved.reserve(sources.size());
        for (const std::filesystem::path &item : sources) {
            const std::filesystem::path target = trashDirectory / item.filename();
            if (!PathDoesNotExist(target)) {
                const bool rollbackComplete = RollbackPathMoves(moved);
                if (rollbackComplete)
                    std::filesystem::remove_all(trashDirectory, error);
                m_viewModel.contentBrowserOperationError = rollbackComplete ? "workspace.content_browser.operation.delete_failed"
                                                                            : "workspace.content_browser.operation.rollback_failed";
                return;
            }
            std::filesystem::rename(item, target, error);
            if (error) {
                const bool rollbackComplete = RollbackPathMoves(moved);
                if (rollbackComplete)
                    std::filesystem::remove_all(trashDirectory, error);
                m_viewModel.contentBrowserOperationError = rollbackComplete ? "workspace.content_browser.operation.delete_failed"
                                                                            : "workspace.content_browser.operation.rollback_failed";
                return;
            }
            moved.emplace_back(item, target);
        }

        if (m_durableFiles->SyncDirectory(source.parent_path()).HasError() || m_durableFiles->SyncDirectory(trashDirectory).HasError()) {
            const bool rollbackComplete = RollbackPathMoves(moved);
            if (rollbackComplete)
                std::filesystem::remove_all(trashDirectory, error);
            m_viewModel.contentBrowserOperationError = rollbackComplete ? "workspace.content_browser.operation.delete_failed"
                                                                        : "workspace.content_browser.operation.rollback_failed";
            return;
        }

        const std::filesystem::path projectRoot = NormalizeAbsolute(m_viewModel.projectRoot);
        auto rebuilt = Assets::RebuildAssetRegistry(*m_mutableAssetRegistry, projectRoot, Assets::AssetRegistryOpenMode::Edit);
        if (rebuilt.HasError() || rebuilt.Value().status != Assets::AssetRegistryBuildStatus::Complete) {
            const bool rollbackComplete = RollbackPathMoves(moved);
            if (rollbackComplete)
                std::filesystem::remove_all(trashDirectory, error);
            static_cast<void>(Assets::RebuildAssetRegistry(*m_mutableAssetRegistry, projectRoot, Assets::AssetRegistryOpenMode::Edit));
            m_viewModel.contentBrowserOperationError = rollbackComplete ? "workspace.content_browser.operation.registry_failed"
                                                                        : "workspace.content_browser.operation.rollback_failed";
            return;
        }
        LOG_INFO("editor.content_browser", "Moved asset entry to recoverable project trash: %s", trashDirectory.string().c_str());
        m_assetRegistry = m_mutableAssetRegistry->Snapshot();
        m_viewModel.assetRegistryRevision = m_assetRegistry.Revision();
        m_viewModel.contentBrowser =
            BuildContentBrowserDirectory(projectRoot, m_viewModel.contentBrowser.absoluteCurrentPath, m_assetRegistry, m_importerCatalog);
    }

    void EditorWorkspaceController::RefreshSelectionProjection() {
        const SelectionSnapshot &selection = m_selection.Current();
        m_viewModel.primarySelection = selection.primary;
        m_viewModel.viewportCamera = m_viewportScene.camera;
        m_viewModel.primarySelectionWorldTransform.reset();
        m_viewModel.primarySelectionPreviewWorldTransform.reset();
        m_viewModel.primarySelectionParentWorldTransform.reset();
        m_viewModel.primarySelectionWorldBounds.reset();
        const std::optional<Runtime::RuntimeSceneView> active = m_runtimeScene.ActiveScene();
        if (selection.primary && active && m_viewportScene.runtimeSceneId == active->RuntimeId()) {
            const Result<SceneObjectWorldTransforms> transforms = ResolveSceneObjectWorldTransforms(*active, *selection.primary);
            if (transforms.HasValue()) {
                m_viewModel.primarySelectionWorldTransform = transforms.Value().localToWorld;
                m_viewModel.primarySelectionParentWorldTransform = transforms.Value().parentToWorld;
            }
        }
        if (m_viewportScene.instances.size() != m_viewportScene.instanceObjects.size()) {
            return;
        }
        for (std::size_t index = 0; index < m_viewportScene.instances.size(); ++index) {
            m_viewportScene.instances[index].presentation.tint = {0.12F, 0.72F, 1.0F};
            m_viewportScene.instances[index].presentation.tintStrength =
                std::ranges::find(selection.objects, m_viewportScene.instanceObjects[index]) != selection.objects.end() ? 0.65F : 0.0F;
            if (selection.primary == m_viewportScene.instanceObjects[index]) {
                const Result<Math::Aabb> bounds =
                    Math::TransformAabb(m_viewportScene.instances[index].localBounds, m_viewportScene.instances[index].localToWorld);
                if (bounds.HasValue())
                    m_viewModel.primarySelectionWorldBounds = bounds.Value();
            }
        }
    }
}  // namespace Horo::Editor
