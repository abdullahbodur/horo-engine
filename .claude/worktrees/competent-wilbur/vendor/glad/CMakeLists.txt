add_library(glad STATIC src/glad.c)
target_include_directories(glad PUBLIC include)

# glad.c uses platform-specific dynamic loading
if(WIN32)
    # nothing extra needed on Windows
elseif(UNIX AND NOT APPLE)
    target_link_libraries(glad PUBLIC dl)
endif()
