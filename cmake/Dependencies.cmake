include(FetchContent)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "" FORCE)

set(HORO_NLOHMANN_JSON_REVISION "9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03")
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG "${HORO_NLOHMANN_JSON_REVISION}"
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

if(BUILD_TESTING)
    set(HORO_CATCH2_REVISION "6ee0826dcae55ed1e06b2c5701981221e979e1e6")
    set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG "${HORO_CATCH2_REVISION}"
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()

if(HORO_BUILD_RENDER_OPENGL)
    find_package(OpenGL REQUIRED)
endif()

if(HORO_BUILD_EDITOR_GUI)
    set(HORO_SDL3_REVISION
        "f87239e71e42da91ca317a12eefb82cfbf3393eb"
    )
    set(HORO_IMGUI_REVISION
        "993fa347495860ed44b83574254ef2a317d0c14f"
    )


    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG "${HORO_SDL3_REVISION}"
    )
    FetchContent_MakeAvailable(SDL3)

    if(TARGET SDL3::SDL3-static)
        set(HORO_SDL3_TARGET SDL3::SDL3-static)
    elseif(TARGET SDL3::SDL3)
        set(HORO_SDL3_TARGET SDL3::SDL3)
    elseif(TARGET SDL3-static)
        set(HORO_SDL3_TARGET SDL3-static)
    elseif(TARGET SDL3)
        set(HORO_SDL3_TARGET SDL3)
    else()
        message(FATAL_ERROR "SDL3 dependency did not provide a usable CMake target")
    endif()

    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG "${HORO_IMGUI_REVISION}"
    )
    FetchContent_MakeAvailable(imgui)

    if(HORO_ENABLE_IMGUI_UI_TESTS)
        set(HORO_IMGUI_TEST_ENGINE_REVISION
            "4018a79b61da483544ccbfbc2f6e8e85a35c2cbc"
        )
        FetchContent_Declare(
            imgui_test_engine
            GIT_REPOSITORY https://github.com/ocornut/imgui_test_engine.git
            GIT_TAG "${HORO_IMGUI_TEST_ENGINE_REVISION}"
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(imgui_test_engine)
    endif()

    if(HORO_BUILD_RENDER_OPENGL)
        add_library(HoroThirdPartyGlad STATIC
            ${CMAKE_CURRENT_LIST_DIR}/../vendor/glad/src/gl.c
        )
        add_library(HoroThirdParty::Glad ALIAS HoroThirdPartyGlad)
        target_include_directories(HoroThirdPartyGlad
            PUBLIC
                ${CMAKE_CURRENT_LIST_DIR}/../vendor/glad/include
        )
    endif()

    add_library(HoroThirdPartyImGui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    )

    if(HORO_ENABLE_IMGUI_UI_TESTS)
        target_sources(HoroThirdPartyImGui PRIVATE
            ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_capture_tool.cpp
            ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_context.cpp
            ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_coroutine.cpp
            ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_engine.cpp
            ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_exporters.cpp
            ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_perftool.cpp
            ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_ui.cpp
            ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_utils.cpp
        )
        target_include_directories(HoroThirdPartyImGui PRIVATE
            ${imgui_test_engine_SOURCE_DIR}
            ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine
        )
    endif()
    add_library(HoroThirdParty::ImGui ALIAS HoroThirdPartyImGui)

    target_compile_features(HoroThirdPartyImGui PUBLIC cxx_std_20)
    target_include_directories(HoroThirdPartyImGui
        PUBLIC
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/backends
    )
    target_compile_definitions(HoroThirdPartyImGui
        PUBLIC
            GL_SILENCE_DEPRECATION
    )
    if(HORO_ENABLE_IMGUI_UI_TESTS)
        target_compile_definitions(HoroThirdPartyImGui
            PUBLIC
                IMGUI_ENABLE_TEST_ENGINE
                IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1
                IMGUI_TEST_ENGINE_ENABLE_STD_FUNCTION=1
        )
    endif()
    target_link_libraries(HoroThirdPartyImGui
        PUBLIC
            ${HORO_SDL3_TARGET}
    )

    if(HORO_BUILD_RENDER_OPENGL)
        add_library(HoroThirdPartyImGuiOpenGL STATIC
            ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        )
        add_library(HoroThirdParty::ImGuiOpenGL ALIAS HoroThirdPartyImGuiOpenGL)
        target_compile_features(HoroThirdPartyImGuiOpenGL PUBLIC cxx_std_20)
        target_include_directories(HoroThirdPartyImGuiOpenGL PUBLIC
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/backends
        )
        target_compile_definitions(HoroThirdPartyImGuiOpenGL PUBLIC GL_SILENCE_DEPRECATION)
        target_link_libraries(HoroThirdPartyImGuiOpenGL PUBLIC HoroThirdPartyImGui OpenGL::GL)
    endif()

    if(HORO_BUILD_RENDER_METAL)
        add_library(HoroThirdPartyImGuiMetal STATIC
            ${imgui_SOURCE_DIR}/backends/imgui_impl_metal.mm
        )
        add_library(HoroThirdParty::ImGuiMetal ALIAS HoroThirdPartyImGuiMetal)
        target_compile_features(HoroThirdPartyImGuiMetal PUBLIC cxx_std_20)
        target_compile_options(HoroThirdPartyImGuiMetal PRIVATE -fobjc-arc)
        target_include_directories(HoroThirdPartyImGuiMetal PUBLIC
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/backends
        )
        target_link_libraries(HoroThirdPartyImGuiMetal PUBLIC
            HoroThirdPartyImGui
            "-framework Metal"
            "-framework QuartzCore"
        )
    endif()
endif()
