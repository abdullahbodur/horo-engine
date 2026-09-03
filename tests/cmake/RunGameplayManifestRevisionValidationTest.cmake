if(NOT DEFINED HORO_ENGINE_SOURCE_DIR OR NOT DEFINED HORO_TEST_BINARY_DIR)
    message(FATAL_ERROR "Gameplay manifest validation test inputs are incomplete")
endif()

set(_writer "${HORO_ENGINE_SOURCE_DIR}/cmake/WriteGameplayModuleManifest.cmake")
set(_manifest "${HORO_TEST_BINARY_DIR}/gameplay_module.json")
set(_revision "${HORO_TEST_BINARY_DIR}/descriptor.revision")
file(MAKE_DIRECTORY "${HORO_TEST_BINARY_DIR}")
file(REMOVE "${_manifest}" "${_revision}")

function(expect_manifest_failure label)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DMANIFEST_OUTPUT=${_manifest}"
            "-DMODULE_ID=game.tests"
            "-DBUILD_FINGERPRINT=test-fingerprint"
            "-DMODULE_PATH=${HORO_TEST_BINARY_DIR}/module"
            "-DDESCRIPTOR_REVISION_FILE=${_revision}"
            -P "${_writer}"
        RESULT_VARIABLE _result
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(_result EQUAL 0 OR EXISTS "${_manifest}")
        message(FATAL_ERROR "Manifest publication accepted ${label}")
    endif()
endfunction()

expect_manifest_failure("a missing descriptor revision")
foreach(_invalid IN ITEMS "" "0" "invalid" "18446744073709551616")
    file(WRITE "${_revision}" "${_invalid}")
    expect_manifest_failure("invalid descriptor revision '${_invalid}'")
endforeach()
