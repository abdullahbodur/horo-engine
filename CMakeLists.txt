cmake_minimum_required(VERSION 3.25)

# Detect if this is a standalone build or embedded via add_subdirectory()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Standalone mode: local dev, CI, tests
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
    option(MONOLITH_ENGINE_BUILD_STANDALONE_EDITOR "Build the standalone HoroEditor executable" ON)

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
    "${CMAKE_CURRENT_SOURCE_DIR}/mcp/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/physics/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/scene/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/input/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/standalone/*.cpp"
)
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "standalone[/\\\\]HoroEditorMain\\.cpp$")

add_library(MonolithEngine STATIC ${ENGINE_SOURCES})
add_library(MonolithEngine::MonolithEngine ALIAS MonolithEngine)

target_include_directories(MonolithEngine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${MONOLITH_VENDOR_NLOHMANN_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:include/vendor/nlohmann>
)

target_link_libraries(MonolithEngine
    PUBLIC
        glfw
        glad
        imgui_lib
    PRIVATE
        glm
        stb
        nlohmann_json
        tinygltf
)

if(MONOLITH_ENGINE_ENABLE_VULKAN)
    target_link_libraries(MonolithEngine PUBLIC monolith_volk monolith_vulkan_headers)
    target_compile_definitions(MonolithEngine PUBLIC MONOLITH_HAS_VULKAN=1)
endif()

if(APPLE)
    target_link_libraries(MonolithEngine PUBLIC "-framework OpenGL")
elseif(WIN32)
    target_link_libraries(MonolithEngine PUBLIC opengl32 comdlg32 ole32 shell32 ws2_32)
else()
    target_link_libraries(MonolithEngine PUBLIC GL)
endif()

target_compile_features(MonolithEngine PUBLIC cxx_std_20)

if(WIN32)
    target_compile_definitions(MonolithEngine PUBLIC NOMINMAX)
endif()

# Optional gcov/llvm-cov instrumentation (GCC/Clang only).
option(MONOLITH_ENGINE_COVERAGE "Add coverage flags to MonolithEngine (for reports after ctest)" OFF)
if(MONOLITH_ENGINE_COVERAGE AND NOT MSVC)
    target_compile_options(MonolithEngine PUBLIC --coverage -O0 -g)
    target_link_options(MonolithEngine PUBLIC --coverage)
endif()

# Copy GLSL shaders to bin/shaders alongside the executable
add_custom_command(TARGET MonolithEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/renderer/shaders
        ${CMAKE_BINARY_DIR}/bin/shaders
    COMMENT "Copying shaders to bin/shaders"
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND MONOLITH_ENGINE_BUILD_STANDALONE_EDITOR)
    add_executable(HoroEditor standalone/HoroEditorMain.cpp)
    target_link_libraries(HoroEditor PRIVATE MonolithEngine)
    target_compile_features(HoroEditor PRIVATE cxx_std_20)

    if(WIN32)
        target_compile_definitions(HoroEditor PRIVATE NOMINMAX)
    endif()

    add_custom_command(TARGET HoroEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/sdk/assets
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/editor_schema.json
            ${CMAKE_BINARY_DIR}/sdk/assets/editor_schema.json
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/sdk/renderer
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/renderer/shaders
            ${CMAKE_BINARY_DIR}/sdk/renderer/shaders
        COMMENT "Copying standalone editor SDK resources"
    )
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

    install(DIRECTORY core editor input math mcp physics renderer scene standalone
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
# In standalone mode there are no game assets; in embedded mode app/CMakeLists.txt
# handles the copy. Keeping it here would break standalone CI with a missing-dir error.

# ---- Tests (standalone only) ----
if(MONOLITH_ENGINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
