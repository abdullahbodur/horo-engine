if(NOT DEFINED VALIDATOR OR NOT DEFINED TEST_DIRECTORY OR NOT DEFINED ESCAPED_MANIFEST)
    message(FATAL_ERROR "Validator contract test inputs are required")
endif()

file(MAKE_DIRECTORY "${TEST_DIRECTORY}/manifest-directory")
file(REMOVE "${TEST_DIRECTORY}/missing.json")
string(REPEAT "x" 65537 oversized_manifest)
file(WRITE "${TEST_DIRECTORY}/oversized.json" "${oversized_manifest}")

function(require_validator_failure name manifest expected_code)
    execute_process(
        COMMAND "${VALIDATOR}" --json "${manifest}"
        RESULT_VARIABLE validator_result
        OUTPUT_VARIABLE validator_output
        ERROR_VARIABLE validator_error)
    if(NOT validator_result EQUAL 2)
        message(FATAL_ERROR
            "${name}: validator returned ${validator_result}, expected 2: ${validator_error}")
    endif()
    string(FIND "${validator_output}" "\"code\":\"${expected_code}\"" code_index)
    if(code_index EQUAL -1)
        message(FATAL_ERROR "${name}: validator output omitted ${expected_code}: ${validator_output}")
    endif()
endfunction()

require_validator_failure(
    "missing input"
    "${TEST_DIRECTORY}/missing.json"
    "extension.manifest.input_missing")
require_validator_failure(
    "directory input"
    "${TEST_DIRECTORY}/manifest-directory"
    "extension.manifest.input_not_file")
require_validator_failure(
    "oversized input"
    "${TEST_DIRECTORY}/oversized.json"
    "extension.manifest.input_too_large")

if(UNIX)
    file(WRITE "${TEST_DIRECTORY}/unreadable.json" "{}")
    file(CHMOD "${TEST_DIRECTORY}/unreadable.json" PERMISSIONS OWNER_WRITE)
    require_validator_failure(
        "unreadable input"
        "${TEST_DIRECTORY}/unreadable.json"
        "extension.manifest.input_unreadable")
    file(CHMOD "${TEST_DIRECTORY}/unreadable.json" PERMISSIONS OWNER_READ OWNER_WRITE)
endif()

execute_process(
    COMMAND "${VALIDATOR}" --json "${ESCAPED_MANIFEST}"
    RESULT_VARIABLE escaped_result
    OUTPUT_VARIABLE escaped_output
    ERROR_VARIABLE escaped_error)
if(NOT escaped_result EQUAL 1)
    message(FATAL_ERROR
        "escaped diagnostic: validator returned ${escaped_result}, expected 1: ${escaped_error}")
endif()
set(expected_path [=["path":"$.future:authority\"\\\u0001"]=])
string(FIND "${escaped_output}" "${expected_path}" path_index)
if(path_index EQUAL -1)
    message(FATAL_ERROR "escaped diagnostic path was not encoded exactly: ${escaped_output}")
endif()
