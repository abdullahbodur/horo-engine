add_executable(HoroEditor
    HoroEditor/main.cpp
)

target_compile_features(HoroEditor PRIVATE cxx_std_20)

target_link_libraries(HoroEditor
    PRIVATE
        HoroEngine::EditorGuiBackend
)
