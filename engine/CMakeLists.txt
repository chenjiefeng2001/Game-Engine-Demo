# 精确 GLOB + CONFIGURE_DEPENDS（新文件自动发现，CMake 3.12+）
file(GLOB ENGINE_SOURCES CONFIGURE_DEPENDS
    src/*.cpp
    src/Animation/*.cpp
    src/Audio/*.cpp
    src/Box2D/*.cpp
    src/Core/*.cpp
    src/Core/Level/*.cpp
    src/Core/Memory/*.cpp
    src/Core/GameObject/*.cpp
    src/Core/Physics/*.cpp
    src/Core/RenderResources/*.cpp
    src/Core/Resources/*.cpp
    src/Core/Content/*.cpp
    src/Core/Renderer/*.cpp
    src/Core/RHI/*.cpp
    src/Core/Scene/*.cpp
    src/Core/ECS/*.cpp
    src/Jolt/*.cpp
    src/Debug/*.cpp
    src/Editor/*.cpp
    src/Editor/Animation/*.cpp
    src/Editor/ShaderGraph/*.cpp
    src/Editor/VFXGraph/*.cpp
    src/OpenAL/*.cpp
    src/OpenGL/*.cpp
    src/OpenGL/Resources/*.cpp
    src/Platform/*.cpp
    src/Platform/glfw/*.cpp
    src/RHI/*.cpp
    src/Rendering/*.cpp
    src/Scripting/*.cpp
)
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/D3D12")
    # D3D12 源文件 — 仅在 Windows 平台编译
    file(GLOB D3D12_SOURCES CONFIGURE_DEPENDS src/D3D12/*.cpp)
    list(APPEND ENGINE_SOURCES ${D3D12_SOURCES})
endif()
file(GLOB ENGINE_HEADERS CONFIGURE_DEPENDS
    include/*.h
    include/Engine/*.h
    include/Engine/Animation/*.h
    include/Engine/Core/*.h
    include/Engine/Core/Level/*.h
    include/Engine/Core/Memory/*.h
    include/Engine/Core/Containers/*.h
    include/Engine/Core/GameObject/*.h
    include/Engine/Core/ECS/*.h
    include/Engine/Core/Physics/*.h
    include/Engine/Core/RenderResources/*.h
    include/Engine/Core/Resources/*.h
    include/Engine/Core/Renderer/*.h
    include/Engine/Core/RHI/*.h
    include/Engine/Core/Scene/*.h
    include/Engine/Core/Audio/*.h
    include/Engine/Debug/*.h
    include/Engine/Editor/*.h
    include/Engine/Editor/VFXGraph/*.h
    include/Engine/Audio/*.h
    include/Engine/OpenGL/*.h
    include/Engine/OpenAL/*.h
    include/Engine/Box2D/*.h
    include/Engine/Platform/*.h
    include/Engine/Rendering/*.h
    include/Engine/Scripting/*.h
    include/Engine/Vulkan/*.h
    include/Engine/Jolt/*.h
)

add_library(EngineCore STATIC
    ${ENGINE_SOURCES}
    ${ENGINE_HEADERS}
)

# ── Shader 单一真理源嵌入（v3.0 Phase 1）──
# assets/shaders/*.comp.glsl 为唯一来源；修改着色器后重新构建即生效。
# 生成器为纯 CMake 脚本，零外部依赖；fail-fast 不静默回退。
set(GL46_SHADER_DIR      "${PROJECT_SOURCE_DIR}/assets/shaders")
set(GL46_SHADER_GEN_HDR  "${CMAKE_CURRENT_SOURCE_DIR}/src/OpenGL/GL46ShaderSources.g.h")
file(GLOB GL46_SHADER_DEPS "${GL46_SHADER_DIR}/*.comp.glsl")
add_custom_command(
    OUTPUT "${GL46_SHADER_GEN_HDR}"
    COMMAND ${CMAKE_COMMAND}
            -DSHADER_DIR=${GL46_SHADER_DIR}
            -DOUT_FILE=${GL46_SHADER_GEN_HDR}
            -P ${PROJECT_SOURCE_DIR}/tools/embed_shaders.cmake
    DEPENDS ${GL46_SHADER_DEPS} "${PROJECT_SOURCE_DIR}/tools/embed_shaders.cmake"
    COMMENT "Embedding compute shaders -> GL46ShaderSources.g.h"
    VERBATIM)
target_sources(EngineCore PRIVATE "${GL46_SHADER_GEN_HDR}")

target_include_directories(EngineCore
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/third_party/glm"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"

)
target_link_libraries(EngineCore PUBLIC glad2 glfw stb box2d OpenAL::OpenAL imgui spdlog::spdlog freetype imguizmo Jolt)

# Vulkan API 头的 provenance：来自仓库 pinned 的 Vulkan-Headers submodule，
# 无条件链接 —— 与 host 是否安装 Vulkan SDK 无关。
# 链接顺序刻意早于下方 if(Vulkan_FOUND) 内的 ${Vulkan_INCLUDE_DIRS}，
# 使 pinned 头始终优先于 SDK 头，保持单一 source of truth。
if(TARGET VulkanHeaders)
    target_link_libraries(EngineCore PUBLIC VulkanHeaders)
endif()

# Jolt Physics: 仅在 Debug 构建启用调试渲染器
# 注意：Jolt 使用 #ifdef 检查，所以非 Debug 配置必须完全不定义
target_compile_definitions(EngineCore PRIVATE
    $<$<CONFIG:Debug>:JPH_DEBUG_RENDERER>
)
if(TARGET d3d12ma)
    target_link_libraries(EngineCore PUBLIC d3d12ma)
endif()

# ── Tracy Profiler（Debug 默认启用，Release 关闭） ──
if(TARGET tracy)
    target_link_libraries(EngineCore PUBLIC tracy)
    target_compile_definitions(EngineCore PUBLIC
        $<$<CONFIG:Debug>:ENGINE_ENABLE_PROFILING>
        $<$<CONFIG:Debug>:TRACY_ENABLE>
        $<$<CONFIG:Debug>:TRACY_ON_DEMAND>
    )
endif()

if(WIN32)
    target_link_libraries(EngineCore PRIVATE dbghelp.lib d3d12.lib dxgi.lib dxguid.lib)
    target_compile_definitions(EngineCore PUBLIC ENGINE_HAS_D3D12)
    
    # D3D12MA 实现文件（INTERFACE 库不编译源码，需要显示添加）
    target_sources(EngineCore PRIVATE "${PROJECT_SOURCE_DIR}/third_party/d3d12ma/src/D3D12MemAlloc.cpp")
endif()

# ════════════════════════════════════════════════════════
# Vulkan SDK 检测 + 链接 + 源代码
# ════════════════════════════════════════════════════════
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    # 添加 Vulkan 源文件
    file(GLOB VULKAN_SOURCES CONFIGURE_DEPENDS src/Vulkan/*.cpp)
    target_sources(EngineCore PRIVATE ${VULKAN_SOURCES})
    
    # VMA 实现文件（VMA_IMPLEMENTATION 必须恰好在一个 .cpp 中定义）
    # VMAImpl.cpp 应已包含在 src/Vulkan/ 下；若不存在则添加默认实现
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/Vulkan/VMAImpl.cpp")
        file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/src/Vulkan/VMAImpl.cpp"
            "#define VMA_IMPLEMENTATION\n#include <vk_mem_alloc.h>\n")
    endif()

    target_link_libraries(EngineCore PUBLIC Vulkan::Vulkan vma)
    target_include_directories(EngineCore PRIVATE "${Vulkan_INCLUDE_DIRS}")
    target_compile_definitions(EngineCore PUBLIC ENGINE_HAS_VULKAN)
    message(STATUS "Vulkan SDK: found at ${Vulkan_INCLUDE_DIRS}")
    message(STATUS "Vulkan sources: ${VULKAN_SOURCES}")
else()
    message(WARNING "Vulkan SDK not found — Vulkan backend disabled")
endif()

# ── 预编译头文件加速 ──
target_precompile_headers(EngineCore PRIVATE include/Engine/PCH.h)

# 确保第三方 include 路径可用（仅添加实际需要的子目录）
target_include_directories(EngineCore PUBLIC
    "${PROJECT_SOURCE_DIR}/third_party/box2d/include"
    "${PROJECT_SOURCE_DIR}/third_party/spirv-cross"
    "${PROJECT_SOURCE_DIR}/third_party/shaderc/libshaderc/include"
    "${PROJECT_SOURCE_DIR}/third_party"
)

# ════════════════════════════════════════════════════════
# SPIRV-Cross 库 — 着色器反射（直接纳入 EngineCore 统一编译）
# ════════════════════════════════════════════════════════
# 将 SPIRV-Cross 源文件直接加入 EngineCore，而非独立静态库。
# 这样避免了 MSVC 单独库目标不继承根项目 /MDd 运行时的问题。
if(EXISTS "${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_cross.cpp")
    target_sources(EngineCore PRIVATE
        "${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_cross.cpp"
        "${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_cross_parsed_ir.cpp"
        "${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_glsl.cpp"
        "${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_hlsl.cpp"
        "${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_msl.cpp"
        "${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_reflect.cpp"
        "${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_parser.cpp"
        "${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_cfg.cpp"
        "${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_cross_util.cpp"
        "${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_cross_c.cpp"
    )
    target_include_directories(EngineCore PUBLIC "${PROJECT_SOURCE_DIR}/third_party/spirv-cross")
    target_compile_definitions(EngineCore PUBLIC ENGINE_HAS_SPIRV_CROSS)
    message(STATUS "SPIRV-Cross sources: integrated directly into EngineCore")
endif()

# ════════════════════════════════════════════════════════
# Shaderc 库 — GLSL/HLSL → SPIR-V 编译
# ════════════════════════════════════════════════════════
# 检测预编译的 shaderc_combined.lib（Debug / Release / RelWithDebInfo 三个配置）
set(SHADERC_RELEASE_LIB "${PROJECT_SOURCE_DIR}/third_party/shaderc/build/libshaderc/Release/shaderc_combined.lib")
set(SHADERC_DEBUG_LIB "${PROJECT_SOURCE_DIR}/third_party/shaderc/build/libshaderc/Debug/shaderc_combined.lib")
# B5-RD: RelWithDebInfo 是**有意启用 ASan** 的配置（见根 CMakeLists.txt 的
# /fsanitize=address genex），但原先磁盘上只有 Debug(/MDd+ASan) 与
# Release(/MD, 无 ASan) 两个产物；optimized 回退到 Release 会让 ASan 目标
# 文件（注解值 1）链接非 ASan 的 shaderc（注解值 0），产生 492 条
# LNK2038 annotate_string/annotate_vector 不匹配（仅 sandbox/SpirVTest）。
#
# 该产物由 third_party/shaderc/build-asan-rwd 以 /MD + /fsanitize=address 构建
# （已验证含 __sanitizer_annotate_contiguous_container 与 __asan_）。
#
# **可复现性注意**：shaderc 是 submodule，其 .gitignore 含 `build-*/`，
# 因此这个 ASan 产物不会被提交，**新克隆的仓库默认没有它**。所以它只是
# **可选增强**：缺失时 RelWithDebInfo 回退到 Release shaderc（回到改动前的
# 行为，即 SpirVTest 在 RelWithDebInfo 下仍会 LNK2038），但不会像把
# EXISTS 写进必需条件那样让 shaderc 对其他人整体失效。
# 需要完整 RelWithDebInfo + ASan 支持的构建请先生成该产物。
set(SHADERC_RELWITHDEBINFO_LIB "${PROJECT_SOURCE_DIR}/third_party/shaderc/build-asan-rwd/libshaderc/RelWithDebInfo/shaderc_combined.lib")
set(SHADERC_HAVE_RELWITHDEBINFO OFF)
if(EXISTS "${SHADERC_RELWITHDEBINFO_LIB}")
    set(SHADERC_HAVE_RELWITHDEBINFO ON)
    set(SHADERC_HAVE_RELWITHDEBINFO_STR " + RelWithDebInfo/ASan")
else()
    set(SHADERC_HAVE_RELWITHDEBINFO_STR "")
endif()

# 必需条件仍然只有 Debug + Release（与改动前一致）
if(EXISTS "${SHADERC_RELEASE_LIB}" AND EXISTS "${SHADERC_DEBUG_LIB}")
    message(STATUS "Shaderc: found (Debug + Release${SHADERC_HAVE_RELWITHDEBINFO_STR}) — enabling GLSL→SPIR-V compilation")

    # 创建接口库封装 shaderc 链接细节
    add_library(shaderc_wrapper INTERFACE)
    target_include_directories(shaderc_wrapper INTERFACE
        "${PROJECT_SOURCE_DIR}/third_party/shaderc/libshaderc/include"
    )
    # 刻意**全部**使用生成式表达式，不再混用 debug/optimized 关键字：
    # CMake 的 target_link_libraries 关键字形式与生成式混用会被错误解析，
    # 曾在 Release/MinSizeRel/RelWithDebInfo 的依赖列表里注入 "0.lib"/"1.lib"
    # 这类伪库名，导致 LNK1181。映射语义保持不变：
    #     Debug           -> Debug shaderc
    #     RelWithDebInfo  -> /MD + ASan shaderc（仅当该产物存在）
    #     Release/MinSizeRel -> Release shaderc
    # ASan 产物缺失时退回改动前的映射（RelWithDebInfo -> Release shaderc）。
    if(SHADERC_HAVE_RELWITHDEBINFO)
        target_link_libraries(shaderc_wrapper INTERFACE
            "$<$<CONFIG:Debug>:${SHADERC_DEBUG_LIB}>"
            "$<$<CONFIG:RelWithDebInfo>:${SHADERC_RELWITHDEBINFO_LIB}>"
            "$<$<NOT:$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>>:${SHADERC_RELEASE_LIB}>"
        )
    else()
        target_link_libraries(shaderc_wrapper INTERFACE
            "$<$<CONFIG:Debug>:${SHADERC_DEBUG_LIB}>"
            "$<$<NOT:$<CONFIG:Debug>>:${SHADERC_RELEASE_LIB}>"
        )
    endif()
    target_compile_definitions(shaderc_wrapper INTERFACE ENGINE_HAS_SHADERC)

    # EngineCore 链接 shaderc，ShaderCompiler.cpp 真正可用
    target_link_libraries(EngineCore PUBLIC shaderc_wrapper)
    message(STATUS "Shaderc wrapper: linked to EngineCore")
else()
    message(WARNING "Shaderc lib not found — GLSL→SPIR-V compilation disabled")
endif()

# ── Lua 脚本支持（Scripting 主线 S1：唯一运行时 = Lua 5.4）──
if(EXISTS "${PROJECT_SOURCE_DIR}/third_party/lua/src/lua.h")
    file(GLOB LUA_SOURCES "${PROJECT_SOURCE_DIR}/third_party/lua/src/*.c")
    list(FILTER LUA_SOURCES EXCLUDE REGEX "lua\\.c$|luac\\.c$")
    add_library(lua STATIC ${LUA_SOURCES})
    target_include_directories(lua PUBLIC "${PROJECT_SOURCE_DIR}/third_party/lua/src")
    target_link_libraries(EngineCore PUBLIC lua)
    target_compile_definitions(EngineCore PRIVATE ENGINE_ENABLE_LUA)
endif()

# source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${ENGINE_SOURCES} ${ENGINE_HEADERS})
