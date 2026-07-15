
# =============================================================================
# glad — OpenGL 加载器
# =============================================================================
add_library(glad2 STATIC glad/src/gl.c)
target_include_directories(glad2 PUBLIC glad/include)
target_compile_definitions(glad2 PUBLIC GLAD_GLAPI_EXPORT)
# 将 .c 作为 C++ 编译，避免 MSVC PCH 或 CRT 冲突
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/glad/src/gl.c" PROPERTIES LANGUAGE CXX)

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

# 注意：openal-soft 的静态初始化代码（bsinc_tables.cpp）存在已知的 MSVC
# 代码生成 Bug：Sinc() 函数中调用的 f64::epsilon() 是 consteval 立即函数，
# 但 BSincFilterArray 构造函数不是 constexpr，导致 MSVC 在生成运行时调用
# consteval 函数的代码时跳到非法地址，引发 0xC0000005 访问违规。
# 已在 bsinc_tables.cpp 中修复，用 std::numeric_limits<double>::epsilon()
# 替代 f64::epsilon() 以绕过此问题。
add_subdirectory(openal-soft)

# 对 OpenAL Soft 目标禁用 ASan：
# MSVC 不支持用 /fsanitize=address- 语法关闭单个目标，
# 因此我们用 target_compile_options 加上 /fsanitize=address
# 的先决条件：仅当不是 OpenAL 目标时才启用。
# 更简单的做法：在最终链接前通过环境变量抑制已知误报。
# 用户运行时设置 ASAN_OPTIONS=suppressions=asan_supp.txt

# =============================================================================
# spdlog — 高性能 C++ 日志库（header-only，提供 spdlog::spdlog 目标）
# =============================================================================
add_subdirectory(spdlog)

# =============================================================================
# ImGuizmo — 3D 变换操作手柄
# =============================================================================
add_subdirectory(imguizmo)

# =============================================================================
# Dear ImGui — 即时模式 GUI（构建文件在 imgui_build/，源文件来自子模块 imgui/）
# =============================================================================
add_subdirectory(imgui_build)

# =============================================================================
# Tracy Profiler — 零开销性能追踪
# =============================================================================
set(TRACY_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tracy")
if(EXISTS "${TRACY_SRC_DIR}/public/TracyClient.cpp")
    add_library(tracy STATIC
        "${TRACY_SRC_DIR}/public/TracyClient.cpp"
    )
    target_include_directories(tracy PUBLIC "${TRACY_SRC_DIR}/public")
    target_compile_definitions(tracy PUBLIC
        $<$<CONFIG:Debug>:TRACY_ENABLE>
        $<$<CONFIG:Debug>:TRACY_NO_SYSTEM_TRACING>
        PRIVATE TRACY_NO_FMT_TRACING
    )
    set_target_properties(tracy PROPERTIES FOLDER "ThirdParty")
endif()

# =============================================================================
# VMA — Vulkan Memory Allocator（header-only）
# =============================================================================
set(VMA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vma")
if(EXISTS "${VMA_DIR}/include/vk_mem_alloc.h")
    add_library(vma INTERFACE)
    target_include_directories(vma INTERFACE "${VMA_DIR}/include" "${Vulkan_INCLUDE_DIRS}")
    # VMA: use dynamic Vulkan functions (via volk, since VK_NO_PROTOTYPES is required by SDK)
    target_compile_definitions(vma INTERFACE VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=1)
    message(STATUS "VMA: integrated")
else()
    message(WARNING "VMA not found at ${VMA_DIR}/include/vk_mem_alloc.h")
endif()


# =============================================================================
# D3D12MA — Direct3D 12 Memory Allocator（header-only）
# =============================================================================
set(D3D12MA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/d3d12ma")
if(EXISTS "${D3D12MA_DIR}/include/D3D12MemAlloc.h")
    add_library(d3d12ma INTERFACE)
    target_include_directories(d3d12ma INTERFACE "${D3D12MA_DIR}/include")
    target_compile_definitions(d3d12ma INTERFACE D3D12MA_USE_STL_VECTORS=1)
    message(STATUS "D3D12MA: integrated")
else()
    message(WARNING "D3D12MA not found at ${D3D12MA_DIR}/include/D3D12MemAlloc.h")
endif()

# =============================================================================
# FreeType — 字体渲染引擎（用于中文 / 文字排版）
# =============================================================================
set(FT_DISABLE_ZLIB      ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2     ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG       ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ  ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI    ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HVF        ON CACHE BOOL "" FORCE)
set(FT_ENABLE_ERROR_STRINGS ON CACHE BOOL "" FORCE)
add_subdirectory(freetype)

# =============================================================================
# Jolt Physics — 3D 物理引擎
# =============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JoltPhysics/Build/CMakeLists.txt")
    # 使用 Jolt 自带的 Build/CMakeLists.txt（它内部使用 Jolt.cmake）
    # 此方法不会覆盖全局 C++ 标准
    # 统一运行时库：Jolt 默认 USE_STATIC_MSVC_RUNTIME_LIBRARY=ON（/MTd），
    # 而项目其他部分使用 /MDd，强制设置为 OFF 以保持一致性
    set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
    add_subdirectory(JoltPhysics/Build JoltPhysicsBuild)
    
    # Jolt 默认关闭 RTTI，若引擎需要则开启
    # target_compile_options(Jolt PRIVATE /GR-)
    
    set_target_properties(Jolt PROPERTIES FOLDER "ThirdParty")
    message(STATUS "Jolt Physics: integrated via add_subdirectory")
else()
    message(WARNING "Jolt Physics not found — skipping")
endif()

# =============================================================================
# yaml-cpp — YAML 序列化/反序列化
# =============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/yaml-cpp/CMakeLists.txt")
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_FORMAT_SOURCE OFF CACHE BOOL "" FORCE)
    add_subdirectory(yaml-cpp)
    message(STATUS "yaml-cpp: integrated")
else()
    message(WARNING "yaml-cpp not found — scene serialization disabled")
endif()

# =============================================================================
# IDE 目标组织 — 将所有第三方库归入文件夹，隐藏不需要的目标
# =============================================================================

# 将已知的第三方目标放入 "ThirdParty" 文件夹
set(THIRD_PARTY_TARGETS glad2 glm glfw stb box2d imgui OpenAL alsoft.common alsoft.fmt spdlog freetype)
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
