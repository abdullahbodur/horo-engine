include_guard(GLOBAL)

set(HORO_RECAST_NAVIGATION_REVISION "9f4ce64458dfae86e1239c525ddc219c4e9e06f1")

# Populate only the reviewed Detour runtime sources. The upstream top-level build
# also creates bake, crowd, tile-cache, debug, demo, and example targets, which
# are deliberately outside the initial runtime-only provider composition.
function(horo_add_navigation_runtime_dependency)
    FetchContent_Declare(horo_recast_navigation
        URL https://codeload.github.com/recastnavigation/recastnavigation/tar.gz/${HORO_RECAST_NAVIGATION_REVISION}
        URL_HASH SHA256=7dfd8cffc2e2a725b40cfc0ebf89686502ddbfd24619a5481acdf52255618ec2
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR horo-no-upstream-cmake-entrypoint
    )
    FetchContent_MakeAvailable(horo_recast_navigation)

    add_library(HoroThirdPartyDetour STATIC
        ${horo_recast_navigation_SOURCE_DIR}/Detour/Source/DetourAlloc.cpp
        ${horo_recast_navigation_SOURCE_DIR}/Detour/Source/DetourAssert.cpp
        ${horo_recast_navigation_SOURCE_DIR}/Detour/Source/DetourCommon.cpp
        ${horo_recast_navigation_SOURCE_DIR}/Detour/Source/DetourNavMesh.cpp
        ${horo_recast_navigation_SOURCE_DIR}/Detour/Source/DetourNavMeshBuilder.cpp
        ${horo_recast_navigation_SOURCE_DIR}/Detour/Source/DetourNavMeshQuery.cpp
        ${horo_recast_navigation_SOURCE_DIR}/Detour/Source/DetourNode.cpp
    )
    add_library(HoroThirdParty::Detour ALIAS HoroThirdPartyDetour)
    target_compile_definitions(HoroThirdPartyDetour PUBLIC DT_POLYREF64 RC_DISABLE_ASSERTS)
    target_include_directories(HoroThirdPartyDetour PUBLIC ${horo_recast_navigation_SOURCE_DIR}/Detour/Include)
    set_target_properties(HoroThirdPartyDetour PROPERTIES POSITION_INDEPENDENT_CODE ON)

    file(SHA256 "${horo_recast_navigation_SOURCE_DIR}/License.txt" horo_recast_navigation_license_digest)
    if(NOT horo_recast_navigation_license_digest STREQUAL "ffad87821cce7cfd3be00b63ffadd01b04e83a4c6a0bcc68f06362ffd42d705e")
        message(FATAL_ERROR "Recast Navigation license differs from the reviewed source")
    endif()
    install(FILES "${horo_recast_navigation_SOURCE_DIR}/License.txt"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/horo-engine/licenses"
        RENAME RecastNavigation-9f4ce644-LICENSE COMPONENT NavigationNotices)
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/third-party/RecastNavigation.txt" CONTENT
"Name: Recast Navigation
License: zlib
Source: https://github.com/recastnavigation/recastnavigation
Commit: 9f4ce64458dfae86e1239c525ddc219c4e9e06f1
Archive-SHA256: 7dfd8cffc2e2a725b40cfc0ebf89686502ddbfd24619a5481acdf52255618ec2
License-SHA256: ffad87821cce7cfd3be00b63ffadd01b04e83a4c6a0bcc68f06362ffd42d705e
License-File: RecastNavigation-9f4ce644-LICENSE
")
    install(FILES "${CMAKE_BINARY_DIR}/third-party/RecastNavigation.txt"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/horo-engine/licenses" COMPONENT NavigationNotices)
endfunction()
