# The graphical executable is an optional composition root. Foundation and
# headless hosts must remain buildable when the GUI adapter is intentionally off.
if(HORO_BUILD_EDITOR_GUI)
    add_executable(HoroEditor
        HoroEditor/main.cpp
    )

    target_compile_features(HoroEditor PRIVATE cxx_std_20)

    target_link_libraries(HoroEditor
        PRIVATE
            HoroEngine::EditorGuiBackend
    )
endif()
