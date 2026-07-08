include(FetchContent)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "" FORCE)

if(HORO_BUILD_EDITOR_GUI)
    set(HORO_SDL2_REVISION
        "9c821dc21ccbd69b2bda421fdb35cb4ae2da8f5e"
    )
    set(HORO_IMGUI_REVISION
        "993fa347495860ed44b83574254ef2a317d0c14f"
    )

    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        SDL2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG "${HORO_SDL2_REVISION}"
    )
    FetchContent_MakeAvailable(SDL2)

    if(TARGET SDL2::SDL2-static)
        set(HORO_SDL2_TARGET SDL2::SDL2-static)
    elseif(TARGET SDL2::SDL2)
        set(HORO_SDL2_TARGET SDL2::SDL2)
    elseif(TARGET SDL2-static)
        set(HORO_SDL2_TARGET SDL2-static)
    elseif(TARGET SDL2)
        set(HORO_SDL2_TARGET SDL2)
    else()
        message(FATAL_ERROR "SDL2 dependency did not provide a usable CMake target")
    endif()

    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG "${HORO_IMGUI_REVISION}"
    )
    FetchContent_MakeAvailable(imgui)

    find_package(OpenGL REQUIRED)

    add_library(HoroThirdPartyImGui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    )
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
    target_link_libraries(HoroThirdPartyImGui
        PUBLIC
            ${HORO_SDL2_TARGET}
            OpenGL::GL
    )
endif()
