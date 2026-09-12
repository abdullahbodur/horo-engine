include_guard(GLOBAL)

function(horo_configure_extension_sdk)
    set(package_root
        "${CMAKE_BINARY_DIR}/sdk/extensions/${HORO_EXTENSION_SDK_VERSION}")
    set(package_cmake_dir
        "${package_root}/lib/cmake/HoroEngineExtensionSdk")

    file(MAKE_DIRECTORY
        "${package_root}/include/Horo/Extensions"
        "${package_root}/share/horo/extension-sdk"
        "${package_cmake_dir}")

    configure_file(
        "${PROJECT_SOURCE_DIR}/include/Horo/Extensions/ExtensionAbi.h"
        "${package_root}/include/Horo/Extensions/ExtensionAbi.h"
        COPYONLY)
    configure_file(
        "${PROJECT_SOURCE_DIR}/LICENSE"
        "${package_root}/LICENSE"
        COPYONLY)
    configure_file(
        "${PROJECT_SOURCE_DIR}/sdk/extension-sdk.json.in"
        "${package_root}/share/horo/extension-sdk/extension-sdk.json"
        @ONLY)
    configure_file(
        "${PROJECT_SOURCE_DIR}/sdk/ExtensionSdkREADME.md.in"
        "${package_root}/README.md"
        @ONLY)

    configure_package_config_file(
        "${PROJECT_SOURCE_DIR}/sdk/cmake/HoroEngineExtensionSdkConfig.cmake.in"
        "${package_cmake_dir}/HoroEngineExtensionSdkConfig.cmake"
        INSTALL_DESTINATION "lib/cmake/HoroEngineExtensionSdk"
        NO_SET_AND_CHECK_MACRO)
    write_basic_package_version_file(
        "${package_cmake_dir}/HoroEngineExtensionSdkConfigVersion.cmake"
        VERSION "${HORO_EXTENSION_SDK_VERSION}"
        COMPATIBILITY SameMajorVersion
        ARCH_INDEPENDENT)

    set(HORO_EXTENSION_SDK_PACKAGE_DIR "${package_root}" CACHE INTERNAL
        "Self-contained extension C ABI SDK package")

    install(DIRECTORY "${package_root}/"
        DESTINATION
            "${CMAKE_INSTALL_DATADIR}/horo/sdk/extensions/${HORO_EXTENSION_SDK_VERSION}"
        COMPONENT HoroExtensionSdk)
endfunction()
