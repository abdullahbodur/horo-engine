#include "HoroEditorApp.h"

#include "Horo/Application/ProjectCompatibility.h"
#include "Horo/Assets/AssetRegistry.h"
#include "Horo/Editor/DefaultScreenFactories.h"
#include "Horo/Editor/DefaultWorkspacePanels.h"
#include "Horo/Editor/EditorConfiguration.h"
#include "Horo/Editor/EditorDataBus.h"
#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorMenuModel.h"
#include "Horo/Editor/EditorSettingsEvents.h"
#include "Horo/Editor/EditorSettingsService.h"
#include "Horo/Editor/EditorSettingsStore.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/GuiRoute.h"
#include "Horo/Editor/GuiScreenHost.h"
#include "Horo/Editor/Localization/LocalizationService.h"
#include "Horo/Editor/ProjectCreationService.h"
#include "Horo/Editor/ProjectOpenService.h"
#include "Horo/Editor/RecentProjectInspectionService.h"
#include "Horo/Editor/WelcomeController.h"
#include "Horo/Editor/WorkspacePanelRegistry.h"
#include "Horo/Foundation/DataBus.h"
#include "Horo/Foundation/JobSystem.h"
#include "Horo/Foundation/Logging/Logger.h"
#include "Horo/Foundation/Platform.h"
#include "Horo/Runtime/Input.h"
#include "Horo/Runtime/Render/RenderFrontend.h"
#include "Horo/Runtime/RuntimeHost.h"
#include "editor/EditorServiceErrors.h"
#include "editor/document/EditorViewportSceneExtractor.h"
#include "editor/input/EditorInputActions.h"
#include "editor/project_model/RendererAvailability.h"
#include "editor/renderer/EditorGuiRenderer.h"
#include "editor/renderer/EditorViewportRenderer.h"
#include "runtime/input/sdl/SdlInputBackend.h"

#if defined(HORO_HAS_RENDER_OPENGL)
#include "runtime/renderer/modules/opengl/OpenGLBackendModule.h"
#endif
#if defined(HORO_HAS_RENDER_METAL)
#include "runtime/renderer/modules/metal/MetalBackendModule.h"
#endif

#include "Horo/Editor/SettingsModal.h"
#include "editor/menu/EditorMenuPlatform.h"
#if defined(HORO_HAS_RENDER_OPENGL)
#include "editor/renderer/opengl/EditorGuiRendererOpenGL.h"
#include "editor/renderer/opengl/EditorViewportRendererOpenGL.h"
#include "editor/renderer/opengl/SdlOpenGLPresentationPort.h"
#endif
#if defined(HORO_HAS_RENDER_METAL)
#include "editor/renderer/metal/EditorGuiRendererMetal.h"
#include "editor/renderer/metal/EditorViewportRendererMetal.h"
#include "editor/renderer/metal/SdlMetalPresentationPort.h"
#endif

