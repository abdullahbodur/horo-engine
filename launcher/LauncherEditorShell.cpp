#include "launcher/LauncherEditorShell.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <string_view>
#include <system_error>

#include <imgui.h>

#include "core/Logger.h"
#include "core/ProjectPath.h"
#include "editor/SceneSerializer.h"
#include "launcher/LauncherProjectTemplate.h"
#include "launcher/NativeFolderDialog.h"
#include "math/MathUtils.h"
#include "math/Quaternion.h"
#include "renderer/ObjLoader.h"
#include "scene/Entity.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/TransformComponent.h"

namespace Monolith::Launcher {
    namespace fs = std::filesystem;

    namespace {
        std::string BufferToString(const std::array<char, 512> &buffer) {
            return std::string(buffer.data());
        }

        std::string BufferToString(const std::array<char, 256> &buffer) {
            return std::string(buffer.data());
        }

        template<size_t N>
        void CopyToBuffer(std::array<char, N> *buffer, std::string_view value) {
            if (!buffer)
                return;
            buffer->fill('\0');
            if (value.empty())
                return;
            const size_t count = std::min(value.size(), N - 1);
            std::copy_n(value.data(), count, buffer->data());
            (*buffer)[count] = '\0';
        }

        Vec3 ForwardFromYawPitch(float yawDeg, float pitchDeg) {
            const float yawRad = ToRadians(yawDeg);
            const float pitchRad = ToRadians(std::clamp(pitchDeg, -89.0f, 89.0f));
            return {
                -std::sin(yawRad) * std::cos(pitchRad), std::sin(pitchRad),
                -std::cos(yawRad) * std::cos(pitchRad)
            };
        }

        fs::path DefaultBrowseDirectory(const fs::path &rawPath) {
            if (rawPath.empty())
                return fs::current_path();

            std::error_code ec;
            if (fs::is_directory(rawPath, ec))
                return rawPath;

            const fs::path parent = rawPath.parent_path();
            ec.clear();
            if (!parent.empty() && fs::is_directory(parent, ec))
                return parent;

            return fs::current_path();
        }

        bool IsInstalledEnginePrefix(const fs::path &candidate) {
            if (candidate.empty())
                return false;

            std::error_code ec;
            return fs::is_regular_file(candidate / "lib" / "cmake" / "MonolithEngine" /
                                       "MonolithEngineConfig.cmake",
                                       ec) &&
                   !ec;
        }

        bool IsBuildTreeEnginePrefix(const fs::path &candidate) {
            if (candidate.empty())
                return false;

            std::error_code ec;
            const bool hasConfig =
                    fs::is_regular_file(candidate / "MonolithEngineConfig.cmake", ec) && !ec;
            ec.clear();
            const bool hasTargets =
                    fs::is_regular_file(candidate / "MonolithEngineTargets.cmake", ec) && !ec;
            return hasConfig && hasTargets;
        }

        fs::path NormalizePathForLookup(const fs::path &rawPath) {
            if (rawPath.empty())
                return {};

            std::error_code ec;
            fs::path normalized = fs::weakly_canonical(rawPath, ec);
            if (ec)
                normalized = fs::absolute(rawPath, ec);
            if (ec)
                normalized = rawPath;
            return normalized.lexically_normal();
        }

        void ApplyTransformUpdateFromObject(Scene *scene,
                                            const Editor::SceneObject &object) {
            if (!scene)
                return;
            if (const auto runtimeEntityIt = object.props.find("_eid");
                runtimeEntityIt != object.props.end()) {
                try {
                    const auto entity =
                            static_cast<Entity>(std::stoul(runtimeEntityIt->second));
                    if (scene->GetRegistry().IsAlive(entity) &&
                        scene->GetRegistry().Has<TransformComponent>(entity)) {
                        auto &transform = scene->GetRegistry().Get<TransformComponent>(entity);
                        transform.current.position = object.position;
                        transform.previous.position = object.position;
                        transform.current.scale = object.scale;
                        transform.previous.scale = object.scale;
                        transform.current.rotation = Quaternion::FromEuler(
                            ToRadians(object.pitch), ToRadians(object.yaw),
                            ToRadians(object.roll));
                        transform.previous.rotation = transform.current.rotation;
                    }
                } catch (const std::invalid_argument &e) {
                    // UI state may still reference transient runtime ids while scene reload
                    // is in progress.
                    LogDebug("[Launcher] Ignoring invalid runtime entity id '{}': {}",
                             runtimeEntityIt->second, e.what());
                } catch (const std::out_of_range &e) {
                    // Ignore malformed runtime ids coming from stale serialized props.
                    LogDebug("[Launcher] Ignoring out-of-range runtime entity id '{}': {}",
                             runtimeEntityIt->second, e.what());
                }
            }
        }

