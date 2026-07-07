add_library(HoroEditorScreens STATIC
    editor/screens/WelcomeScreen.cpp
    editor/settings/EditorSettingsStore.cpp
)

add_library(HoroEngine::EditorScreens ALIAS HoroEditorScreens)

target_compile_features(HoroEditorScreens PUBLIC cxx_std_20)
target_include_directories(HoroEditorScreens
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

if(HORO_BUILD_EDITOR_GUI)
    add_library(HoroEditorGuiBackend STATIC
        editor/design_system/components/EditorUiComponents.cpp
        editor/app/EditorGuiApp.cpp
        editor/app/EditorTheme.cpp
        editor/modals/new_project/NewProjectModal.cpp
        editor/modals/settings/SettingsModal.cpp
        editor/screens/welcome/WelcomeScreenGui.cpp
    )

    add_library(HoroEngine::EditorGuiBackend ALIAS HoroEditorGuiBackend)

    target_compile_features(HoroEditorGuiBackend PUBLIC cxx_std_20)
    target_include_directories(HoroEditorGuiBackend
        PUBLIC
            ${PROJECT_SOURCE_DIR}/include
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src
            ${PROJECT_SOURCE_DIR}/vendor/stb
    )
    target_compile_definitions(HoroEditorGuiBackend
        PRIVATE
            GL_SILENCE_DEPRECATION
            HORO_EDITOR_ASSET_ROOT="${PROJECT_SOURCE_DIR}/assets"
    )
    target_link_libraries(HoroEditorGuiBackend
        PUBLIC
            HoroEngine::EditorScreens
        PRIVATE
            HoroThirdParty::ImGui
    )
endif()
