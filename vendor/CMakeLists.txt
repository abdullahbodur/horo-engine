include(FetchContent)

# ---- GLFW ----
if(NOT TARGET glfw)
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE
    )
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)
endif()

# ---- glad (OpenGL 4.1 Core, pre-generated, committed) ----
if(NOT TARGET glad)
    add_subdirectory(glad)
endif()

# ---- GLM (header-only reference) ----
if(NOT TARGET glm)
    add_library(glm INTERFACE)
    target_include_directories(glm INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/glm)
endif()

# ---- stb_image (header-only, compiled in renderer) ----
if(NOT TARGET stb)
    add_library(stb INTERFACE)
    target_include_directories(stb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)
endif()

# ---- Dear ImGui ----
if(NOT TARGET imgui_lib)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.6
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(imgui)

    add_library(imgui_lib STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(imgui_lib PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_link_libraries(imgui_lib PUBLIC glfw glad)
    # Suppress vendor warnings (MSVC -WX, GCC/Clang -Werror)
    target_compile_options(imgui_lib PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W0>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>
    )
endif()

# ---- nlohmann/json (header-only) ----
if(NOT TARGET nlohmann_json::nlohmann_json)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
        GIT_SHALLOW    TRUE
    )
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    set(JSON_Install    OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# ---- tinygltf (header-only glTF 2.0 / GLB loader) ----
if(NOT TARGET tinygltf)
    FetchContent_Declare(
        tinygltf
        GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
        GIT_TAG        v2.9.3
        GIT_SHALLOW    TRUE
    )
    set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "" FORCE)
    set(TINYGLTF_INSTALL              OFF CACHE BOOL "" FORCE)
    set(TINYGLTF_HEADER_ONLY          ON  CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(tinygltf)

    # tinygltf's CMakeLists creates an INTERFACE target named tinygltf.
    # Tell it to reuse our already-fetched nlohmann/json and stb_image
    # so there are no duplicate symbol issues.
    target_compile_definitions(tinygltf INTERFACE
        TINYGLTF_NO_INCLUDE_JSON
        TINYGLTF_NO_STB_IMAGE
        TINYGLTF_NO_STB_IMAGE_WRITE
    )
    target_link_libraries(tinygltf INTERFACE nlohmann_json::nlohmann_json stb)
    # Suppress vendor warnings
    target_compile_options(tinygltf INTERFACE
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>
    )
endif()
