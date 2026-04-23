cmake_minimum_required(VERSION 3.25)

# Detect if this is a launcher build or embedded via add_subdirectory()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Launcher mode: local dev, CI, tests
    project(MonolithEngine VERSION 0.1.0 LANGUAGES CXX C)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

    option(MONOLITH_ENGINE_BUILD_TESTS "Build engine unit tests" ON)
    option(MONOLITH_ENGINE_ENABLE_VULKAN "Fetch Vulkan headers/loader and compile Vulkan backend scaffolding" OFF)
    option(MONOLITH_ENGINE_BUILD_STANDALONE_EDITOR "Build HoroEditor (SDK launcher + editor shell executable)" ON)
    option(MONOLITH_FRAMEBUFFER_STREAM "Enable WebSocket framebuffer streaming for Theia/browser editor (port 39282)" OFF)

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

# ---- MonolithEngine static library ----
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/math/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/mcp/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/physics/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/scene/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/input/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/studio/*.cpp"
)
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "studio[/\\\\]HoroEditorMain\\.cpp$")

add_library(MonolithEngine STATIC ${ENGINE_SOURCES})
add_library(MonolithEngine::MonolithEngine ALIAS MonolithEngine)

function(monolith_configure_engine_target target_name imgui_target)
    target_include_directories(${target_name}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
            $<INSTALL_INTERFACE:include>
            $<BUILD_INTERFACE:${MONOLITH_VENDOR_NLOHMANN_INCLUDE_DIR}>
            $<INSTALL_INTERFACE:include/vendor/nlohmann>
    )

    target_link_libraries(${target_name}
        PUBLIC
            glfw
            glad
            ${imgui_target}
        PRIVATE
            glm
            stb
            nlohmann_json
            tinygltf
    )

    if(MONOLITH_ENGINE_ENABLE_VULKAN)
        target_link_libraries(${target_name} PUBLIC monolith_volk monolith_vulkan_headers)
        target_compile_definitions(${target_name} PUBLIC MONOLITH_HAS_VULKAN=1)
    endif()

    if(APPLE)
        target_link_libraries(${target_name} PUBLIC "-framework OpenGL")
    elseif(WIN32)
        target_link_libraries(${target_name} PUBLIC opengl32 comdlg32 ole32 shell32 ws2_32)
    else()
        target_link_libraries(${target_name} PUBLIC GL)
    endif()

    target_compile_features(${target_name} PUBLIC cxx_std_20)

    if(WIN32)
        target_compile_definitions(${target_name} PUBLIC NOMINMAX)
    endif()

    if(MONOLITH_ENGINE_COVERAGE AND NOT MSVC)
        target_compile_options(${target_name} PUBLIC --coverage -O0 -g)
        target_link_options(${target_name} PUBLIC --coverage)
    endif()
endfunction()

# Optional gcov/llvm-cov instrumentation (GCC/Clang only).
option(MONOLITH_ENGINE_COVERAGE "Add coverage flags to MonolithEngine (for reports after ctest)" OFF)

monolith_configure_engine_target(MonolithEngine imgui_lib)

if(MONOLITH_FRAMEBUFFER_STREAM)
    target_compile_definitions(MonolithEngine PUBLIC MONOLITH_FRAMEBUFFER_STREAM=1)
endif()

if(MONOLITH_ENGINE_BUILD_TESTS AND TARGET imgui_test_lib)
    set(MONOLITH_UI_TEST_SOURCES
        tests/UiTestRegistry.cpp
        tests/ui_scenarios/LauncherUiScenarios.cpp
    )
    add_library(MonolithEngineImGuiTest STATIC ${ENGINE_SOURCES})
    target_sources(MonolithEngineImGuiTest PRIVATE ${MONOLITH_UI_TEST_SOURCES})
    target_compile_definitions(MonolithEngineImGuiTest PUBLIC
        IMGUI_USER_CONFIG=\"imgui_test_engine_config.h\"
        MONOLITH_STANDALONE_UI_AUTOMATION=1
    )
    target_include_directories(MonolithEngineImGuiTest PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tests>
    )
    monolith_configure_engine_target(MonolithEngineImGuiTest imgui_test_lib)
endif()

# Copy GLSL shaders to bin/shaders alongside the executable
add_custom_command(TARGET MonolithEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/renderer/shaders
        ${CMAKE_BINARY_DIR}/bin/shaders
    COMMENT "Copying shaders to bin/shaders"
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND MONOLITH_ENGINE_BUILD_STANDALONE_EDITOR)
    add_executable(HoroEditor studio/HoroEditorMain.cpp)
    target_link_libraries(HoroEditor PRIVATE MonolithEngine)
    target_compile_features(HoroEditor PRIVATE cxx_std_20)

    if(WIN32)
        target_compile_definitions(HoroEditor PRIVATE NOMINMAX)
    endif()

    add_custom_command(TARGET HoroEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:HoroEditor>/../sdk/assets
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/editor_schema.json
            $<TARGET_FILE_DIR:HoroEditor>/../sdk/assets/editor_schema.json
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:HoroEditor>/../sdk/renderer
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/renderer/shaders
            $<TARGET_FILE_DIR:HoroEditor>/../sdk/renderer/shaders
        COMMENT "Copying HoroEditor SDK resources next to launcher binary"
    )

    if(MONOLITH_ENGINE_BUILD_TESTS AND TARGET MonolithEngineImGuiTest)
        add_executable(HoroEditorUiTest studio/HoroEditorMain.cpp)
        target_link_libraries(HoroEditorUiTest PRIVATE MonolithEngineImGuiTest)
        target_compile_features(HoroEditorUiTest PRIVATE cxx_std_20)
        target_compile_definitions(HoroEditorUiTest PRIVATE MONOLITH_STANDALONE_UI_AUTOMATION=1)

        if(WIN32)
            target_compile_definitions(HoroEditorUiTest PRIVATE NOMINMAX)
        endif()

        add_custom_command(TARGET HoroEditorUiTest POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:HoroEditorUiTest>/../sdk/assets
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/assets/editor_schema.json
                $<TARGET_FILE_DIR:HoroEditorUiTest>/../sdk/assets/editor_schema.json
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:HoroEditorUiTest>/../sdk/renderer
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/renderer/shaders
                $<TARGET_FILE_DIR:HoroEditorUiTest>/../sdk/renderer/shaders
            COMMENT "Copying HoroEditor UI test SDK resources"
        )
    endif()
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    install(TARGETS MonolithEngine glad imgui_lib glfw glm stb tinygltf nlohmann_json
        EXPORT MonolithEngineTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

    if(MONOLITH_ENGINE_BUILD_STANDALONE_EDITOR)
        install(TARGETS HoroEditor
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()

    install(DIRECTORY core studio input math mcp physics renderer scene
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h"
    )
    install(DIRECTORY assets/
        DESTINATION sdk/assets
        FILES_MATCHING PATTERN "editor_schema.json"
    )
    install(DIRECTORY renderer/shaders/
        DESTINATION sdk/renderer/shaders
    )
    install(DIRECTORY ${MONOLITH_VENDOR_GLM_INCLUDE_DIR}/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vendor/glm
    )
    install(DIRECTORY ${MONOLITH_VENDOR_STB_INCLUDE_DIR}/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vendor/stb
    )
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/vendor/glad/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vendor/glad
    )
    install(DIRECTORY ${MONOLITH_VENDOR_GLFW_INCLUDE_DIR}/GLFW
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vendor/glfw
    )
    install(FILES
        ${MONOLITH_VENDOR_IMGUI_INCLUDE_DIR}/imconfig.h
        ${MONOLITH_VENDOR_IMGUI_INCLUDE_DIR}/imgui.h
        ${MONOLITH_VENDOR_IMGUI_INCLUDE_DIR}/imgui_internal.h
        ${MONOLITH_VENDOR_IMGUI_INCLUDE_DIR}/imstb_rectpack.h
        ${MONOLITH_VENDOR_IMGUI_INCLUDE_DIR}/imstb_textedit.h
        ${MONOLITH_VENDOR_IMGUI_INCLUDE_DIR}/imstb_truetype.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vendor/imgui
    )
    install(FILES
        ${MONOLITH_VENDOR_IMGUI_INCLUDE_DIR}/backends/imgui_impl_glfw.h
        ${MONOLITH_VENDOR_IMGUI_INCLUDE_DIR}/backends/imgui_impl_opengl3.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vendor/imgui/backends
    )
    install(DIRECTORY ${MONOLITH_VENDOR_NLOHMANN_INCLUDE_DIR}/nlohmann
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vendor/nlohmann
    )
    install(FILES ${MONOLITH_VENDOR_TINYGLTF_INCLUDE_DIR}/tiny_gltf.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vendor/tinygltf
    )

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/MonolithEngineConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/MonolithEngineConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MonolithEngine
    )
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/MonolithEngineConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )
    export(EXPORT MonolithEngineTargets
        FILE ${CMAKE_CURRENT_BINARY_DIR}/MonolithEngineTargets.cmake
        NAMESPACE MonolithEngine::
    )

    install(EXPORT MonolithEngineTargets
        FILE MonolithEngineTargets.cmake
        NAMESPACE MonolithEngine::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MonolithEngine
    )
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/MonolithEngineConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/MonolithEngineConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MonolithEngine
    )
endif()

# NOTE: The assets/ copy command (${CMAKE_SOURCE_DIR}/assets) is intentionally
# absent here. Game assets belong to the consumer project (monolith/app/).
# In launcher mode there are no game assets; in embedded mode app/CMakeLists.txt
# handles the copy. Keeping it here would break launcher CI with a missing-dir error.

# ---- Tests (launcher only) ----
if(MONOLITH_ENGINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
