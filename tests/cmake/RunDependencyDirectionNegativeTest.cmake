if(NOT HORO_ENGINE_SOURCE_DIR OR NOT HORO_TEST_BINARY_DIR)
    message(FATAL_ERROR "HORO_ENGINE_SOURCE_DIR and HORO_TEST_BINARY_DIR are required")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -S "${HORO_ENGINE_SOURCE_DIR}/tests/cmake/dependency_direction"
        -B "${HORO_TEST_BINARY_DIR}"
        "-DHORO_ENGINE_SOURCE_DIR=${HORO_ENGINE_SOURCE_DIR}"
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error)

set(combined_output "${configure_output}\n${configure_error}")
if(configure_result EQUAL 0)
    message(FATAL_ERROR
        "Representative reverse dependency unexpectedly configured successfully:\n${combined_output}")
endif()

string(FIND "${combined_output}" "Dependency direction violation:" diagnostic_position)
string(FIND "${combined_output}" "HoroFoundation" dependent_position)
string(FIND "${combined_output}" "HoroEditorServices" dependency_position)
if(diagnostic_position EQUAL -1 OR dependent_position EQUAL -1 OR dependency_position EQUAL -1)
    message(FATAL_ERROR
        "Reverse dependency failed without the required actionable diagnostic:\n${combined_output}")
endif()
