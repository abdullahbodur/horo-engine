cmake_minimum_required(VERSION 3.25)
project(HoroDependencyDirectionNegativeFixture LANGUAGES CXX)

if(NOT HORO_ENGINE_SOURCE_DIR)
    message(FATAL_ERROR "HORO_ENGINE_SOURCE_DIR is required")
endif()

list(APPEND CMAKE_MODULE_PATH "${HORO_ENGINE_SOURCE_DIR}/cmake")
include(HoroDependencyDirection)

add_library(HoroFoundation INTERFACE)
add_library(HoroEditorServices INTERFACE)
target_link_libraries(HoroFoundation INTERFACE HoroEditorServices)

horo_allow_target_dependencies(TARGET HoroFoundation)
horo_allow_target_dependencies(TARGET HoroEditorServices DEPENDENCIES HoroFoundation)
horo_verify_dependency_direction(TARGETS HoroFoundation HoroEditorServices)
