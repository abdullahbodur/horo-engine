add_library(glad STATIC src/glad.c)
target_include_directories(glad PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/vendor/glad>
)

# glad.c uses platform-specific dynamic loading
if(WIN32)
    # nothing extra needed on Windows
elseif(UNIX AND NOT APPLE)
    target_link_libraries(glad PUBLIC dl)
endif()