#include <SDL3/SDL.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <filesystem>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace Horo::Editor
{
namespace
{
using Theme::Fonts;

class PreparedAssetRegistryState final : public IPreparedProjectOpenDerivedState
{
  public:
    PreparedAssetRegistryState(Assets::AssetRegistry &registry, std::filesystem::path projectRoot,
                               Assets::AssetRegistryCandidate candidate)
        : registry_(registry), projectRoot_(std::move(projectRoot)), candidate_(std::move(candidate))
    {
    }

    Result<std::string> Install() override
    {
        Assets::AssetRegistry validationRegistry;
        Assets::AssetRegistryCandidate diskCandidate = candidate_;
        const Assets::AssetRegistryBuildReport validated = validationRegistry.Publish(std::move(diskCandidate));
        if (validated.status == Assets::AssetRegistryBuildStatus::Failed)
            return Result<std::string>::Failure(
                MakeError(ProjectOpenErrors::DerivedStateFailed, "Asset registry candidate was rejected."));
        const auto saved = Assets::AssetIndexStore::SaveAtomically(projectRoot_ / ".horo/asset_index.json",
                                                                   validationRegistry.Snapshot());
        if (saved.HasError())
            return Result<std::string>::Failure(saved.ErrorValue());
        const Assets::AssetRegistryBuildReport report = registry_.Publish(std::move(candidate_));
        if (report.status == Assets::AssetRegistryBuildStatus::Failed)
            return Result<std::string>::Failure(
                MakeError(ProjectOpenErrors::DerivedStateFailed, "Validated asset registry publication failed."));
        return Result<std::string>::Success(std::to_string(report.publishedRevision.value));
    }

  private:
    Assets::AssetRegistry &registry_;
    std::filesystem::path projectRoot_;
    Assets::AssetRegistryCandidate candidate_;
};

class AssetRegistryProjectOpenContributor final : public IProjectOpenDerivedStateContributor
{
  public:
    explicit AssetRegistryProjectOpenContributor(Assets::AssetRegistry &registry) : registry_(registry)
    {
    }
    std::string_view Id() const noexcept override
    {
        return "horo.assets.registry";
    }
    Result<std::unique_ptr<IPreparedProjectOpenDerivedState>> Prepare(const std::filesystem::path &projectRoot,
                                                                      const CancellationToken &cancellation) override
    {
        if (cancellation.IsCancellationRequested())
            return Result<std::unique_ptr<IPreparedProjectOpenDerivedState>>::Failure(
                MakeError(ProjectOpenErrors::Cancelled));
        auto candidate = Assets::PrepareAssetRegistryCandidate(projectRoot);
        if (candidate.HasError())
            return Result<std::unique_ptr<IPreparedProjectOpenDerivedState>>::Failure(candidate.ErrorValue());
        return Result<std::unique_ptr<IPreparedProjectOpenDerivedState>>::Success(
            std::make_unique<PreparedAssetRegistryState>(registry_, projectRoot, std::move(candidate).Value()));
    }

  private:
    Assets::AssetRegistry &registry_;
};

struct EditorGuiOptions
{
    bool textPreview = false;
    bool exitAfterFirstFrame = false;
    std::uint64_t exitAfterFrames = 0;
    std::string rendererBackend;
    std::string projectRoot;
};

struct EditorTextures
{
    std::uintptr_t logo{0};
};

[[nodiscard]] std::string AssetPath(const char *rel)
{
    return std::string{HORO_EDITOR_ASSET_ROOT} + "/" + rel;
}

[[nodiscard]] bool LoadEditorCatalogResources(LocalizationService &localization)
{
    const std::filesystem::path root = std::filesystem::path{HORO_EDITOR_ASSET_ROOT} / "localization" / "editor";
    bool loadedAny = false;
    std::error_code error;
    if (!std::filesystem::exists(root, error))
        return false;
    for (const auto &entry : std::filesystem::directory_iterator(root, error))
    {
        if (error || !entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        LocalizationError loadError;
        loadedAny = localization.LoadCatalogFile(entry.path(), &loadError) || loadedAny;
    }
    return loadedAny;
}

[[nodiscard]] const ImWchar *BuildEditorGlyphRanges(ImGuiIO &io)
{
    // ImGui default ranges do not always include the small UI glyphs used
    // by the HTML mockups (arrows, multiplication sign, square icon,
    // middle dot). Without these, they render as '?' in the editor.
    static ImVector<ImWchar> ranges;
    if (ranges.empty())
    {
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
        // Add Latin Extended-A for Turkish and other European characters
        static constexpr std::array<ImWchar, 3> latinExtendedA = {0x0100, 0x017F, 0};
        builder.AddRanges(latinExtendedA.data());

        builder.AddChar(0x00B7); // ·
        builder.AddChar(0x00D7); // ×
        builder.AddChar(0x2013); // –
        builder.AddChar(0x2014); // —
        builder.AddChar(0x2026); // …
        builder.AddChar(0x2190); // ←
        builder.AddChar(0x2191); // ↑
        builder.AddChar(0x2192); // →
        builder.AddChar(0x2193); // ↓
        builder.AddChar(0x25A1); // □
        builder.AddChar(0x25AA); // ▪
        builder.AddChar(0x25AB); // ▫
        builder.AddChar(0x2713); // ✓
        builder.BuildRanges(&ranges);
    }
    return ranges.Data;
}

[[nodiscard]] float QueryRasterizerDensity(SDL_Window *window)
{
    int windowW = 0;
    int windowH = 0;
    int drawableW = 0;
    int drawableH = 0;
    SDL_GetWindowSize(window, &windowW, &windowH);
    SDL_GetWindowSizeInPixels(window, &drawableW, &drawableH);
    if (windowW <= 0 || windowH <= 0)
    {
        return 1.0F;
    }
    const float scaleX = static_cast<float>(drawableW) / static_cast<float>(windowW);
    const float scaleY = static_cast<float>(drawableH) / static_cast<float>(windowH);
    const float scale = (scaleX < scaleY) ? scaleX : scaleY;
    return (scale > 1.0F) ? scale : 1.0F;
}

[[nodiscard]] Fonts LoadEditorFonts(ImGuiIO &io, const float rasterizerDensity)
{
    Fonts f;
    const ImWchar *ranges = BuildEditorGlyphRanges(io);
    ImFontConfig sansCfg{};
    sansCfg.OversampleH = 3;
    sansCfg.OversampleV = 2;
    sansCfg.RasterizerDensity = rasterizerDensity;
    ImFontConfig compactCfg{};
    compactCfg.OversampleH = 3;
    compactCfg.OversampleV = 2;
    compactCfg.RasterizerDensity = rasterizerDensity;
    ImFontConfig emphasisCfg{};
    emphasisCfg.OversampleH = 3;
    emphasisCfg.OversampleV = 2;
    emphasisCfg.RasterizerDensity = rasterizerDensity;
    f.sans = io.Fonts->AddFontFromFileTTF(AssetPath("fonts/inter/InterVariable.ttf").c_str(), Theme::FontPx::Sans,
                                          &sansCfg, ranges);
    f.sansCompact = io.Fonts->AddFontFromFileTTF(AssetPath("fonts/inter/InterVariable.ttf").c_str(),
                                                 Theme::FontPx::SansCompact, &compactCfg, ranges);
    f.sansEmphasis = io.Fonts->AddFontFromFileTTF(AssetPath("fonts/inter/InterVariable.ttf").c_str(),
                                                  Theme::FontPx::SansEmphasis, &emphasisCfg, ranges);
    if (f.sans)
        io.FontDefault = f.sans;
    return f;
}

[[nodiscard]] EditorTextures LoadEditorTextures(IEditorGuiRenderer &guiRenderer)
{
    EditorTextures t;
    auto path = AssetPath("launcher/logo.png");
    int w = 0;
    int h = 0;
    int c = 0;
    auto *px = stbi_load(path.c_str(), &w, &h, &c, 4);
    if (!px)
    {
        LOG_WARN("platform.assets", "Logo texture not found at '%s' — sidebar will render without image.",
                 path.c_str());
        return t;
    }
    if (w <= 0 || h <= 0)
    {
        stbi_image_free(px);
        LOG_WARN("platform.assets", "Logo texture at '%s' has invalid dimensions.", path.c_str());
        return t;
    }
    const std::span pixels{px, static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4U};
    const Result<std::uintptr_t> uploaded = guiRenderer.CreateTexture(EditorRgba8ImageView{
        .width = static_cast<std::uint32_t>(w),
        .height = static_cast<std::uint32_t>(h),
        .pixels = pixels,
    });
    stbi_image_free(px);
    if (uploaded.HasError())
    {
        LOG_WARN("platform.assets", "Logo texture upload failed: %s", uploaded.ErrorValue().message.c_str());
        return t;
    }
    t.logo = uploaded.Value();
    return t;
}

void DestroyEditorTextures(EditorTextures &textures, IEditorGuiRenderer &guiRenderer) noexcept
{
    if (textures.logo == 0)
    {
        return;
    }
    guiRenderer.DestroyTexture(textures.logo);
    textures.logo = 0;
}

// Shared components and the welcome screen renderer live under src/editor/design_system and src/editor/screens.
// Modal implementations live under src/editor/modals.
} // namespace

[[nodiscard]] static EditorGuiOptions ParseOptions(const std::span<char *> args) noexcept
{
    EditorGuiOptions opts;
    for (std::size_t i = 1; i < args.size(); ++i)
    {
        std::string_view a{args[i]};
        if (a == "--text-preview")
            opts.textPreview = true;
        else if (a == "--exit-after-first-frame")
            opts.exitAfterFirstFrame = true;
        else if (a == "--exit-after-frames" && i + 1 < args.size())
        {
            const std::string_view value{args[++i]};
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), opts.exitAfterFrames);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
                opts.exitAfterFrames = 0;
        }
        else if (a.starts_with("--renderer="))
            opts.rendererBackend = std::string{a.substr(std::string_view{"--renderer="}.size())};
        else if (args[i] == std::string_view{"--renderer"} && i + 1 < args.size())
            opts.rendererBackend = args[++i];
        else if (args[i] == std::string_view{"--project"} && i + 1 < args.size())
            opts.projectRoot = args[++i];
    }
    return opts;
}

