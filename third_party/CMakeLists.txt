
# =============================================================================
# glad — OpenGL 加载器
# =============================================================================
add_library(glad2 STATIC glad/src/gl.c)
target_include_directories(glad2 PUBLIC glad/include)
target_compile_definitions(glad2 PUBLIC GLAD_GLAPI_EXPORT)

# =============================================================================
# glm — 数学库
# =============================================================================
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory(glm)

# =============================================================================
# glfw — 窗口 / 输入
# =============================================================================
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(glfw)

# =============================================================================
# stb — 图片加载
# =============================================================================
add_subdirectory(stb)

# =============================================================================
# box2d — 2D 物理引擎
# =============================================================================
# 使用本地副本（已移除 extern/glfw 避免与项目自带 glfw 冲突）
set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_TESTBED    OFF CACHE BOOL "" FORCE)
add_subdirectory(box2d)

# =============================================================================
# OpenAL Soft — 3D 音频引擎
# =============================================================================
# 使用本地子模块（由 git submodule update --init 拉取）
set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ALSOFT_TESTS   OFF CACHE BOOL "" FORCE)
set(ALSOFT_UTILS   OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(openal-soft)
