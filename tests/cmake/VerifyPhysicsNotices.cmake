execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${HORO_BINARY_DIR}" --config "${HORO_CONFIG}"
        --component PhysicsNotices --prefix "${HORO_BINARY_DIR}/tests/physics-notice-install"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Physics notice installation failed: ${output}\n${errors}")
endif()
set(notice_dir "${HORO_BINARY_DIR}/tests/physics-notice-install/${HORO_DATA_DIR}/horo-engine/licenses")
file(SHA256 "${notice_dir}/JoltPhysics-5.6.0-LICENSE" license_digest)
if(NOT license_digest STREQUAL "800abe35d64ad9defd636ff1ee8c961e06f0ebca3ef8d10083e8aa0e8ef86ac3")
    message(FATAL_ERROR "Installed Physics license is not the reviewed upstream license")
endif()
file(READ "${notice_dir}/JoltPhysics.txt" metadata)
foreach(required IN ITEMS "Version: 5.6.0" "License: MIT"
        "Commit: e77f175595e64cb44218cc9d9d56fc365ad0e36a"
        "Archive-SHA256: 1f32328fb763135de10a244568d6ccb2ed9b1e6593fafe6dc6db5b2719d330bd")
    string(FIND "${metadata}" "${required}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Installed Physics metadata is missing ${required}")
    endif()
endforeach()
