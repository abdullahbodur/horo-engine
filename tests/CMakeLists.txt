add_executable(HoroInputTests
    unit/runtime/input/InputTests.cpp
)
target_compile_features(HoroInputTests PRIVATE cxx_std_20)
target_link_libraries(HoroInputTests PRIVATE HoroEngine::Input)
add_test(NAME HoroInputTests COMMAND HoroInputTests)

add_executable(HoroSceneMathTests
    unit/foundation/SceneMathTests.cpp
)
target_compile_features(HoroSceneMathTests PRIVATE cxx_std_20)
target_link_libraries(HoroSceneMathTests PRIVATE HoroEngine::Foundation)
add_test(NAME HoroSceneMathTests COMMAND HoroSceneMathTests)

add_executable(HoroFoundationStringTests
    unit/foundation/StringTests.cpp
)
target_compile_features(HoroFoundationStringTests PRIVATE cxx_std_20)
target_link_libraries(HoroFoundationStringTests PRIVATE HoroEngine::Foundation)
add_test(NAME HoroFoundationStringTests COMMAND HoroFoundationStringTests)

add_executable(HoroPrimitiveMeshTests
    unit/runtime/scene/PrimitiveMeshTests.cpp
)
find_package(Threads REQUIRED)
target_compile_features(HoroPrimitiveMeshTests PRIVATE cxx_std_20)
target_link_libraries(HoroPrimitiveMeshTests PRIVATE HoroEngine::SceneModel Threads::Threads)
add_test(NAME HoroPrimitiveMeshTests COMMAND HoroPrimitiveMeshTests)

add_executable(HoroEditorWelcomeControllerTests
    unit/editor/WelcomeControllerTests.cpp
)

target_compile_features(HoroEditorWelcomeControllerTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorWelcomeControllerTests
    PRIVATE
        HoroEngine::EditorServices
)

add_test(NAME HoroEditorWelcomeControllerTests COMMAND HoroEditorWelcomeControllerTests)

add_executable(HoroEditorModalHostTests
    unit/editor/EditorModalHostTests.cpp
)

target_compile_features(HoroEditorModalHostTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorModalHostTests
    PRIVATE
        HoroEngine::EditorServices
)

add_test(NAME HoroEditorModalHostTests COMMAND HoroEditorModalHostTests)

if (HORO_BUILD_EDITOR_GUI)
    add_executable(HoroGuiScreenHostLifecycleTests
        unit/editor/GuiScreenHostLifecycleTests.cpp
    )
    target_compile_features(HoroGuiScreenHostLifecycleTests PRIVATE cxx_std_20)
    target_link_libraries(HoroGuiScreenHostLifecycleTests PRIVATE HoroEngine::Gui)
    add_test(NAME HoroGuiScreenHostLifecycleTests COMMAND HoroGuiScreenHostLifecycleTests)
endif ()

add_executable(HoroSettingsModalLifecycleTests
    unit/editor/SettingsModalLifecycleTests.cpp
)

target_compile_features(HoroSettingsModalLifecycleTests PRIVATE cxx_std_20)
target_link_libraries(HoroSettingsModalLifecycleTests
    PRIVATE
        HoroEngine::EditorServices
)

add_test(NAME HoroSettingsModalLifecycleTests COMMAND HoroSettingsModalLifecycleTests)

add_executable(HoroEditorLocalizationServiceTests
    unit/editor/LocalizationServiceTests.cpp
)

target_compile_features(HoroEditorLocalizationServiceTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorLocalizationServiceTests
    PRIVATE
        HoroEngine::EditorServices
)

add_test(NAME HoroEditorLocalizationServiceTests COMMAND HoroEditorLocalizationServiceTests)

add_executable(HoroProjectCreationControllerTests
    unit/editor/ProjectCreationControllerTests.cpp
)

target_compile_features(HoroProjectCreationControllerTests PRIVATE cxx_std_20)
target_link_libraries(HoroProjectCreationControllerTests
    PRIVATE
        HoroEngine::EditorServices
)