namespace
{
[[nodiscard]] const Render::RenderBackendModuleInfo *ResolveCompiledBackendModuleInfo(
    const std::string_view requestedBackend) noexcept
{
#if defined(HORO_HAS_RENDER_OPENGL)
    if (requestedBackend.empty() || requestedBackend == "opengl")
        return &Render::GetOpenGLRenderBackendModuleInfo();
#endif
#if defined(HORO_HAS_RENDER_METAL)
    if (requestedBackend.empty() || requestedBackend == "metal")
        return &Render::GetMetalRenderBackendModuleInfo();
#endif
    return nullptr;
}

[[nodiscard]] RendererAvailabilitySnapshot BuildRendererAvailabilitySnapshot(const std::string_view activeBackendId)
{
    std::vector<RendererBackendAvailability> entries;
    entries.reserve(3);
#if defined(HORO_HAS_RENDER_OPENGL)
    entries.push_back(RendererBackendAvailability{"opengl",
                                                  "OpenGL",
                                                  activeBackendId == "opengl" ? RendererAvailabilityState::Active
                                                                              : RendererAvailabilityState::Available,
                                                  {}});
#else
    entries.push_back(RendererBackendAvailability{"opengl", "OpenGL", RendererAvailabilityState::NotInstalled,
                                                  "OpenGL renderer component is not installed in this editor build."});
#endif
#if defined(HORO_HAS_RENDER_METAL)
    entries.push_back(RendererBackendAvailability{"metal",
                                                  "Metal",
                                                  activeBackendId == "metal" ? RendererAvailabilityState::Active
                                                                             : RendererAvailabilityState::Available,
                                                  {}});
#else
    entries.push_back(RendererBackendAvailability{"metal", "Metal", RendererAvailabilityState::NotInstalled,
                                                  "Metal renderer component is not installed in this editor build."});
#endif
    entries.push_back(RendererBackendAvailability{"vulkan", "Vulkan", RendererAvailabilityState::NotInstalled,
                                                  "Vulkan renderer component is not installed in this editor build."});
    return RendererAvailabilitySnapshot{std::move(entries), std::string{activeBackendId}};
}

[[nodiscard]] int RelaunchEditorForProject(const char *executable, const EditorRendererRestartRequest &request)
{
    std::vector<std::string> arguments{executable, "--renderer", request.backendId, "--project", request.projectRoot};
    std::vector<char *> nativeArguments;
    nativeArguments.reserve(arguments.size() + 1);
    for (std::string &argument : arguments)
    {
        nativeArguments.push_back(argument.data());
    }
    nativeArguments.push_back(nullptr);

#if defined(_WIN32)
    _execvp(executable, nativeArguments.data());
#else
    execvp(executable, nativeArguments.data());
#endif
    std::fprintf(stderr, "Unable to restart HoroEditor for renderer '%s': %s\n", request.backendId.c_str(),
                 std::strerror(errno));
    return 1;
}

[[nodiscard]] bool InitializeSdlAndCreateWindow(SDL_Window *&window,
                                                const Render::RenderHostWindowRequirements &windowRequirements)
{
    SDL_WindowFlags rendererWindowFlag = 0;
    switch (windowRequirements.presentation)
    {
    case Render::RenderPresentationKind::OpenGL:
        rendererWindowFlag = SDL_WINDOW_OPENGL;
        break;
    case Render::RenderPresentationKind::Metal:
        rendererWindowFlag = SDL_WINDOW_METAL;
        break;
    default:
        LOG_CRITICAL("editor.renderer", "Selected renderer does not describe an interactive host window.");
        return false;
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        const char *err = SDL_GetError();
        LOG_CRITICAL("platform.sdl", "SDL_Init failed: %s", err);
        std::fprintf(stderr, "SDL_Init: %s\n", err);
        Log::Logger::Shutdown();
        return false;
    }
    if (windowRequirements.presentation == Render::RenderPresentationKind::OpenGL)
    {
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    }

    SDL_WindowFlags windowFlags = rendererWindowFlag;
    if (windowRequirements.resizable)
        windowFlags |= SDL_WINDOW_RESIZABLE;
    if (windowRequirements.highPixelDensity)
        windowFlags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    window = SDL_CreateWindow("Horo Editor", 1000, 760, windowFlags);
    if (!window)
    {
        LOG_CRITICAL("platform.sdl", "SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }

    return true;
}

void ApplySavedThemePreference()
{
    auto doc = LoadEditorSettingsDocument();
    if (doc.loadedFromDisk && !doc.parseError)
    {
        const auto savedIndex = static_cast<int>(doc.settings.themePreset);
        Theme::SelectThemeByIndex(savedIndex);
        LOG_DEBUG("editor.settings", "Restored theme preset index %d from disk.", savedIndex);
    }
    else if (doc.loadedFromDisk && doc.parseError)
    {
        LOG_WARN("editor.settings", "editor_settings.json exists but failed to parse — using defaults.");
    }
    else
    {
        LOG_DEBUG("editor.settings", "No editor_settings.json on disk — using defaults.");
    }
}

void ActivateInitialLocale(const EditorSettings &initialSettings, LocalizationService &localization)
{
    LocalizationError localizationError;
    if (const auto locale = LocaleTag::Parse(initialSettings.languageTag); locale.has_value())
    {
        if (localization.Prepare(*locale, &localizationError))
        {
            (void)localization.ActivatePrepared(&localizationError);
            LOG_INFO("editor.startup", "Activated language tag: '%s'", locale->value.c_str());
        }
        else
        {
            LOG_WARN("editor.startup", "Failed to prepare language tag '%s': %s", locale->value.c_str(),
                     localizationError.message.c_str());
        }
    }
    else
    {
        LOG_WARN("editor.startup", "Failed to parse language tag: '%s'", initialSettings.languageTag.c_str());
    }
}

struct EditorRenderComposition
{
#if defined(HORO_HAS_RENDER_OPENGL)
    std::unique_ptr<Render::IOpenGLPresentationPort> openGlPresentationPort;
#endif
#if defined(HORO_HAS_RENDER_METAL)
    std::unique_ptr<Render::IMetalPresentationPort> metalPresentationPort;
    std::unique_ptr<Render::MetalEditorGraphicsBridge> metalGraphicsBridge;
#endif
    std::unique_ptr<Render::RenderFrontend> frontend;
    std::unique_ptr<IEditorGuiRenderer> guiRenderer;
    std::unique_ptr<IEditorViewportRenderer> viewportRenderer;
    Render::RenderTargetHandle viewportTarget;
};

[[nodiscard]] Error MakeEditorRendererError(const char *code, std::string message)
{
    return Error{.code = ErrorCode{code},
                 .domain = ErrorDomainId{"horo.editor.renderer"},
                 .severity = ErrorSeverity::Critical,
                 .message = std::move(message)};
}

[[nodiscard]] Result<EditorRenderComposition> CreateEditorRenderComposition(SDL_Window &window,
                                                                            const EditorGuiOptions &options)
{
    EditorRenderComposition composition;
    Render::RenderBackendRegistry registry;

    if (options.rendererBackend == "opengl")
    {
#if defined(HORO_HAS_RENDER_OPENGL)
        auto port = std::make_unique<SdlOpenGLPresentationPort>(window);
        if (const Result<void> registered = Render::RegisterOpenGLRenderBackend(registry, *port); registered.HasError())
        {
            return Result<EditorRenderComposition>::Failure(registered.ErrorValue());
        }
        composition.openGlPresentationPort = std::move(port);
#else
        return Result<EditorRenderComposition>::Failure(MakeEditorRendererError(
            "editor.renderer.component_missing", "OpenGL renderer component is not present in this build."));
#endif
    }
    else if (options.rendererBackend == "metal")
    {
#if defined(HORO_HAS_RENDER_METAL)
        auto port = std::make_unique<SdlMetalPresentationPort>(window);
        auto graphicsBridge = std::make_unique<Render::MetalEditorGraphicsBridge>();
        if (const Result<void> registered = Render::RegisterMetalRenderBackend(registry, *port, *graphicsBridge);
            registered.HasError())
        {
            return Result<EditorRenderComposition>::Failure(registered.ErrorValue());
        }
        composition.metalPresentationPort = std::move(port);
        composition.metalGraphicsBridge = std::move(graphicsBridge);
#else
        return Result<EditorRenderComposition>::Failure(MakeEditorRendererError(
            "editor.renderer.component_missing", "Metal renderer component is not present in this build."));
#endif
    }
    else
    {
        return Result<EditorRenderComposition>::Failure(MakeEditorRendererError(
            "editor.renderer.unsupported_backend", "Requested renderer backend is unsupported."));
    }

    if (const Result<void> sealed = registry.Seal(); sealed.HasError())
    {
        return Result<EditorRenderComposition>::Failure(sealed.ErrorValue());
    }
    auto frontendResult = Render::RenderFrontend::Create(
        registry, Render::RenderBackendId{options.rendererBackend},
        Render::RenderBackendConfig{.requirePresentation = true,
                                    .enableValidation = false,
                                    .maxFramesInFlight = 2,
                                    .presentMode = options.exitAfterFirstFrame || options.exitAfterFrames > 0
                                                       ? Render::PresentMode::Immediate
                                                       : Render::PresentMode::Fifo});
    if (frontendResult.HasError())
    {
        return Result<EditorRenderComposition>::Failure(frontendResult.ErrorValue());
    }
    composition.frontend = std::move(frontendResult).Value();

    if (options.rendererBackend == "opengl")
    {
#if defined(HORO_HAS_RENDER_OPENGL)
        auto &port = static_cast<SdlOpenGLPresentationPort &>(*composition.openGlPresentationPort);
        auto guiRenderer = std::make_unique<EditorGuiRendererOpenGL>(window, port.Context());
        if (const Result<void> initialized = guiRenderer->Initialize(); initialized.HasError())
        {
            return Result<EditorRenderComposition>::Failure(initialized.ErrorValue());
        }
        auto viewportRenderer = std::make_unique<EditorViewportRendererOpenGL>();
        if (const Result<void> initialized = viewportRenderer->Initialize(); initialized.HasError())
        {
            return Result<EditorRenderComposition>::Failure(initialized.ErrorValue());
        }
        composition.guiRenderer = std::move(guiRenderer);
        composition.viewportRenderer = std::move(viewportRenderer);
#endif
    }
    else
    {
#if defined(HORO_HAS_RENDER_METAL)
        auto &graphicsBridge = *composition.metalGraphicsBridge;
        auto guiRenderer = std::make_unique<EditorGuiRendererMetal>(window, graphicsBridge);
        if (const Result<void> initialized = guiRenderer->Initialize(); initialized.HasError())
        {
            return Result<EditorRenderComposition>::Failure(initialized.ErrorValue());
        }
        auto viewportRenderer = std::make_unique<EditorViewportRendererMetal>(graphicsBridge);
        if (const Result<void> initialized = viewportRenderer->Initialize(); initialized.HasError())
        {
            return Result<EditorRenderComposition>::Failure(initialized.ErrorValue());
        }
        composition.guiRenderer = std::move(guiRenderer);
        composition.viewportRenderer = std::move(viewportRenderer);
#endif
    }

    if (const Result<void> attached = composition.frontend->AttachStaticMeshPassExecutor(*composition.viewportRenderer);
        attached.HasError())
    {
        return Result<EditorRenderComposition>::Failure(attached.ErrorValue());
    }
    auto viewportTarget = composition.frontend->CreateOffscreenTarget({1, 1});
    if (viewportTarget.HasError())
    {
        return Result<EditorRenderComposition>::Failure(viewportTarget.ErrorValue());
    }
    composition.viewportTarget = viewportTarget.Value();

    return Result<EditorRenderComposition>::Success(std::move(composition));
}

struct RunEditorMainLoopParams
{
    bool exitAfterFirstFrame;
    std::uint64_t exitAfterFrames;
    SDL_Window *window;
    ImGuiIO &io;
    const Fonts &fonts;
    const EditorTextures &textures;
    ProjectCreationService &projectCreationService;
    JobSystem &jobSystem;
    EditorSettingsService &settings;
    EngineDataBus &engineEvents;
    EditorDataBus &editorEvents;
    LocalizationService &localization;
    const RendererAvailabilitySnapshot &rendererAvailability;
    GuiRoute initialRoute;
    EditorModalHost &modalHost;
    Input::SdlInputBackend &inputBackend;
    Input::InputRouter &inputRouter;
    Render::RenderFrontend &renderFrontend;
    IEditorGuiRenderer &guiRenderer;
    IEditorViewportRenderer &viewportRenderer;
    Render::RenderTargetHandle viewportTarget;
};

class EditorRuntimeParticipant final : public Runtime::RuntimeLifecycleParticipant
{
  public:
    EditorRuntimeParticipant(RunEditorMainLoopParams &params, GuiScreenHost &screenHost,
                             EditorViewportSceneState &viewportSceneState, EditorSettingsSnapshot &settingsSnapshot,
                             std::optional<EditorRendererRestartRequest> &rendererRestart) noexcept
        : p_(&params), screenHost_(&screenHost), viewportSceneState_(&viewportSceneState),
          settingsSnapshot_(&settingsSnapshot), rendererRestart_(&rendererRestart)
    {
    }

    void BindHost(Runtime::RuntimeHost &host) noexcept
    {
        host_ = &host;
    }

    Result<void> Startup(const CancellationToken &) override
    {
        return Result<void>::Success();
    }

    Result<void> OnPhase(const Runtime::RuntimePhase phase, const Runtime::FrameContext &context) override
    {
        switch (phase)
        {
        case Runtime::RuntimePhase::BeginFrame:
            p_->inputBackend.BeginFrame(inputFrameNumber_++);
            return Result<void>::Success();
        case Runtime::RuntimePhase::PollPlatformEvents:
            return PollPlatformEvents();
        case Runtime::RuntimePhase::BuildInputSnapshot:
            p_->inputRouter.BeginFrame(p_->inputBackend.Commit());
            return Result<void>::Success();
        case Runtime::RuntimePhase::ApplyQueuedOwnerThreadCommands:
            return Result<void>::Success();
        case Runtime::RuntimePhase::VariableUpdate:
            return UpdatePresentation(context);
        case Runtime::RuntimePhase::RenderExtraction:
            return ExtractFrame();
        case Runtime::RuntimePhase::RenderExecution:
            return ExecuteFrame(context);
        case Runtime::RuntimePhase::RenderGui:
            if (!frame_.has_value())
                return Result<void>::Success();
            return p_->guiRenderer.RenderDrawData();
        case Runtime::RuntimePhase::Presentation:
            return PresentFrame();
        case Runtime::RuntimePhase::CommitDeferredLifecycleChanges:
            return Result<void>::Success();
        case Runtime::RuntimePhase::EndFrame:
            if (p_->exitAfterFirstFrame || (p_->exitAfterFrames > 0 && context.frameNumber >= p_->exitAfterFrames))
            {
                static_cast<void>(screenHost_->RequestCloseApplication());
            }
            return Result<void>::Success();
        case Runtime::RuntimePhase::FixedUpdate:
            break;
        }
        return Result<void>::Success();
    }

    Result<void> OnFixedUpdate(const Runtime::FixedStepContext &) override
    {
        return Result<void>::Success();
    }

    void Shutdown() noexcept override
    {
        frame_.reset();
        *rendererRestart_ = screenHost_->RendererRestartRequest();
        p_->inputRouter.CancelCapture(Input::CaptureCancellationReason::OwnerDestroyed);
        p_->modalHost.ForceDetachAllForShutdown();
        screenHost_->Shutdown();
    }

  private:
    Result<void> PollPlatformEvents()
    {
        while (const std::optional<EditorMenuInvocation> invocation = PollNativeEditorMenuAction())
        {
            screenHost_->DispatchMenuInvocation(*invocation);
        }
        p_->projectCreationService.PumpMainThread();
        p_->engineEvents.DispatchQueued();
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            p_->inputBackend.ProcessEvent(event);
            if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                                                 event.window.windowID == SDL_GetWindowID(p_->window)))
            {
                static_cast<void>(screenHost_->RequestCloseApplication());
                host_->RequestShutdown();
            }
        }

        if (!nativeMenuInstalled_)
        {
            InstallNativeEditorMenuBar(GetEditorMenuModel(), p_->localization);
            nativeMenuInstalled_ = true;
        }
        return Result<void>::Success();
    }

