cmake_minimum_required(VERSION 3.25)
project(HoroMcpBoundaryNegativeFixture LANGUAGES CXX)

if(NOT HORO_ENGINE_SOURCE_DIR)
    message(FATAL_ERROR "HORO_ENGINE_SOURCE_DIR is required")
endif()

list(APPEND CMAKE_MODULE_PATH "${HORO_ENGINE_SOURCE_DIR}/cmake")
include(HoroDependencyDirection)

# A domain/application target importing the MCP adapter reverses the normative
# dependency direction even before either concrete library exists.
horo_allow_target_dependencies(
    TARGET HoroApplication
    DEPENDENCIES HoroFoundation HoroMcpController)
