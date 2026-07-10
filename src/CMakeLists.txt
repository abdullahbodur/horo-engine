add_library(HoroFoundation STATIC
    foundation/logging/Logger.cpp
    foundation/logging/LogContext.cpp
    foundation/data_bus/DataBus.cpp
    foundation/jobs/JobSystem.cpp
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
    editor/screens/WelcomeScreen.cpp
    editor/screens/project_creation/ProjectCreationScreen.cpp
    editor/project_model/ProjectCreationService.cpp
    editor/settings/EditorConfiguration.cpp
    editor/settings/EditorSettingsService.cpp
    editor/settings/EditorSettingsStore.cpp
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

if(TARGET HoroBuildInfo)
    add_dependencies(HoroEditorScreens HoroBuildInfo)
endif()

if(HORO_BUILD_EDITOR_GUI)
    add_library(HoroEditorGuiBackend STATIC
        editor/design_system/components/EditorUiComponents.cpp
        editor/app/EditorGuiApp.cpp
        editor/app/EditorTheme.cpp
        editor/modals/settings/SettingsModal.cpp
        editor/screens/welcome/WelcomeScreenGui.cpp
        editor/screens/project_creation/ProjectCreationScreenGui.cpp
        editor/screens/project_loading/ProjectLoadingScreenGui.cpp
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
    )
    target_link_libraries(HoroEditorGuiBackend
        PUBLIC
            HoroEngine::EditorScreens
            HoroEngine::Foundation
        PRIVATE
            HoroThirdParty::ImGui
    )
endif()