    Result<void> UpdatePresentation(const Runtime::FrameContext &context)
    {
        auto renderBegun = BeginRenderFrame(context);
        if (renderBegun.HasError())
            return Result<void>::Failure(renderBegun.ErrorValue());

        focusedWidgetInputContext_.Reset();
        if (renderBegun.Value())
        {
            const Result<void> guiBegun = p_->guiRenderer.BeginFrame();
            if (guiBegun.HasError())
            {
                frame_.reset();
                return guiBegun;
            }
            ImGui::NewFrame();

            if (p_->io.WantTextInput)
            {
                focusedWidgetInputContext_ = p_->inputRouter.PushContext(Input::InputContextId{"editor.focused_widget"},
                                                                         Input::InputContextKind::FocusedGuiWidget);
            }
        }

        *settingsSnapshot_ = p_->settings.Snapshot();
        const float deltaSeconds = static_cast<float>(context.variableDelta.ToNanoseconds()) / 1'000'000'000.0F;
        p_->modalHost.OnUpdate(deltaSeconds);
        screenHost_->OnUpdate(deltaSeconds);
        return Result<void>::Success();
    }

    Result<void> ExtractFrame()
    {
        if (!frame_.has_value())
            return Result<void>::Success();

        screenHost_->Draw();
        p_->modalHost.Draw();
        ImGui::Render();

        passCount_ = 0;
        const EditorViewportSceneView viewportScene = viewportSceneState_->View();
        const EditorViewportExtent viewportExtent = p_->viewportRenderer.RequestedExtent();
        if (viewportExtent.IsValid())
        {
            const Result<void> resized = p_->renderFrontend.ResizeOffscreenTarget(
                p_->viewportTarget, {viewportExtent.width, viewportExtent.height});
            if (resized.HasError())
                return resized;
            passes_[passCount_++] = Render::RenderPassDescriptor{
                .id = Render::RenderPassId{1},
                .kind = Render::RenderPassKind::Graphics,
                .staticMesh = Render::StaticMeshPassDescriptor{
                    .target = p_->viewportTarget,
                    .extent = {viewportExtent.width, viewportExtent.height},
                    .scene = Render::RenderSceneView{.camera = ToRenderCamera(viewportScene.camera),
                                                     .meshResources = viewportScene.meshResources,
                                                     .instances = viewportScene.instances}}};
        }
        passes_[passCount_++] = Render::RenderPassDescriptor{
            .id = Render::RenderPassId{2},
            .kind = Render::RenderPassKind::Graphics,
            .primaryOutput = Render::PrimaryOutputAttachment{
                .loadOperation = Render::AttachmentLoadOperation::Clear,
                .storeOperation = Render::AttachmentStoreOperation::Store,
                .clearColor = Render::ClearColor{Theme::Bg0().x, Theme::Bg0().y, Theme::Bg0().z, 1.0F}}};
        return Result<void>::Success();
    }

