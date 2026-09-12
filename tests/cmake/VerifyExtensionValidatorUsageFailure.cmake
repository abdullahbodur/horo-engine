if(NOT DEFINED VALIDATOR OR NOT DEFINED MANIFEST)
    message(FATAL_ERROR "Validator usage test inputs are required")
endif()

execute_process(
    COMMAND "${VALIDATOR}" --schema-version 2 "${MANIFEST}"
    RESULT_VARIABLE validator_result
    OUTPUT_VARIABLE validator_output
    ERROR_VARIABLE validator_error)
if(NOT validator_result EQUAL 2)
    message(FATAL_ERROR
        "Validator returned ${validator_result}, expected exact usage exit 2: ${validator_output}${validator_error}")
endif()
string(FIND "${validator_error}" "Usage: horo-extension-validate" usage_index)
if(usage_index EQUAL -1)
    message(FATAL_ERROR "Validator omitted usage guidance: ${validator_error}")
endif()
