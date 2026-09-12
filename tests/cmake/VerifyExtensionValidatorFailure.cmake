execute_process(
    COMMAND "${VALIDATOR}" --json "${MANIFEST}"
    RESULT_VARIABLE validator_result
    OUTPUT_VARIABLE validator_output
    ERROR_VARIABLE validator_error)
if(NOT validator_result EQUAL 1)
    message(FATAL_ERROR
        "Validator returned ${validator_result}, expected 1: ${validator_error}")
endif()
set(expected
    "\"path\":\"$.futureAuthority\",\"code\":\"extension.manifest.unknown_field\"")
string(FIND "${validator_output}" "${expected}" expected_index)
if(expected_index EQUAL -1)
    message(FATAL_ERROR "Validator output omitted the exact path or code: ${validator_output}")
endif()
