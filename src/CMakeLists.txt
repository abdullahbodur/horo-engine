add_library(HoroFoundation STATIC
        foundation/logging/Logger.cpp
        foundation/logging/LogContext.cpp
        foundation/data_bus/DataBus.cpp
        foundation/jobs/JobSystem.cpp
        foundation/config/Configuration.cpp
        foundation/diagnostics/DiagnosticsEngine.cpp
)

add_library(HoroEngine::Foundation ALIAS HoroFoundation)

target_compile_features(HoroFoundation PUBLIC cxx_std_20)
target_include_directories(HoroFoundation
        PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

add_library(HoroPlatform STATIC
        platform/Platform.cpp
)

add_library(HoroEngine::Platform ALIAS HoroPlatform)

target_compile_features(HoroPlatform PUBLIC cxx_std_20)
target_include_directories(HoroPlatform
        PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(HoroPlatform PUBLIC HoroEngine::Foundation)

add_library(HoroEditorScreens STATIC
        editor/screens/welcome/WelcomeController.cpp
        editor/screens/project_creation/ProjectCreationController.cpp
        editor/project_model/ProjectCreationService.cpp
        editor/settings/EditorConfiguration.cpp
        editor/settings/EditorSettingsService.cpp
        editor/settings/EditorSettingsStore.cpp
        editor/localization/LocalizationService.cpp
        editor/menu/EditorMenuModel.cpp
        editor/status_bar/EditorStatusBarModel.cpp
        editor/hierarchy/HierarchyModel.cpp
        editor/data_bus/EditorDataBus.cpp
        editor/modals/EditorModalHost.cpp
        editor/modals/settings/SettingsModalLifecycle.cpp
        editor/modals/settings/SettingsModalState.cpp
)

add_library(HoroEngine::EditorScreens ALIAS HoroEditorScreens)

target_compile_features(HoroEditorScreens PUBLIC cxx_std_20)
target_include_directories(HoroEditorScreens
        PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${HORO_GENERATED_DIR}
)

target_link_libraries(HoroEditorScreens PUBLIC HoroEngine::Foundation)

if (TARGET HoroBuildInfo)
    add_dependencies(HoroEditorScreens HoroBuildInfo)
endif ()

if (HORO_BUILD_EDITOR_GUI)
    add_library(HoroEditorGuiBackend STATIC
            editor/design_system/components/EditorUiComponents.cpp
            editor/app/EditorGuiApp.cpp
            editor/app/EditorTheme.cpp
            editor/menu/EditorMenuPlatform.cpp
            editor/status_bar/EditorStatusBar.cpp
            editor/modals/settings/SettingsModal.cpp
            editor/screens/welcome/WelcomeView.cpp
            editor/screens/welcome/WelcomeScreen.cpp
            editor/screens/project_creation/ProjectCreationView.cpp
            editor/screens/project_creation/ProjectCreationScreen.cpp
            editor/screens/project_loading/ProjectLoadingView.cpp
            editor/screens/project_loading/ProjectLoadingScreen.cpp
            editor/screens/workspace/HierarchyPanel.cpp
            editor/screens/workspace/InspectorPanel.cpp
            editor/screens/workspace/ViewportPanel.cpp
            editor/screens/workspace/WorkspacePanelRegistry.cpp
            editor/screens/workspace/DefaultWorkspacePanels.cpp
            editor/screens/workspace/ContentBrowserPanel.cpp
            editor/screens/workspace/EditorWorkspaceController.cpp
            editor/screens/workspace/WorkspaceLayout.cpp
            editor/screens/workspace/WorkspacePanelHost.cpp
            editor/screens/workspace/WorkspaceLayoutPersistence.cpp
            editor/screens/workspace/ActivityBarLayout.cpp
            editor/screens/workspace/WorkspaceSplitterInteraction.cpp
            editor/screens/workspace/EditorWorkspaceView.cpp
            editor/screens/workspace/EditorWorkspaceScreen.cpp
            editor/screens/GuiScreenHost.cpp
    )

    add_library(HoroEngine::EditorGuiBackend ALIAS HoroEditorGuiBackend)

    target_compile_features(HoroEditorGuiBackend PUBLIC cxx_std_20)
    target_include_directories(HoroEditorGuiBackend
            PUBLIC
            ${PROJECT_SOURCE_DIR}/include
            PRIVATE
            ${PROJECT_SOURCE_DIR}/src
            ${PROJECT_SOURCE_DIR}/vendor/stb
            ${PROJECT_SOURCE_DIR}/vendor/pfd
            ${HORO_GENERATED_DIR}
    )
    target_compile_definitions(HoroEditorGuiBackend
            PRIVATE
            GL_SILENCE_DEPRECATION
            HORO_EDITOR_ASSET_ROOT="${PROJECT_SOURCE_DIR}/assets"
            HORO_ENGINE_VERSION_STRING="${PROJECT_VERSION}"
    )
    target_link_libraries(HoroEditorGuiBackend
            PUBLIC
            HoroEngine::EditorScreens
            HoroEngine::Foundation
            PRIVATE
            HoroThirdParty::ImGui
    )

    if (APPLE)
        set(_HORO_EDITOR_MENU_MAC_SOURCE editor/menu/macos/EditorMenuPlatformMac.mm)
        set_source_files_properties(${_HORO_EDITOR_MENU_MAC_SOURCE} PROPERTIES
                LANGUAGE CXX
                COMPILE_OPTIONS "-x;objective-c++"
        )
        target_sources(HoroEditorGuiBackend PRIVATE ${_HORO_EDITOR_MENU_MAC_SOURCE})
        target_link_libraries(HoroEditorGuiBackend PRIVATE "-framework AppKit")
    endif ()
endif ()

if (HORO_BUILD_EDITOR_GUI)
    target_link_libraries(HoroEditorScreens PRIVATE HoroEngine::EditorGuiBackend)
endif ()
