cmake_minimum_required(VERSION 3.25)

set(HORO_ENGINE_VERSION "0.0.1")

project(HoroEngine VERSION ${HORO_ENGINE_VERSION} LANGUAGES CXX C)

# New project layout is being introduced incrementally. Legacy source is parked
# under deprecated/ as migration reference and is intentionally not part of target
# discovery.

include(GNUInstallDirs)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/CMakeLists.txt")
    add_subdirectory(src)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/CMakeLists.txt")
    add_subdirectory(apps)
endif()

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()
