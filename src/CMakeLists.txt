add_library(HoroFoundation STATIC
        foundation/FoundationErrors.cpp
        foundation/modules/ModuleActivationContext.cpp
        foundation/modules/ModuleDescriptor.cpp
        foundation/modules/ModuleHost.cpp
        foundation/modules/ModuleHostLifecycle.cpp
        foundation/modules/ModuleHostRegistration.cpp
        foundation/error/ErrorCode.cpp
        foundation/diagnostics/DiagnosticBundle.cpp
        foundation/hashing/Sha256.cpp
        foundation/math/SceneMath.cpp
        foundation/text/String.cpp
        foundation/logging/Logger.cpp
        foundation/logging/LogContext.cpp
        foundation/logging/StructuredLogStore.cpp
        foundation/telemetry/Telemetry.cpp
        foundation/telemetry/Operation.cpp
        foundation/diagnostics/BuildOutputStore.cpp
        foundation/diagnostics/OperationStore.cpp
        foundation/diagnostics/OperationHistory.cpp
        foundation/data_bus/DataBus.cpp
        foundation/jobs/JobSystem.cpp
        foundation/config/Configuration.cpp
        foundation/diagnostics/DiagnosticsEngine.cpp
        foundation/paths/PathUtils.cpp
)

add_library(HoroEngine::Foundation ALIAS HoroFoundation)

target_compile_features(HoroFoundation PUBLIC cxx_std_20)
target_compile_definitions(HoroFoundation PUBLIC HORO_ENABLE_TELEMETRY=$<BOOL:${HORO_ENABLE_TELEMETRY}>)
target_include_directories(HoroFoundation
        PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        PRIVATE
        $<TARGET_PROPERTY:nlohmann_json::nlohmann_json,INTERFACE_INCLUDE_DIRECTORIES>
)

if (HORO_ENABLE_OPENTELEMETRY)
    add_library(HoroOpenTelemetry STATIC
            foundation/telemetry/OpenTelemetrySink.cpp
    )
    add_library(HoroEngine::OpenTelemetry ALIAS HoroOpenTelemetry)
    target_compile_features(HoroOpenTelemetry PUBLIC cxx_std_20)
    target_include_directories(HoroOpenTelemetry
            PUBLIC
            ${PROJECT_SOURCE_DIR}/include
    )
    target_link_libraries(HoroOpenTelemetry
            PUBLIC
            HoroEngine::Foundation
            PRIVATE
            nlohmann_json::nlohmann_json
            CURL::libcurl
    )
endif ()

add_library(HoroPlatform STATIC
        platform/Platform.cpp
        platform/PlatformErrors.cpp
)

if (WIN32)
    target_sources(HoroPlatform PRIVATE
            platform/windows/DynamicLibrary_Windows.cpp
            platform/windows/ExternalProcess_Windows.cpp)
else ()
    target_sources(HoroPlatform PRIVATE
            platform/posix/DynamicLibrary_Posix.cpp
            platform/posix/ExternalProcess_Posix.cpp)
    target_link_libraries(HoroPlatform PRIVATE dl)
endif ()

add_library(HoroEngine::Platform ALIAS HoroPlatform)

