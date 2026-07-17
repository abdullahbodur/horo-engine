cmake_minimum_required(VERSION 3.25)

set(HORO_ENGINE_VERSION "0.0.1")

project(HoroEngine VERSION ${HORO_ENGINE_VERSION} LANGUAGES CXX C)

# New project layout is being introduced incrementally. Legacy source is parked
# under deprecated/ as migration reference and is intentionally not part of target
# discovery.

include(GNUInstallDirs)
include(CTest)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(HORO_BUILD_EDITOR_GUI "Build the graphical HoroEditor bootstrap" ON)
option(HORO_BUILD_RENDER_OPENGL "Build the OpenGL renderer backend" ${HORO_BUILD_EDITOR_GUI})
set(_HORO_BUILD_RENDER_METAL_DEFAULT OFF)
if(APPLE AND HORO_BUILD_EDITOR_GUI)
    set(_HORO_BUILD_RENDER_METAL_DEFAULT ON)
endif()
option(HORO_BUILD_RENDER_METAL "Build the Metal renderer backend" ${_HORO_BUILD_RENDER_METAL_DEFAULT})
option(HORO_ENABLE_GPU_SMOKE_TESTS "Register tests that require a display server and hardware graphics context" OFF)

if(HORO_BUILD_RENDER_METAL AND NOT APPLE)
    message(FATAL_ERROR "HORO_BUILD_RENDER_METAL is supported only on Apple platforms")
endif()

if(HORO_BUILD_EDITOR_GUI AND NOT HORO_BUILD_RENDER_OPENGL AND NOT HORO_BUILD_RENDER_METAL)
    message(FATAL_ERROR "The editor GUI composition requires at least one interactive renderer")
endif()

include(Dependencies)
include(GenerateBuildInfo)


if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/CMakeLists.txt")
    add_subdirectory(src)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/CMakeLists.txt")
    add_subdirectory(apps)
endif()

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()
