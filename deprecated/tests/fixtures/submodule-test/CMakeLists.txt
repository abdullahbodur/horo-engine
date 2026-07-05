# ---- Submodule Smoke Test Fixture ----
#
# Proves Horo Engine compiles and links when consumed as a submodule
# via add_subdirectory(engine).  This CMakeLists.txt mimics a minimal
# game project that vendors the engine.
#
# CI overrides HORO_ENGINE_ROOT to the checkout root.
# Local dev can symlink engine/ → ../../.. or pass -DHORO_ENGINE_ROOT=...

cmake_minimum_required(VERSION 3.25)
project(SubmoduleSmoke LANGUAGES CXX C)

# ---- Engine root ------------------------------------------------
# Default: an engine/ symlink or checkout alongside this file.
# CI passes the absolute path to the engine repo checkout.
set(HORO_ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/engine" CACHE PATH
    "Path to the Horo Engine repository root")

# ---- Gate expensive launcher-only features ----------------------
# In submodule mode these options don't exist (they're defined only
# inside the engine's launcher-mode block), but being explicit here
# protects against future option leakage and documents intent.
set(HORO_ENGINE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(HORO_ENGINE_BUILD_STANDALONE_EDITOR OFF CACHE BOOL "" FORCE)
set(HORO_ENGINE_ENABLE_SHADER_COOKER OFF CACHE BOOL "" FORCE)
set(HORO_ENGINE_ENABLE_VULKAN OFF CACHE BOOL "" FORCE)

# ---- Bring in the engine as a subdirectory ----------------------
add_subdirectory(${HORO_ENGINE_ROOT} engine)

# ---- Smoke executable -------------------------------------------
add_executable(smoke-submodule src/main.cpp)
target_link_libraries(smoke-submodule PRIVATE
    HoroEngine::HoroEngine
    HoroEngine::RendererNull
)
target_compile_features(smoke-submodule PRIVATE cxx_std_20)
