include(FetchContent)

# ---- Catch2 (header-only test framework) ----
FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.6.0
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Catch2)

# Unit tests live under build/bin/tests/ so build/bin/ only holds MonolithApp (+ copied assets).
set(MONOLITH_TEST_BIN_DIR "${CMAKE_BINARY_DIR}/bin/tests")
file(MAKE_DIRECTORY "${MONOLITH_TEST_BIN_DIR}")
function(monolith_set_test_output_dir target_name)
    set_target_properties(${target_name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${MONOLITH_TEST_BIN_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG "${MONOLITH_TEST_BIN_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${MONOLITH_TEST_BIN_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${MONOLITH_TEST_BIN_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${MONOLITH_TEST_BIN_DIR}")
endfunction()

# ---- test_math ----
add_executable(test_math test_math.cpp)
target_link_libraries(test_math PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_math)
add_test(NAME test_math COMMAND $<TARGET_FILE:test_math>)

# ---- test_physics ----
add_executable(test_physics test_physics.cpp)
target_link_libraries(test_physics PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_physics)
add_test(NAME test_physics COMMAND $<TARGET_FILE:test_physics>)

# ---- test_math_extended ----
add_executable(test_math_extended test_math_extended.cpp)
target_link_libraries(test_math_extended PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_math_extended)
add_test(NAME test_math_extended COMMAND $<TARGET_FILE:test_math_extended>)

# ---- test_physics_extended ----
add_executable(test_physics_extended test_physics_extended.cpp)
target_link_libraries(test_physics_extended PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_physics_extended)
add_test(NAME test_physics_extended COMMAND $<TARGET_FILE:test_physics_extended>)

# ---- test_ecs ----
add_executable(test_ecs test_ecs.cpp)
target_link_libraries(test_ecs PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_ecs)
add_test(NAME test_ecs COMMAND $<TARGET_FILE:test_ecs>)

# ---- test_gjk ----
add_executable(test_gjk test_gjk.cpp)
target_link_libraries(test_gjk PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_gjk)
add_test(NAME test_gjk COMMAND $<TARGET_FILE:test_gjk>)

# ---- test_integration ----
add_executable(test_integration test_integration.cpp)
target_link_libraries(test_integration PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_integration)
add_test(NAME test_integration COMMAND $<TARGET_FILE:test_integration>)

# ---- test_constraints ----
add_executable(test_constraints test_constraints.cpp)
target_link_libraries(test_constraints PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_constraints)
add_test(NAME test_constraints COMMAND $<TARGET_FILE:test_constraints>)

# ---- test_physics_world ----
add_executable(test_physics_world test_physics_world.cpp)
target_link_libraries(test_physics_world PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_physics_world)
add_test(NAME test_physics_world COMMAND $<TARGET_FILE:test_physics_world>)

# ---- test_scene ----
add_executable(test_scene test_scene.cpp)
target_link_libraries(test_scene PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_scene)
add_test(NAME test_scene COMMAND $<TARGET_FILE:test_scene>)

# ---- test_scene_project_model ----
add_executable(test_scene_project_model test_scene_project_model.cpp)
target_link_libraries(test_scene_project_model PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_scene_project_model)
add_test(NAME test_scene_project_model COMMAND $<TARGET_FILE:test_scene_project_model>)

# ---- test_scene_runtime_conversion ----
add_executable(test_scene_runtime_conversion test_scene_runtime_conversion.cpp)
target_link_libraries(test_scene_runtime_conversion PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_scene_runtime_conversion)
add_test(NAME test_scene_runtime_conversion COMMAND $<TARGET_FILE:test_scene_runtime_conversion>)

# ---- test_scene_runtime_coordinator ----
add_executable(test_scene_runtime_coordinator test_scene_runtime_coordinator.cpp)
target_link_libraries(test_scene_runtime_coordinator PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_scene_runtime_coordinator)
add_test(NAME test_scene_runtime_coordinator COMMAND $<TARGET_FILE:test_scene_runtime_coordinator>)

# ---- test_scene_reference_loading ----
add_executable(test_scene_reference_loading test_scene_reference_loading.cpp)
target_link_libraries(test_scene_reference_loading PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_scene_reference_loading)
add_test(NAME test_scene_reference_loading COMMAND $<TARGET_FILE:test_scene_reference_loading>)

# ---- test_scene_lifecycle ----
add_executable(test_scene_lifecycle test_scene_lifecycle.cpp)
target_link_libraries(test_scene_lifecycle PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_scene_lifecycle)
add_test(NAME test_scene_lifecycle COMMAND $<TARGET_FILE:test_scene_lifecycle>)

# ---- test_camera ----
add_executable(test_camera test_camera.cpp)
target_link_libraries(test_camera PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_camera)
add_test(NAME test_camera COMMAND $<TARGET_FILE:test_camera>)

# ---- test_math_coverage ----
add_executable(test_math_coverage test_math_coverage.cpp)
target_link_libraries(test_math_coverage PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_math_coverage)
add_test(NAME test_math_coverage COMMAND $<TARGET_FILE:test_math_coverage>)

# ---- test_physics_coverage ----
add_executable(test_physics_coverage test_physics_coverage.cpp)
target_link_libraries(test_physics_coverage PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_physics_coverage)
add_test(NAME test_physics_coverage COMMAND $<TARGET_FILE:test_physics_coverage>)

# ---- test_editor ----
add_executable(test_editor test_editor.cpp)
target_link_libraries(test_editor PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_editor)
add_test(NAME test_editor COMMAND $<TARGET_FILE:test_editor>)

# ---- test_transform ----
add_executable(test_transform test_transform.cpp)
target_link_libraries(test_transform PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_transform)
add_test(NAME test_transform COMMAND $<TARGET_FILE:test_transform>)

# ---- test_physics_deep ----
add_executable(test_physics_deep test_physics_deep.cpp)
target_link_libraries(test_physics_deep PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_physics_deep)
add_test(NAME test_physics_deep COMMAND $<TARGET_FILE:test_physics_deep>)

# ---- test_registry_extended ----
add_executable(test_registry_extended test_registry_extended.cpp)
target_link_libraries(test_registry_extended PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_registry_extended)
add_test(NAME test_registry_extended COMMAND $<TARGET_FILE:test_registry_extended>)

# ---- test_camera_extended ----
add_executable(test_camera_extended test_camera_extended.cpp)
target_link_libraries(test_camera_extended PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_camera_extended)
add_test(NAME test_camera_extended COMMAND $<TARGET_FILE:test_camera_extended>)

# ---- test_core ----
add_executable(test_core test_core.cpp)
target_link_libraries(test_core PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_core)
add_test(NAME test_core COMMAND $<TARGET_FILE:test_core>)

# ---- test_window ----
add_executable(test_window test_window.cpp)
target_link_libraries(test_window PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_window)
add_test(NAME test_window COMMAND $<TARGET_FILE:test_window>)
add_test(NAME test_window_logfilter COMMAND $<TARGET_FILE:test_window> "[core][window][logfilter]")
set_tests_properties(test_window_logfilter PROPERTIES
        ENVIRONMENT "MONOLITH_LOG_LEVEL=error;MONOLITH_GLFW_VISIBLE=0;MONOLITH_GLFW_SAMPLES=0")
add_test(NAME test_window_glfw_init_fail COMMAND $<TARGET_FILE:test_window> "[core][window][glfw-init-fail]")
set_tests_properties(test_window_glfw_init_fail PROPERTIES
        ENVIRONMENT "DISPLAY=;WAYLAND_DISPLAY=;MONOLITH_GLFW_VISIBLE=0;MONOLITH_GLFW_SAMPLES=0")

# ---- test_logger_env ----
add_executable(test_logger_env test_logger_env.cpp)
target_link_libraries(test_logger_env PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_logger_env)
add_test(NAME test_logger_env_warn COMMAND $<TARGET_FILE:test_logger_env> "[logger][env][warn]")
set_tests_properties(test_logger_env_warn PROPERTIES ENVIRONMENT "MONOLITH_LOG_LEVEL=warning")
add_test(NAME test_logger_env_error COMMAND $<TARGET_FILE:test_logger_env> "[logger][env][error]")
set_tests_properties(test_logger_env_error PROPERTIES ENVIRONMENT "MONOLITH_LOG_LEVEL=error")
add_test(NAME test_logger_env_invalid COMMAND $<TARGET_FILE:test_logger_env> "[logger][env][invalid]")
set_tests_properties(test_logger_env_invalid PROPERTIES ENVIRONMENT "MONOLITH_LOG_LEVEL=verbose")
add_test(NAME test_logger_env_debug COMMAND $<TARGET_FILE:test_logger_env> "[logger][env][debug]")
set_tests_properties(test_logger_env_debug PROPERTIES ENVIRONMENT "MONOLITH_LOG_LEVEL=debug")
add_test(NAME test_logger_env_empty COMMAND $<TARGET_FILE:test_logger_env> "[logger][env][empty]")
set_tests_properties(test_logger_env_empty PROPERTIES ENVIRONMENT "MONOLITH_LOG_LEVEL=")
add_test(NAME test_logger_env_mixed_debug COMMAND $<TARGET_FILE:test_logger_env> "[logger][env][mixed_debug]")
set_tests_properties(test_logger_env_mixed_debug PROPERTIES ENVIRONMENT "MONOLITH_LOG_LEVEL=DeBuG")
add_test(NAME test_logger_env_warn_alias COMMAND $<TARGET_FILE:test_logger_env> "[logger][env][warn_alias]")
set_tests_properties(test_logger_env_warn_alias PROPERTIES ENVIRONMENT "MONOLITH_LOG_LEVEL=warn")
add_test(NAME test_logger_env_unknown_level COMMAND $<TARGET_FILE:test_logger_env> "[logger][env][unknown_level]")
set_tests_properties(test_logger_env_unknown_level PROPERTIES ENVIRONMENT "MONOLITH_LOG_LEVEL=info")
add_test(NAME test_logger_env_unset COMMAND $<TARGET_FILE:test_logger_env> "[logger][env][unset]")
set_tests_properties(test_logger_env_unset PROPERTIES ENVIRONMENT "MONOLITH_LOG_LEVEL=debug")

# ---- test_scene_systems ----
add_executable(test_scene_systems test_scene_systems.cpp)
target_link_libraries(test_scene_systems PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_scene_systems)
add_test(NAME test_scene_systems COMMAND $<TARGET_FILE:test_scene_systems>)

# ---- test_gjk_extended ----
add_executable(test_gjk_extended test_gjk_extended.cpp)
target_link_libraries(test_gjk_extended PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_gjk_extended)
add_test(NAME test_gjk_extended COMMAND $<TARGET_FILE:test_gjk_extended>)

# ---- test_quaternion_extended ----
add_executable(test_quaternion_extended test_quaternion_extended.cpp)
target_link_libraries(test_quaternion_extended PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_quaternion_extended)
add_test(NAME test_quaternion_extended COMMAND $<TARGET_FILE:test_quaternion_extended>)

# ---- test_obj_import ----
add_executable(test_obj_import test_obj_import.cpp)
target_link_libraries(test_obj_import PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_obj_import)
add_test(NAME test_obj_import COMMAND $<TARGET_FILE:test_obj_import>)

# ---- test_gizmo ----
add_executable(test_gizmo test_gizmo.cpp)
target_link_libraries(test_gizmo PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_gizmo)
add_test(NAME test_gizmo COMMAND $<TARGET_FILE:test_gizmo>)

# ---- test_animation ----
add_executable(test_animation test_animation.cpp)
target_link_libraries(test_animation PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_animation)
add_test(NAME test_animation COMMAND $<TARGET_FILE:test_animation>)

# ---- test_plan_editor_features (CLI, LogBuffer, Project filter) ----
add_executable(test_plan_editor_features test_plan_editor_features.cpp)
target_link_libraries(test_plan_editor_features PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_plan_editor_features)
add_test(NAME test_plan_editor_features COMMAND $<TARGET_FILE:test_plan_editor_features>)

# ---- test_mcp ----
add_executable(test_mcp test_mcp.cpp)
target_link_libraries(test_mcp PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_mcp)
add_test(NAME test_mcp COMMAND $<TARGET_FILE:test_mcp>)

# ---- test_architecture_docs ----
add_executable(test_architecture_docs test_architecture_docs.cpp)
target_link_libraries(test_architecture_docs PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_architecture_docs)
add_test(NAME test_architecture_docs COMMAND $<TARGET_FILE:test_architecture_docs>)

# ---- test_renderer_foundation ----
add_executable(test_renderer_foundation test_renderer_foundation.cpp)
target_link_libraries(test_renderer_foundation PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_renderer_foundation)
add_test(NAME test_renderer_foundation COMMAND $<TARGET_FILE:test_renderer_foundation>)

# ---- test_launcher_core ----
add_executable(test_launcher_core test_launcher_core.cpp)
target_link_libraries(test_launcher_core PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_launcher_core)
add_test(NAME test_launcher_core COMMAND $<TARGET_FILE:test_launcher_core>)

# ---- test_launcher_unit ----
add_executable(test_launcher_unit test_launcher_unit.cpp)
target_link_libraries(test_launcher_unit PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_launcher_unit)
add_test(NAME test_launcher_unit COMMAND $<TARGET_FILE:test_launcher_unit>)

# ---- test_ui_automation_config ----
add_executable(test_ui_automation_config test_ui_automation_config.cpp)
target_link_libraries(test_ui_automation_config PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_ui_automation_config)
add_test(NAME test_ui_automation_config COMMAND $<TARGET_FILE:test_ui_automation_config>)

# ---- test_input ----
add_executable(test_input test_input.cpp)
target_link_libraries(test_input PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_input)
add_test(NAME test_input COMMAND $<TARGET_FILE:test_input>)

# ---- test_mesh_cache ----
add_executable(test_mesh_cache test_mesh_cache.cpp)
target_link_libraries(test_mesh_cache PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_mesh_cache)
add_test(NAME test_mesh_cache COMMAND $<TARGET_FILE:test_mesh_cache>)

# ---- test_editor_unit ----
add_executable(test_editor_unit test_editor_unit.cpp)
target_link_libraries(test_editor_unit PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_editor_unit)
add_test(NAME test_editor_unit COMMAND $<TARGET_FILE:test_editor_unit>)

# ---- test_renderer_unit ----
add_executable(test_renderer_unit test_renderer_unit.cpp)
target_link_libraries(test_renderer_unit PRIVATE MonolithEngine Catch2::Catch2WithMain)
monolith_set_test_output_dir(test_renderer_unit)
add_test(NAME test_renderer_unit COMMAND $<TARGET_FILE:test_renderer_unit>)