add_test(NAME HoroProjectCreationControllerTests COMMAND HoroProjectCreationControllerTests)

add_executable(HoroRendererAvailabilityTests
    unit/editor/RendererAvailabilityTests.cpp
)
target_compile_features(HoroRendererAvailabilityTests PRIVATE cxx_std_20)
target_link_libraries(HoroRendererAvailabilityTests PRIVATE HoroEngine::EditorServices)
add_test(NAME HoroRendererAvailabilityTests COMMAND HoroRendererAvailabilityTests)

add_executable(HoroProjectMetadataTests
    unit/editor/ProjectMetadataTests.cpp
)
target_compile_features(HoroProjectMetadataTests PRIVATE cxx_std_20)
target_link_libraries(HoroProjectMetadataTests PRIVATE HoroEngine::EditorServices)
add_test(NAME HoroProjectMetadataTests COMMAND HoroProjectMetadataTests)

add_executable(HoroProjectCreationServiceTests
    unit/editor/ProjectCreationServiceTests.cpp
)

target_compile_features(HoroProjectCreationServiceTests PRIVATE cxx_std_20)
target_link_libraries(HoroProjectCreationServiceTests
    PRIVATE
        HoroEngine::EditorServices
)

add_test(NAME HoroProjectCreationServiceTests COMMAND HoroProjectCreationServiceTests)

add_executable(HoroEditorSettingsStoreTests
    unit/editor/EditorSettingsStoreTests.cpp
)

target_compile_features(HoroEditorSettingsStoreTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorSettingsStoreTests
    PRIVATE
        HoroEngine::EditorServices
)

add_test(NAME HoroEditorSettingsStoreTests COMMAND HoroEditorSettingsStoreTests)

add_executable(HoroWorkspaceLayoutTests
    unit/editor/WorkspaceLayoutTests.cpp
)
target_compile_features(HoroWorkspaceLayoutTests PRIVATE cxx_std_20)
target_link_libraries(HoroWorkspaceLayoutTests PRIVATE HoroEngine::EditorServices)
add_test(NAME HoroWorkspaceLayoutTests COMMAND HoroWorkspaceLayoutTests)

add_executable(HoroWorkspacePanelHostTests
    unit/editor/WorkspacePanelHostTests.cpp
)
target_link_libraries(HoroWorkspacePanelHostTests PRIVATE HoroEngine::EditorServices)
add_test(NAME HoroWorkspacePanelHostTests COMMAND HoroWorkspacePanelHostTests)

add_executable(HoroWorkspaceLayoutPersistenceTests
    unit/editor/WorkspaceLayoutPersistenceTests.cpp
)
target_link_libraries(HoroWorkspaceLayoutPersistenceTests PRIVATE HoroEngine::EditorServices)
add_test(NAME HoroWorkspaceLayoutPersistenceTests COMMAND HoroWorkspaceLayoutPersistenceTests)

add_executable(HoroActivityBarLayoutTests
    unit/editor/ActivityBarLayoutTests.cpp
)
target_compile_features(HoroActivityBarLayoutTests PRIVATE cxx_std_20)
target_link_libraries(HoroActivityBarLayoutTests PRIVATE HoroEngine::EditorServices)
add_test(NAME HoroActivityBarLayoutTests COMMAND HoroActivityBarLayoutTests)

add_executable(HoroWorkspaceSplitterInteractionTests
    unit/editor/WorkspaceSplitterInteractionTests.cpp
)
target_compile_features(HoroWorkspaceSplitterInteractionTests PRIVATE cxx_std_20)
target_include_directories(HoroWorkspaceSplitterInteractionTests PRIVATE
    ${PROJECT_SOURCE_DIR}/src/editor/screens/workspace
)
target_link_libraries(HoroWorkspaceSplitterInteractionTests PRIVATE HoroEngine::EditorServices)
add_test(NAME HoroWorkspaceSplitterInteractionTests COMMAND HoroWorkspaceSplitterInteractionTests)