    Result<bool> BeginRenderFrame(const Runtime::FrameContext &context)
    {
        int drawableWidth = 0;
        int drawableHeight = 0;
        SDL_GetWindowSizeInPixels(p_->window, &drawableWidth, &drawableHeight);
        if (drawableWidth <= 0 || drawableHeight <= 0)
            return Result<bool>::Success(false);

        const Render::FramebufferExtent outputExtent{static_cast<std::uint32_t>(drawableWidth),
                                                     static_cast<std::uint32_t>(drawableHeight)};
        if (outputExtent.width != committedOutputExtent_.width || outputExtent.height != committedOutputExtent_.height)
        {
            const Result<void> resized = p_->renderFrontend.Resize(outputExtent);
            if (resized.HasError())
                return Result<bool>::Failure(resized.ErrorValue());
            committedOutputExtent_ = outputExtent;
        }

        auto begun = p_->renderFrontend.BeginFrame(
            Render::FrameDescriptor{.frameNumber = context.frameNumber, .outputExtent = outputExtent});
        if (begun.HasError())
            return Result<bool>::Failure(begun.ErrorValue());
        frame_.emplace(std::move(begun).Value());
        return Result<bool>::Success(true);
    }

    Result<void> ExecuteFrame(const Runtime::FrameContext &)
    {
        if (!frame_.has_value())
            return Result<void>::Success();
        return frame_->Execute(std::span<const Render::RenderPassDescriptor>{passes_.data(), passCount_});
    }

