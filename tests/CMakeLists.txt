include(Catch)

execute_process(
        COMMAND "${Python3_EXECUTABLE}" -c "import pytest; print(pytest.__version__)"
        RESULT_VARIABLE HORO_PYTEST_IMPORT_RESULT
        OUTPUT_VARIABLE HORO_PYTEST_VERSION
        ERROR_VARIABLE HORO_PYTEST_IMPORT_ERROR
        OUTPUT_STRIP_TRAILING_WHITESPACE
)
if (NOT HORO_PYTEST_IMPORT_RESULT EQUAL 0)
    message(FATAL_ERROR
            "BUILD_TESTING requires pytest for repository Python tests. "
            "Install the declared test dependencies with: "
            "${Python3_EXECUTABLE} -m pip install -r ${PROJECT_SOURCE_DIR}/scripts/requirements.txt\n"
            "Python error: ${HORO_PYTEST_IMPORT_ERROR}"
    )
endif ()
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

function(horo_register_catch_test target)
    cmake_parse_arguments(ARG "" "" "LABELS;EXTRA_ARGS;PROPERTIES" ${ARGN})
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_link_libraries(${target} PRIVATE Catch2::Catch2WithMain)
    catch_discover_tests(${target}
            TEST_PREFIX "${target}::"
            EXTRA_ARGS ${ARG_EXTRA_ARGS}
            PROPERTIES LABELS "${ARG_LABELS}" ${ARG_PROPERTIES}
    )
endfunction()

if(HORO_ENABLE_IMGUI_UI_TESTS)
    if(NOT TARGET HoroThirdPartyImGui)
        message(FATAL_ERROR "HoroEditorUiAutomationTests requires instrumented HoroThirdPartyImGui")
    endif()
    get_target_property(HORO_IMGUI_TEST_ENGINE_DEFINITIONS HoroThirdPartyImGui COMPILE_DEFINITIONS)
    if(NOT "IMGUI_ENABLE_TEST_ENGINE" IN_LIST HORO_IMGUI_TEST_ENGINE_DEFINITIONS)
        message(FATAL_ERROR "HoroEditorUiAutomationTests requires IMGUI_ENABLE_TEST_ENGINE")
    endif()

    add_executable(HoroEditorUiAutomationTests
        gui/EditorUiAutomationHarnessTests.cpp
        gui/FullEditorUiAutomationScenarios.cpp
        helpers/editor_ui/EditorUiTestHarness.cpp
        helpers/editor_ui/EditorUiTestSurface.cpp
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
    )
    target_link_libraries(HoroEditorUiAutomationTests PRIVATE
        HoroEngine::Gui
        HoroEngine::ProjectMigrations
        HoroThirdParty::ImGui
    )
    if(HORO_BUILD_RENDER_OPENGL)
        target_sources(HoroEditorUiAutomationTests PRIVATE
            helpers/editor_ui/InteractiveOpenGlEditorUiTestSurface.cpp
        )
        target_compile_definitions(HoroEditorUiAutomationTests PRIVATE
            HORO_UI_TEST_HAS_INTERACTIVE_OPENGL=1
        )
        target_link_libraries(HoroEditorUiAutomationTests PRIVATE
            HoroEngine::EditorViewportOpenGL
            HoroThirdParty::Glad
        )
    endif()
    horo_register_catch_test(HoroEditorUiAutomationTests
        LABELS "gui;headless;imgui"
        PROPERTIES TIMEOUT 120
    )
endif()

add_executable(HoroInputTests
        unit/runtime/input/InputTests.cpp
)
target_compile_features(HoroInputTests PRIVATE cxx_std_20)
target_link_libraries(HoroInputTests PRIVATE HoroEngine::Input)
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
endif ()

add_executable(HoroSettingsModalLifecycleTests
        unit/editor/SettingsModalLifecycleTests.cpp
)

target_compile_features(HoroSettingsModalLifecycleTests PRIVATE cxx_std_20)
target_link_libraries(HoroSettingsModalLifecycleTests
        PRIVATE
        HoroEngine::EditorServices
)
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
    target_link_libraries(HoroEditorWorkspaceControllerTests PRIVATE HoroEngine::Gui)
