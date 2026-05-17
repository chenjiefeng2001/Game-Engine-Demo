
add_library(glad2 STATIC glad/src/gl.c)
target_include_directories(glad2 PUBLIC glad/include)
target_compile_definitions(glad2 PUBLIC GLAD_GLAPI_EXPORT)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(glfw)
add_subdirectory(stb)
