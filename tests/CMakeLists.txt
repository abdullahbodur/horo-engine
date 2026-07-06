add_executable(HoroEditorWelcomeScreenTests
    unit/editor/WelcomeScreenTests.cpp
)

target_compile_features(HoroEditorWelcomeScreenTests PRIVATE cxx_std_20)
target_link_libraries(HoroEditorWelcomeScreenTests
    PRIVATE
        HoroEngine::EditorScreens
)

add_test(NAME HoroEditorWelcomeScreenTests COMMAND HoroEditorWelcomeScreenTests)
