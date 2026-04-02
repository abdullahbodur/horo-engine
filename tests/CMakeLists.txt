include(FetchContent)

# ---- Catch2 (header-only test framework) ----
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.6.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

# ---- test_math ----
add_executable(test_math test_math.cpp)
target_link_libraries(test_math PRIVATE MonolithEngine Catch2::Catch2WithMain)
add_test(NAME test_math COMMAND test_math)

# ---- test_physics ----
add_executable(test_physics test_physics.cpp)
target_link_libraries(test_physics PRIVATE MonolithEngine Catch2::Catch2WithMain)
add_test(NAME test_physics COMMAND test_physics)

# ---- test_math_extended ----
add_executable(test_math_extended test_math_extended.cpp)
target_link_libraries(test_math_extended PRIVATE MonolithEngine Catch2::Catch2WithMain)
add_test(NAME test_math_extended COMMAND test_math_extended)

# ---- test_physics_extended ----
add_executable(test_physics_extended test_physics_extended.cpp)
target_link_libraries(test_physics_extended PRIVATE MonolithEngine Catch2::Catch2WithMain)
add_test(NAME test_physics_extended COMMAND test_physics_extended)

# ---- test_ecs ----
add_executable(test_ecs test_ecs.cpp)
target_link_libraries(test_ecs PRIVATE MonolithEngine Catch2::Catch2WithMain)
add_test(NAME test_ecs COMMAND test_ecs)

# ---- test_gjk ----
add_executable(test_gjk test_gjk.cpp)
target_link_libraries(test_gjk PRIVATE MonolithEngine Catch2::Catch2WithMain)
add_test(NAME test_gjk COMMAND test_gjk)

# ---- test_integration ----
add_executable(test_integration test_integration.cpp)
target_link_libraries(test_integration PRIVATE MonolithEngine Catch2::Catch2WithMain)
add_test(NAME test_integration COMMAND test_integration)

# ---- test_constraints ----
add_executable(test_constraints test_constraints.cpp)
target_link_libraries(test_constraints PRIVATE MonolithEngine Catch2::Catch2WithMain)
add_test(NAME test_constraints COMMAND test_constraints)

# ---- test_physics_world ----
add_executable(test_physics_world test_physics_world.cpp)
target_link_libraries(test_physics_world PRIVATE MonolithEngine Catch2::Catch2WithMain)
add_test(NAME test_physics_world COMMAND test_physics_world)

# ---- test_scene ----
add_executable(test_scene test_scene.cpp)
target_link_libraries(test_scene PRIVATE MonolithEngine Catch2::Catch2WithMain)
add_test(NAME test_scene COMMAND test_scene)

# ---- test_camera ----
add_executable(test_camera test_camera.cpp)
target_link_libraries(test_camera PRIVATE MonolithEngine Catch2::Catch2WithMain)
add_test(NAME test_camera COMMAND test_camera)

# ---- test_math_coverage ----
add_executable(test_math_coverage test_math_coverage.cpp)
target_link_libraries(test_math_coverage PRIVATE MonolithEngine Catch2::Catch2WithMain)
add_test(NAME test_math_coverage COMMAND test_math_coverage)

# ---- test_physics_coverage ----
add_executable(test_physics_coverage test_physics_coverage.cpp)
target_link_libraries(test_physics_coverage PRIVATE MonolithEngine Catch2::Catch2WithMain)
add_test(NAME test_physics_coverage COMMAND test_physics_coverage)

# ---- test_editor ----
add_executable(test_editor test_editor.cpp)
target_link_libraries(test_editor PRIVATE MonolithEngine Catch2::Catch2WithMain)
add_test(NAME test_editor COMMAND test_editor)

# ---- test_transform ----
add_executable(test_transform test_transform.cpp)
target_link_libraries(test_transform PRIVATE MonolithEngine Catch2::Catch2WithMain)
add_test(NAME test_transform COMMAND test_transform)

# ---- test_physics_deep ----
add_executable(test_physics_deep test_physics_deep.cpp)
target_link_libraries(test_physics_deep PRIVATE MonolithEngine Catch2::Catch2WithMain)
add_test(NAME test_physics_deep COMMAND test_physics_deep)

# ---- test_registry_extended ----
add_executable(test_registry_extended test_registry_extended.cpp)
target_link_libraries(test_registry_extended PRIVATE MonolithEngine Catch2::Catch2WithMain)
add_test(NAME test_registry_extended COMMAND test_registry_extended)

# ---- test_camera_extended ----
add_executable(test_camera_extended test_camera_extended.cpp)
target_link_libraries(test_camera_extended PRIVATE MonolithEngine Catch2::Catch2WithMain)
add_test(NAME test_camera_extended COMMAND test_camera_extended)

# ---- test_core ----
add_executable(test_core test_core.cpp)
target_link_libraries(test_core PRIVATE MonolithEngine Catch2::Catch2WithMain)
add_test(NAME test_core COMMAND test_core)

# ---- test_scene_systems ----
add_executable(test_scene_systems test_scene_systems.cpp)
target_link_libraries(test_scene_systems PRIVATE MonolithEngine Catch2::Catch2WithMain)
add_test(NAME test_scene_systems COMMAND test_scene_systems)

# ---- test_gjk_extended ----
add_executable(test_gjk_extended test_gjk_extended.cpp)
target_link_libraries(test_gjk_extended PRIVATE MonolithEngine Catch2::Catch2WithMain)
add_test(NAME test_gjk_extended COMMAND test_gjk_extended)

# ---- test_quaternion_extended ----
add_executable(test_quaternion_extended test_quaternion_extended.cpp)
target_link_libraries(test_quaternion_extended PRIVATE MonolithEngine Catch2::Catch2WithMain)
add_test(NAME test_quaternion_extended COMMAND test_quaternion_extended)

# ---- test_engine_config ----
add_executable(test_engine_config test_engine_config.cpp)
target_link_libraries(test_engine_config PRIVATE MonolithEngine Catch2::Catch2WithMain)
add_test(NAME test_engine_config COMMAND test_engine_config)
