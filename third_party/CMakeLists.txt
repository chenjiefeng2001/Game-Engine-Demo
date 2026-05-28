
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
# box2d 的 samples/tests 由 PROJECT_IS_TOP_LEVEL 保护，作为子目录时自动跳过
add_subdirectory(box2d)

# =============================================================================
# OpenAL Soft — 3D 音频引擎
# =============================================================================
# 使用本地子模块（由 git submodule update --init 拉取）
set(ALSOFT_EXAMPLES        OFF CACHE BOOL "" FORCE)
set(ALSOFT_TESTS           OFF CACHE BOOL "" FORCE)
set(ALSOFT_UTILS           OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL         OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_UTILS   OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_CONFIG  OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_HRTF_DATA OFF CACHE BOOL "" FORCE)
add_subdirectory(openal-soft)

# =============================================================================
# Dear ImGui — 即时模式 GUI（构建文件在 imgui_build/，源文件来自子模块 imgui/）
# =============================================================================
add_subdirectory(imgui_build)

# =============================================================================
# IDE 目标组织 — 将所有第三方库归入文件夹，隐藏不需要的目标
# =============================================================================

# 将已知的第三方目标放入 "ThirdParty" 文件夹
set(THIRD_PARTY_TARGETS glad2 glm glfw stb box2d imgui OpenAL alsoft.common alsoft.fmt)
foreach(target ${THIRD_PARTY_TARGETS})
    if(TARGET ${target})
        set_target_properties(${target} PROPERTIES FOLDER "ThirdParty")
    endif()
endforeach()

# 隐藏 OpenAL Soft 的示例公共库（被 ALSOFT_EXAMPLES=OFF 理应禁用的残留目标）
if(TARGET alsoft.excommon)
    set_target_properties(alsoft.excommon PROPERTIES
        EXCLUDE_FROM_ALL TRUE
        EXCLUDE_FROM_DEFAULT_BUILD TRUE
        FOLDER "ThirdParty/_Hidden"
    )
endif()