if (HORO_BUILD_EDITOR_GUI)
    add_executable(HoroEditorWorkspaceControllerTests
        unit/editor/EditorWorkspaceControllerTests.cpp
    )

    target_compile_features(HoroEditorWorkspaceControllerTests PRIVATE cxx_std_20)
    target_link_libraries(HoroEditorWorkspaceControllerTests PRIVATE HoroEngine::Gui)

    add_test(NAME HoroEditorWorkspaceControllerTests COMMAND HoroEditorWorkspaceControllerTests)
endif ()

add_executable(HoroEditorMenuModelTests
    unit/editor/EditorMenuModelTests.cpp
)

target_compile_features(HoroEditorMenuModelTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorMenuModelTests PRIVATE HoroEngine::EditorServices)

add_test(NAME HoroEditorMenuModelTests COMMAND HoroEditorMenuModelTests)

add_executable(HoroEditorStatusBarModelTests
    unit/editor/EditorStatusBarModelTests.cpp
)

target_compile_features(HoroEditorStatusBarModelTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorStatusBarModelTests PRIVATE HoroEngine::EditorServices)

add_test(NAME HoroEditorStatusBarModelTests COMMAND HoroEditorStatusBarModelTests)

add_executable(HoroHierarchyModelTests
    unit/editor/HierarchyModelTests.cpp
)

target_compile_features(HoroHierarchyModelTests PRIVATE cxx_std_20)
target_link_libraries(HoroHierarchyModelTests PRIVATE HoroEngine::EditorServices)

add_test(NAME HoroHierarchyModelTests COMMAND HoroHierarchyModelTests)

add_executable(HoroEditorSelectionModelTests
    unit/editor/EditorSelectionModelTests.cpp
)

target_compile_features(HoroEditorSelectionModelTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorSelectionModelTests PRIVATE HoroEngine::EditorServices)

add_test(NAME HoroEditorSelectionModelTests COMMAND HoroEditorSelectionModelTests)

add_executable(HoroEditorViewportModelTests
    unit/editor/EditorViewportModelTests.cpp
)

target_compile_features(HoroEditorViewportModelTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorViewportModelTests PRIVATE HoroEngine::EditorServices)

add_test(NAME HoroEditorViewportModelTests COMMAND HoroEditorViewportModelTests)

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

    add_test(NAME HoroHierarchyPanelRenderTests COMMAND HoroHierarchyPanelRenderTests)

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

    add_test(NAME HoroInspectorPanelRenderTests COMMAND HoroInspectorPanelRenderTests)

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

    add_test(NAME HoroContentBrowserPanelRenderTests COMMAND HoroContentBrowserPanelRenderTests)

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

    add_test(NAME HoroViewportPanelRenderTests COMMAND HoroViewportPanelRenderTests)
endif ()

add_executable(HoroDataBusTests
    unit/foundation/DataBusTests.cpp
)

target_compile_features(HoroDataBusTests PRIVATE cxx_std_20)
target_link_libraries(HoroDataBusTests
    PRIVATE
        HoroEngine::EditorServices
)

add_test(NAME HoroDataBusTests COMMAND HoroDataBusTests)

add_executable(HoroFoundationResultTests
    unit/foundation/ResultTests.cpp
)

target_compile_features(HoroFoundationResultTests PRIVATE cxx_std_20)
target_link_libraries(HoroFoundationResultTests PRIVATE HoroEngine::Foundation)

add_test(NAME HoroFoundationResultTests COMMAND HoroFoundationResultTests)

add_executable(HoroFoundationPathsTimeHandlesTests
    unit/foundation/PathsTimeHandlesTests.cpp
)

target_compile_features(HoroFoundationPathsTimeHandlesTests PRIVATE cxx_std_20)
target_link_libraries(HoroFoundationPathsTimeHandlesTests PRIVATE HoroEngine::Foundation)

add_test(NAME HoroFoundationPathsTimeHandlesTests COMMAND HoroFoundationPathsTimeHandlesTests)

