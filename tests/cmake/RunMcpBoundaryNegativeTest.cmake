if(NOT HORO_ENGINE_SOURCE_DIR OR NOT HORO_TEST_BINARY_DIR)
    message(FATAL_ERROR "HORO_ENGINE_SOURCE_DIR and HORO_TEST_BINARY_DIR are required")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -S "${HORO_ENGINE_SOURCE_DIR}/tests/cmake/mcp_boundary"
        -B "${HORO_TEST_BINARY_DIR}"
        "-DHORO_ENGINE_SOURCE_DIR=${HORO_ENGINE_SOURCE_DIR}"
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error)

set(combined_output "${configure_output}\n${configure_error}")
if(configure_result EQUAL 0)
    message(FATAL_ERROR
        "Representative domain-to-MCP dependency unexpectedly configured successfully:\n${combined_output}")
endif()

foreach(required_text
        "MCP boundary violation:"
        "HoroApplication"
        "HoroMcpController")
    string(FIND "${combined_output}" "${required_text}" diagnostic_position)
    if(diagnostic_position EQUAL -1)
        message(FATAL_ERROR
            "MCP boundary rejection omitted '${required_text}':\n${combined_output}")
    endif()
endforeach()