endif ()

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
    add_executable(HoroContentBrowserPanelRenderTests
            unit/editor/ContentBrowserPanelRenderTests.cpp
    )

    target_compile_features(HoroContentBrowserPanelRenderTests PRIVATE cxx_std_20)
    target_include_directories(HoroContentBrowserPanelRenderTests PRIVATE
            ${PROJECT_SOURCE_DIR}/src/editor/screens/workspace
    )
    target_link_libraries(HoroContentBrowserPanelRenderTests
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
add_executable(HoroEditorViewportPickingTests
        unit/editor/EditorViewportPickingTests.cpp
)
target_compile_features(HoroEditorViewportPickingTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorViewportPickingTests PRIVATE HoroEngine::EditorRenderExtraction)
add_executable(HoroRenderBackendRegistryTests
        unit/runtime/renderer/RenderBackendRegistryTests.cpp
)

target_compile_features(HoroRenderBackendRegistryTests PRIVATE cxx_std_20)
target_link_libraries(HoroRenderBackendRegistryTests
        PRIVATE
        HoroEngine::RenderNull
)
add_executable(HoroRenderFrontendTests
        unit/runtime/renderer/RenderFrontendTests.cpp
)

target_compile_features(HoroRenderFrontendTests PRIVATE cxx_std_20)
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
function(horo_add_public_header_compile_target target owning_target)
    set(generated_sources)
    foreach (header IN LISTS ARGN)
        string(REPLACE "/" "_" source_name "${header}")
        set(source "${CMAKE_CURRENT_BINARY_DIR}/header_compile/${source_name}.cpp")
        file(GENERATE OUTPUT "${source}" CONTENT "#include \"${header}\"\n")
        list(APPEND generated_sources "${source}")
    endforeach ()
    add_library(${target} OBJECT ${generated_sources})
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_link_libraries(${target} PRIVATE ${owning_target})
endfunction()

horo_add_public_header_compile_target(HoroRuntimePublicHeaderCompile HoroEngine::RuntimeScene
        Horo/Runtime/FrameScheduler.h
        Horo/Runtime/RuntimeHost.h
        Horo/Runtime/RuntimeLifecycle.h
        Horo/Runtime/Scene/RuntimeScene.h
        Horo/Runtime/Scene/RuntimeSceneDefinition.h
)
horo_add_public_header_compile_target(HoroAssetsPublicHeaderCompile HoroEngine::Assets
        Horo/Assets/AssetId.h
        Horo/Assets/AssetRegistry.h
        Horo/Assets/AssetProvider.h
)
horo_add_public_header_compile_target(HoroApplicationPublicHeaderCompile HoroEngine::Application
        Horo/Application/ProjectVersion.h
        Horo/Application/ProjectCompatibility.h
        Horo/Application/ProjectMigration.h
        Horo/Application/ProjectMigrationCatalog.h
)
horo_add_public_header_compile_target(HoroPlatformPublicHeaderCompile HoroEngine::Platform
        Horo/Foundation/Platform.h
)
horo_add_public_header_compile_target(HoroEditorServicesPublicHeaderCompile HoroEngine::EditorServices
        Horo/Editor/ProjectMutation.h
        Horo/Editor/ProjectMigrationTransaction.h
        Horo/Editor/ProjectSession.h
        Horo/Editor/ProjectOpenService.h
        Horo/Editor/RecentProjectInspectionService.h
)

if (HORO_BUILD_RENDER_OPENGL)
    add_executable(HoroRenderOpenGLTests
            unit/runtime/renderer/OpenGLRenderBackendTests.cpp
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
        HoroSceneMathTests
        HoroFoundationStringTests
        HoroPrimitiveMeshTests
        HoroEditorWelcomeControllerTests
        HoroEditorModalHostTests
        HoroGuiScreenHostLifecycleTests
        HoroSettingsModalLifecycleTests
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
        HoroEditorMenuModelTests
        HoroEditorStatusBarModelTests
        HoroHierarchyModelTests
        HoroEditorSelectionModelTests
        HoroEditorViewportModelTests
        HoroHierarchyPanelRenderTests
        HoroInspectorPanelRenderTests
        HoroContentBrowserPanelRenderTests
        HoroViewportPanelRenderTests
        HoroDataBusTests
        HoroFoundationResultTests
        HoroFoundationPathsTimeHandlesTests
        HoroFoundationCancellationProgressTests
        HoroFoundationConfigurationTests
        HoroFoundationDiagnosticsTests
        HoroFoundationJobSystemTests
        HoroPlatformTests
        HoroEditorViewportSceneTests
        HoroSceneDocumentTests
        HoroEditorViewportPickingTests
        HoroRenderBackendRegistryTests
        HoroRenderFrontendTests
        HoroRuntimeLifecycleTests
        HoroRuntimeSceneTests
        HoroAssetRegistryTests
        HoroAssetProviderTests
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
