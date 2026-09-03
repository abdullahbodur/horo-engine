if(NOT DEFINED MANIFEST_OUTPUT OR NOT DEFINED MODULE_ID OR
   NOT DEFINED BUILD_FINGERPRINT OR NOT DEFINED MODULE_PATH OR
   NOT DEFINED DESCRIPTOR_REVISION_FILE)
    message(FATAL_ERROR "Gameplay module manifest inputs are incomplete")
endif()

if(NOT EXISTS "${DESCRIPTOR_REVISION_FILE}")
    message(FATAL_ERROR "Gameplay module descriptor revision is missing")
endif()
file(READ "${DESCRIPTOR_REVISION_FILE}" DESCRIPTOR_REVISION LIMIT 32)
string(STRIP "${DESCRIPTOR_REVISION}" DESCRIPTOR_REVISION)
string(LENGTH "${DESCRIPTOR_REVISION}" _descriptor_revision_length)
if(NOT DESCRIPTOR_REVISION MATCHES "^[0-9]+$" OR DESCRIPTOR_REVISION STREQUAL "0" OR
   _descriptor_revision_length GREATER 20 OR
   (_descriptor_revision_length EQUAL 20 AND DESCRIPTOR_REVISION STRGREATER "18446744073709551615"))
    message(FATAL_ERROR "Gameplay module descriptor revision is invalid")
endif()

get_filename_component(_manifest_directory "${MANIFEST_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_manifest_directory}")

string(REPLACE "\\" "\\\\" _module_path "${MODULE_PATH}")
string(REPLACE "\"" "\\\"" _module_path "${_module_path}")
string(REPLACE "\\" "\\\\" _module_id "${MODULE_ID}")
string(REPLACE "\"" "\\\"" _module_id "${_module_id}")
string(REPLACE "\\" "\\\\" _fingerprint "${BUILD_FINGERPRINT}")
string(REPLACE "\"" "\\\"" _fingerprint "${_fingerprint}")

set(_temporary "${MANIFEST_OUTPUT}.tmp")
file(WRITE "${_temporary}"
"{\n  \"schemaVersion\": 1,\n  \"moduleId\": \"${_module_id}\",\n  \"buildFingerprint\": \"${_fingerprint}\",\n  \"descriptorRevision\": ${DESCRIPTOR_REVISION},\n  \"artifactPath\": \"${_module_path}\"\n}\n")
file(RENAME "${_temporary}" "${MANIFEST_OUTPUT}")