    Result<void> PresentFrame()
    {
        if (!frame_.has_value())
            return Result<void>::Success();
        Result<void> result = frame_->Present();
        frame_.reset();
        return result;
    }

    RunEditorMainLoopParams *p_{};
    GuiScreenHost *screenHost_{};
    EditorViewportSceneState *viewportSceneState_{};
    EditorSettingsSnapshot *settingsSnapshot_{};
    std::optional<EditorRendererRestartRequest> *rendererRestart_{};
    Runtime::RuntimeHost *host_{};
    Input::FrameNumber inputFrameNumber_{1};
    Input::InputContextToken focusedWidgetInputContext_;
    Render::FramebufferExtent committedOutputExtent_{};
    std::array<Render::RenderPassDescriptor, 2> passes_{};
    std::size_t passCount_{};
    std::optional<Render::RenderFrameScope> frame_;
    bool nativeMenuInstalled_{false};
};

std::optional<EditorRendererRestartRequest> RunEditorMainLoop(RunEditorMainLoopParams &p)
{
    ThemeContext themeContext{p.fonts};
    EditorSettingsSnapshot settingsSnapshot = p.settings.Snapshot();
    EditorGuiContext guiContext{p.engineEvents, p.editorEvents, p.localization, themeContext, settingsSnapshot};

    // Borrowed screen services must outlive the host that invokes screen OnLeave().
    EditorViewportSceneState viewportSceneState;
    NativeDurableFileSystem durableFiles;
    SystemWallClock wallClock;
    ProjectMutationCoordinator mutationCoordinator{durableFiles};
    ProjectMigrationTransactionService migrationTransactions{durableFiles, wallClock, mutationCoordinator, p.jobSystem};
    ProjectOpenPreflightService projectOpenPreflight{migrationTransactions};
    RecentProjectInspectionService recentProjectInspection{p.jobSystem, projectOpenPreflight};
    Assets::AssetRegistry assetRegistry;
    AssetRegistryProjectOpenContributor assetRegistryContributor{assetRegistry};
    std::array<IProjectOpenDerivedStateContributor *, 1> projectOpenContributors{&assetRegistryContributor};
    ProjectOpenService projectOpenService{
        p.jobSystem,           durableFiles,           projectOpenPreflight,   mutationCoordinator,
        migrationTransactions, p.rendererAvailability, projectOpenContributors};
    auto runtimeScene = std::make_unique<Runtime::RuntimeSceneService>();
    Runtime::RuntimeSceneService *runtimeSceneService = runtimeScene.get();
    ScreenRegistry screenRegistry;
    RegisterWelcomeScreen(screenRegistry);
    RegisterProjectCreationScreen(screenRegistry);
    RegisterProjectLoadingScreen(screenRegistry);
    RegisterEditorWorkspaceScreen(screenRegistry);
    WorkspacePanelRegistry workspacePanelRegistry;
    RegisterDefaultWorkspacePanels(workspacePanelRegistry);
    GuiScreenHost screenHost{guiContext,
                             p.modalHost,
                             p.settings,
                             p.localization,
                             p.engineEvents,
                             p.projectCreationService,
                             p.inputRouter,
                             p.rendererAvailability,
                             std::move(screenRegistry),
                             std::move(workspacePanelRegistry),
                             (std::uintptr_t)(void *)(intptr_t)p.textures.logo};
    screenHost.Services().Register<IEditorViewportRenderer>(p.viewportRenderer);
    screenHost.Services().Register<EditorViewportSceneState>(viewportSceneState);
    screenHost.Services().Register<Runtime::RuntimeSceneService>(*runtimeSceneService);
    screenHost.Services().Register<ProjectOpenService>(projectOpenService);
    screenHost.Services().Register<RecentProjectInspectionService>(recentProjectInspection);
    if (const Result<void> started = screenHost.Start(std::move(p.initialRoute)); started.HasError())
    {
        LOG_ERROR("editor.screens", "Initial screen startup failed: %s", started.ErrorValue().message.c_str());
        screenHost.RequestFatalShutdown();
        screenHost.Shutdown();
        return std::nullopt;
    }
    std::optional<EditorRendererRestartRequest> rendererRestart;
    SteadyClock clock;
    auto createdRuntime = Runtime::RuntimeHost::Create(clock);
    if (createdRuntime.HasError())
    {
        LOG_ERROR("editor.runtime", "Runtime host creation failed: %s", createdRuntime.ErrorValue().message.c_str());
        screenHost.RequestFatalShutdown();
        screenHost.Shutdown();
        return std::nullopt;
    }
    std::unique_ptr<Runtime::RuntimeHost> runtime = std::move(createdRuntime).Value();
    if (const Result<void> addedScene = runtime->AddParticipant(std::move(runtimeScene)); addedScene.HasError())
    {
        LOG_ERROR("editor.runtime", "Runtime scene service registration failed: %s",
                  addedScene.ErrorValue().message.c_str());
        screenHost.RequestFatalShutdown();
        screenHost.Shutdown();
        return std::nullopt;
    }
    auto participant = std::make_unique<EditorRuntimeParticipant>(p, screenHost, viewportSceneState, settingsSnapshot,
                                                                  rendererRestart);
    participant->BindHost(*runtime);
    if (const Result<void> added = runtime->AddParticipant(std::move(participant));
        added.HasError() || runtime->Startup().HasError())
    {
        LOG_ERROR("editor.runtime", "Runtime host startup failed.");
        screenHost.RequestFatalShutdown();
        runtime->Shutdown();
        return std::nullopt;
    }

    while (!screenHost.IsApplicationCloseRequested() && (runtime->State() == Runtime::RuntimeLifecycleState::Running ||
                                                         runtime->State() == Runtime::RuntimeLifecycleState::Suspended))
    {
        const Result<void> frame = runtime->RunFrame();
        if (frame.HasError())
        {
            if (frame.ErrorValue().code.Value() != "runtime.host.cancelled")
            {
                LOG_ERROR("editor.runtime", "Runtime frame failed: %s", frame.ErrorValue().message.c_str());
                screenHost.RequestFatalShutdown();
            }
            break;
        }
    }
    runtime->Shutdown();
    return rendererRestart;
}
} // namespace