add_executable(HoroFoundationCancellationProgressTests
    unit/foundation/CancellationProgressTests.cpp
)

target_compile_features(HoroFoundationCancellationProgressTests PRIVATE cxx_std_20)
target_link_libraries(HoroFoundationCancellationProgressTests PRIVATE HoroEngine::Foundation)

add_test(NAME HoroFoundationCancellationProgressTests COMMAND HoroFoundationCancellationProgressTests)

add_executable(HoroFoundationConfigurationTests
    unit/foundation/ConfigurationTests.cpp
)

target_compile_features(HoroFoundationConfigurationTests PRIVATE cxx_std_20)
target_link_libraries(HoroFoundationConfigurationTests PRIVATE HoroEngine::Foundation)

add_test(NAME HoroFoundationConfigurationTests COMMAND HoroFoundationConfigurationTests)

add_executable(HoroFoundationDiagnosticsTests
    unit/foundation/DiagnosticsTests.cpp
)

target_compile_features(HoroFoundationDiagnosticsTests PRIVATE cxx_std_20)
target_link_libraries(HoroFoundationDiagnosticsTests PRIVATE HoroEngine::Foundation)

add_test(NAME HoroFoundationDiagnosticsTests COMMAND HoroFoundationDiagnosticsTests)

add_executable(HoroFoundationJobSystemTests
    unit/foundation/JobSystemTests.cpp
)

target_compile_features(HoroFoundationJobSystemTests PRIVATE cxx_std_20)
target_link_libraries(HoroFoundationJobSystemTests PRIVATE HoroEngine::Foundation)

add_test(NAME HoroFoundationJobSystemTests COMMAND HoroFoundationJobSystemTests)

add_executable(HoroPlatformTests
    unit/foundation/PlatformTests.cpp
)

target_compile_features(HoroPlatformTests PRIVATE cxx_std_20)
target_link_libraries(HoroPlatformTests PRIVATE HoroEngine::Platform)

add_test(NAME HoroPlatformTests COMMAND HoroPlatformTests)

add_executable(HoroEditorViewportSceneTests
    unit/editor/EditorViewportSceneTests.cpp
)

target_compile_features(HoroEditorViewportSceneTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorViewportSceneTests PRIVATE HoroEngine::EditorViewportScene)

add_test(NAME HoroEditorViewportSceneTests COMMAND HoroEditorViewportSceneTests)

add_executable(HoroSceneDocumentTests
    unit/editor/SceneDocumentTests.cpp
)
target_compile_features(HoroSceneDocumentTests PRIVATE cxx_std_20)
target_link_libraries(HoroSceneDocumentTests PRIVATE HoroEngine::EditorRenderExtraction)
add_test(NAME HoroSceneDocumentTests COMMAND HoroSceneDocumentTests)

add_executable(HoroEditorViewportPickingTests
    unit/editor/EditorViewportPickingTests.cpp
)
target_compile_features(HoroEditorViewportPickingTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorViewportPickingTests PRIVATE HoroEngine::EditorRenderExtraction)
add_test(NAME HoroEditorViewportPickingTests COMMAND HoroEditorViewportPickingTests)

add_executable(HoroRenderBackendRegistryTests
    unit/runtime/renderer/RenderBackendRegistryTests.cpp
)

target_compile_features(HoroRenderBackendRegistryTests PRIVATE cxx_std_20)
target_link_libraries(HoroRenderBackendRegistryTests
    PRIVATE
        HoroEngine::RenderNull
)

add_test(NAME HoroRenderBackendRegistryTests COMMAND HoroRenderBackendRegistryTests)

add_executable(HoroRenderFrontendTests
    unit/runtime/renderer/RenderFrontendTests.cpp
)

target_compile_features(HoroRenderFrontendTests PRIVATE cxx_std_20)
target_link_libraries(HoroRenderFrontendTests
    PRIVATE
        HoroEngine::RenderFrontend
        HoroEngine::RenderNull
)

