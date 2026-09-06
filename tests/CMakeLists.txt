include(Catch)

add_test(NAME HoroDependencyDirectionNegativeConfigureTest
    COMMAND "${CMAKE_COMMAND}"
        "-DHORO_ENGINE_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DHORO_TEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/dependency-direction-negative"
        -P "${PROJECT_SOURCE_DIR}/tests/cmake/RunDependencyDirectionNegativeTest.cmake")
set_tests_properties(HoroDependencyDirectionNegativeConfigureTest PROPERTIES
    LABELS "architecture;cmake")

add_test(NAME HoroGameplayManifestRevisionValidationTest
    COMMAND "${CMAKE_COMMAND}"
        "-DHORO_ENGINE_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DHORO_TEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/gameplay-manifest-validation"
        -P "${PROJECT_SOURCE_DIR}/tests/cmake/RunGameplayManifestRevisionValidationTest.cmake")
set_tests_properties(HoroGameplayManifestRevisionValidationTest PROPERTIES
    LABELS "gameplay;cmake")

# One translation unit per registered public header verifies that consumers can
# compile the contract using only its owning target's usage requirements.
horo_add_public_header_consumer_targets()

execute_process(
        COMMAND "${Python3_EXECUTABLE}" -c "import pytest; print(pytest.__version__)"
        RESULT_VARIABLE HORO_PYTEST_IMPORT_RESULT
        OUTPUT_VARIABLE HORO_PYTEST_VERSION
        ERROR_VARIABLE HORO_PYTEST_IMPORT_ERROR
        OUTPUT_STRIP_TRAILING_WHITESPACE
)
if (NOT HORO_PYTEST_IMPORT_RESULT EQUAL 0)
    message(WARNING
            "BUILD_TESTING: pytest is not available; skipping repository Python tests."
    )
else ()
    message(STATUS "Using pytest ${HORO_PYTEST_VERSION} with ${Python3_EXECUTABLE}")

    add_test(NAME HoroPythonToolingTests
            COMMAND "${Python3_EXECUTABLE}" -m pytest
            "${PROJECT_SOURCE_DIR}/tests/python"
            --quiet
            --basetemp "${CMAKE_CURRENT_BINARY_DIR}/pytest-temp"
    )
    set_tests_properties(HoroPythonToolingTests PROPERTIES
            LABELS "python;tooling"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1;PYTHONUTF8=1"
    )
endif ()