// ── public entry ─────────────────────────────────────────────────────────

/** @copydoc RunEditorGuiApp */
int RunEditorGuiApp(const int argc, char **argv)
{
    // ── Bootstrap logging before any subsystem ───────────────────────
    Log::Logger::Init("~/.horo/logs", "horo-editor");

    // Setup base MDC for the whole application run
    Log::LogContext appCtx("app", "horo-editor", "run_id", "1");

    Log::Logger::DumpStartupInfo();

    auto opts = ParseOptions(std::span{argv, static_cast<std::size_t>(argc)});
    std::vector<RecentProjectEntry> recentProjects = LoadRecentProjectsFromDisk();
    WelcomeScreenController ctrl{recentProjects};
    auto vm = ctrl.BuildViewModel();

    if (opts.textPreview)
    {
        std::fputs(RenderWelcomeScreenText(vm).c_str(), stdout);
        return 0;
    }

    std::string startupProjectName;
    if (!opts.projectRoot.empty())
    {
        const Application::ProjectCompatibilitySnapshot compatibility =
            Application::InspectProjectCompatibility(opts.projectRoot);
        const bool mayOpen = compatibility.status == Application::ProjectCompatibilityStatus::Current ||
                             compatibility.status == Application::ProjectCompatibilityStatus::CompatibleReleaseLine;
        if (!mayOpen || !compatibility.metadata.has_value())
        {
            LOG_CRITICAL("editor.project", "Project startup preflight failed for '%s': %s", opts.projectRoot.c_str(),
                         compatibility.diagnostic.has_value() ? compatibility.diagnostic->message.c_str()
                                                              : "Project version is not compatible with this editor.");
            Log::Logger::Shutdown();
            return 1;
        }
        opts.rendererBackend = compatibility.metadata->renderBackend;
        startupProjectName = compatibility.metadata->name;
    }

    const Render::RenderBackendModuleInfo *moduleInfo = ResolveCompiledBackendModuleInfo(opts.rendererBackend);
    if (moduleInfo == nullptr || !moduleInfo->supportsInteractivePresentation)
    {
        LOG_CRITICAL("editor.renderer", "Requested renderer component '%s' is unavailable.",
                     opts.rendererBackend.c_str());
        Log::Logger::Shutdown();
        return 1;
    }
    opts.rendererBackend = moduleInfo->id.Value();
    const RendererAvailabilitySnapshot rendererAvailability = BuildRendererAvailabilitySnapshot(opts.rendererBackend);

    SDL_Window *w = nullptr;
    if (!InitializeSdlAndCreateWindow(w, moduleInfo->windowRequirements))
        return 1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    auto fonts = LoadEditorFonts(io, QueryRasterizerDensity(w));
    Theme::Apply(ImGui::GetStyle());
    ApplySavedThemePreference();

    auto compositionResult = CreateEditorRenderComposition(*w, opts);
    if (compositionResult.HasError())
    {
        LOG_CRITICAL("editor.renderer", "Renderer startup failed for '%s': %s", opts.rendererBackend.c_str(),
                     compositionResult.ErrorValue().message.c_str());
        ImGui::DestroyContext();
        SDL_DestroyWindow(w);
        SDL_Quit();
        Log::Logger::Shutdown();
        return 1;
    }
    EditorRenderComposition composition = std::move(compositionResult).Value();
    EditorTextures textures = LoadEditorTextures(*composition.guiRenderer);

    LOG_INFO("editor.startup", "Editor initialised with renderer '%s' — entering main loop",
             opts.rendererBackend.c_str());

    EngineDataBus engineEvents;
    JobSystem jobSystem{JobSystemConfig{.workerCount = 2, .maxQueuedJobs = 256}};
    ProjectCreationService projectCreationService{jobSystem, engineEvents};
    EditorDataBus editorEvents;
    const EditorSettings initialSettings = LoadEditorSettingsDocument().settings;
    LOG_INFO("editor.startup", "Loaded language tag from disk: '%s'", initialSettings.languageTag.c_str());
    LocalizationService localization{LocaleTag{"en-US"}};
    const bool loadedCatalogs = LoadEditorCatalogResources(localization);
    LOG_INFO("editor.startup", "Catalog resources loaded: %s", loadedCatalogs ? "true" : "false");
    ActivateInitialLocale(initialSettings, localization);

    ConfigurationService configuration = CreateEditorConfigurationService(initialSettings, &engineEvents);
    EditorSettingsService settings{initialSettings, configuration, editorEvents, localization};

    const Subscription localizationSub = editorEvents.Subscribe<EditorSettingsChangedEvent>(
        [&settings, &localization](const EditorSettingsChangedEvent &event) {
            if (event.phase == SettingsChangePhase::Committed)
            {
                if (const auto loc = LocaleTag::Parse(settings.Snapshot().settings.languageTag);
                    loc.has_value() && localization.ActiveLocale() != *loc && localization.Prepare(*loc))
                {
                    (void)localization.ActivatePrepared();
                    InstallNativeEditorMenuBar(GetEditorMenuModel(), localization);
                }
            }
        });

    Input::SdlInputBackend inputBackend;
    Input::InputRouter inputRouter;
    const Result<void> installedInputActions = inputRouter.SetActionMap(BuildEditorInputActions());
    if (installedInputActions.HasError())
        LOG_CRITICAL("editor.input", "Built-in input action map is invalid: %s",
                     installedInputActions.ErrorValue().message.c_str());
    const std::filesystem::path editorInputProfile =
        ResolveEditorSettingsHomeDirectory() / ".horo" / "input" / "editor.json";
    std::error_code inputProfileError;
    if (std::filesystem::exists(editorInputProfile, inputProfileError) && !inputProfileError)
    {
        const Result<Input::InputBindingProfile> loaded = Input::LoadBindingProfile(editorInputProfile);
        if (loaded.HasError())
            LOG_ERROR("editor.input", "Keeping default input bindings; unable to load '%s': %s",
                      editorInputProfile.string().c_str(), loaded.ErrorValue().message.c_str());
        else if (const Result<void> applied = inputRouter.SetProfile(loaded.Value()); applied.HasError())
            LOG_ERROR("editor.input", "Keeping last valid input bindings; profile '%s' is invalid: %s",
                      editorInputProfile.string().c_str(), applied.ErrorValue().message.c_str());
    }
    EditorModalHost modalHost{editorEvents, inputRouter};
    GuiRoute initialRoute = opts.projectRoot.empty()
                                ? GuiRoute{GuiRouteKind::Welcome, WelcomeRouteParameters{}}
                                : GuiRoute{GuiRouteKind::ProjectLoading,
                                           ProjectLoadingRouteParameters{opts.projectRoot, startupProjectName}};
    RunEditorMainLoopParams loopParams{opts.exitAfterFirstFrame,
                                       opts.exitAfterFrames,
                                       w,
                                       io,
                                       fonts,
                                       textures,
                                       projectCreationService,
                                       jobSystem,
                                       settings,
                                       engineEvents,
                                       editorEvents,
                                       localization,
                                       rendererAvailability,
                                       std::move(initialRoute),
                                       modalHost,
                                       inputBackend,
                                       inputRouter,
                                       *composition.frontend,
                                       *composition.guiRenderer,
                                       *composition.viewportRenderer,
                                       composition.viewportTarget};
    const std::optional<EditorRendererRestartRequest> rendererRestart = RunEditorMainLoop(loopParams);

    DestroyEditorTextures(textures, *composition.guiRenderer);
    composition.frontend->DetachStaticMeshPassExecutor(*composition.viewportRenderer);
    (void)composition.frontend->ReleaseOffscreenTarget(composition.viewportTarget);
    composition.viewportRenderer.reset();
    composition.guiRenderer.reset();
    ImGui::DestroyContext();
    composition.frontend.reset();
#if defined(HORO_HAS_RENDER_OPENGL)
    composition.openGlPresentationPort.reset();
#endif
#if defined(HORO_HAS_RENDER_METAL)
    composition.metalPresentationPort.reset();
#endif
    SDL_DestroyWindow(w);
    SDL_Quit();

    if (rendererRestart.has_value())
    {
        LOG_INFO("editor.renderer", "Restarting editor with project renderer '%s' for '%s'.",
                 rendererRestart->backendId.c_str(), rendererRestart->projectRoot.c_str());
        Log::Logger::Shutdown();
        return RelaunchEditorForProject(argv[0], *rendererRestart);
    }

    Log::Logger::Shutdown();
    return 0;
}
} // namespace Horo::Editor
