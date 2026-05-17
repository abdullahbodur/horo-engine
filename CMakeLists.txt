cmake_minimum_required(VERSION 3.25)

# x-release-please-start-version
set(HORO_ENGINE_VERSION "0.3.0")
# x-release-please-end

# Detect if this is a launcher build or embedded via add_subdirectory()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Launcher mode: local dev, CI, tests
    project(HoroEngine VERSION ${HORO_ENGINE_VERSION} LANGUAGES CXX C)
    if(APPLE)
        set(CMAKE_ObjCXX_COMPILER ${CMAKE_CXX_COMPILER})
        enable_language(ObjCXX)
    endif()
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

    option(HORO_ENGINE_BUILD_TESTS "Build engine unit tests" ON)
    option(HORO_ENGINE_ENABLE_VULKAN "Fetch Vulkan headers/loader and compile Vulkan backend scaffolding" OFF)
    option(HORO_ENGINE_BUILD_STANDALONE_EDITOR "Build HoroEditor (SDK launcher + editor shell executable)" ON)

    set(HORO_RENDERER "opengl" CACHE STRING "Renderer backend: opengl | vulkan | null")
    set_property(CACHE HORO_RENDERER PROPERTY STRINGS opengl vulkan null)

    if(MSVC)
        add_compile_options(/W4 /WX /Zc:preprocessor)
    else()
        add_compile_options(-Wall -Wextra -Wpedantic)
    endif()
endif()

# Always bring our own vendor deps.
# Guards in vendor/CMakeLists.txt prevent duplicate target errors if a parent
# project already defined any of these targets.
add_subdirectory(vendor)

# ---- OpenGL backend source files (PRIVATE glad isolation) ----
# Only files that directly include <glad/glad.h> belong here.
# renderer/Mesh.cpp, SkinnedMesh.cpp, DebugDraw.cpp, DebugHUD.cpp,
# Shader.cpp and Texture.cpp do NOT include glad directly (they use the
# clean OpenGL* headers) so they remain in HoroEngine to avoid a circular
# dependency (EditorLayer.cpp in HoroEngine calls Mesh).
set(HORO_RENDERER_OPENGL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer/OpenGLRenderBackend.cpp
)

# ---- HoroEngine static library ----
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/math/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/mcp/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/mcp/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/physics/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/renderer/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/scene/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/input/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/editor/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/launcher/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp"
)
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "ui/launcher[/\\\\]HoroEditorMain\\.cpp$")
# Exclude only the file that directly includes <glad/glad.h>; it lives in horo-renderer-opengl.
list(FILTER ENGINE_SOURCES EXCLUDE REGEX
    "[/\\\\]renderer[/\\\\]OpenGLRenderBackend\\.cpp$")