add_compile_definitions(HORO_PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

function(horo_register_catch_test target)
    cmake_parse_arguments(ARG "" "" "LABELS;EXTRA_ARGS;PROPERTIES" ${ARGN})
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE HORO_PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(${target} PRIVATE Catch2::Catch2WithMain)
    catch_discover_tests(${target}
            TEST_PREFIX "${target}::"
            EXTRA_ARGS ${ARG_EXTRA_ARGS}
            PROPERTIES LABELS "${ARG_LABELS}" ${ARG_PROPERTIES}
    )
endfunction()

add_executable(HoroPackageValidationTests
    unit/packages/PackagePathTests.cpp
    unit/packages/PackageArchiveTests.cpp
    unit/packages/PackageFileManifestTests.cpp)
target_link_libraries(HoroPackageValidationTests PRIVATE HoroEngine::Packages nlohmann_json::nlohmann_json miniz)
horo_register_catch_test(HoroPackageValidationTests LABELS "packages;headless")

if (HORO_ENABLE_IMGUI_UI_TESTS)
    if (NOT TARGET HoroThirdPartyImGui)
        message(FATAL_ERROR "HoroEditorUiAutomationTests requires instrumented HoroThirdPartyImGui")
    endif ()
    get_target_property(HORO_IMGUI_TEST_ENGINE_DEFINITIONS HoroThirdPartyImGui COMPILE_DEFINITIONS)
    if (NOT "IMGUI_ENABLE_TEST_ENGINE" IN_LIST HORO_IMGUI_TEST_ENGINE_DEFINITIONS)
        message(FATAL_ERROR "HoroEditorUiAutomationTests requires IMGUI_ENABLE_TEST_ENGINE")
    endif ()

    add_executable(HoroEditorUiAutomationTests
            gui/EditorUiAutomationHarnessTests.cpp
            gui/FullEditorUiAutomationScenarios.cpp
            helpers/editor_ui/EditorUiTestHarness.cpp
            helpers/editor_ui/EditorUiTestSurface.cpp
            helpers/editor_ui/InteractiveEditorUiTestRenderer.cpp
            helpers/editor_ui/FullEditorUiTestActions.cpp
            helpers/editor_ui/FullEditorUiTestHost.cpp
            helpers/editor_ui/FullEditorUiTestSetups.cpp
    )
    target_include_directories(HoroEditorUiAutomationTests PRIVATE
            ${PROJECT_SOURCE_DIR}/tests/helpers/editor_ui
            ${PROJECT_SOURCE_DIR}/src
            ${PROJECT_SOURCE_DIR}/src/editor/screens/workspace
            ${imgui_test_engine_SOURCE_DIR}
            ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine
    )
    target_compile_definitions(HoroEditorUiAutomationTests PRIVATE
            HORO_PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
            HORO_GAMEPLAY_SDK_PACKAGE_DIR="${HORO_GAMEPLAY_SDK_PACKAGE_DIR}"
            HORO_GAMEPLAY_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
    )
    target_link_libraries(HoroEditorUiAutomationTests PRIVATE
            HoroEngine::Gui
            HoroEngine::ProjectMigrations
            HoroEngine::RenderFrontend
            HoroThirdParty::ImGui
            nlohmann_json::nlohmann_json
    )
    if (HORO_BUILD_RENDER_OPENGL)
        target_sources(HoroEditorUiAutomationTests PRIVATE
                helpers/editor_ui/InteractiveOpenGlEditorUiTestSurface.cpp
        )
        target_compile_definitions(HoroEditorUiAutomationTests PRIVATE
                HORO_UI_TEST_HAS_INTERACTIVE_OPENGL=1
        )
        target_link_libraries(HoroEditorUiAutomationTests PRIVATE
                HoroEngine::EditorViewportOpenGL
        )
    endif ()
    if (HORO_BUILD_RENDER_METAL)
        target_sources(HoroEditorUiAutomationTests PRIVATE
                helpers/editor_ui/InteractiveMetalEditorUiTestSurface.mm
        )
        target_compile_definitions(HoroEditorUiAutomationTests PRIVATE
                HORO_UI_TEST_HAS_INTERACTIVE_METAL=1
        )
        target_link_libraries(HoroEditorUiAutomationTests PRIVATE
                HoroEngine::EditorViewportMetal
        )
    endif ()
    horo_register_catch_test(HoroEditorUiAutomationTests
            LABELS "gui;headless;imgui"
            PROPERTIES TIMEOUT 120
    )
endif ()

add_executable(HoroInputTests
        unit/runtime/input/InputTests.cpp
)
target_compile_features(HoroInputTests PRIVATE cxx_std_20)
target_link_libraries(HoroInputTests PRIVATE HoroEngine::Input)
add_executable(HoroAudioApiTests
        unit/runtime/audio/AudioBackendCapabilitiesTests.cpp
        unit/runtime/audio/AudioCallbackEventsTests.cpp
        unit/runtime/audio/AudioDeviceDiscoveryTests.cpp
        unit/runtime/audio/AudioDeviceNegotiationTests.cpp
        unit/runtime/audio/AudioDeviceTimingTests.cpp
        unit/runtime/audio/AudioFormatTests.cpp
        unit/runtime/audio/AudioIdentityTests.cpp
        unit/runtime/audio/AudioResamplerPlanTests.cpp
        unit/runtime/audio/AudioPlanarBlockTests.cpp
)
target_include_directories(HoroAudioApiTests PRIVATE ${PROJECT_SOURCE_DIR}/src/audio/api)
target_compile_features(HoroAudioApiTests PRIVATE cxx_std_20)
target_link_libraries(HoroAudioApiTests PRIVATE HoroEngine::AudioApi)
horo_register_catch_test(HoroAudioApiTests LABELS "unit;audio;headless")
add_executable(HoroAudioBackendContractTests
        unit/runtime/audio/AudioBackendContractTests.cpp
)
target_compile_features(HoroAudioBackendContractTests PRIVATE cxx_std_20)
target_link_libraries(HoroAudioBackendContractTests PRIVATE HoroAudioBackendContract)
horo_register_catch_test(HoroAudioBackendContractTests LABELS "unit;audio;headless")
add_executable(HoroAudioDspTests
    unit/runtime/audio/ResamplerKernelTests.cpp
    unit/runtime/audio/AudioResamplerTests.cpp
)
target_include_directories(HoroAudioDspTests PRIVATE ${PROJECT_SOURCE_DIR}/src/audio/resampling)
target_compile_features(HoroAudioDspTests PRIVATE cxx_std_20)
target_link_libraries(HoroAudioDspTests PRIVATE HoroEngine::AudioDsp)
horo_register_catch_test(HoroAudioDspTests LABELS "unit;audio;headless")
add_executable(HoroAudioResamplerBenchmark EXCLUDE_FROM_ALL benchmarks/audio/ResamplerBenchmark.cpp)
target_compile_features(HoroAudioResamplerBenchmark PRIVATE cxx_std_20)
target_link_libraries(HoroAudioResamplerBenchmark PRIVATE HoroEngine::AudioDsp)

add_executable(HoroAudioMemoryTests unit/runtime/audio/AudioMemoryTests.cpp)
target_compile_features(HoroAudioMemoryTests PRIVATE cxx_std_20)
target_link_libraries(HoroAudioMemoryTests PRIVATE HoroEngine::AudioMemory)
horo_register_catch_test(HoroAudioMemoryTests LABELS "unit;audio;headless")
add_executable(HoroAudioCommandTests
    unit/runtime/audio/AudioCommandTests.cpp
    unit/runtime/audio/AudioCommandBufferTests.cpp
    unit/runtime/audio/AudioCommandStagingTests.cpp
)
target_compile_features(HoroAudioCommandTests PRIVATE cxx_std_20)
target_link_libraries(HoroAudioCommandTests PRIVATE HoroEngine::AudioCommands)
horo_register_catch_test(HoroAudioCommandTests LABELS "unit;audio;headless")

add_executable(HoroPhysicsTests
        unit/runtime/physics/CanonicalSolverTests.cpp
        unit/runtime/physics/PhysicsBodyDescriptorTests.cpp
        unit/runtime/physics/PhysicsConstraintDescriptorTests.cpp
        unit/runtime/physics/PhysicsCookedShapeDescriptorTests.cpp
        unit/runtime/physics/PhysicsHandleRegistryTests.cpp
        unit/runtime/physics/PhysicsIdentityTests.cpp
        unit/runtime/physics/PhysicsPoseTests.cpp
        unit/runtime/physics/PhysicsQueryTests.cpp
        unit/runtime/physics/PhysicsShapeDescriptorTests.cpp
        unit/runtime/physics/PhysicsStepPolicyTests.cpp
        unit/runtime/physics/PhysicsWorldBudgetsTests.cpp
        unit/runtime/physics/PhysicsWorldDescriptorTests.cpp
        unit/runtime/physics/PhysicsWorldSettingsTests.cpp
        unit/runtime/physics/PhysicsWorldTests.cpp
)
target_compile_features(HoroPhysicsTests PRIVATE cxx_std_20)
target_include_directories(HoroPhysicsTests PRIVATE ${PROJECT_SOURCE_DIR}/src/physics)
target_compile_definitions(HoroPhysicsTests PRIVATE HORO_TEST_PHYSICS_NATIVE=$<BOOL:${HORO_BUILD_PHYSICS_NATIVE}>)
target_link_libraries(HoroPhysicsTests PRIVATE HoroEngine::Physics HoroEngine::Runtime HoroEngine::Platform)
horo_register_catch_test(HoroPhysicsTests LABELS "unit;physics;headless")

add_executable(HoroNavigationApiTests
        support/navigation/DeterministicNavigationProvider.cpp
        unit/runtime/navigation/NavigationAreaRegistryTests.cpp
        unit/runtime/navigation/NavigationCapabilitiesTests.cpp
        unit/runtime/navigation/NavigationIdentityTests.cpp
        unit/runtime/navigation/NavigationOutcomesTests.cpp
        unit/runtime/navigation/NavigationProviderTests.cpp
)
target_compile_features(HoroNavigationApiTests PRIVATE cxx_std_20)
target_include_directories(HoroNavigationApiTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/support)
target_link_libraries(HoroNavigationApiTests PRIVATE HoroEngine::NavigationApi HoroEngine::NavigationNull)
horo_register_catch_test(HoroNavigationApiTests LABELS "unit;navigation;headless")

add_executable(HoroWorldStreamingTests
        unit/runtime/world_streaming/WorldStreamingIdentityTests.cpp
)
target_compile_features(HoroWorldStreamingTests PRIVATE cxx_std_20)
target_link_libraries(HoroWorldStreamingTests PRIVATE HoroEngine::WorldStreaming)
horo_register_catch_test(HoroWorldStreamingTests LABELS "unit;world_streaming;headless")

foreach(horo_physics_probe IN ITEMS iOS Linux Darwin)
    set(horo_physics_expected "Canonical Physics is not qualified for this target")
    if(horo_physics_probe STREQUAL "Linux")
        set(horo_physics_expected "Canonical Physics supports Linux/Windows x86_64 only")
    elseif(horo_physics_probe STREQUAL "Darwin")
        set(horo_physics_expected "Canonical Physics requires macOS 14 or newer")
    endif()
    add_test(NAME HoroPhysicsRejectsUnsupported${horo_physics_probe} COMMAND ${CMAKE_COMMAND}
        -DHORO_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DHORO_SYSTEM=${horo_physics_probe} -DHORO_PROCESSOR=arm64 -DHORO_DEPLOYMENT=13
        "-DHORO_EXPECTED_ERROR=${horo_physics_expected}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyPhysicsUnsupportedTarget.cmake)
endforeach()
if(HORO_BUILD_PHYSICS_NATIVE)
    add_executable(HoroPhysicsNativeBoundaryTests
        unit/runtime/physics/CanonicalNativeBoundaryTests.cpp
        unit/runtime/physics/CanonicalPhysicsRuntimeTests.cpp
        unit/runtime/physics/CanonicalWorldSettingsTests.cpp)
    target_compile_features(HoroPhysicsNativeBoundaryTests PRIVATE cxx_std_20)
    target_include_directories(HoroPhysicsNativeBoundaryTests PRIVATE ${PROJECT_SOURCE_DIR}/src/physics)
    target_link_libraries(HoroPhysicsNativeBoundaryTests PRIVATE HoroEngine::Physics Jolt)
    horo_register_catch_test(HoroPhysicsNativeBoundaryTests LABELS "unit;physics;headless")
    add_test(NAME HoroPhysicsNotices COMMAND ${CMAKE_COMMAND}
        -DHORO_BINARY_DIR=${CMAKE_BINARY_DIR}
        -DHORO_DATA_DIR=${CMAKE_INSTALL_DATADIR}
        -DHORO_CONFIG=$<CONFIG>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyPhysicsNotices.cmake)
endif()
add_executable(HoroGameplayRuntimeTests
        unit/gameplay/BehaviorRuntimeTests.cpp
        unit/gameplay/ComponentRegistryTests.cpp
        unit/gameplay/GameplayRegistrationTests.cpp
)
target_compile_features(HoroGameplayRuntimeTests PRIVATE cxx_std_20)
target_link_libraries(HoroGameplayRuntimeTests PRIVATE HoroEngine::GameplayRuntime)
horo_add_gameplay_module(HoroTestGameModule
        MODULE_ID game.tests
        NO_MANIFEST
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/gameplay_module/TestGameModule.cpp
)
get_property(HORO_TEST_GAME_MODULE_REVISION_FILE TARGET HoroTestGameModule PROPERTY HORO_GAMEPLAY_DESCRIPTOR_REVISION_FILE)
add_executable(HoroGameModuleHostTests
        unit/gameplay/GameModuleHostTests.cpp
)
target_include_directories(HoroGameModuleHostTests PRIVATE "${PROJECT_SOURCE_DIR}/tests/helpers")
target_compile_features(HoroGameModuleHostTests PRIVATE cxx_std_20)
target_compile_definitions(HoroGameModuleHostTests PRIVATE
        HORO_TEST_GAME_MODULE_PATH="$<TARGET_FILE:HoroTestGameModule>"
        HORO_TEST_GAME_MODULE_REVISION_PATH="${HORO_TEST_GAME_MODULE_REVISION_FILE}"
)
target_link_libraries(HoroGameModuleHostTests PRIVATE HoroEngine::GameplayModuleHost)
add_dependencies(HoroGameModuleHostTests HoroTestGameModule)
add_executable(HoroLuaBehaviorTests
        unit/gameplay/LuaBehaviorTests.cpp
)
target_compile_features(HoroLuaBehaviorTests PRIVATE cxx_std_20)
target_link_libraries(HoroLuaBehaviorTests PRIVATE HoroEngine::GameplayLua)
add_executable(HoroEditorPlaySessionControllerTests
        unit/editor/EditorPlaySessionControllerTests.cpp
)
target_compile_features(HoroEditorPlaySessionControllerTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorPlaySessionControllerTests PRIVATE HoroEngine::EditorServices)
add_executable(HoroProjectGameplayRegistryTests
        unit/editor/ProjectGameplayRegistryTests.cpp
)
target_include_directories(HoroProjectGameplayRegistryTests PRIVATE "${PROJECT_SOURCE_DIR}/tests/helpers")
target_compile_features(HoroProjectGameplayRegistryTests PRIVATE cxx_std_20)
target_compile_definitions(HoroProjectGameplayRegistryTests PRIVATE
        HORO_TEST_GAME_MODULE_PATH="$<TARGET_FILE:HoroTestGameModule>"
        HORO_TEST_GAME_MODULE_REVISION_PATH="${HORO_TEST_GAME_MODULE_REVISION_FILE}"
)
target_link_libraries(HoroProjectGameplayRegistryTests PRIVATE HoroEngine::EditorServices)
add_dependencies(HoroProjectGameplayRegistryTests HoroTestGameModule)
add_executable(HoroSceneMathTests
        unit/foundation/SceneMathTests.cpp
)
target_compile_features(HoroSceneMathTests PRIVATE cxx_std_20)
target_link_libraries(HoroSceneMathTests PRIVATE HoroEngine::Foundation)
add_executable(HoroFoundationStringTests
        unit/foundation/StringTests.cpp
)
target_compile_features(HoroFoundationStringTests PRIVATE cxx_std_20)
target_link_libraries(HoroFoundationStringTests PRIVATE HoroEngine::Foundation)
add_executable(HoroFoundationSha256Tests
        unit/foundation/Sha256Tests.cpp
)
target_compile_features(HoroFoundationSha256Tests PRIVATE cxx_std_20)
target_link_libraries(HoroFoundationSha256Tests PRIVATE HoroEngine::Foundation)
add_executable(HoroStructuredLogStoreTests
        unit/foundation/StructuredLogStoreTests.cpp
)
target_compile_features(HoroStructuredLogStoreTests PRIVATE cxx_std_20)
target_link_libraries(HoroStructuredLogStoreTests PRIVATE HoroEngine::Foundation Threads::Threads)
add_executable(HoroObservabilityTests
        unit/foundation/ObservabilityTests.cpp
)
target_compile_features(HoroObservabilityTests PRIVATE cxx_std_20)
target_link_libraries(HoroObservabilityTests PRIVATE HoroEngine::Foundation Threads::Threads miniz)
add_executable(HoroHostObservabilityTests
        unit/application/HostObservabilityTests.cpp
)
target_compile_features(HoroHostObservabilityTests PRIVATE cxx_std_20)
target_link_libraries(HoroHostObservabilityTests PRIVATE HoroEngine::Application Threads::Threads)
if (HORO_ENABLE_OPENTELEMETRY)
    add_executable(HoroOpenTelemetryTests
            unit/foundation/OpenTelemetrySinkTests.cpp
    )
    target_compile_features(HoroOpenTelemetryTests PRIVATE cxx_std_20)
    target_link_libraries(HoroOpenTelemetryTests
            PRIVATE
            HoroEngine::OpenTelemetry
            Threads::Threads
            nlohmann_json::nlohmann_json
    )
endif ()
add_executable(HoroTelemetryFastPathBenchmark
        benchmarks/TelemetryFastPathBenchmark.cpp
)
target_compile_features(HoroTelemetryFastPathBenchmark PRIVATE cxx_std_20)
target_link_libraries(HoroTelemetryFastPathBenchmark PRIVATE HoroEngine::Foundation Threads::Threads)
add_test(NAME HoroTelemetryFastPathBenchmark COMMAND HoroTelemetryFastPathBenchmark)
set_tests_properties(HoroTelemetryFastPathBenchmark PROPERTIES LABELS "native;benchmark;observability")
add_executable(HoroActivityStoreTests
        unit/foundation/ActivityStoreTests.cpp
)
target_compile_features(HoroActivityStoreTests PRIVATE cxx_std_20)
target_link_libraries(HoroActivityStoreTests PRIVATE HoroEngine::Foundation Threads::Threads)

add_executable(HoroPrimitiveMeshTests
        unit/runtime/scene/PrimitiveMeshTests.cpp
)
find_package(Threads REQUIRED)
target_compile_features(HoroPrimitiveMeshTests PRIVATE cxx_std_20)
target_link_libraries(HoroPrimitiveMeshTests PRIVATE HoroEngine::SceneModel Threads::Threads)
add_executable(HoroEditorWelcomeControllerTests
        unit/editor/WelcomeControllerTests.cpp
)

target_compile_features(HoroEditorWelcomeControllerTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorWelcomeControllerTests
        PRIVATE
        HoroEngine::EditorServices
)
add_executable(HoroEditorModalHostTests
        unit/editor/EditorModalHostTests.cpp
)

target_compile_features(HoroEditorModalHostTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorModalHostTests
        PRIVATE
        HoroEngine::EditorServices
)
if (HORO_BUILD_EDITOR_GUI)
    add_executable(HoroGuiScreenHostLifecycleTests
            unit/editor/GuiScreenHostLifecycleTests.cpp
    )
    target_compile_features(HoroGuiScreenHostLifecycleTests PRIVATE cxx_std_20)
    target_link_libraries(HoroGuiScreenHostLifecycleTests PRIVATE HoroEngine::Gui)

    add_executable(HoroSettingsModalLifecycleTests
            unit/editor/SettingsModalLifecycleTests.cpp
    )
    target_compile_features(HoroSettingsModalLifecycleTests PRIVATE cxx_std_20)
    target_link_libraries(HoroSettingsModalLifecycleTests
            PRIVATE
            HoroEngine::Gui
            HoroEngine::EditorServices
    )
    add_executable(HoroAssetImportModalLifecycleTests
            unit/editor/AssetImportModalLifecycleTests.cpp
    )
    target_compile_features(HoroAssetImportModalLifecycleTests PRIVATE cxx_std_20)
    target_link_libraries(HoroAssetImportModalLifecycleTests
            PRIVATE
            HoroEngine::Gui
            HoroEngine::EditorServices
            HoroEngine::Assets
            HoroEngine::Foundation
    )
endif ()
add_executable(HoroEditorLocalizationServiceTests
        unit/editor/LocalizationServiceTests.cpp
)

target_compile_features(HoroEditorLocalizationServiceTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorLocalizationServiceTests
        PRIVATE
        HoroEngine::EditorServices
)
add_executable(HoroProjectCreationControllerTests
        unit/editor/ProjectCreationControllerTests.cpp
)

target_compile_features(HoroProjectCreationControllerTests PRIVATE cxx_std_20)
target_link_libraries(HoroProjectCreationControllerTests
        PRIVATE
        HoroEngine::EditorServices
)
add_executable(HoroRendererAvailabilityTests
        unit/editor/RendererAvailabilityTests.cpp
)
target_compile_features(HoroRendererAvailabilityTests PRIVATE cxx_std_20)
target_link_libraries(HoroRendererAvailabilityTests PRIVATE HoroEngine::EditorServices)
add_executable(HoroProjectMetadataTests
        unit/editor/ProjectMetadataTests.cpp
)
target_compile_features(HoroProjectMetadataTests PRIVATE cxx_std_20)
target_link_libraries(HoroProjectMetadataTests PRIVATE HoroEngine::EditorServices)
add_executable(HoroProjectCompatibilityTests
        unit/application/ProjectCompatibilityTests.cpp
)
target_compile_features(HoroProjectCompatibilityTests PRIVATE cxx_std_20)
target_link_libraries(HoroProjectCompatibilityTests PRIVATE HoroEngine::Application)
add_executable(HoroProjectMigrationTests
        unit/application/ProjectMigrationTests.cpp
)
target_compile_features(HoroProjectMigrationTests PRIVATE cxx_std_20)
target_link_libraries(HoroProjectMigrationTests PRIVATE HoroEngine::ProjectMigrations)
add_executable(HoroProjectMigrationTransactionTests
        unit/editor/ProjectMigrationTransactionTests.cpp
)
target_compile_features(HoroProjectMigrationTransactionTests PRIVATE cxx_std_20)
target_link_libraries(HoroProjectMigrationTransactionTests PRIVATE
        HoroEngine::EditorServices HoroEngine::ProjectMigrations)
add_executable(HoroProjectOpenServiceTests
        unit/editor/ProjectOpenServiceTests.cpp
)
target_compile_features(HoroProjectOpenServiceTests PRIVATE cxx_std_20)
target_link_libraries(HoroProjectOpenServiceTests PRIVATE
        HoroEngine::EditorServices HoroEngine::ProjectMigrations nlohmann_json::nlohmann_json)

add_executable(HoroProjectIntegrityValidatorServiceTests
        unit/editor/ProjectIntegrityValidatorServiceTests.cpp
)
target_compile_features(HoroProjectIntegrityValidatorServiceTests PRIVATE cxx_std_20)
target_link_libraries(HoroProjectIntegrityValidatorServiceTests PRIVATE
        HoroEngine::EditorServices)
add_executable(HoroProjectMigrationIntegrationTests
        integration/project/ProjectMigrationIntegrationTests.cpp
        helpers/project_migration/ProjectMigrationTestFixture.cpp
        helpers/project_migration/ProjectMigrationTestFixture.h
)
target_compile_features(HoroProjectMigrationIntegrationTests PRIVATE cxx_std_20)
target_include_directories(HoroProjectMigrationIntegrationTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/helpers/project_migration)
target_link_libraries(HoroProjectMigrationIntegrationTests PRIVATE
        HoroEngine::EditorServices HoroEngine::ProjectMigrations nlohmann_json::nlohmann_json)
horo_register_catch_test(HoroProjectMigrationIntegrationTests
        LABELS "migration")
add_executable(HoroRecentProjectInspectionServiceTests
        unit/editor/RecentProjectInspectionServiceTests.cpp
)
target_compile_features(HoroRecentProjectInspectionServiceTests PRIVATE cxx_std_20)
target_link_libraries(HoroRecentProjectInspectionServiceTests PRIVATE
        HoroEngine::EditorServices HoroEngine::ProjectMigrations)
add_executable(HoroProjectCreationServiceTests
        unit/editor/ProjectCreationServiceTests.cpp
)

target_compile_features(HoroProjectCreationServiceTests PRIVATE cxx_std_20)
target_link_libraries(HoroProjectCreationServiceTests
        PRIVATE
        HoroEngine::EditorServices
)
add_executable(HoroEditorSettingsStoreTests
        unit/editor/EditorSettingsStoreTests.cpp
)

target_compile_features(HoroEditorSettingsStoreTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorSettingsStoreTests
        PRIVATE
        HoroEngine::EditorServices
)
add_executable(HoroWorkspaceLayoutTests
        unit/editor/WorkspaceLayoutTests.cpp
)
target_compile_features(HoroWorkspaceLayoutTests PRIVATE cxx_std_20)
target_link_libraries(HoroWorkspaceLayoutTests PRIVATE HoroEngine::EditorServices)
add_executable(HoroWorkspacePanelHostTests
        unit/editor/WorkspacePanelHostTests.cpp
)
target_link_libraries(HoroWorkspacePanelHostTests PRIVATE HoroEngine::EditorServices)
add_executable(HoroWorkspaceLayoutPersistenceTests
        unit/editor/WorkspaceLayoutPersistenceTests.cpp
)
target_link_libraries(HoroWorkspaceLayoutPersistenceTests PRIVATE HoroEngine::EditorServices)
add_executable(HoroActivityBarLayoutTests
        unit/editor/ActivityBarLayoutTests.cpp
)
target_compile_features(HoroActivityBarLayoutTests PRIVATE cxx_std_20)
target_link_libraries(HoroActivityBarLayoutTests PRIVATE HoroEngine::EditorServices)
add_executable(HoroWorkspaceSplitterInteractionTests
        unit/editor/WorkspaceSplitterInteractionTests.cpp
)
target_compile_features(HoroWorkspaceSplitterInteractionTests PRIVATE cxx_std_20)
target_include_directories(HoroWorkspaceSplitterInteractionTests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/editor/screens/workspace
)
target_link_libraries(HoroWorkspaceSplitterInteractionTests PRIVATE HoroEngine::EditorServices)
if (HORO_BUILD_EDITOR_GUI)
    add_executable(HoroEditorWorkspaceControllerTests
            unit/editor/EditorWorkspaceControllerTests.cpp
    )

    target_compile_features(HoroEditorWorkspaceControllerTests PRIVATE cxx_std_20)
    target_link_libraries(HoroEditorWorkspaceControllerTests PRIVATE HoroEngine::Gui nlohmann_json::nlohmann_json)
endif ()

add_executable(HoroGameplayBehaviorFilenameModalStateTests
        unit/editor/GameplayBehaviorFilenameModalStateTests.cpp
)
target_compile_features(HoroGameplayBehaviorFilenameModalStateTests PRIVATE cxx_std_20)
target_include_directories(HoroGameplayBehaviorFilenameModalStateTests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(HoroGameplayBehaviorFilenameModalStateTests PRIVATE HoroEngine::EditorServices)

add_executable(HoroNotificationServiceTests
        unit/editor/NotificationServiceTests.cpp
)
target_compile_features(HoroNotificationServiceTests PRIVATE cxx_std_20)
target_link_libraries(HoroNotificationServiceTests PRIVATE HoroEngine::EditorServices)

add_executable(HoroEditorMenuModelTests
        unit/editor/EditorMenuModelTests.cpp
)

target_compile_features(HoroEditorMenuModelTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorMenuModelTests PRIVATE HoroEngine::EditorServices)
add_executable(HoroEditorStatusBarModelTests
        unit/editor/EditorStatusBarModelTests.cpp
)

target_compile_features(HoroEditorStatusBarModelTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorStatusBarModelTests PRIVATE HoroEngine::EditorServices)
add_executable(HoroHierarchyModelTests
        unit/editor/HierarchyModelTests.cpp
)

target_compile_features(HoroHierarchyModelTests PRIVATE cxx_std_20)
target_link_libraries(HoroHierarchyModelTests PRIVATE HoroEngine::EditorServices)
add_executable(HoroEditorSelectionModelTests
        unit/editor/EditorSelectionModelTests.cpp
)

target_compile_features(HoroEditorSelectionModelTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorSelectionModelTests PRIVATE HoroEngine::EditorServices)
add_executable(HoroEditorViewportModelTests
        unit/editor/EditorViewportModelTests.cpp
)

target_compile_features(HoroEditorViewportModelTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorViewportModelTests PRIVATE HoroEngine::EditorServices)
if (HORO_BUILD_EDITOR_GUI)
    add_executable(HoroHierarchyPanelRenderTests
            unit/editor/HierarchyPanelRenderTests.cpp
    )

    target_compile_features(HoroHierarchyPanelRenderTests PRIVATE cxx_std_20)
    target_include_directories(HoroHierarchyPanelRenderTests PRIVATE
            ${PROJECT_SOURCE_DIR}/src/editor/screens/workspace
    )
    target_link_libraries(HoroHierarchyPanelRenderTests
            PRIVATE
            HoroEngine::Gui
            HoroThirdParty::ImGui
    )
    add_executable(HoroHierarchyEditSessionTests
            unit/editor/HierarchyEditSessionTests.cpp
    )

    target_compile_features(HoroHierarchyEditSessionTests PRIVATE cxx_std_20)
    target_link_libraries(HoroHierarchyEditSessionTests
            PRIVATE
            HoroEngine::Gui
    )
    add_executable(HoroInspectorPanelRenderTests
            unit/editor/InspectorPanelRenderTests.cpp
    )

    target_compile_features(HoroInspectorPanelRenderTests PRIVATE cxx_std_20)
    target_include_directories(HoroInspectorPanelRenderTests PRIVATE
            ${PROJECT_SOURCE_DIR}/src/editor/screens/workspace
    )
    target_link_libraries(HoroInspectorPanelRenderTests
            PRIVATE
            HoroEngine::Gui
            HoroThirdParty::ImGui
    )
    add_executable(HoroInspectorEditSessionTests
            unit/editor/InspectorEditSessionTests.cpp
    )

    target_compile_features(HoroInspectorEditSessionTests PRIVATE cxx_std_20)
    target_include_directories(HoroInspectorEditSessionTests PRIVATE
            ${PROJECT_SOURCE_DIR}/src/editor/screens/workspace
    )
    target_link_libraries(HoroInspectorEditSessionTests
            PRIVATE
            HoroEngine::Gui
    )
    add_executable(HoroGlobalDockPanelRenderTests
            unit/editor/GlobalDockPanelRenderTests.cpp
    )

    target_compile_features(HoroGlobalDockPanelRenderTests PRIVATE cxx_std_20)
    target_include_directories(HoroGlobalDockPanelRenderTests PRIVATE
            ${PROJECT_SOURCE_DIR}/src/editor/screens/workspace
    )
    target_link_libraries(HoroGlobalDockPanelRenderTests
            PRIVATE
            HoroEngine::Gui
            HoroThirdParty::ImGui
    )
    add_executable(HoroAssetBrowserInteractionSessionTests
            unit/editor/AssetBrowserInteractionSessionTests.cpp
    )

    target_compile_features(
            HoroAssetBrowserInteractionSessionTests PRIVATE cxx_std_20
    )
    target_link_libraries(HoroAssetBrowserInteractionSessionTests
            PRIVATE
            HoroEngine::Gui
    )
    add_executable(HoroEditorUiComponentsRenderTests
            unit/editor/EditorUiComponentsRenderTests.cpp
    )

    target_compile_features(HoroEditorUiComponentsRenderTests PRIVATE cxx_std_20)
    target_link_libraries(HoroEditorUiComponentsRenderTests
            PRIVATE
            HoroEngine::Gui
            HoroThirdParty::ImGui
    )
    add_executable(HoroViewportPanelRenderTests
            unit/editor/ViewportPanelRenderTests.cpp
    )

    target_compile_features(HoroViewportPanelRenderTests PRIVATE cxx_std_20)
    target_include_directories(HoroViewportPanelRenderTests PRIVATE
            ${PROJECT_SOURCE_DIR}/src/editor/screens/workspace
    )
    target_link_libraries(HoroViewportPanelRenderTests
            PRIVATE
            HoroEngine::Gui
            HoroThirdParty::ImGui
    )
    add_executable(HoroTransformGizmoMathTests
            unit/editor/TransformGizmoMathTests.cpp
    )
    target_compile_features(HoroTransformGizmoMathTests PRIVATE cxx_std_20)
    target_link_libraries(HoroTransformGizmoMathTests
            PRIVATE
            HoroEngine::Gui
    )
    horo_register_catch_test(HoroTransformGizmoMathTests
            LABELS "unit;editor;viewport;gizmo"
    )
endif ()

add_executable(HoroDataBusTests
        unit/foundation/DataBusTests.cpp
)

target_compile_features(HoroDataBusTests PRIVATE cxx_std_20)
target_link_libraries(HoroDataBusTests
        PRIVATE
        HoroEngine::EditorServices
)
add_executable(HoroFoundationResultTests
        unit/foundation/ResultTests.cpp
)

target_compile_features(HoroFoundationResultTests PRIVATE cxx_std_20)
target_link_libraries(HoroFoundationResultTests PRIVATE HoroEngine::Foundation)
add_executable(HoroFoundationModuleDescriptorTests
        unit/foundation/ModuleDescriptorTests.cpp
        unit/foundation/ModuleDescriptorGraphTests.cpp
)

target_compile_features(HoroFoundationModuleDescriptorTests PRIVATE cxx_std_20)
target_link_libraries(HoroFoundationModuleDescriptorTests PRIVATE HoroEngine::Foundation)

add_executable(HoroFoundationModuleHostCompositionTests
        unit/foundation/ModuleHostCompositionTests.cpp
        unit/foundation/ModuleHostLifecycleTests.cpp
)

target_compile_features(HoroFoundationModuleHostCompositionTests PRIVATE cxx_std_20)
target_link_libraries(HoroFoundationModuleHostCompositionTests PRIVATE HoroEngine::Foundation)
add_executable(HoroHostModuleCompositionTests
        unit/application/HostModuleCompositionTests.cpp
)
target_compile_features(HoroHostModuleCompositionTests PRIVATE cxx_std_20)
target_link_libraries(HoroHostModuleCompositionTests PRIVATE HoroHostModuleComposition)
add_executable(HoroFoundationPathsTimeHandlesTests
        unit/foundation/PathsTimeHandlesTests.cpp
)

target_compile_features(HoroFoundationPathsTimeHandlesTests PRIVATE cxx_std_20)
target_link_libraries(HoroFoundationPathsTimeHandlesTests PRIVATE HoroEngine::Foundation)
add_executable(HoroFoundationCancellationProgressTests
        unit/foundation/CancellationProgressTests.cpp
)

target_compile_features(HoroFoundationCancellationProgressTests PRIVATE cxx_std_20)
target_link_libraries(HoroFoundationCancellationProgressTests PRIVATE HoroEngine::Foundation)
add_executable(HoroFoundationConfigurationTests
        unit/foundation/ConfigurationTests.cpp
)

target_compile_features(HoroFoundationConfigurationTests PRIVATE cxx_std_20)
target_link_libraries(HoroFoundationConfigurationTests PRIVATE HoroEngine::Foundation)
add_executable(HoroFoundationDiagnosticsTests
        unit/foundation/DiagnosticsTests.cpp
)

target_compile_features(HoroFoundationDiagnosticsTests PRIVATE cxx_std_20)
target_link_libraries(HoroFoundationDiagnosticsTests PRIVATE HoroEngine::Foundation)
add_executable(HoroFoundationJobSystemTests
        unit/foundation/JobSystemTests.cpp
)

target_compile_features(HoroFoundationJobSystemTests PRIVATE cxx_std_20)
target_link_libraries(HoroFoundationJobSystemTests PRIVATE HoroEngine::Foundation)
add_executable(HoroPlatformTests
        unit/foundation/PlatformTests.cpp
)

target_compile_features(HoroPlatformTests PRIVATE cxx_std_20)
target_link_libraries(HoroPlatformTests PRIVATE HoroEngine::Platform)
add_executable(HoroGameplayBuildServiceTests
        unit/application/GameplayBuildServiceTests.cpp
)
target_compile_features(HoroGameplayBuildServiceTests PRIVATE cxx_std_20)
target_compile_definitions(HoroGameplayBuildServiceTests PRIVATE
        HORO_GAMEPLAY_SDK_PACKAGE_DIR="${HORO_GAMEPLAY_SDK_PACKAGE_DIR}"
        HORO_GAMEPLAY_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
        HORO_TEST_CMAKE_GENERATOR="${CMAKE_GENERATOR}"
)
target_link_libraries(HoroGameplayBuildServiceTests
        PRIVATE
        HoroEngine::GameplayBuild
        HoroEngine::Platform
)
add_executable(HoroEditorViewportSceneTests
        unit/editor/EditorViewportSceneTests.cpp
)

target_compile_features(HoroEditorViewportSceneTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorViewportSceneTests PRIVATE HoroEngine::EditorViewportScene)
add_executable(HoroSceneDocumentTests
        unit/editor/SceneDocumentTests.cpp
)
target_compile_features(HoroSceneDocumentTests PRIVATE cxx_std_20)
target_link_libraries(HoroSceneDocumentTests PRIVATE HoroEngine::EditorRenderExtraction)
add_executable(HoroSceneDocumentPersistenceTests
        unit/editor/SceneDocumentPersistenceTests.cpp
)
target_compile_features(HoroSceneDocumentPersistenceTests PRIVATE cxx_std_20)
target_link_libraries(HoroSceneDocumentPersistenceTests PRIVATE HoroEngine::EditorServices)
add_executable(HoroEditorViewportPickingTests
        unit/editor/EditorViewportPickingTests.cpp
)
target_compile_features(HoroEditorViewportPickingTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorViewportPickingTests PRIVATE HoroEngine::EditorRenderExtraction)
add_executable(HoroAssetSceneDropTests
        unit/editor/AssetSceneDropTests.cpp
)
target_compile_features(HoroAssetSceneDropTests PRIVATE cxx_std_20)
target_link_libraries(HoroAssetSceneDropTests PRIVATE HoroEngine::EditorRenderExtraction)
add_executable(HoroEditorViewportResourceTests
        unit/editor/EditorViewportResourcesTests.cpp
)
target_link_libraries(HoroEditorViewportResourceTests
        PRIVATE
        HoroEngine::EditorViewportResources
        HoroEngine::RenderNull
)

# White-box editor tests opt into implementation headers explicitly. Keep this
# non-installed test interface separate from every production usage requirement;
# legacy editor/... include spellings require the source root until those tests
# migrate to narrower per-subsystem spellings.
add_library(HoroEditorTestInternals INTERFACE)
target_include_directories(HoroEditorTestInternals INTERFACE ${PROJECT_SOURCE_DIR}/src)
set(HORO_EDITOR_WHITE_BOX_TEST_TARGETS
        HoroEditorPlaySessionControllerTests
        HoroProjectGameplayRegistryTests
        HoroGuiScreenHostLifecycleTests
        HoroProjectCreationControllerTests
        HoroRendererAvailabilityTests
        HoroProjectMetadataTests
        HoroProjectOpenServiceTests
        HoroProjectMigrationIntegrationTests
        HoroEditorWorkspaceControllerTests
        HoroGameplayBehaviorFilenameModalStateTests
        HoroEditorSelectionModelTests
        HoroEditorViewportModelTests
        HoroHierarchyPanelRenderTests
        HoroHierarchyEditSessionTests
        HoroInspectorPanelRenderTests
        HoroInspectorEditSessionTests
        HoroGlobalDockPanelRenderTests
        HoroAssetBrowserInteractionSessionTests
        HoroViewportPanelRenderTests
        HoroTransformGizmoMathTests
        HoroEditorViewportSceneTests
        HoroSceneDocumentTests
        HoroSceneDocumentPersistenceTests
        HoroEditorViewportPickingTests
        HoroAssetSceneDropTests
        HoroEditorViewportResourceTests
)
foreach (target IN LISTS HORO_EDITOR_WHITE_BOX_TEST_TARGETS)
    if (TARGET ${target})
        target_link_libraries(${target} PRIVATE HoroEditorTestInternals)
    endif ()
endforeach ()

add_executable(HoroRenderBackendRegistryTests
        unit/runtime/renderer/RenderBackendRegistryTests.cpp
)

target_compile_features(HoroRenderBackendRegistryTests PRIVATE cxx_std_20)
target_link_libraries(HoroRenderBackendRegistryTests
        PRIVATE
        HoroEngine::RenderNull
)
add_executable(HoroRenderGraphTests
        unit/runtime/renderer/RenderGraphTests.cpp
)
target_compile_features(HoroRenderGraphTests PRIVATE cxx_std_20)
target_link_libraries(HoroRenderGraphTests PRIVATE HoroEngine::RenderApi)
add_executable(HoroRenderSubmissionTests
        unit/runtime/renderer/RenderSubmissionTests.cpp
)
target_compile_features(HoroRenderSubmissionTests PRIVATE cxx_std_20)
target_link_libraries(HoroRenderSubmissionTests PRIVATE HoroEngine::RenderApi)
add_executable(HoroRenderAdapterTests
        unit/runtime/renderer/RenderAdapterTests.cpp
)
target_compile_features(HoroRenderAdapterTests PRIVATE cxx_std_20)
target_link_libraries(HoroRenderAdapterTests PRIVATE HoroEngine::RenderApi)
add_executable(HoroRenderFrontendTests
        unit/runtime/renderer/RenderFrontendTests.cpp
        unit/runtime/renderer/RenderResourceRegistryTests.cpp
)

target_compile_features(HoroRenderFrontendTests PRIVATE cxx_std_20)
target_include_directories(HoroRenderFrontendTests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/runtime/renderer/frontend
)
target_link_libraries(HoroRenderFrontendTests
        PRIVATE
        HoroEngine::RenderFrontend
        HoroEngine::RenderNull
)
add_executable(HoroRuntimeLifecycleTests
        unit/runtime/lifecycle/RuntimeLifecycleTests.cpp
)
target_compile_features(HoroRuntimeLifecycleTests PRIVATE cxx_std_20)
target_link_libraries(HoroRuntimeLifecycleTests
        PRIVATE
        HoroEngine::Runtime
        HoroEngine::Platform
        HoroEngine::RenderFrontend
        HoroEngine::RenderNull
)
add_executable(HoroRuntimeSaveIdentityTests
        unit/runtime/save/SaveIdentityTests.cpp
)
target_compile_features(HoroRuntimeSaveIdentityTests PRIVATE cxx_std_20)
target_link_libraries(HoroRuntimeSaveIdentityTests PRIVATE HoroEngine::Runtime)
add_executable(HoroRuntimeSceneTests
        unit/runtime/scene/RuntimeSceneTests.cpp
)
target_compile_features(HoroRuntimeSceneTests PRIVATE cxx_std_20)
target_link_libraries(HoroRuntimeSceneTests PRIVATE HoroEngine::RuntimeScene)
add_executable(HoroAssetRegistryTests
        unit/runtime/assets/AssetRegistryTests.cpp
)
target_compile_features(HoroAssetRegistryTests PRIVATE cxx_std_20)
target_link_libraries(HoroAssetRegistryTests PRIVATE HoroEngine::Assets)
add_executable(HoroAssetProviderTests
        unit/runtime/assets/AssetProviderTests.cpp
)
target_compile_features(HoroAssetProviderTests PRIVATE cxx_std_20)
target_include_directories(HoroAssetProviderTests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/runtime/assets/runtime_provider
)
target_link_libraries(HoroAssetProviderTests PRIVATE HoroEngine::Assets)
add_executable(HoroAssetCookTests
        unit/runtime/assets/AssetCookContractTests.cpp
)
target_compile_features(HoroAssetCookTests PRIVATE cxx_std_20)
target_link_libraries(HoroAssetCookTests PRIVATE HoroEngine::Assets)
add_executable(HoroCookCatalogTests
        unit/runtime/assets/CookCatalogTests.cpp
)
target_compile_features(HoroCookCatalogTests PRIVATE cxx_std_20)
target_link_libraries(HoroCookCatalogTests PRIVATE HoroEngine::Assets)
add_executable(HoroHeadlessNullCookerTests
        unit/runtime/assets/HeadlessNullCookerTests.cpp
)
target_compile_features(HoroHeadlessNullCookerTests PRIVATE cxx_std_20)
target_link_libraries(HoroHeadlessNullCookerTests PRIVATE HoroEngine::Assets)
target_include_directories(HoroHeadlessNullCookerTests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/runtime/assets/cooker/builtin
)
add_executable(HoroAssetCookCacheTests
        unit/runtime/assets/AssetCookCacheTests.cpp
)
target_compile_features(HoroAssetCookCacheTests PRIVATE cxx_std_20)
target_link_libraries(HoroAssetCookCacheTests PRIVATE HoroEngine::Assets)
add_executable(HoroAssetCookOutputTests
        unit/runtime/assets/AssetCookOutputTests.cpp
)
target_compile_features(HoroAssetCookOutputTests PRIVATE cxx_std_20)
target_link_libraries(HoroAssetCookOutputTests PRIVATE HoroEngine::Assets)
add_executable(HoroAssetCookServiceTests
        unit/runtime/assets/AssetCookServiceTests.cpp
)
target_compile_features(HoroAssetCookServiceTests PRIVATE cxx_std_20)
target_link_libraries(HoroAssetCookServiceTests PRIVATE HoroEngine::Assets HoroEngine::Foundation)
target_include_directories(HoroAssetCookServiceTests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/runtime/assets/cooker/builtin
)
add_executable(HoroAssetCookIntegrationTests
        integration/assets/AssetCookIntegrationTests.cpp
)
target_compile_features(HoroAssetCookIntegrationTests PRIVATE cxx_std_20)
target_compile_definitions(HoroAssetCookIntegrationTests PRIVATE PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(HoroAssetCookIntegrationTests PRIVATE HoroEngine::Assets HoroEngine::Foundation)
target_include_directories(HoroAssetCookIntegrationTests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/runtime/assets/cooker/builtin
)
add_executable(HoroAssetImporterCatalogTests
        unit/runtime/assets/AssetImporterCatalogTests.cpp
)
target_compile_features(HoroAssetImporterCatalogTests PRIVATE cxx_std_20)
target_link_libraries(HoroAssetImporterCatalogTests PRIVATE HoroEngine::Assets)
add_executable(HoroAssetImportOperationTests
        unit/runtime/assets/AssetImportOperationTests.cpp
        unit/runtime/assets/AssetReimportTests.cpp
)
target_compile_features(HoroAssetImportOperationTests PRIVATE cxx_std_20)
target_link_libraries(HoroAssetImportOperationTests PRIVATE
        HoroEngine::Assets HoroEngine::Foundation HoroEngine::Platform)
add_executable(HoroObjMeshImporterTests
        unit/runtime/assets/ObjMeshImporterTests.cpp
)
target_compile_features(HoroObjMeshImporterTests PRIVATE cxx_std_20)
target_link_libraries(HoroObjMeshImporterTests PRIVATE HoroEngine::Assets)
target_include_directories(HoroObjMeshImporterTests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/runtime/assets/importer/builtin/obj_mesh
)
add_executable(HoroFbxMeshImporterTests
        unit/runtime/assets/FbxMeshImporterTests.cpp
)
target_compile_features(HoroFbxMeshImporterTests PRIVATE cxx_std_20)
target_link_libraries(HoroFbxMeshImporterTests PRIVATE HoroEngine::Assets)
target_compile_definitions(HoroFbxMeshImporterTests PRIVATE
        HORO_UFBX_TEST_DATA_DIR="${ufbx_SOURCE_DIR}/data"
)
target_include_directories(HoroFbxMeshImporterTests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/runtime/assets/importer/builtin/fbx_mesh
)
add_executable(HoroModularAssetImportIntegrationTests
        integration/assets/ModularAssetImportIntegrationTests.cpp
)
target_compile_features(HoroModularAssetImportIntegrationTests PRIVATE cxx_std_20)
target_compile_definitions(HoroModularAssetImportIntegrationTests PRIVATE PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(HoroModularAssetImportIntegrationTests PRIVATE HoroEngine::Assets HoroEngine::Foundation)
target_include_directories(HoroModularAssetImportIntegrationTests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/runtime/assets/cooker/builtin
        ${PROJECT_SOURCE_DIR}/src/runtime/assets/importer
)
add_executable(HoroExtensionDiscoveryTests
        unit/extensions/DiscoveryPathsTests.cpp
        unit/extensions/DiscoveryPlanTests.cpp
        unit/extensions/DiscoveryRootPolicyTests.cpp
)
target_compile_features(HoroExtensionDiscoveryTests PRIVATE cxx_std_20)
target_include_directories(HoroExtensionDiscoveryTests PRIVATE ${PROJECT_SOURCE_DIR}/src/extensions/discovery)
target_link_libraries(HoroExtensionDiscoveryTests PRIVATE HoroEngine::Extensions Catch2::Catch2WithMain)
add_test(NAME HoroExtensionDiscoveryTests COMMAND HoroExtensionDiscoveryTests)

add_executable(HoroExtensionAbiCConsumer unit/extensions/ExtensionAbiCConsumer.c)
target_compile_features(HoroExtensionAbiCConsumer PRIVATE c_std_11)
set_target_properties(HoroExtensionAbiCConsumer PROPERTIES C_EXTENSIONS OFF)
target_link_libraries(HoroExtensionAbiCConsumer PRIVATE HoroEngine::Extensions)
add_test(NAME HoroExtensionAbiCConsumer COMMAND HoroExtensionAbiCConsumer)

add_executable(HoroExtensionManagerTests
        unit/extensions/ExtensionManagerTests.cpp
        unit/extensions/ExtensionManifestParserTests.cpp
        unit/extensions/ExtensionAbiValidationTests.cpp
        unit/extensions/ExtensionAbiIntegrationTests.cpp
        unit/extensions/ExternalImporterRegistrationTests.cpp
)
foreach(abi_fixture_mode RANGE 0 3)
    set(abi_fixture_target HoroExtensionAbiFixture${abi_fixture_mode})
    add_library(${abi_fixture_target} MODULE fixtures/extensions/AbiModule.c)
    target_compile_features(${abi_fixture_target} PRIVATE c_std_11)
    set_target_properties(${abi_fixture_target} PROPERTIES C_EXTENSIONS OFF)
    target_link_libraries(${abi_fixture_target} PRIVATE HoroEngine::Extensions)
    target_compile_definitions(${abi_fixture_target} PRIVATE HORO_ABI_FIXTURE_MODE=${abi_fixture_mode})
    add_dependencies(HoroExtensionManagerTests ${abi_fixture_target})
    target_compile_definitions(HoroExtensionManagerTests PRIVATE
            HORO_ABI_FIXTURE_${abi_fixture_mode}="$<TARGET_FILE:${abi_fixture_target}>")
endforeach()
target_include_directories(HoroExtensionManagerTests PRIVATE ${PROJECT_SOURCE_DIR}/src/extensions/host)
target_include_directories(HoroExtensionManagerTests PRIVATE ${PROJECT_SOURCE_DIR}/src/extensions/capabilities/asset_pipeline_points)
target_compile_features(HoroExtensionManagerTests PRIVATE cxx_std_20)
target_link_libraries(HoroExtensionManagerTests PRIVATE HoroEngine::Extensions Catch2::Catch2WithMain)
if(TARGET HoroAssetImporterBasicExample)
    add_dependencies(HoroExtensionManagerTests HoroAssetImporterBasicExample)
    target_compile_definitions(HoroExtensionManagerTests PRIVATE
            HORO_BASIC_EXTENSION_DIR="$<TARGET_FILE_DIR:HoroAssetImporterBasicExample>"
    )
endif()
add_test(NAME HoroExtensionManagerTests COMMAND HoroExtensionManagerTests)

if (HORO_BUILD_RENDER_OPENGL)
    add_executable(HoroRenderOpenGLTests
            unit/runtime/renderer/OpenGLRenderBackendTests.cpp
            unit/runtime/renderer/OpenGLRenderResourceTests.cpp
    )

    target_compile_features(HoroRenderOpenGLTests PRIVATE cxx_std_20)
    target_include_directories(HoroRenderOpenGLTests PRIVATE
            ${PROJECT_SOURCE_DIR}/src/runtime/renderer/modules/opengl
    )
    target_link_libraries(HoroRenderOpenGLTests
            PRIVATE
            HoroEngine::RenderFrontend
            HoroEngine::RenderOpenGL
    )
    if (HORO_BUILD_EDITOR_GUI)
        add_executable(HoroEditorViewportOpenGLSmoke
                integration/renderer/EditorViewportOpenGLSmoke.cpp
        )

        target_compile_features(HoroEditorViewportOpenGLSmoke PRIVATE cxx_std_20)
        target_compile_definitions(HoroEditorViewportOpenGLSmoke PRIVATE GL_SILENCE_DEPRECATION)
        target_include_directories(HoroEditorViewportOpenGLSmoke PRIVATE ${PROJECT_SOURCE_DIR}/src)
        target_link_libraries(HoroEditorViewportOpenGLSmoke
                PRIVATE
                Catch2::Catch2WithMain
                HoroEngine::EditorViewportOpenGL
                HoroEngine::RenderFrontend
                HoroThirdParty::Glad
        )

        if (HORO_ENABLE_GPU_SMOKE_TESTS)
            if (TARGET HoroEditor)
                set(HORO_OPENGL_FIRST_FRAME_HOME "${CMAKE_CURRENT_BINARY_DIR}/first-frame-home/opengl")
                file(MAKE_DIRECTORY "${HORO_OPENGL_FIRST_FRAME_HOME}")
                add_test(NAME HoroEditorOpenGLFirstFrame
                        COMMAND $<TARGET_FILE:HoroEditor> --renderer opengl --exit-after-first-frame)
                set_tests_properties(HoroEditorOpenGLFirstFrame PROPERTIES
                        LABELS "gpu;display;editor"
                        ENVIRONMENT "HOME=${HORO_OPENGL_FIRST_FRAME_HOME};USERPROFILE=${HORO_OPENGL_FIRST_FRAME_HOME}"
                )
            endif ()
        endif ()
    endif ()
endif ()

if (HORO_BUILD_RENDER_METAL)
    add_executable(HoroRenderMetalTests
            unit/runtime/renderer/MetalRenderBackendTests.cpp
    )

    target_compile_features(HoroRenderMetalTests PRIVATE cxx_std_20)
    target_include_directories(HoroRenderMetalTests PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(HoroRenderMetalTests
            PRIVATE
            HoroEngine::RenderFrontend
            HoroEngine::RenderMetal
    )
    if (HORO_BUILD_EDITOR_GUI)
        add_executable(HoroEditorViewportMetalSmoke
                integration/renderer/EditorViewportMetalSmoke.mm
        )

        target_compile_features(HoroEditorViewportMetalSmoke PRIVATE cxx_std_20)
        target_include_directories(HoroEditorViewportMetalSmoke PRIVATE ${PROJECT_SOURCE_DIR}/src)
        target_link_libraries(HoroEditorViewportMetalSmoke
                PRIVATE
                Catch2::Catch2WithMain
                HoroEngine::EditorViewportMetal
                HoroEngine::RenderFrontend
                HoroEngine::RenderMetal
                ${HORO_SDL3_TARGET}
                "-framework Metal"
                "-framework QuartzCore"
        )

        if (HORO_ENABLE_GPU_SMOKE_TESTS)
            if (TARGET HoroEditor)
                set(HORO_METAL_FIRST_FRAME_HOME "${CMAKE_CURRENT_BINARY_DIR}/first-frame-home/metal")
                file(MAKE_DIRECTORY "${HORO_METAL_FIRST_FRAME_HOME}")
                add_test(NAME HoroEditorMetalFirstFrame
                        COMMAND $<TARGET_FILE:HoroEditor> --renderer metal --exit-after-first-frame)
                set_tests_properties(HoroEditorMetalFirstFrame PROPERTIES
                        LABELS "gpu;display;metal;editor"
                        ENVIRONMENT "HOME=${HORO_METAL_FIRST_FRAME_HOME};USERPROFILE=${HORO_METAL_FIRST_FRAME_HOME}"
                        FAIL_REGULAR_EXPRESSION "Runtime frame failed"
                )
            endif ()
        endif ()
    endif ()
endif ()

set(HORO_CATCH_TEST_TARGETS
        HoroInputTests
        HoroGameplayRuntimeTests
        HoroGameModuleHostTests
        HoroLuaBehaviorTests
        HoroEditorPlaySessionControllerTests
        HoroProjectGameplayRegistryTests
        HoroProjectIntegrityValidatorServiceTests
        HoroSceneMathTests
        HoroFoundationStringTests
        HoroFoundationSha256Tests
        HoroStructuredLogStoreTests
        HoroObservabilityTests
        HoroHostObservabilityTests
        HoroOpenTelemetryTests
        HoroActivityStoreTests
        HoroPrimitiveMeshTests
        HoroEditorWelcomeControllerTests
        HoroEditorModalHostTests
        HoroGuiScreenHostLifecycleTests
        HoroSettingsModalLifecycleTests
        HoroAssetImportModalLifecycleTests
        HoroEditorLocalizationServiceTests
        HoroProjectCreationControllerTests
        HoroRendererAvailabilityTests
        HoroProjectMetadataTests
        HoroProjectCompatibilityTests
        HoroProjectMigrationTests
        HoroProjectMigrationTransactionTests
        HoroProjectOpenServiceTests
        HoroRecentProjectInspectionServiceTests
        HoroProjectCreationServiceTests
        HoroEditorSettingsStoreTests
        HoroWorkspaceLayoutTests
        HoroWorkspacePanelHostTests
        HoroWorkspaceLayoutPersistenceTests
        HoroActivityBarLayoutTests
        HoroWorkspaceSplitterInteractionTests
        HoroEditorWorkspaceControllerTests
        HoroGameplayBehaviorFilenameModalStateTests
        HoroNotificationServiceTests
        HoroEditorMenuModelTests
        HoroEditorStatusBarModelTests
        HoroHierarchyModelTests
        HoroEditorSelectionModelTests
        HoroEditorViewportModelTests
        HoroHierarchyPanelRenderTests
        HoroHierarchyEditSessionTests
        HoroInspectorEditSessionTests
        HoroInspectorPanelRenderTests
        HoroGlobalDockPanelRenderTests
        HoroAssetBrowserInteractionSessionTests
        HoroEditorUiComponentsRenderTests
        HoroViewportPanelRenderTests
        HoroDataBusTests
        HoroFoundationResultTests
        HoroFoundationModuleDescriptorTests
        HoroFoundationModuleHostCompositionTests
        HoroHostModuleCompositionTests
        HoroFoundationPathsTimeHandlesTests
        HoroFoundationCancellationProgressTests
        HoroFoundationConfigurationTests
        HoroFoundationDiagnosticsTests
        HoroFoundationJobSystemTests
        HoroPlatformTests
        HoroGameplayBuildServiceTests
        HoroEditorViewportSceneTests
        HoroSceneDocumentTests
        HoroSceneDocumentPersistenceTests
        HoroEditorViewportPickingTests
        HoroAssetSceneDropTests
        HoroRenderBackendRegistryTests
        HoroRenderAdapterTests
        HoroRenderGraphTests
        HoroRenderSubmissionTests
        HoroRenderFrontendTests
        HoroEditorViewportResourceTests
        HoroRuntimeLifecycleTests
        HoroRuntimeSaveIdentityTests
        HoroRuntimeSceneTests
        HoroAssetRegistryTests
        HoroAssetProviderTests
        HoroAssetCookTests
        HoroCookCatalogTests
        HoroHeadlessNullCookerTests
        HoroAssetCookCacheTests
        HoroAssetCookOutputTests
        HoroAssetCookServiceTests
        HoroAssetCookIntegrationTests
        HoroAssetImporterCatalogTests
        HoroAssetImportOperationTests
        HoroModularAssetImportIntegrationTests
        HoroObjMeshImporterTests
        HoroFbxMeshImporterTests
        HoroRenderOpenGLTests
        HoroRenderMetalTests
)

foreach (target IN LISTS HORO_CATCH_TEST_TARGETS)
    if (TARGET ${target})
        horo_register_catch_test(${target} LABELS "native")
    endif ()
endforeach ()

if (HORO_ENABLE_GPU_SMOKE_TESTS)
    if (TARGET HoroEditorViewportOpenGLSmoke)
        horo_register_catch_test(HoroEditorViewportOpenGLSmoke LABELS "gpu;display;opengl")
    endif ()
    if (TARGET HoroEditorViewportMetalSmoke)
        horo_register_catch_test(HoroEditorViewportMetalSmoke
                LABELS "gpu;display;metal"
                PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1"
        )
    endif ()
endif ()