        // Validates a normalized project root and sets outError on failure.
        // Extracted from OpenProject() to reduce cognitive complexity (cpp:S3776).
        bool CheckProjectRootValid(const fs::path &projectRoot, std::string *outError) {
            if (projectRoot.empty()) {
                if (outError)
                    *outError = "Project path is empty.";
                return false;
            }
            if (!IsLauncherProjectRoot(projectRoot)) {
                if (outError)
                    *outError = "Project manifest not found at " +
                                ResolveProjectManifestPath(projectRoot).string();
                return false;
            }
            return true;
        }

        // Styled button and layout helpers extracted from RenderLauncher() lambdas so
        // their internal branching does not inflate the enclosing function's cognitive
        // complexity (cpp:S3776).

        void RenderCenteredText(const char *text, float panelX, float contentWidth,
                                float scale = 1.0f) {
            ImGui::SetWindowFontScale(scale);
            if (const float textWidth = ImGui::CalcTextSize(text).x;
                textWidth < contentWidth)
                ImGui::SetCursorPosX(panelX + (contentWidth - textWidth) * 0.5f);
            ImGui::TextUnformatted(text);
            ImGui::SetWindowFontScale(1.0f);
        }

        void RenderCenteredEmptyState(const char *text) {
            if (const float regionHeight = ImGui::GetContentRegionAvail().y;
                regionHeight > 40.0f)
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + regionHeight * 0.25f);
            const float availableWidth = ImGui::GetContentRegionAvail().x;
            if (const float textWidth = ImGui::CalcTextSize(text).x;
                textWidth < availableWidth)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                     (availableWidth - textWidth) * 0.5f);
            ImGui::TextDisabled("%s", text);
        }

        bool RenderPrimaryButton(const char *label, const ImVec2 &size) {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18.0f, 16.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.41f, 0.68f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  ImVec4(0.24f, 0.46f, 0.75f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                  ImVec4(0.18f, 0.36f, 0.62f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.97f, 1.0f, 1.0f));
            const bool pressed = ImGui::Button(label, size);
            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar(2);
            return pressed;
        }

        bool RenderSecondaryButton(const char *label, const ImVec2 &size) {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 12.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.15f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  ImVec4(0.16f, 0.20f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                  ImVec4(0.14f, 0.18f, 0.24f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.94f, 0.98f, 1.0f));
            const bool pressed = ImGui::Button(label, size);
            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar(2);
            return pressed;
        }

        bool RenderRecentProjectButton(const char *title, const ImVec2 &size) {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 14.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.12f, 0.16f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  ImVec4(0.16f, 0.20f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                  ImVec4(0.15f, 0.18f, 0.24f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.96f, 1.0f, 1.0f));
            const bool pressed = ImGui::Button(title, size);
            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar(2);
            return pressed;
        }

        void RenderLabeledInput(const char *title, const char *id, char *buffer,
                                size_t bufferSize) {
            ImGui::TextDisabled("%s", title);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText(id, buffer, bufferSize);
        }
    } // namespace

    // Loads the project manifest and default scene document for the given project
    // root. Extracted to reduce cognitive complexity of OpenProject() (cpp:S3776).
    // resolveAsset wraps the caller's ResolveAssetPath member to avoid having to
    // expose it as a free function.
    template<typename F>
    static bool
    LoadLauncherDocuments(const fs::path &projectRoot,
                          LauncherProjectDocument &projectDocument,
                          Editor::SceneDocument &sceneDocument,
                          F &&resolveAsset,
                          std::string *outError) {
        projectDocument = LoadProjectManifestDocument(projectRoot);
        if (projectDocument.parseError) {
            if (outError)
                *outError = projectDocument.error;
            return false;
        }
        try {
            const fs::path scenePath = resolveAsset(
                (projectRoot / projectDocument.manifest.defaultScene)
                .lexically_normal()
                .generic_string());
            sceneDocument = Editor::SceneSerializer::LoadFromFile(scenePath.string());
        } catch (const Editor::SceneSerializerException &e) {
            if (outError)
                *outError = e.what();
            return false;
        }
        return true;
    }

    // Mode-toggle button with custom styling. Extracted from RenderLauncher() to
    // satisfy the 20-line lambda limit (cpp:S1188).
    static bool RenderModeButton(const char *title, bool selected,
                                 const ImVec2 &size) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18.0f, 18.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.54f, 0.93f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  ImVec4(0.28f, 0.58f, 0.97f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                  ImVec4(0.20f, 0.45f, 0.82f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.18f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  ImVec4(0.18f, 0.24f, 0.34f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                  ImVec4(0.16f, 0.22f, 0.31f, 1.0f));
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.96f, 1.0f, 1.0f));
        const bool pressed = ImGui::Button(title, size);
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);
        return pressed;
    }

    void LauncherEditorShell::Attach(Editor::EditorLayer *editor, Scene *scene,
                                     SceneReferenceRuntime *runtime,
                                     Camera *camera) {
        m_editor = editor;
        m_scene = scene;
        m_runtime = runtime;
        m_camera = camera;
    }

    void LauncherEditorShell::Initialize() {
        m_homeDocument = LoadEditorHomeDocument();
        if (m_homeDocument.parseError)
            LogWarn("[Launcher] Editor home settings load fallback: {}",
                    m_homeDocument.error);
        PruneMissingRecentProjects(&m_homeDocument);
        std::string saveError;
        SaveEditorHomeDocument(&m_homeDocument, &saveError);

        CopyToBuffer(&m_newProjectNameInput, "MyHoroGame");
        CopyToBuffer(&m_newProjectPathInput,
                     (fs::current_path() / "MyHoroGame").string());

        ConfigureRuntimeCallbacks();
    }

    void LauncherEditorShell::Shutdown() {
        m_processRunner.Stop();
        std::string saveError;
        SaveEditorHomeDocument(&m_homeDocument, &saveError);
    }

    void LauncherEditorShell::ConfigureRuntimeCallbacks() {
        if (!m_editor || !m_runtime)
            return;

        m_editor->SetFileMenuRenderCallback([this]() {
            if (!HasActiveProject())
                return;
            if (ImGui::MenuItem("Close Project"))
                CloseProject();
        });
        m_editor->SetOverlayRenderCallback([this]() { RenderOverlay(); });
        m_runtime->SetPropEntityCreatedCallback(
            [this](const RuntimeSceneProp &prop, Entity entity, Scene &sceneRef) {
                MeshComponent component;
                component.meshTag = prop.meshTag;
                component.mesh = LoadMeshForTag(prop.meshTag);
                component.material = std::make_shared<Material>();
                component.material->shader = EnsureSceneShader();
                component.material->albedoMap = LoadTexture(prop.albedoMap);
                sceneRef.GetRegistry().Add<MeshComponent>(entity, std::move(component));
            });
        m_editor->SetTransformCallback([this](const Editor::SceneObject &object) {
            ApplyTransformUpdateFromObject(m_scene, object);

            std::string lightError;
            if (!m_runtime->UpdateLiveLight(object, &lightError) &&
                !lightError.empty() && object.type == Editor::SceneObjectType::Light) {
                LogWarn("[Launcher] Live light update failed: {}", lightError);
            }
        });
    }

    bool LauncherEditorShell::OpenProject(const fs::path &projectPath,
                                          std::string *outError) {
        if (outError)
            outError->clear();

        const fs::path projectRoot = NormalizeProjectRootInput(projectPath);
        if (!CheckProjectRootValid(projectRoot, outError))
            return false;

        LauncherProjectDocument projectDocument;
        Editor::SceneDocument sceneDocument;
        if (!LoadLauncherDocuments(
            projectRoot, projectDocument, sceneDocument,
            [this](const std::string &p) { return ResolveAssetPath(p); }, outError))
            return false;

        UnloadCurrentProjectState();
        m_projectRoot = projectRoot;
        m_projectDocument = projectDocument;
        ProjectPath::SetProjectRoot(projectRoot);

        if (m_editor)
            SetupEditorForProject(projectRoot, sceneDocument);

        if (m_runtime) {
            const SceneRuntimeOperationResult load =
                    m_runtime->LoadDocument(sceneDocument);
            if (!load.ok) {
                if (outError)
                    *outError = load.error;
                CloseProject();
                return false;
            }
        }

        if (m_editor && m_scene)
            m_editor->SyncRuntimeEntityIds(m_scene->GetRegistry());
        RefreshCameraFromSceneCamera();
        RememberRecentProject(&m_homeDocument, projectRoot);
        std::string saveError;
        SaveEditorHomeDocument(&m_homeDocument, &saveError);
        m_launcherError.clear();
        LogInfo("[Launcher] Opened project: {}", projectRoot.string());
        return true;
    }

    void LauncherEditorShell::CloseProject() {
        m_processRunner.Stop();

        if (m_runtime && m_runtime->GetCoordinator().IsActive()) {
            const SceneRuntimeOperationResult unload = m_runtime->Unload();
            if (!unload.ok)
                LogWarn("[Launcher] Runtime unload failed while closing project: {}",
                        unload.error);
        }

        if (m_editor)
            m_editor->SaveWorkspaceStateNow();

        if (m_editor) {
            if (m_editor->IsActive())
                m_editor->Toggle();
            m_editor->SetCursorVisible(true);
        }

        if (m_scene)
            m_scene->Clear();

        ProjectPath::SetProjectRoot({});
        if (m_editor) {
            m_editor->ReloadWorkspaceStateFromDisk();
            m_editor->SetProjectBrowserRoot({});
        }
        m_projectDocument = {};
        m_projectRoot.clear();
    }

    void LauncherEditorShell::UnloadCurrentProjectState() {
        m_processRunner.Stop();
        if (m_runtime && m_runtime->GetCoordinator().IsActive()) {
            const SceneRuntimeOperationResult unload = m_runtime->Unload();
            if (!unload.ok)
                LogWarn("[Launcher] Runtime unload during project switch failed: {}",
                        unload.error);
        }
        if (m_scene)
            m_scene->Clear();
        if (m_editor)
            m_editor->SaveWorkspaceStateNow();
    }

    void LauncherEditorShell::SetupEditorForProject(
        const fs::path &projectRoot, const Editor::SceneDocument &sceneDocument) {
        m_editor->ReloadWorkspaceStateFromDisk();
        m_editor->SetProjectBrowserRoot(projectRoot);
        m_editor->LoadDocument(sceneDocument);
        if (!m_editor->IsActive())
            m_editor->Toggle();
    }

    void LauncherEditorShell::Update() {
        m_processRunner.Poll();
        HandlePendingSceneReload();

        if (HasActiveProject() && m_editor && !m_editor->IsActive())
            CloseProject();
    }

    void LauncherEditorShell::RenderOverlay() {
        if (!HasActiveProject())
            RenderLauncher();
    }

    void LauncherEditorShell::HandlePendingSceneReload() {
        if (!HasActiveProject() || !m_editor || !m_runtime ||
            !m_editor->WantsSceneReload())
            return;

        if (const SceneRuntimeOperationResult reload =
                    m_runtime->ReloadDocument(m_editor->GetPendingDocument());
            !reload.ok) {
            LogError("[Launcher] Runtime reload failed: {}", reload.error);
        } else {
            if (m_scene)
                m_editor->SyncRuntimeEntityIds(m_scene->GetRegistry());
            RefreshCameraFromSceneCamera();
        }
        m_editor->AcknowledgeReload();
    }

    void LauncherEditorShell::RefreshCameraFromSceneCamera() {
        if (!m_camera || !m_runtime || !m_runtime->GetSceneCamera().has_value())
            return;

        const RuntimeSceneCamera &sceneCamera = *m_runtime->GetSceneCamera();
        m_camera->position = sceneCamera.position;
        m_camera->target = sceneCamera.position +
                           ForwardFromYawPitch(sceneCamera.yaw, sceneCamera.pitch);
        m_camera->fovY = sceneCamera.fovY;
        m_camera->zNear = sceneCamera.nearClip;
        m_camera->zFar = sceneCamera.farClip;
    }

    bool LauncherEditorShell::OpenProjectFromPicker(std::string *outError) {
        const fs::path pickedPath = PickFolderPath(
            "Select Horo project folder",
            DefaultBrowseDirectory(m_homeDocument.state.lastProjectPath));
        if (pickedPath.empty()) {
            if (outError)
                outError->clear();
            return false;
        }

        return OpenProject(pickedPath, outError);
    }

    void LauncherEditorShell::RenderLauncher() {
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        const ImGuiWindowFlags flags =
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::Begin("Horo Launcher", nullptr, flags);

        const float outerPadding = 20.0f;
        const float contentWidth =
                std::min(viewport->Size.x - outerPadding * 2.0f, 1120.0f);
        const float panelX =
                std::max(outerPadding, (viewport->Size.x - contentWidth) * 0.5f);

        ImGui::SetCursorPos(ImVec2(panelX, outerPadding));
        ImGui::BeginGroup();

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        RenderCenteredText("Welcome to Horo Editor", panelX, contentWidth, 1.95f);
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        RenderCenteredText("Open an existing game project or start a new one.",
                           panelX, contentWidth, 1.28f);
        ImGui::Dummy(ImVec2(0.0f, 16.0f));

        if (!m_launcherError.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "%s",
                               m_launcherError.c_str());
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
        }

        const float actionGap = 12.0f;
        const float actionWidth = (contentWidth - actionGap) * 0.5f;
        const ImVec2 actionSize(actionWidth, 56.0f);
        if (RenderModeButton("Open Existing Project", false, actionSize)) {
            std::string openError;
            if (!OpenProjectFromPicker(&openError) && !openError.empty())
                m_launcherError = openError;
        }
        ImGui::SameLine(0.0f, actionGap);
        RenderModeButton("Create New Project", true, actionSize);

        ImGui::Dummy(ImVec2(0.0f, 14.0f));
        RenderNewProjectPanel(contentWidth);

        ImGui::Dummy(ImVec2(0.0f, 12.0f));
        ImGui::TextUnformatted("Recent Projects");
        ImGui::TextDisabled("Resume work from your latest launcher projects.");
        ImGui::Dummy(ImVec2(0.0f, 6.0f));

        const float recentHeight =
                std::max(160.0f, std::min(260.0f, viewport->Size.y - outerPadding * 2.0f -
                                                  320.0f));
        RenderRecentProjectsList(contentWidth, recentHeight);

        ImGui::EndGroup();
        ImGui::End();
    }

    void LauncherEditorShell::RenderNewProjectPanel(float contentWidth) {
        const float panelHeight = 252.0f;
        ImGui::BeginChild("LauncherPanel", ImVec2(contentWidth, panelHeight), true,
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);

        ImGui::TextUnformatted("Create New Project");
        ImGui::TextDisabled("Pick a location and Horo will scaffold a minimal "
            "launcher-ready project.");
        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        RenderLabeledInput("Project Name", "##new-project-name",
                           m_newProjectNameInput.data(), m_newProjectNameInput.size());
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        RenderLabeledInput("Project Path", "##new-project-path",
                           m_newProjectPathInput.data(), m_newProjectPathInput.size());
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        if (RenderSecondaryButton("Browse Location...", ImVec2(-1.0f, 34.0f))) {
            const fs::path pickedLocation = PickFolderPath(
                "Select project location",
                DefaultBrowseDirectory(BufferToString(m_newProjectPathInput)));
            if (!pickedLocation.empty()) {
                const std::string projectName = BufferToString(m_newProjectNameInput);
                fs::path resolvedProjectPath = pickedLocation;
                if (!projectName.empty() && resolvedProjectPath.filename() != projectName)
                    resolvedProjectPath /= projectName;
                CopyToBuffer(&m_newProjectPathInput,
                             resolvedProjectPath.lexically_normal().string());
                m_launcherError.clear();
            }
        }
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        if (RenderPrimaryButton("Create Project", ImVec2(-1.0f, 40.0f))) {
            std::string createError;
            if (!CreateProjectFromLauncher(&createError))
                m_launcherError = createError;
        }
        ImGui::EndChild();
    }

    void LauncherEditorShell::RenderRecentProjectsList(float contentWidth,
                                                       float panelHeight) {
        ImGui::BeginChild("RecentProjectsList", ImVec2(contentWidth, panelHeight),
                          true);
        if (m_homeDocument.state.recentProjects.empty()) {
            RenderCenteredEmptyState("No recent projects yet.");
        } else {
            for (const std::string &recentPath: m_homeDocument.state.recentProjects) {
                const fs::path path(recentPath);
                if (const std::string title =
                            path.filename().empty() ? recentPath : path.filename().string();
                    !RenderRecentProjectButton(title.c_str(), ImVec2(-1.0f, 38.0f))) {
                    ImGui::TextDisabled("%s", recentPath.c_str());
                    ImGui::Dummy(ImVec2(0.0f, 8.0f));
                    continue;
                }
                if (std::string openError; !OpenProject(path, &openError))
                    m_launcherError = openError;
                ImGui::TextDisabled("%s", recentPath.c_str());
                ImGui::Dummy(ImVec2(0.0f, 8.0f));
            }
        }
        ImGui::EndChild();
    }

    void LauncherEditorShell::RenderProjectToolbar() {
        ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.92f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoCollapse |
                                       ImGuiWindowFlags_NoSavedSettings;
        ImGui::Begin("Launcher Project", nullptr, flags);

        ImGui::TextUnformatted(m_projectDocument.manifest.projectName.c_str());
        ImGui::TextDisabled("%s", m_projectRoot.string().c_str());
        ImGui::Separator();

        if (!m_processRunner.IsActive()) {
            if (ImGui::Button("Configure"))
                ExecuteManifestCommand(m_projectDocument.manifest.configureCommand,
                                       "configure");
            ImGui::SameLine();
            if (ImGui::Button("Build"))
                ExecuteManifestCommand(m_projectDocument.manifest.buildCommand, "build");
            ImGui::SameLine();
            if (ImGui::Button("Run Game"))
                ExecuteManifestCommand(m_projectDocument.manifest.runCommand, "run");
        } else {
            if (ImGui::Button("Stop Process"))
                m_processRunner.Stop();
        }

        ImGui::SameLine();
        if (ImGui::Button("Back To Home"))
            CloseProject();

        if (const ExternalProcessStatus &status = m_processRunner.GetStatus();
            !status.label.empty()) {
            ImGui::Separator();
            ImGui::Text("Last command: %s", status.label.c_str());
            if (!status.commandLine.empty())
                ImGui::TextWrapped("%s", status.commandLine.c_str());
            if (status.active)
                ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), "Running");
            else if (status.finished && status.terminatedByUser)
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "Stopped by user");
            else if (status.finished)
                ImGui::Text("Exit code: %d", status.exitCode);
            if (!status.error.empty())
                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "%s",
                                   status.error.c_str());
        }

        ImGui::End();
    }

    void LauncherEditorShell::ExecuteManifestCommand(
        const LauncherProjectCommand &command, const std::string &label) {
        std::string resolveError;
        ResolvedLauncherCommand resolved;
        if (!ResolveLauncherCommand(command, m_projectRoot, ResolveCommandSdkRoot(),
                                    &resolved, &resolveError)) {
            m_launcherError = resolveError;
            return;
        }

        std::string startError;
        if (!m_processRunner.Start(resolved, label, &startError))
            m_launcherError = startError;
    }

    bool LauncherEditorShell::CreateProjectFromLauncher(std::string *outError) {
        LauncherProjectDocument createdProject;
        const LauncherProjectTemplateRequest request{
            .projectRoot = BufferToString(m_newProjectPathInput),
            .projectName = BufferToString(m_newProjectNameInput),
            .sdkRoot = ResolveCommandSdkRoot(),
        };
        if (!CreateLauncherProjectTemplate(request, &createdProject, outError))
            return false;

        return OpenProject(request.projectRoot, outError);
    }

    fs::path LauncherEditorShell::ResolveCommandSdkRoot() const {
        std::vector<fs::path> candidates;
        const fs::path assetSdkRoot = NormalizePathForLookup(ProjectPath::SdkRoot());
        if (!assetSdkRoot.empty()) {
            candidates.push_back(assetSdkRoot);
            if (assetSdkRoot.filename() == "sdk")
                candidates.push_back(assetSdkRoot.parent_path());
        }

        std::error_code ec;
        if (const fs::path exeDir = NormalizePathForLookup(fs::current_path(ec));
            !ec && !exeDir.empty()) {
            candidates.push_back(exeDir);
            candidates.push_back(exeDir.parent_path());
            candidates.push_back(exeDir.parent_path().parent_path());
        }

        for (const fs::path &candidate: candidates) {
            if (IsInstalledEnginePrefix(candidate) ||
                IsBuildTreeEnginePrefix(candidate))
                return candidate;
        }

        if (!assetSdkRoot.empty() && assetSdkRoot.filename() == "sdk")
            return assetSdkRoot.parent_path();
        return assetSdkRoot;
    }

    fs::path
    LauncherEditorShell::NormalizeProjectRootInput(const fs::path &rawPath) const {
        if (rawPath.empty())
            return {};

        std::error_code ec;
        fs::path normalized = rawPath;
        if (fs::is_regular_file(normalized, ec) &&
            normalized.filename() == "project.json")
            normalized = normalized.parent_path().parent_path();

        normalized = fs::weakly_canonical(normalized, ec);
        if (ec)
            normalized = fs::absolute(rawPath, ec);
        if (ec)
            normalized = rawPath;
        return normalized.lexically_normal();
    }

    fs::path
    LauncherEditorShell::ResolveAssetPath(const std::string &rawPath) const {
        if (rawPath.empty())
            return {};

        if (fs::path path(rawPath); path.is_absolute())
            return path;
        return ProjectPath::Resolve(rawPath);
    }

    fs::path LauncherEditorShell::ResolveShaderPath(const char *fileName) const {
        const std::array<fs::path, 4> candidates = {
            ProjectPath::ResolveSdk(std::string("renderer/shaders/") + fileName),
            ProjectPath::ResolveSdk(std::string("bin/shaders/") + fileName),
            ProjectPath::ResolveSdk(std::string("sdk/renderer/shaders/") + fileName),
            ProjectPath::Root() / "renderer" / "shaders" / fileName,
        };

        for (const fs::path &candidate: candidates) {
            std::error_code ec;
            if (fs::is_regular_file(candidate, ec) && !ec)
                return candidate;
        }
        return candidates.front();
    }

    std::shared_ptr<Shader> LauncherEditorShell::EnsureSceneShader() {
        if (!m_sceneShader) {
            m_sceneShader = std::make_shared<Shader>(
                Shader::FromFiles(ResolveShaderPath("basic.vert").string(),
                                  ResolveShaderPath("basic.frag").string()));
        }
        return m_sceneShader;
    }

    std::shared_ptr<Mesh>
    LauncherEditorShell::LoadMeshForTag(const std::string &meshTag) {
        if (meshTag.empty())
            return {};

        if (const auto it = m_meshCache.find(meshTag); it != m_meshCache.end())
            return it->second;

        auto mesh = std::make_shared<Mesh>();
        try {
            if (meshTag == "box")
                *mesh = Mesh::CreateBox();
            else if (meshTag == "sphere")
                *mesh = Mesh::CreateSphere();
            else if (meshTag == "cylinder")
                *mesh = Mesh::CreateCylinder();
            else if (meshTag == "pyramid")
                *mesh = Mesh::CreatePyramid();
            else if (meshTag == "plane")
                *mesh = Mesh::CreatePlane();
            else
                *mesh = ObjLoader::Load(ResolveAssetPath(meshTag).string());
        } catch (const ObjLoader::ObjLoaderException &e) {
            LogWarn("[Launcher] Failed to load mesh '{}': {}", meshTag, e.what());
            return {};
        }

        m_meshCache[meshTag] = mesh;
        return mesh;
    }

    std::shared_ptr<Texture>
    LauncherEditorShell::LoadTexture(const std::string &rawPath) {
        if (rawPath.empty())
            return {};

        const fs::path path = ResolveAssetPath(rawPath);
        const std::string key = path.generic_string();
        if (const auto it = m_textureCache.find(key); it != m_textureCache.end())
            return it->second;

        auto texture = std::make_shared<Texture>(Texture::FromFile(path.string()));
        if (!texture || !texture->IsValid())
            return {};
        m_textureCache[key] = texture;
        return texture;
    }
} // namespace Monolith::Launcher