target_compile_features(HoroPlatform PUBLIC cxx_std_20)
target_include_directories(HoroPlatform
        PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(HoroPlatform PUBLIC HoroEngine::Foundation)

add_library(HoroApplication STATIC
        application/observability/HostObservability.cpp
        application/project/ProjectErrors.cpp
        application/project/ProjectVersion.cpp
        application/project/ProjectCompatibility.cpp
        application/project_migration/ProjectMigrationRegistry.cpp
        application/project_migration/ProjectMigrationExecution.cpp
)
add_library(HoroEngine::Application ALIAS HoroApplication)
target_compile_features(HoroApplication PUBLIC cxx_std_20)
target_include_directories(HoroApplication
        PUBLIC ${PROJECT_SOURCE_DIR}/include
        PRIVATE ${HORO_GENERATED_DIR}
)
target_link_libraries(HoroApplication
        PUBLIC HoroEngine::Foundation
        PRIVATE nlohmann_json::nlohmann_json
)
add_dependencies(HoroApplication HoroProjectCompatibilityCatalog)

add_library(HoroProjectMigrations STATIC
        ${HORO_PROJECT_MIGRATION_CATALOG_CPP}
        ${HORO_PROJECT_MIGRATION_SOURCES}
)
add_library(HoroEngine::ProjectMigrations ALIAS HoroProjectMigrations)
target_compile_features(HoroProjectMigrations PUBLIC cxx_std_20)
target_include_directories(HoroProjectMigrations
        PUBLIC ${PROJECT_SOURCE_DIR}/include
        PRIVATE ${PROJECT_SOURCE_DIR} ${HORO_GENERATED_DIR}
)
target_link_libraries(HoroProjectMigrations PUBLIC HoroEngine::Application PRIVATE nlohmann_json::nlohmann_json)
add_dependencies(HoroProjectMigrations HoroProjectMigrationCatalog)

add_library(HoroRuntime STATIC
        runtime/frame/FrameScheduler.cpp
        runtime/lifecycle/RuntimeLifecycle.cpp
        runtime/lifecycle/RuntimeHost.cpp
        runtime/lifecycle/RuntimeErrors.cpp
)
add_library(HoroEngine::Runtime ALIAS HoroRuntime)
target_compile_features(HoroRuntime PUBLIC cxx_std_20)
target_include_directories(HoroRuntime PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(HoroRuntime PUBLIC HoroEngine::Foundation)

add_library(HoroGameplayApi STATIC
        gameplay/api/BehaviorTypes.cpp
        gameplay/api/GameModule.cpp
        gameplay/api/GameplayErrors.cpp
)
add_library(HoroEngine::GameplayApi ALIAS HoroGameplayApi)
target_compile_features(HoroGameplayApi PUBLIC cxx_std_20)
target_include_directories(HoroGameplayApi PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(HoroGameplayApi PUBLIC HoroEngine::Foundation)
target_compile_definitions(HoroGameplayApi PRIVATE
        HORO_GAMEPLAY_SDK_FINGERPRINT="${HORO_GAMEPLAY_SDK_FINGERPRINT}"
)

add_library(HoroRuntimeScene STATIC
        runtime/scene/RuntimeSceneDefinition.cpp
        runtime/scene/RuntimeScene.cpp
        runtime/scene/RuntimeSceneErrors.cpp
)
add_library(HoroEngine::RuntimeScene ALIAS HoroRuntimeScene)
target_compile_features(HoroRuntimeScene PUBLIC cxx_std_20)
target_include_directories(HoroRuntimeScene PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(HoroRuntimeScene PUBLIC HoroEngine::Foundation HoroEngine::Runtime HoroEngine::GameplayApi)

add_library(HoroGameplayRuntime STATIC
        gameplay/runtime/BehaviorRegistry.cpp
        gameplay/runtime/BehaviorRuntime.cpp
)
add_library(HoroEngine::GameplayRuntime ALIAS HoroGameplayRuntime)
target_compile_features(HoroGameplayRuntime PUBLIC cxx_std_20)
target_include_directories(HoroGameplayRuntime PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(HoroGameplayRuntime PUBLIC HoroEngine::GameplayApi HoroEngine::RuntimeScene)

add_library(HoroGameplayModuleHost STATIC
        gameplay/module/GameModuleHost.cpp
)
add_library(HoroEngine::GameplayModuleHost ALIAS HoroGameplayModuleHost)
target_compile_features(HoroGameplayModuleHost PUBLIC cxx_std_20)
target_include_directories(HoroGameplayModuleHost PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(HoroGameplayModuleHost PUBLIC HoroEngine::GameplayRuntime PRIVATE HoroEngine::Platform)

add_library(HoroGameplayLua STATIC
        gameplay/lua/LuaBehavior.cpp
)

add_library(HoroGameplayBuild STATIC
        application/gameplay/GameplayBuildService.cpp
)
add_library(HoroEngine::GameplayBuild ALIAS HoroGameplayBuild)
target_compile_features(HoroGameplayBuild PUBLIC cxx_std_20)
target_include_directories(HoroGameplayBuild PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(HoroGameplayBuild
        PUBLIC HoroEngine::Foundation
        PRIVATE HoroEngine::Platform HoroEngine::GameplayModuleHost nlohmann_json::nlohmann_json)
add_library(HoroEngine::GameplayLua ALIAS HoroGameplayLua)
target_compile_features(HoroGameplayLua PUBLIC cxx_std_20)
target_include_directories(HoroGameplayLua PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(HoroGameplayLua
        PUBLIC HoroEngine::GameplayRuntime
        PRIVATE HoroThirdParty::Lua nlohmann_json::nlohmann_json
)

add_library(HoroAssets STATIC
        runtime/assets/AssetErrors.cpp
        runtime/assets/registry/AssetId.cpp
        runtime/assets/registry/AssetRegistry.cpp
        runtime/assets/runtime_provider/AssetProviderRead.h
        runtime/assets/runtime_provider/AssetProvider.cpp
        runtime/assets/cooker/AssetCookTypes.cpp
        runtime/assets/cooker/CookCatalog.cpp
        runtime/assets/cooker/builtin/HeadlessMeshCooker.cpp
        runtime/assets/cache/AssetCookCache.cpp
        runtime/assets/cooker/AssetCookOutput.cpp
        runtime/assets/cooker/AssetCookService.cpp
        runtime/assets/importer/AssetImporterCatalog.cpp
        runtime/assets/importer/AssetImportMetadata.cpp
        runtime/assets/importer/AssetImportOperation.cpp
        runtime/assets/importer/AssetReimport.cpp
        runtime/assets/importer/ProjectAssetImportCommitter.cpp
        runtime/assets/importer/builtin/obj_mesh/ObjMeshImporter.cpp
        runtime/assets/importer/builtin/obj_mesh/ObjMeshPreviewProvider.cpp
        runtime/assets/importer/builtin/fbx_mesh/FbxMeshImporter.cpp
        runtime/assets/importer/builtin/fbx_mesh/FbxMeshParser.cpp
)
add_library(HoroEngine::Assets ALIAS HoroAssets)
target_compile_features(HoroAssets PUBLIC cxx_std_20)
target_include_directories(HoroAssets PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(HoroAssets
        PUBLIC HoroEngine::Foundation
        PRIVATE nlohmann_json::nlohmann_json HoroThirdParty::Ufbx
)
target_link_libraries(HoroRuntimeScene PUBLIC HoroEngine::Assets)

add_library(HoroInput STATIC
        runtime/input/Input.cpp
        runtime/input/InputErrors.cpp
)
add_library(HoroEngine::Input ALIAS HoroInput)
target_compile_features(HoroInput PUBLIC cxx_std_20)
target_include_directories(HoroInput PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(HoroInput PUBLIC HoroEngine::Foundation PRIVATE nlohmann_json::nlohmann_json)

add_library(HoroRenderApi INTERFACE)
add_library(HoroEngine::RenderApi ALIAS HoroRenderApi)

target_compile_features(HoroRenderApi INTERFACE cxx_std_20)
target_include_directories(HoroRenderApi
        INTERFACE
        ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(HoroRenderApi INTERFACE HoroEngine::Foundation)

add_library(HoroRenderBackendRegistry STATIC
        runtime/renderer/backend_registry/RenderBackendRegistry.cpp
        runtime/renderer/backend_registry/RenderBackendRegistryErrors.cpp
)
add_library(HoroEngine::RenderBackendRegistry ALIAS HoroRenderBackendRegistry)

target_compile_features(HoroRenderBackendRegistry PUBLIC cxx_std_20)
target_include_directories(HoroRenderBackendRegistry
        PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(HoroRenderBackendRegistry PUBLIC HoroEngine::RenderApi)

add_library(HoroRenderFrontend STATIC
        runtime/renderer/frontend/RenderFrontend.cpp
        runtime/renderer/frontend/RenderFrontendErrors.cpp
        runtime/renderer/frontend/RenderResourceOperations.cpp
        runtime/renderer/frontend/RenderResourceRegistry.cpp
)
add_library(HoroEngine::RenderFrontend ALIAS HoroRenderFrontend)

target_compile_features(HoroRenderFrontend PUBLIC cxx_std_20)
target_include_directories(HoroRenderFrontend
        PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(HoroRenderFrontend
        PUBLIC
        HoroEngine::RenderApi
        HoroEngine::RenderBackendRegistry
)

add_library(HoroSceneModel STATIC
        runtime/scene/primitive/PrimitiveCatalog.cpp
        runtime/scene/primitive/PrimitiveMesh.cpp
        runtime/scene/primitive/PrimitiveMeshErrors.cpp
)
add_library(HoroEngine::SceneModel ALIAS HoroSceneModel)

target_compile_features(HoroSceneModel PUBLIC cxx_std_20)
target_include_directories(HoroSceneModel
        PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(HoroSceneModel PUBLIC HoroEngine::Foundation HoroEngine::RenderApi)

# RuntimeSceneDefinition exposes PrimitiveMeshDescriptor in its public scene
# contract, so the owning SceneModel dependency is part of the public edge.
target_link_libraries(HoroRuntimeScene PUBLIC HoroEngine::SceneModel)

add_library(HoroRenderNull STATIC
        runtime/renderer/modules/null/NullRenderBackend.cpp
        runtime/renderer/modules/null/NullRenderBackendErrors.cpp
)
add_library(HoroEngine::RenderNull ALIAS HoroRenderNull)

target_compile_features(HoroRenderNull PUBLIC cxx_std_20)
target_include_directories(HoroRenderNull
        PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(HoroRenderNull
        PUBLIC
        HoroEngine::RenderApi
        HoroEngine::RenderBackendRegistry
)

if (HORO_BUILD_RENDER_OPENGL)
    add_library(HoroRenderOpenGL STATIC
            runtime/renderer/modules/opengl/OpenGLRenderBackend.cpp
            runtime/renderer/modules/opengl/OpenGLRenderBackendErrors.cpp
    )
    add_library(HoroEngine::RenderOpenGL ALIAS HoroRenderOpenGL)

    target_compile_features(HoroRenderOpenGL PUBLIC cxx_std_20)
    target_include_directories(HoroRenderOpenGL
            PUBLIC
            ${PROJECT_SOURCE_DIR}/include
    )
    target_compile_definitions(HoroRenderOpenGL PRIVATE GL_SILENCE_DEPRECATION)
    target_link_libraries(HoroRenderOpenGL
            PUBLIC
            HoroEngine::RenderBackendRegistry
            PRIVATE
            OpenGL::GL
    )
endif ()

if (HORO_BUILD_RENDER_METAL)
    add_library(HoroRenderMetal STATIC
            runtime/renderer/modules/metal/MetalRenderBackend.cpp
            runtime/renderer/modules/metal/MetalRenderBackendErrors.cpp
            runtime/renderer/modules/metal/MetalRuntime.mm
    )
    add_library(HoroEngine::RenderMetal ALIAS HoroRenderMetal)

    target_compile_features(HoroRenderMetal PUBLIC cxx_std_20)
    target_compile_options(HoroRenderMetal PRIVATE -fobjc-arc)
    target_include_directories(HoroRenderMetal
            PUBLIC
            ${PROJECT_SOURCE_DIR}/include
    )
    target_link_libraries(HoroRenderMetal
            PUBLIC
            HoroEngine::RenderBackendRegistry
            PRIVATE
            "-framework Foundation"
            "-framework Metal"
            "-framework QuartzCore"
    )
endif ()

add_library(HoroEditorModel STATIC
        editor/data_bus/EditorDataBus.cpp
        editor/document/SceneDocument.cpp
        editor/document/RuntimeSceneConversion.cpp
        editor/project_model/EditorModelErrors.cpp
        editor/project_model/EditorSelectionModel.cpp
        editor/project_model/EditorViewportCamera.cpp
        editor/project_model/EditorViewportModel.cpp
)
add_library(HoroEngine::EditorModel ALIAS HoroEditorModel)

target_compile_features(HoroEditorModel PUBLIC cxx_std_20)
target_include_directories(HoroEditorModel
        PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)
target_link_libraries(HoroEditorModel
        PUBLIC
        HoroEngine::Foundation
        HoroEngine::SceneModel
        HoroEngine::RuntimeScene
)

add_library(HoroEditorViewportScene STATIC
        editor/renderer/EditorViewportScene.cpp
        editor/renderer/grid/EditorViewportGridGeometry.cpp
        editor/screens/workspace/panels/viewport/visualizers/light/LightVisualizerGeometry.cpp
)
add_library(HoroEngine::EditorViewportScene ALIAS HoroEditorViewportScene)

target_compile_features(HoroEditorViewportScene PUBLIC cxx_std_20)
target_include_directories(HoroEditorViewportScene
        PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)
target_link_libraries(HoroEditorViewportScene PUBLIC HoroEngine::EditorModel)

add_library(HoroEditorRenderExtraction STATIC
        editor/document/EditorAssetMeshCache.cpp
        editor/document/EditorRenderExtractionErrors.cpp
        editor/document/EditorViewportSceneExtractor.cpp
        editor/document/EditorViewportPicking.cpp
        editor/screens/workspace/AssetSceneDrop.cpp
)
add_library(HoroEngine::EditorRenderExtraction ALIAS HoroEditorRenderExtraction)

target_compile_features(HoroEditorRenderExtraction PUBLIC cxx_std_20)
target_include_directories(HoroEditorRenderExtraction
        PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)
target_link_libraries(HoroEditorRenderExtraction
        PUBLIC
        HoroEngine::EditorModel
        HoroEngine::EditorViewportScene
)

if (HORO_BUILD_EDITOR_GUI AND HORO_BUILD_RENDER_METAL)
    add_library(HoroEditorViewportMetal STATIC
            editor/renderer/metal/EditorGuiRendererMetal.mm
            editor/renderer/metal/EditorViewportRendererMetal.mm
            editor/renderer/metal/SdlMetalPresentationPort.mm
    )
    add_library(HoroEngine::EditorViewportMetal ALIAS HoroEditorViewportMetal)

    target_compile_features(HoroEditorViewportMetal PUBLIC cxx_std_20)
    target_compile_options(HoroEditorViewportMetal PRIVATE -fobjc-arc)
    target_include_directories(HoroEditorViewportMetal
            PUBLIC
            ${PROJECT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(HoroEditorViewportMetal
            PUBLIC
            HoroEngine::EditorViewportScene
            HoroEngine::RenderMetal
            ${HORO_SDL3_TARGET}
            PRIVATE
            HoroThirdParty::ImGuiMetal
            "-framework Metal"
            "-framework QuartzCore"
    )
endif ()

add_library(HoroEditorServices STATIC
        editor/EditorServiceErrors.cpp
        editor/document/SceneDocumentComparison.cpp
        editor/document/SceneDocumentPersistence.cpp
        editor/document/SceneFileWatchService.cpp
        editor/screens/welcome/WelcomeController.cpp
        editor/screens/project_creation/ProjectCreationController.cpp
        editor/gameplay/ProjectGameplayRegistry.cpp
        editor/gameplay/EditorPlaySessionController.cpp
        editor/screens/workspace/ContentBrowserModel.cpp
        editor/screens/workspace/GameplayBehaviorRequestValidation.cpp
        editor/modals/gameplay_behavior/GameplayBehaviorFilenameModalState.cpp
        editor/project_model/ProjectCreationService.cpp
        editor/project_model/ProjectMutation.cpp
        editor/project_model/ProjectMigrationTransaction.cpp
        editor/project_model/ProjectOpenService.cpp
        editor/project_model/ProjectIntegrityValidatorService.cpp
        editor/project_model/RecentProjectInspectionService.cpp
        editor/project_model/ProjectMetadata.cpp
        editor/project_model/RendererAvailability.cpp
        editor/settings/EditorConfiguration.cpp
        editor/settings/EditorSettingsService.cpp
        editor/settings/EditorSettingsStore.cpp
        editor/localization/LocalizationService.cpp
        editor/menu/EditorMenuModel.cpp
        editor/status_bar/EditorStatusBarModel.cpp
        editor/hierarchy/HierarchyModel.cpp
        editor/NotificationService.cpp
        editor/input/EditorInputActions.cpp
        editor/modals/EditorModalHost.cpp
        editor/screens/workspace/WorkspaceLayout.cpp
        editor/screens/workspace/WorkspacePanelHost.cpp
        editor/screens/workspace/WorkspaceLayoutPersistence.cpp
        editor/screens/workspace/ActivityBarLayout.cpp
        editor/screens/workspace/WorkspaceSplitterInteraction.cpp
)

add_library(HoroEngine::EditorServices ALIAS HoroEditorServices)

target_compile_features(HoroEditorServices PUBLIC cxx_std_20)
target_include_directories(HoroEditorServices
        PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
        PRIVATE
        ${HORO_GENERATED_DIR}
)

target_link_libraries(HoroEditorServices
        PUBLIC
        HoroEngine::Foundation
        HoroEngine::Application
        HoroEngine::Platform
        HoroEngine::EditorModel
        HoroEngine::GameplayLua
        HoroEngine::GameplayModuleHost
        HoroEngine::GameplayBuild
        HoroEngine::Input
        HoroEngine::ProjectMigrations
        PRIVATE
        HoroEngine::Assets
        nlohmann_json::nlohmann_json
)

if (TARGET HoroBuildInfo)
    add_dependencies(HoroEditorServices HoroBuildInfo)
endif ()

if (HORO_BUILD_EDITOR_GUI AND HORO_BUILD_RENDER_OPENGL)
    add_library(HoroEditorViewportOpenGL STATIC
            editor/renderer/opengl/EditorGuiRendererOpenGL.cpp
            editor/renderer/opengl/EditorViewportRendererOpenGL.cpp
            editor/renderer/opengl/SdlOpenGLPresentationPort.cpp
    )
    add_library(HoroEngine::EditorViewportOpenGL ALIAS HoroEditorViewportOpenGL)

    target_compile_features(HoroEditorViewportOpenGL PUBLIC cxx_std_20)
    target_include_directories(HoroEditorViewportOpenGL
            PUBLIC
            ${PROJECT_SOURCE_DIR}/include
            PRIVATE
            ${PROJECT_SOURCE_DIR}/src
    )
    target_compile_definitions(HoroEditorViewportOpenGL PRIVATE GL_SILENCE_DEPRECATION)
    target_link_libraries(HoroEditorViewportOpenGL
            PUBLIC
            HoroEngine::EditorViewportScene
            HoroEngine::RenderOpenGL
            ${HORO_SDL3_TARGET}
            PRIVATE
            HoroThirdParty::Glad
            HoroThirdParty::ImGuiOpenGL
    )

endif ()

if (HORO_BUILD_EDITOR_GUI)
    add_library(HoroInputSdl STATIC
            runtime/input/sdl/SdlInputBackend.cpp
    )
    add_library(HoroEngine::InputSdl ALIAS HoroInputSdl)
    target_compile_features(HoroInputSdl PUBLIC cxx_std_20)
    target_include_directories(HoroInputSdl PUBLIC ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(HoroInputSdl PUBLIC HoroEngine::Input PRIVATE ${HORO_SDL3_TARGET})

    add_library(HoroGui STATIC
            editor/design_system/components/EditorUiComponents.cpp
            editor/design_system/icons/EditorIcons.cpp
            editor/design_system/components/EditorSnackbarHost.cpp
            editor/app/EditorTheme.cpp
            editor/menu/EditorMenuPlatform.cpp
            editor/status_bar/EditorStatusBar.cpp
            editor/modals/settings/SettingsModal.cpp
            editor/modals/settings/SettingsModalLifecycle.cpp
            editor/modals/settings/SettingsModalState.cpp
            editor/modals/asset_import/AssetImportModal.cpp
            editor/modals/asset_import/AssetImportModalLifecycle.cpp
            editor/modals/asset_import/AssetImportModalPresentation.cpp
            editor/modals/scene_compare/SceneConflictCompareModal.cpp
            editor/modals/gameplay_behavior/GameplayBehaviorFilenameModal.cpp
            editor/screens/welcome/WelcomeView.cpp
            editor/screens/welcome/WelcomeScreen.cpp
            editor/screens/project_creation/ProjectCreationView.cpp
            editor/screens/project_creation/ProjectCreationScreen.cpp
            editor/screens/project_loading/ProjectLoadingView.cpp
            editor/screens/project_loading/ProjectLoadingScreen.cpp
            editor/screens/workspace/panels/hierarchy/HierarchyEditSession.cpp
            editor/screens/workspace/panels/hierarchy/HierarchyPanel.cpp
            editor/screens/workspace/panels/inspector/InspectorEditSession.cpp
            editor/screens/workspace/panels/inspector/InspectorPanel.cpp
            editor/screens/workspace/panels/input_mapping/InputMappingPanel.cpp
            editor/screens/workspace/panels/game/GamePanel.cpp
            editor/screens/workspace/panels/viewport/ViewportPanel.cpp
            editor/screens/workspace/panels/viewport/visualizers/light/LightMarkerLayer.cpp
        editor/screens/workspace/panels/viewport/gizmo/TransformGizmoController.cpp
            editor/screens/workspace/panels/viewport/gizmo/TransformGizmoGeometry.cpp
            editor/screens/workspace/panels/viewport/gizmo/TransformGizmoMath.cpp
            editor/screens/workspace/panels/viewport/interaction/ViewportInteractionCapture.cpp
            editor/screens/workspace/panels/viewport/interaction/ViewportInteractionController.cpp
            editor/screens/workspace/panels/viewport/navigation/ViewportNavigationController.cpp
            editor/screens/workspace/WorkspacePanelRegistry.cpp
            editor/screens/workspace/DefaultWorkspacePanels.cpp
            editor/screens/workspace/panels/global_dock/GlobalDockPanel.cpp
            editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserGrid.cpp
            editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserActions.cpp
            editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserCards.cpp
            editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserDialogs.cpp
            editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserInteractionSession.cpp
            editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserPane.cpp
            editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserToolbar.cpp
            editor/screens/workspace/panels/global_dock/panes/audio/GlobalDockAudioPane.cpp
            editor/screens/workspace/panels/global_dock/panes/build_output/GlobalDockBuildOutputPane.cpp
            editor/screens/workspace/panels/global_dock/panes/console/GlobalDockConsolePane.cpp
            editor/screens/workspace/panels/global_dock/panes/localization/GlobalDockLocalizationPane.cpp
            editor/screens/workspace/panels/global_dock/panes/mcp/GlobalDockMcpPane.cpp
            editor/screens/workspace/panels/global_dock/panes/network/GlobalDockNetworkPane.cpp
            editor/screens/workspace/panels/global_dock/panes/operations/GlobalDockOperationsPane.cpp
            editor/screens/workspace/panels/global_dock/panes/performance/GlobalDockPerformancePane.cpp
            editor/screens/workspace/panels/global_dock/panes/physics/GlobalDockPhysicsPane.cpp
            editor/screens/workspace/panels/global_dock/panes/shared/GlobalDockStatusPane.cpp
            editor/screens/workspace/EditorWorkspaceController.cpp
            editor/screens/workspace/EditorWorkspaceControllerCommands.cpp
            editor/screens/workspace/EditorWorkspaceControllerContentBrowser.cpp
            editor/screens/workspace/EditorWorkspaceView.cpp
            editor/screens/workspace/EditorWorkspaceScreen.cpp
            editor/screens/NavigationErrors.cpp
            editor/screens/GuiScreenHost.cpp
    )

    add_library(HoroEngine::Gui ALIAS HoroGui)

    target_compile_features(HoroGui PUBLIC cxx_std_20)
    target_include_directories(HoroGui
            PUBLIC
            ${PROJECT_SOURCE_DIR}/include
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
            PRIVATE
            ${PROJECT_SOURCE_DIR}/vendor/stb
            ${PROJECT_SOURCE_DIR}/vendor/pfd
            ${HORO_GENERATED_DIR}
    )
    target_compile_definitions(HoroGui
            PRIVATE
            GL_SILENCE_DEPRECATION
            HORO_ENGINE_VERSION_STRING="${PROJECT_VERSION}"
    )
    target_link_libraries(HoroGui
            PUBLIC
            HoroEngine::EditorServices
            HoroEngine::Foundation
            HoroThirdParty::ImGui
            PRIVATE
            HoroEngine::EditorRenderExtraction
            nlohmann_json::nlohmann_json
    )

    if (APPLE)
        set(_HORO_EDITOR_MENU_MAC_SOURCE editor/menu/macos/EditorMenuPlatformMac.mm)
        set_source_files_properties(${_HORO_EDITOR_MENU_MAC_SOURCE} PROPERTIES
                LANGUAGE CXX
                COMPILE_OPTIONS "-x;objective-c++"
        )
        target_sources(HoroGui PRIVATE ${_HORO_EDITOR_MENU_MAC_SOURCE})
        target_link_libraries(HoroGui PRIVATE "-framework AppKit")
    endif ()
endif ()

add_library(HoroExtensions STATIC
        extensions/host/ExtensionManager.cpp
        extensions/manifest/ExtensionManifestParser.cpp
        extensions/registry/ExtensionErrors.cpp
        extensions/registry/ExtensionInventory.cpp
        extensions/marketplace/ExtensionMarketplace.cpp
        extensions/capabilities/asset_pipeline_points/ExternalAssetImporter.cpp
)

add_library(HoroEngine::Extensions ALIAS HoroExtensions)

target_compile_features(HoroExtensions PUBLIC cxx_std_20)
target_include_directories(HoroExtensions
        PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(HoroExtensions
        PUBLIC HoroEngine::Foundation HoroEngine::Platform HoroEngine::Assets
        PRIVATE nlohmann_json::nlohmann_json CURL::libcurl miniz
)

if (HORO_BUILD_EDITOR_GUI)
    target_link_libraries(HoroGui PRIVATE HoroEngine::Extensions)
endif ()

# Apply the build-tree public/private include contract only after every optional
# production target has been declared.
include(HoroPublicHeaderOwnership)
