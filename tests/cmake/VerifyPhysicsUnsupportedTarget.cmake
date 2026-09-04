if(HORO_PROBE)
    include("${HORO_SOURCE_DIR}/cmake/HoroPhysicsDependency.cmake")
    horo_add_canonical_physics_dependency()
    message(FATAL_ERROR "Unsupported target unexpectedly reached dependency acquisition")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}"
    -DHORO_PROBE=ON -DHORO_SOURCE_DIR=${HORO_SOURCE_DIR}
    -DCMAKE_SYSTEM_NAME=${HORO_SYSTEM} -DCMAKE_SYSTEM_PROCESSOR=${HORO_PROCESSOR}
    -DCMAKE_OSX_DEPLOYMENT_TARGET=${HORO_DEPLOYMENT}
    -P "${CMAKE_CURRENT_LIST_FILE}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
string(FIND "${output}\n${errors}" "${HORO_EXPECTED_ERROR}" error_position)
if(result EQUAL 0 OR error_position EQUAL -1)
    message(FATAL_ERROR "Missing expected unsupported-target rejection: ${output}\n${errors}")
endif()
