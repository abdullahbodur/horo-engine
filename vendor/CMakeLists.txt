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
    target_include_directories(glfw PUBLIC
        $<BUILD_INTERFACE:${glfw_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/vendor/glfw>
    )
    set(MONOLITH_VENDOR_GLFW_INCLUDE_DIR "${glfw_SOURCE_DIR}/include" CACHE INTERNAL "" FORCE)
endif()

# ---- Optional Vulkan headers + loader scaffolding ----
if(MONOLITH_ENGINE_ENABLE_VULKAN)
    if(NOT TARGET monolith_vulkan_headers)
        FetchContent_Declare(
            vulkan_headers
            GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
            GIT_TAG        vulkan-sdk-1.4.341.0
            GIT_SHALLOW    TRUE
        )
        FetchContent_Populate(vulkan_headers)

        add_library(monolith_vulkan_headers INTERFACE)
        target_include_directories(monolith_vulkan_headers INTERFACE
            ${vulkan_headers_SOURCE_DIR}/include
        )
    endif()

    if(NOT TARGET monolith_volk)
        FetchContent_Declare(
            volk
            GIT_REPOSITORY https://github.com/zeux/volk.git
            GIT_TAG        1.4.304
            GIT_SHALLOW    TRUE
        )
        FetchContent_Populate(volk)

        add_library(monolith_volk STATIC ${volk_SOURCE_DIR}/volk.c)
        target_include_directories(monolith_volk PUBLIC ${volk_SOURCE_DIR})
        target_link_libraries(monolith_volk PUBLIC monolith_vulkan_headers)
        target_compile_definitions(monolith_volk PUBLIC VK_NO_PROTOTYPES)
        target_compile_options(monolith_volk PRIVATE
            $<$<CXX_COMPILER_ID:MSVC>:/W0>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>
        )
    endif()
endif()

# ---- glad (OpenGL 4.1 Core, pre-generated, committed) ----
if(NOT TARGET glad)
    add_subdirectory(glad)
endif()

# ---- GLM (header-only reference) ----
if(NOT TARGET glm)
    add_library(glm INTERFACE)
    target_include_directories(glm INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/glm>
        $<INSTALL_INTERFACE:include/vendor/glm>
    )
endif()

# ---- stb_image (header-only, compiled in renderer) ----
if(NOT TARGET stb)
    add_library(stb INTERFACE)
    target_include_directories(stb INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/stb>
        $<INSTALL_INTERFACE:include/vendor/stb>
    )
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

    set(MONOLITH_IMGUI_SOURCES
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )

    if(MONOLITH_ENGINE_ENABLE_VULKAN)
        list(APPEND MONOLITH_IMGUI_SOURCES
            ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
        )
    endif()

    add_library(imgui_lib STATIC ${MONOLITH_IMGUI_SOURCES})
    target_include_directories(imgui_lib PUBLIC
        $<BUILD_INTERFACE:${imgui_SOURCE_DIR}>
        $<BUILD_INTERFACE:${imgui_SOURCE_DIR}/backends>
        $<INSTALL_INTERFACE:include/vendor/imgui>
        $<INSTALL_INTERFACE:include/vendor/imgui/backends>
    )
    target_link_libraries(imgui_lib PUBLIC glfw glad)
    set(MONOLITH_VENDOR_IMGUI_INCLUDE_DIR "${imgui_SOURCE_DIR}" CACHE INTERNAL "" FORCE)

    if(MONOLITH_ENGINE_ENABLE_VULKAN)
        target_compile_definitions(imgui_lib PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)
        target_link_libraries(imgui_lib PUBLIC monolith_volk monolith_vulkan_headers)
    endif()

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
    if(TARGET nlohmann_json)
        target_include_directories(nlohmann_json INTERFACE
            $<BUILD_INTERFACE:${nlohmann_json_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include/vendor/nlohmann>
        )
        set_property(TARGET nlohmann_json PROPERTY SOURCES "")
    endif()
    set(MONOLITH_VENDOR_NLOHMANN_INCLUDE_DIR "${nlohmann_json_SOURCE_DIR}/include" CACHE INTERNAL "" FORCE)
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
    target_link_libraries(tinygltf INTERFACE nlohmann_json stb)
    # Suppress vendor warnings
    target_compile_options(tinygltf INTERFACE
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>
    )
    target_include_directories(tinygltf INTERFACE
        $<BUILD_INTERFACE:${tinygltf_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/vendor/tinygltf>
    )
    set(MONOLITH_VENDOR_TINYGLTF_INCLUDE_DIR "${tinygltf_SOURCE_DIR}" CACHE INTERNAL "" FORCE)
endif()

set(MONOLITH_VENDOR_GLM_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/glm" CACHE INTERNAL "" FORCE)
set(MONOLITH_VENDOR_STB_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/stb" CACHE INTERNAL "" FORCE)
