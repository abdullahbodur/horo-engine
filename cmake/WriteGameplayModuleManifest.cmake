if(NOT DEFINED MANIFEST_OUTPUT OR NOT DEFINED MODULE_ID OR
   NOT DEFINED BUILD_FINGERPRINT OR NOT DEFINED MODULE_PATH OR
   NOT DEFINED DESCRIPTOR_REVISION)
    message(FATAL_ERROR "Gameplay module manifest inputs are incomplete")
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
