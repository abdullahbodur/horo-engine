add_library(HoroAssetImporterBasicExample MODULE
        src/BasicAssetImporter.cpp
)

target_compile_features(HoroAssetImporterBasicExample PRIVATE cxx_std_20)
target_include_directories(HoroAssetImporterBasicExample PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)
set_target_properties(HoroAssetImporterBasicExample PROPERTIES
        PREFIX ""
        OUTPUT_NAME "horo_asset_importer_basic"
        SUFFIX "${CMAKE_SHARED_LIBRARY_SUFFIX}"
)

add_custom_command(TARGET HoroAssetImporterBasicExample POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/extension.json"
                "$<TARGET_FILE_DIR:HoroAssetImporterBasicExample>/extension.json"
        VERBATIM
)
