if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(GLFW_COCOA_FILE "${SOURCE_DIR}/src/cocoa_window.m")
if(NOT EXISTS "${GLFW_COCOA_FILE}")
  message(FATAL_ERROR "GLFW cocoa source not found: ${GLFW_COCOA_FILE}")
endif()

file(READ "${GLFW_COCOA_FILE}" CONTENT)
set(ORIGINAL_CONTENT "${CONTENT}")

function(insert_unused_casts signature_block insert_block)
  string(FIND "${CONTENT}" "${signature_block}" SIG_POS)
  if(SIG_POS LESS 0)
    message(FATAL_ERROR "Expected function signature not found in GLFW source")
  endif()

  string(FIND "${CONTENT}" "${insert_block}" CAST_POS)
  if(CAST_POS LESS 0)
    string(REPLACE "${signature_block}" "${signature_block}${insert_block}" CONTENT "${CONTENT}")
    set(CONTENT "${CONTENT}" PARENT_SCOPE)
  endif()
endfunction()

insert_unused_casts(
"void _glfwSetWindowIconCocoa(_GLFWwindow* window,\n                             int count, const GLFWimage* images)\n{\n"
"    (void) window;\n    (void) count;\n    (void) images;\n\n")

insert_unused_casts(
"void _glfwRequestWindowAttentionCocoa(_GLFWwindow* window)\n{\n"
"    (void) window;\n\n")

insert_unused_casts(
"void _glfwSetWindowMonitorCocoa(_GLFWwindow* window,\n                                _GLFWmonitor* monitor,\n                                int xpos, int ypos,\n                                int width, int height,\n                                int refreshRate)\n{\n"
"    (void) refreshRate;\n\n")

insert_unused_casts(
"void _glfwSetRawMouseMotionCocoa(_GLFWwindow *window, GLFWbool enabled)\n{\n"
"    (void) window;\n    (void) enabled;\n\n")

insert_unused_casts(
"void _glfwSetCursorCocoa(_GLFWwindow* window, _GLFWcursor* cursor)\n{\n"
"    (void) cursor;\n\n")

insert_unused_casts(
"GLFWbool _glfwGetPhysicalDevicePresentationSupportCocoa(VkInstance instance,\n                                                        VkPhysicalDevice device,\n                                                        uint32_t queuefamily)\n{\n"
"    (void) instance;\n    (void) device;\n    (void) queuefamily;\n\n")

if(NOT CONTENT STREQUAL ORIGINAL_CONTENT)
  file(WRITE "${GLFW_COCOA_FILE}" "${CONTENT}")
  message(STATUS "Applied GLFW unused-parameter cleanup patch")
else()
  message(STATUS "GLFW unused-parameter cleanup patch already applied")
endif()