add_test(NAME HoroRenderFrontendTests COMMAND HoroRenderFrontendTests)

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
add_test(NAME HoroRuntimeLifecycleTests COMMAND HoroRuntimeLifecycleTests)

add_executable(HoroRuntimeSceneTests
    unit/runtime/scene/RuntimeSceneTests.cpp
)
target_compile_features(HoroRuntimeSceneTests PRIVATE cxx_std_20)
target_link_libraries(HoroRuntimeSceneTests PRIVATE HoroEngine::RuntimeScene)
add_test(NAME HoroRuntimeSceneTests COMMAND HoroRuntimeSceneTests)

if(HORO_BUILD_RENDER_OPENGL)
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

    add_test(NAME HoroRenderOpenGLTests COMMAND HoroRenderOpenGLTests)

    if(HORO_BUILD_EDITOR_GUI)
        add_executable(HoroEditorViewportOpenGLSmoke
            integration/renderer/EditorViewportOpenGLSmoke.cpp
        )

        target_compile_features(HoroEditorViewportOpenGLSmoke PRIVATE cxx_std_20)
        target_compile_definitions(HoroEditorViewportOpenGLSmoke PRIVATE GL_SILENCE_DEPRECATION)
        target_include_directories(HoroEditorViewportOpenGLSmoke PRIVATE ${PROJECT_SOURCE_DIR}/src)
        target_link_libraries(HoroEditorViewportOpenGLSmoke
            PRIVATE
                HoroEngine::EditorViewportOpenGL
                HoroEngine::RenderFrontend
                HoroThirdParty::Glad
        )

        if(HORO_ENABLE_GPU_SMOKE_TESTS)
            add_test(
                NAME HoroEditorViewportOpenGLSmoke
                COMMAND HoroEditorViewportOpenGLSmoke ${CMAKE_CURRENT_BINARY_DIR}/horo-viewport-cube.ppm
            )
            set_tests_properties(HoroEditorViewportOpenGLSmoke PROPERTIES LABELS "gpu;display")
            if(TARGET HoroEditor)
                add_test(NAME HoroEditorOpenGLFirstFrame
                         COMMAND $<TARGET_FILE:HoroEditor> --renderer opengl --exit-after-first-frame)
                set_tests_properties(HoroEditorOpenGLFirstFrame PROPERTIES LABELS "gpu;display;editor")
            endif()
        endif()
    endif()
endif()

if(HORO_BUILD_RENDER_METAL)
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

    add_test(NAME HoroRenderMetalTests COMMAND HoroRenderMetalTests)

    if(HORO_BUILD_EDITOR_GUI)
        add_executable(HoroEditorViewportMetalSmoke
            integration/renderer/EditorViewportMetalSmoke.mm
        )

        target_compile_features(HoroEditorViewportMetalSmoke PRIVATE cxx_std_20)
        target_include_directories(HoroEditorViewportMetalSmoke PRIVATE ${PROJECT_SOURCE_DIR}/src)
        target_link_libraries(HoroEditorViewportMetalSmoke
            PRIVATE
                HoroEngine::EditorViewportMetal
                HoroEngine::RenderFrontend
                HoroEngine::RenderMetal
                ${HORO_SDL3_TARGET}
                "-framework Metal"
                "-framework QuartzCore"
        )

        if(HORO_ENABLE_GPU_SMOKE_TESTS)
            add_test(NAME HoroEditorViewportMetalSmoke COMMAND HoroEditorViewportMetalSmoke)
            set_tests_properties(HoroEditorViewportMetalSmoke PROPERTIES
                LABELS "gpu;display;metal"
                ENVIRONMENT "MTL_DEBUG_LAYER=1"
            )
            if(TARGET HoroEditor)
                add_test(NAME HoroEditorMetalFirstFrame
                         COMMAND $<TARGET_FILE:HoroEditor> --renderer metal --exit-after-first-frame)
                set_tests_properties(HoroEditorMetalFirstFrame PROPERTIES LABELS "gpu;display;metal;editor")
            endif()
        endif()
    endif()
endif()
