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

    # ---- macOS: patch nsgl_context.m so hardware acceleration is not required
    #      when GLFW_COCOA_GRAPHICS_SWITCHING is enabled.
    #
    # By default GLFW unconditionally adds NSOpenGLPFAAccelerated to the pixel
    # format attribute list, which requires a hardware-backed renderer.  On
    # headless CI runners (GitHub Actions macOS) there is no display connection
    # that satisfies NSOpenGL's hardware requirement, causing glfwCreateWindow
    # to throw "NSGL: Failed to find a suitable pixel format".
    #
    # GLFW already gates NSOpenGLPFAAllowOfflineRenderers on ctxconfig->nsgl.offline
    # (set via GLFW_COCOA_GRAPHICS_SWITCHING = GLFW_TRUE).  We extend that guard to
    # also skip NSOpenGLPFAAccelerated in offline mode so any available renderer
    # (hardware or software) is accepted.  Hardware is still preferred on machines
    # that expose a GPU; this only adds a fallback path.
    if(APPLE)
        set(_glfw_nsgl "${glfw_SOURCE_DIR}/src/nsgl_context.m")
        if(EXISTS "${_glfw_nsgl}")
            execute_process(
                COMMAND python3 -c
                    "import sys\n\
path = sys.argv[1]\n\
with open(path) as f:\n\
    src = f.read()\n\
old = '    ADD_ATTRIB(NSOpenGLPFAAccelerated);'\n\
new = '    if (!ctxconfig->nsgl.offline)\\n        ADD_ATTRIB(NSOpenGLPFAAccelerated);'\n\
patched = src.replace(old, new, 1)\n\
if patched != src:\n\
    with open(path, 'w') as f:\n\
        f.write(patched)\n\
    print('GLFW nsgl_context.m: patched NSOpenGLPFAAccelerated guard')\n\
else:\n\
    print('GLFW nsgl_context.m: already patched or pattern not found')"
                    "${_glfw_nsgl}"
                OUTPUT_VARIABLE _glfw_patch_out
                ERROR_VARIABLE  _glfw_patch_err
                RESULT_VARIABLE _glfw_patch_result
            )
            if(_glfw_patch_result EQUAL 0)
                message(STATUS "${_glfw_patch_out}")
            else()
                message(WARNING "GLFW nsgl patch failed: ${_glfw_patch_err}")
            endif()
        endif()
    endif()

    target_include_directories(glfw PUBLIC
        $<BUILD_INTERFACE:${glfw_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/vendor/glfw>
    )
    # GLFW deliberately enables its own warning flags. Keep third-party warning
    # noise out of launcher/editor builds; engine diagnostics should come from
    # Horo-owned targets, not vendored platform backends.
    target_compile_options(glfw PRIVATE
        $<$<C_COMPILER_ID:MSVC>:/W0>
        $<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>
    )
    set(HORO_VENDOR_GLFW_INCLUDE_DIR "${glfw_SOURCE_DIR}/include" CACHE INTERNAL "" FORCE)
endif()

# ---- Optional Vulkan headers + loader scaffolding ----
if(HORO_ENGINE_ENABLE_VULKAN)
    if(NOT TARGET horo_vulkan_headers)
        FetchContent_Declare(
            vulkan_headers
            GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
            GIT_TAG        vulkan-sdk-1.4.341.0
            GIT_SHALLOW    TRUE
        )
        FetchContent_Populate(vulkan_headers)

        add_library(horo_vulkan_headers INTERFACE)
        target_include_directories(horo_vulkan_headers INTERFACE
            ${vulkan_headers_SOURCE_DIR}/include
        )
    endif()

    if(NOT TARGET horo_volk)
        FetchContent_Declare(
            volk
            GIT_REPOSITORY https://github.com/zeux/volk.git
            GIT_TAG        1.4.304
            GIT_SHALLOW    TRUE
        )
        FetchContent_Populate(volk)

        add_library(horo_volk STATIC ${volk_SOURCE_DIR}/volk.c)
        target_include_directories(horo_volk PUBLIC ${volk_SOURCE_DIR})
        target_link_libraries(horo_volk PUBLIC horo_vulkan_headers)
        target_compile_definitions(horo_volk PUBLIC VK_NO_PROTOTYPES)
        target_compile_options(horo_volk PRIVATE
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

    set(HORO_IMGUI_SOURCES
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )

    if(HORO_ENGINE_ENABLE_VULKAN)
        list(APPEND HORO_IMGUI_SOURCES
            ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
        )
    endif()

    function(horo_add_imgui_target target_name)
        set(options ENABLE_TEST_ENGINE)
        set(oneValueArgs USER_CONFIG_DIR USER_CONFIG_HEADER)
        cmake_parse_arguments(HORO_IMGUI "${options}" "${oneValueArgs}" "" ${ARGN})

        add_library(${target_name} STATIC ${HORO_IMGUI_SOURCES})
        target_include_directories(${target_name} PUBLIC
            $<BUILD_INTERFACE:${imgui_SOURCE_DIR}>
            $<BUILD_INTERFACE:${imgui_SOURCE_DIR}/backends>
            $<INSTALL_INTERFACE:include/vendor/imgui>
            $<INSTALL_INTERFACE:include/vendor/imgui/backends>
        )
        target_link_libraries(${target_name} PUBLIC glfw)
        target_link_libraries(${target_name} PRIVATE glad)

        if(HORO_ENGINE_ENABLE_VULKAN)
            target_compile_definitions(${target_name} PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)
            target_link_libraries(${target_name} PUBLIC horo_volk horo_vulkan_headers)
        endif()

        if(HORO_IMGUI_ENABLE_TEST_ENGINE)
            if(NOT HORO_IMGUI_USER_CONFIG_DIR OR NOT HORO_IMGUI_USER_CONFIG_HEADER)
                message(FATAL_ERROR "horo_add_imgui_target(... ENABLE_TEST_ENGINE ...) requires USER_CONFIG_DIR and USER_CONFIG_HEADER")
            endif()

            if(NOT TARGET horo_imgui_test_engine)
                FetchContent_Declare(
                    imgui_test_engine
                    GIT_REPOSITORY https://github.com/ocornut/imgui_test_engine.git
                    GIT_TAG        v1.91.6
                    GIT_SHALLOW    TRUE
                )
                FetchContent_MakeAvailable(imgui_test_engine)
                find_package(Threads REQUIRED)

                add_library(horo_imgui_test_engine STATIC
                    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_capture_tool.cpp
                    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_context.cpp
                    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_coroutine.cpp
                    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_engine.cpp
                    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_exporters.cpp
                    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_perftool.cpp
                    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_ui.cpp
                    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_utils.cpp
                )
                target_include_directories(horo_imgui_test_engine PUBLIC
                    $<BUILD_INTERFACE:${imgui_SOURCE_DIR}>
                    $<BUILD_INTERFACE:${imgui_test_engine_SOURCE_DIR}>
                )
                target_link_libraries(horo_imgui_test_engine PUBLIC Threads::Threads)
                target_compile_definitions(horo_imgui_test_engine PUBLIC
                    IMGUI_USER_CONFIG=\"${HORO_IMGUI_USER_CONFIG_HEADER}\"
                )
                target_include_directories(horo_imgui_test_engine PUBLIC
                    $<BUILD_INTERFACE:${HORO_IMGUI_USER_CONFIG_DIR}>
                )
                target_compile_options(horo_imgui_test_engine PRIVATE
                    $<$<CXX_COMPILER_ID:MSVC>:/W0>
                    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>
                )
                set(HORO_VENDOR_IMGUI_TEST_ENGINE_INCLUDE_DIR "${imgui_test_engine_SOURCE_DIR}" CACHE INTERNAL "" FORCE)
            endif()

            target_compile_definitions(${target_name} PUBLIC
                IMGUI_USER_CONFIG=\"${HORO_IMGUI_USER_CONFIG_HEADER}\"
            )
            target_include_directories(${target_name} PUBLIC
                $<BUILD_INTERFACE:${HORO_IMGUI_USER_CONFIG_DIR}>
            )
            target_link_libraries(${target_name} PUBLIC horo_imgui_test_engine)
        endif()

        # Suppress vendor warnings (MSVC -WX, GCC/Clang -Werror)
        target_compile_options(${target_name} PRIVATE
            $<$<CXX_COMPILER_ID:MSVC>:/W0>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>
        )
    endfunction()

    horo_add_imgui_target(imgui_lib)
    set(HORO_VENDOR_IMGUI_INCLUDE_DIR "${imgui_SOURCE_DIR}" CACHE INTERNAL "" FORCE)

    if(HORO_ENGINE_BUILD_TESTS AND NOT TARGET imgui_test_lib)
        horo_add_imgui_target(
            imgui_test_lib
            ENABLE_TEST_ENGINE
            USER_CONFIG_DIR "${CMAKE_CURRENT_LIST_DIR}/../tests"
            USER_CONFIG_HEADER "imgui_test_engine_config.h"
        )
    endif()
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
    set(HORO_VENDOR_NLOHMANN_INCLUDE_DIR "${nlohmann_json_SOURCE_DIR}/include" CACHE INTERNAL "" FORCE)
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
    set(HORO_VENDOR_TINYGLTF_INCLUDE_DIR "${tinygltf_SOURCE_DIR}" CACHE INTERNAL "" FORCE)
endif()

set(HORO_VENDOR_GLM_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/glm" CACHE INTERNAL "" FORCE)
set(HORO_VENDOR_STB_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/stb" CACHE INTERNAL "" FORCE)

# ---- ufbx (single-source FBX parser, MIT) ----
# Vendored amalgamation pinned at v0.21.3 (commit 83bc7cf) under vendor/ufbx/.
# The library compiles ufbx.c as C99 and exposes ufbx.h to consumers; the
# upstream project is dual-licensed MIT / public domain — see vendor/ufbx/LICENSE.
if(NOT TARGET ufbx)
    add_library(ufbx STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/ufbx/ufbx.c
    )
    target_include_directories(ufbx PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/ufbx>
        $<INSTALL_INTERFACE:include/vendor/ufbx>
    )
    set_target_properties(ufbx PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
    # Suppress vendor warnings so the engine's -Wall -Wextra -Wpedantic / /WX
    # flags do not fail on third-party code.
    target_compile_options(ufbx PRIVATE
        $<$<C_COMPILER_ID:MSVC>:/W0>
        $<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>
    )
    if(UNIX AND NOT APPLE)
        # ufbx uses libm symbols (sqrt, fmod, etc.) and pthread for optional
        # threading helpers. macOS provides both via the system libc.
        target_link_libraries(ufbx PUBLIC m)
    endif()
endif()
set(HORO_VENDOR_UFBX_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ufbx" CACHE INTERNAL "" FORCE)

# ---- xxHash (extremely fast non-cryptographic hash, BSD-2-Clause) ----
if(NOT TARGET xxhash)
    FetchContent_Declare(
        xxhash
        GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
        GIT_TAG        v0.8.3
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  cmake_unofficial
    )
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(XXHASH_BUILD_XXHSUM OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(xxhash)
    target_include_directories(xxhash PUBLIC
        $<BUILD_INTERFACE:${xxhash_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/vendor/xxhash>
    )
    # Suppress vendor warnings
    target_compile_options(xxhash PRIVATE
        $<$<C_COMPILER_ID:MSVC>:/W0>
        $<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>
    )
endif()

# ---- LZ4 (fast compression, BSD-2-Clause) ----
if(NOT TARGET lz4_static)
    FetchContent_Declare(
        lz4
        GIT_REPOSITORY https://github.com/lz4/lz4.git
        GIT_TAG        v1.10.0
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  build/cmake
    )
    set(LZ4_BUILD_CLI   OFF CACHE BOOL "" FORCE)
    set(LZ4_BUILD_LEGACY_LZ4C OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(lz4)
    target_include_directories(lz4_static PUBLIC
        $<BUILD_INTERFACE:${lz4_SOURCE_DIR}/lib>
        $<INSTALL_INTERFACE:include/vendor/lz4>
    )
    # Suppress vendor warnings
    target_compile_options(lz4_static PRIVATE
        $<$<C_COMPILER_ID:MSVC>:/W0>
        $<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>
    )
endif()

# ---- bc7enc (BC7 texture block encoder, MIT / public domain) ----
if(NOT TARGET bc7enc)
    add_library(bc7enc STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/bc7enc/bc7enc.c
    )
    target_include_directories(bc7enc PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/bc7enc>
        $<INSTALL_INTERFACE:include/vendor/bc7enc>
    )
    set_target_properties(bc7enc PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
    )
    # Suppress vendor warnings
    target_compile_options(bc7enc PRIVATE
        $<$<C_COMPILER_ID:MSVC>:/W0>
        $<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>
    )
endif()

# ---- glslang (GLSL → SPIR-V reference compiler, Khronos) ----
# Gated behind HORO_ENGINE_ENABLE_SHADER_COOKER so CI / headless builds
# can skip the large glslang+spirv-tools dependency chain.
if(HORO_ENGINE_ENABLE_SHADER_COOKER AND NOT TARGET glslang)
    FetchContent_Declare(
        glslang
        GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
        GIT_TAG        15.1.0
        GIT_SUBMODULES_RECURSE ON
    )
    set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
    set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(ENABLE_SPIRV_TOOLS_INSTALL OFF CACHE BOOL "" FORCE)
    set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
    set(SKIP_GLSLANG_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glslang)

    # Suppress vendor warnings so engine -Wall -Wextra -Wpedantic flags
    # do not fail on third-party code.
    if(TARGET glslang)
        target_compile_options(glslang PRIVATE
            $<$<CXX_COMPILER_ID:MSVC>:/WX->
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>
        )
    endif()
    if(TARGET SPIRV)
        target_compile_options(SPIRV PRIVATE
            $<$<CXX_COMPILER_ID:MSVC>:/WX->
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>
        )
    endif()
    if(TARGET glslang-default-resource-limits)
        target_compile_options(glslang-default-resource-limits PRIVATE
            $<$<CXX_COMPILER_ID:MSVC>:/WX->
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>
        )
    endif()
endif()
