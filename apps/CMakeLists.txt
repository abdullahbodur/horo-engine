# The graphical executable is an optional composition root. Foundation and
# headless hosts must remain buildable when the GUI adapter is intentionally off.
if(HORO_BUILD_EDITOR_GUI)
    add_executable(HoroEditor
        HoroEditor/main.cpp
        HoroEditor/app/HoroEditorApp.cpp
    )

    if(APPLE)
        set(HORO_EDITOR_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/HoroEditor/resources/HoroEditor.icns")
        set_source_files_properties("${HORO_EDITOR_APP_ICON}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources"
        )
        target_sources(HoroEditor PRIVATE "${HORO_EDITOR_APP_ICON}")
        set_target_properties(HoroEditor PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME "Horo Editor"
            MACOSX_BUNDLE_GUI_IDENTIFIER "dev.horo.editor"
            MACOSX_BUNDLE_ICON_FILE "HoroEditor.icns"
            MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        )
    endif()

    target_compile_features(HoroEditor PRIVATE cxx_std_20)

    target_include_directories(HoroEditor
        PRIVATE
            ${PROJECT_SOURCE_DIR}/apps/HoroEditor/app
            ${PROJECT_SOURCE_DIR}/src
            ${PROJECT_SOURCE_DIR}/vendor/stb
            ${PROJECT_SOURCE_DIR}/vendor/pfd
            ${HORO_GENERATED_DIR}
    )

    target_compile_definitions(HoroEditor
        PRIVATE
            GL_SILENCE_DEPRECATION
            HORO_EDITOR_ASSET_ROOT="${PROJECT_SOURCE_DIR}/assets"
            HORO_ENGINE_VERSION_STRING="${PROJECT_VERSION}"
    )

    target_link_libraries(HoroEditor
        PRIVATE
            HoroEngine::Gui
            HoroEngine::EditorRenderExtraction
            HoroEngine::RenderFrontend
            HoroEngine::Runtime
            HoroEngine::RuntimeScene
            HoroEngine::Platform
            HoroEngine::InputSdl
            HoroThirdParty::ImGui
            ${HORO_SDL3_TARGET}
    )

    # Development composition: concrete backends are linked explicitly at the
    # executable boundary until RenderModuleHost owns installed-product loading.
    if(HORO_BUILD_RENDER_OPENGL)
        target_compile_definitions(HoroEditor PRIVATE HORO_HAS_RENDER_OPENGL=1)
        target_link_libraries(HoroEditor PRIVATE HoroEngine::EditorViewportOpenGL)
    endif()

    if(HORO_BUILD_RENDER_METAL)
        target_compile_definitions(HoroEditor PRIVATE HORO_HAS_RENDER_METAL=1)
        target_link_libraries(HoroEditor PRIVATE HoroEngine::EditorViewportMetal)
    endif()
endif()
