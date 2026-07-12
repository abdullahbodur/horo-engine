add_executable(HoroEditorWelcomeControllerTests
    unit/editor/WelcomeControllerTests.cpp
)

target_compile_features(HoroEditorWelcomeControllerTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorWelcomeControllerTests
    PRIVATE
        HoroEngine::EditorScreens
)

add_test(NAME HoroEditorWelcomeControllerTests COMMAND HoroEditorWelcomeControllerTests)

add_executable(HoroEditorModalHostTests
    unit/editor/EditorModalHostTests.cpp
)

target_compile_features(HoroEditorModalHostTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorModalHostTests
    PRIVATE
        HoroEngine::EditorScreens
)

add_test(NAME HoroEditorModalHostTests COMMAND HoroEditorModalHostTests)

add_executable(HoroSettingsModalLifecycleTests
    unit/editor/SettingsModalLifecycleTests.cpp
)

target_compile_features(HoroSettingsModalLifecycleTests PRIVATE cxx_std_20)
target_link_libraries(HoroSettingsModalLifecycleTests
    PRIVATE
        HoroEngine::EditorScreens
)

add_test(NAME HoroSettingsModalLifecycleTests COMMAND HoroSettingsModalLifecycleTests)

add_executable(HoroEditorLocalizationServiceTests
    unit/editor/LocalizationServiceTests.cpp
)

target_compile_features(HoroEditorLocalizationServiceTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorLocalizationServiceTests
    PRIVATE
        HoroEngine::EditorScreens
)

add_test(NAME HoroEditorLocalizationServiceTests COMMAND HoroEditorLocalizationServiceTests)

add_executable(HoroProjectCreationControllerTests
    unit/editor/ProjectCreationControllerTests.cpp
)

target_compile_features(HoroProjectCreationControllerTests PRIVATE cxx_std_20)
target_link_libraries(HoroProjectCreationControllerTests
    PRIVATE
        HoroEngine::EditorScreens
)

add_test(NAME HoroProjectCreationControllerTests COMMAND HoroProjectCreationControllerTests)

add_executable(HoroProjectCreationServiceTests
    unit/editor/ProjectCreationServiceTests.cpp
)

target_compile_features(HoroProjectCreationServiceTests PRIVATE cxx_std_20)
target_link_libraries(HoroProjectCreationServiceTests
    PRIVATE
        HoroEngine::EditorScreens
)

add_test(NAME HoroProjectCreationServiceTests COMMAND HoroProjectCreationServiceTests)

add_executable(HoroEditorSettingsStoreTests
    unit/editor/EditorSettingsStoreTests.cpp
)

target_compile_features(HoroEditorSettingsStoreTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorSettingsStoreTests
    PRIVATE
        HoroEngine::EditorScreens
)

add_test(NAME HoroEditorSettingsStoreTests COMMAND HoroEditorSettingsStoreTests)

add_executable(HoroDataBusTests
    unit/foundation/DataBusTests.cpp
)

target_compile_features(HoroDataBusTests PRIVATE cxx_std_20)
target_link_libraries(HoroDataBusTests
    PRIVATE
        HoroEngine::Foundation
        HoroEngine::EditorScreens
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