if(APPLE)
    list(APPEND ENGINE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/core/Window_mac.mm")
endif()

add_library(HoroEngine STATIC ${ENGINE_SOURCES})
add_library(HoroEngine::HoroEngine ALIAS HoroEngine)

function(horo_configure_engine_target target_name imgui_target)
    target_include_directories(${target_name}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
            $<INSTALL_INTERFACE:include>
            $<BUILD_INTERFACE:${HORO_VENDOR_NLOHMANN_INCLUDE_DIR}>
            $<INSTALL_INTERFACE:include/vendor/nlohmann>
    )

    target_link_libraries(${target_name}
        PUBLIC
            glfw
            ${imgui_target}
        PRIVATE
            glad
            glm
            stb
            nlohmann_json
            tinygltf
            ufbx
    )

    if(HORO_ENGINE_ENABLE_VULKAN)
        target_link_libraries(${target_name} PUBLIC horo_volk horo_vulkan_headers)
        target_compile_definitions(${target_name} PUBLIC HORO_HAS_VULKAN=1)
    endif()

    if(WIN32)
        # Windows system libs needed for GLFW and general app infrastructure.
        target_link_libraries(${target_name} PUBLIC comdlg32 dwmapi ole32 shell32 ws2_32)
        target_link_libraries(${target_name} PRIVATE opengl32)
    elseif(APPLE)
        target_link_libraries(${target_name} PRIVATE "-framework OpenGL" "-framework AppKit")
    else()
        target_link_libraries(${target_name} PRIVATE GL)
    endif()

    target_compile_features(${target_name} PUBLIC cxx_std_20)
    target_compile_definitions(${target_name} PUBLIC HORO_ENGINE_VERSION="${HORO_ENGINE_VERSION}")

    if(WIN32)
        target_compile_definitions(${target_name} PUBLIC NOMINMAX)
    endif()

    if(HORO_ENGINE_COVERAGE AND NOT MSVC)
        target_compile_options(${target_name} PUBLIC --coverage -O0 -g)
        target_link_options(${target_name} PUBLIC --coverage)
    endif()
endfunction()

# Optional llvm-cov instrumentation (GCC/Clang only).
option(HORO_ENGINE_COVERAGE "Add coverage flags to HoroEngine (for reports after ctest)" OFF)

horo_configure_engine_target(HoroEngine imgui_lib)
# core/Window.cpp and editor files include <glad/glad.h> directly; add glad
# as PRIVATE so the library isn't forced on consumers, but expose the include
# directory so consumers (e.g. starter app) can call gl* functions directly.
target_link_libraries(HoroEngine PRIVATE glad)

# Tell HoroEngine which renderer backend is active so RenderBackendFactory.cpp
# can conditionally compile in only the required backend.
if(NOT HORO_RENDERER STREQUAL "null")
    target_compile_definitions(HoroEngine PRIVATE HORO_RENDERER_OPENGL=1)
endif()
target_include_directories(HoroEngine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/glad/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/vendor/glad>
)

# ---- horo-renderer-opengl: PRIVATE glad + OpenGL ----
# Dedicated backend marker/consumer target.  The sources compiled here
# include <glad/glad.h>, and glad is kept PRIVATE so its include directories
# cannot transitively reach HoroEditor or test translation units.
# HoroEngine also uses glad PRIVATE (Window.cpp, EditorLayer.cpp, etc.) but
# the PUBLIC interface of both targets is glad-free.
add_library(horo-renderer-opengl STATIC ${HORO_RENDERER_OPENGL_SOURCES})
add_library(HoroEngine::RendererOpenGL ALIAS horo-renderer-opengl)

target_include_directories(horo-renderer-opengl PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(horo-renderer-opengl
    PRIVATE HoroEngine
    PRIVATE glad stb
)
if(APPLE)
    target_link_libraries(horo-renderer-opengl PRIVATE "-framework OpenGL")
elseif(WIN32)
    target_link_libraries(horo-renderer-opengl PRIVATE opengl32)
else()
    target_link_libraries(horo-renderer-opengl PRIVATE GL)
endif()
target_compile_definitions(horo-renderer-opengl INTERFACE HORO_RENDERER_OPENGL=1)

if(HORO_ENGINE_COVERAGE AND NOT MSVC)
    target_compile_options(horo-renderer-opengl PUBLIC --coverage -O0 -g)
    target_link_options(horo-renderer-opengl PUBLIC --coverage)
endif()

# ---- horo-renderer-null: no GPU dependency (headless / CI unit tests) ----
add_library(horo-renderer-null INTERFACE)
add_library(HoroEngine::RendererNull ALIAS horo-renderer-null)
target_link_libraries(horo-renderer-null INTERFACE HoroEngine)
target_compile_definitions(horo-renderer-null INTERFACE HORO_RENDERER_NULL=1)

if(HORO_ENGINE_BUILD_TESTS AND TARGET imgui_test_lib)
    set(HORO_UI_TEST_SOURCES
        tests/UiTestRegistry.cpp
        tests/ui_scenarios/LauncherUiScenarios.cpp
        tests/ui_scenarios/EditorUiScenarios.cpp
    )
    add_library(HoroEngineImGuiTest STATIC ${ENGINE_SOURCES})
    target_sources(HoroEngineImGuiTest PRIVATE ${HORO_UI_TEST_SOURCES})
    target_compile_definitions(HoroEngineImGuiTest PUBLIC
        IMGUI_USER_CONFIG=\"imgui_test_engine_config.h\"
        HORO_STANDALONE_UI_AUTOMATION=1
    )
    target_include_directories(HoroEngineImGuiTest PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tests>
    )
    horo_configure_engine_target(HoroEngineImGuiTest imgui_test_lib)

    # Add the OpenGL backend sources so HoroEngineImGuiTest is self-contained.
    # RenderBackendFactory.cpp unconditionally references OpenGLRenderBackend,
    # so the sources must be present regardless of the selected renderer.
    target_sources(HoroEngineImGuiTest PRIVATE ${HORO_RENDERER_OPENGL_SOURCES})
    target_include_directories(HoroEngineImGuiTest PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    )
    target_compile_definitions(HoroEngineImGuiTest PUBLIC HORO_RENDERER_OPENGL=1)
endif()

# Copy GLSL shaders to bin/shaders alongside the executable
add_custom_command(TARGET HoroEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/renderer/shaders
        ${CMAKE_BINARY_DIR}/bin/shaders
    COMMENT "Copying shaders to bin/shaders"
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND HORO_ENGINE_BUILD_STANDALONE_EDITOR)
    add_executable(HoroEditor ui/launcher/HoroEditorMain.cpp)
    target_compile_features(HoroEditor PRIVATE cxx_std_20)

    if(WIN32)
        target_compile_definitions(HoroEditor PRIVATE NOMINMAX)
        target_sources(HoroEditor PRIVATE ui/launcher/HoroEditor.rc)
    endif()

    if(HORO_RENDERER STREQUAL "opengl")
        target_compile_definitions(HoroEditor PRIVATE HORO_RENDERER_OPENGL=1)
        target_link_libraries(HoroEditor PRIVATE HoroEngine horo-renderer-opengl)
    elseif(HORO_RENDERER STREQUAL "vulkan")
        message(WARNING "HORO_RENDERER=vulkan: Vulkan backend not yet implemented; linking OpenGL backend as placeholder.")
        target_compile_definitions(HoroEditor PRIVATE HORO_RENDERER_VULKAN=1)
        target_link_libraries(HoroEditor PRIVATE HoroEngine horo-renderer-opengl)
    elseif(HORO_RENDERER STREQUAL "null")
        target_compile_definitions(HoroEditor PRIVATE HORO_RENDERER_NULL=1)
        target_link_libraries(HoroEditor PRIVATE HoroEngine horo-renderer-null)
    else()
        target_link_libraries(HoroEditor PRIVATE HoroEngine)
    endif()

    add_custom_command(TARGET HoroEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:HoroEditor>/../sdk/assets
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/editor_schema.json
            $<TARGET_FILE_DIR:HoroEditor>/../sdk/assets/editor_schema.json
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:HoroEditor>/../sdk/assets/fonts
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/InterVariable.ttf
            $<TARGET_FILE_DIR:HoroEditor>/../sdk/assets/fonts/InterVariable.ttf
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:HoroEditor>/../sdk/assets/launcher
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/launcher/logo.png
            $<TARGET_FILE_DIR:HoroEditor>/../sdk/assets/launcher/logo.png
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/launcher/logo.ico
            $<TARGET_FILE_DIR:HoroEditor>/../sdk/assets/launcher/logo.ico
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/launcher/logo_with_title.png
            $<TARGET_FILE_DIR:HoroEditor>/../sdk/assets/launcher/logo_with_title.png
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/launcher/discord-icon.png
            $<TARGET_FILE_DIR:HoroEditor>/../sdk/assets/launcher/discord-icon.png
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:HoroEditor>/../sdk/renderer
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/renderer/shaders
            $<TARGET_FILE_DIR:HoroEditor>/../sdk/renderer/shaders
        COMMENT "Copying HoroEditor SDK resources next to launcher binary"
    )

    if(HORO_ENGINE_BUILD_TESTS AND TARGET HoroEngineImGuiTest)
        add_executable(HoroEditorUiTest ui/launcher/HoroEditorMain.cpp)
        target_link_libraries(HoroEditorUiTest PRIVATE HoroEngineImGuiTest)
        target_compile_features(HoroEditorUiTest PRIVATE cxx_std_20)
        target_compile_definitions(HoroEditorUiTest PRIVATE HORO_STANDALONE_UI_AUTOMATION=1)

        if(WIN32)
            target_compile_definitions(HoroEditorUiTest PRIVATE NOMINMAX)
            target_sources(HoroEditorUiTest PRIVATE ui/launcher/HoroEditor.rc)
        endif()

        add_custom_command(TARGET HoroEditorUiTest POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:HoroEditorUiTest>/../sdk/assets
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/assets/editor_schema.json
                $<TARGET_FILE_DIR:HoroEditorUiTest>/../sdk/assets/editor_schema.json
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:HoroEditorUiTest>/../sdk/assets/fonts
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/InterVariable.ttf
                $<TARGET_FILE_DIR:HoroEditorUiTest>/../sdk/assets/fonts/InterVariable.ttf
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:HoroEditorUiTest>/../sdk/assets/launcher
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/assets/launcher/logo.png
                $<TARGET_FILE_DIR:HoroEditorUiTest>/../sdk/assets/launcher/logo.png
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/assets/launcher/logo.ico
                $<TARGET_FILE_DIR:HoroEditorUiTest>/../sdk/assets/launcher/logo.ico
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/assets/launcher/logo_with_title.png
                $<TARGET_FILE_DIR:HoroEditorUiTest>/../sdk/assets/launcher/logo_with_title.png
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/assets/launcher/discord-icon.png
                $<TARGET_FILE_DIR:HoroEditorUiTest>/../sdk/assets/launcher/discord-icon.png
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:HoroEditorUiTest>/../sdk/renderer
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/renderer/shaders
                $<TARGET_FILE_DIR:HoroEditorUiTest>/../sdk/renderer/shaders
            COMMENT "Copying HoroEditor UI test SDK resources"
        )
    endif()
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    install(TARGETS HoroEngine horo-renderer-opengl glad imgui_lib glfw glm stb tinygltf nlohmann_json ufbx
        EXPORT HoroEngineTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

    if(HORO_ENGINE_BUILD_STANDALONE_EDITOR)
        install(TARGETS HoroEditor
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()

    install(DIRECTORY core input math mcp physics renderer scene ui
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h"
    )
    install(DIRECTORY assets/
        DESTINATION sdk/assets
        FILES_MATCHING PATTERN "editor_schema.json"
    )
    install(DIRECTORY assets/fonts/
        DESTINATION sdk/assets/fonts
        FILES_MATCHING PATTERN "*.ttf"
    )
    install(DIRECTORY assets/launcher/
        DESTINATION sdk/assets/launcher
        FILES_MATCHING PATTERN "*.png"
    )
    install(DIRECTORY renderer/shaders/
        DESTINATION sdk/renderer/shaders
    )
    install(DIRECTORY ${HORO_VENDOR_GLM_INCLUDE_DIR}/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vendor/glm
    )
    install(DIRECTORY ${HORO_VENDOR_STB_INCLUDE_DIR}/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vendor/stb
    )
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/vendor/glad/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vendor/glad
    )
    install(DIRECTORY ${HORO_VENDOR_GLFW_INCLUDE_DIR}/GLFW
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vendor/glfw
    )
    install(FILES
        ${HORO_VENDOR_IMGUI_INCLUDE_DIR}/imconfig.h
        ${HORO_VENDOR_IMGUI_INCLUDE_DIR}/imgui.h
        ${HORO_VENDOR_IMGUI_INCLUDE_DIR}/imgui_internal.h
        ${HORO_VENDOR_IMGUI_INCLUDE_DIR}/imstb_rectpack.h
        ${HORO_VENDOR_IMGUI_INCLUDE_DIR}/imstb_textedit.h
        ${HORO_VENDOR_IMGUI_INCLUDE_DIR}/imstb_truetype.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vendor/imgui
    )
    install(FILES
        ${HORO_VENDOR_IMGUI_INCLUDE_DIR}/backends/imgui_impl_glfw.h
        ${HORO_VENDOR_IMGUI_INCLUDE_DIR}/backends/imgui_impl_opengl3.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vendor/imgui/backends
    )
    install(DIRECTORY ${HORO_VENDOR_NLOHMANN_INCLUDE_DIR}/nlohmann
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vendor/nlohmann
    )
    install(FILES ${HORO_VENDOR_TINYGLTF_INCLUDE_DIR}/tiny_gltf.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vendor/tinygltf
    )

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/HoroEngineConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/HoroEngineConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HoroEngine
    )
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/HoroEngineConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )
    export(EXPORT HoroEngineTargets
        FILE ${CMAKE_CURRENT_BINARY_DIR}/HoroEngineTargets.cmake
        NAMESPACE HoroEngine::
    )

    install(EXPORT HoroEngineTargets
        FILE HoroEngineTargets.cmake
        NAMESPACE HoroEngine::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HoroEngine
    )
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/HoroEngineConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/HoroEngineConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HoroEngine
    )
endif()

# NOTE: The assets/ copy command (${CMAKE_SOURCE_DIR}/assets) is intentionally
# absent here. Game assets belong to the consumer project (horo/app/).
# In launcher mode there are no game assets; in embedded mode app/CMakeLists.txt
# handles the copy. Keeping it here would break launcher CI with a missing-dir error.

# ---- Tests (launcher only) ----
if(HORO_ENGINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
