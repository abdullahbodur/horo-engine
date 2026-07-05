# ---- Find-Package Smoke Test Fixture ----
#
# Proves a game project can consume Horo Engine as an installed SDK
# via find_package(HoroEngine).  The CI smoke script installs the
# engine into a prefix and points CMAKE_PREFIX_PATH at it.
#
# This fixture is the "Path B" counterpart to the parent fixture's
# "Path A" add_subdirectory() consumption.

cmake_minimum_required(VERSION 3.25)
project(FindPackageSmoke LANGUAGES CXX C)

find_package(HoroEngine REQUIRED)

add_executable(smoke-find-package src/main.cpp)
target_link_libraries(smoke-find-package PRIVATE
    HoroEngine::HoroEngine
    HoroEngine::RendererNull
)
target_compile_features(smoke-find-package PRIVATE cxx_std_20)
