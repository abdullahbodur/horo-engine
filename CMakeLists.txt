cmake_minimum_required(VERSION 3.25)

# Detect if this is a standalone build or embedded via add_subdirectory()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Standalone mode: local dev, CI, tests
    project(HoroEngine VERSION 0.1.0 LANGUAGES CXX C)

    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

    option(HORO_ENGINE_BUILD_TESTS "Build engine unit tests" ON)

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

# ---- HoroEngine static library ----
file(GLOB_RECURSE ENGINE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/math/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/physics/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/scene/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/input/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cpp"
)

add_library(HoroEngine STATIC ${ENGINE_SOURCES})

target_include_directories(HoroEngine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}   # engine internal: #include "math/Vec3.h"
                                      # when embedded at engine/: game code uses
                                      # #include "engine/math/Vec3.h" via parent's
                                      # CMAKE_SOURCE_DIR include path
)

target_link_libraries(HoroEngine
    PUBLIC
        glfw
        glad
        glm
        stb
        imgui_lib
        nlohmann_json::nlohmann_json
)

if(APPLE)
    target_link_libraries(HoroEngine PUBLIC "-framework OpenGL")
elseif(WIN32)
    target_link_libraries(HoroEngine PUBLIC opengl32 comdlg32)
else()
    target_link_libraries(HoroEngine PUBLIC GL)
endif()

target_compile_features(HoroEngine PUBLIC cxx_std_20)

if(WIN32)
    target_compile_definitions(HoroEngine PUBLIC NOMINMAX)
endif()

# Copy GLSL shaders to bin/shaders alongside the executable
add_custom_command(TARGET HoroEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/renderer/shaders
        ${CMAKE_BINARY_DIR}/bin/shaders
    COMMENT "Copying shaders to bin/shaders"
)

# NOTE: The assets/ copy command (${CMAKE_SOURCE_DIR}/assets) is intentionally
# absent here. Game assets belong to the consumer project (horo/app/).
# In standalone mode there are no game assets; in embedded mode app/CMakeLists.txt
# handles the copy. Keeping it here would break standalone CI with a missing-dir error.

# ---- Tests (standalone only) ----
if(HORO_ENGINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
