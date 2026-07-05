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
    src/Core/Renderer/*.cpp
    src/Core/RHI/*.cpp
    src/Core/Scene/*.cpp
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

target_include_directories(EngineCore
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/third_party/glm"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"

)
target_link_libraries(EngineCore PUBLIC glad2 glfw stb box2d OpenAL::OpenAL imgui spdlog::spdlog freetype imguizmo Jolt)
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
endif()

# ════════════════════════════════════════════════════════
# Vulkan SDK 检测 + 链接 + 源代码
# ════════════════════════════════════════════════════════
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    # 添加 Vulkan 源文件
    file(GLOB VULKAN_SOURCES CONFIGURE_DEPENDS src/Vulkan/*.cpp)
    target_sources(EngineCore PRIVATE ${VULKAN_SOURCES})

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
# SPIRV-Cross 库 — 着色器反射（SPIR-V → UBO/Sampler 提取）
# ════════════════════════════════════════════════════════
if(EXISTS "${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_cross.cpp")
    add_library(spirvcross STATIC
        "${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_cross.cpp"
        "${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_glsl.cpp"
        "${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_hlsl.cpp"
        "${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_msl.cpp"
        "${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_reflect.cpp"
        "${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_parser.cpp"
        "${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_cfg.cpp"
        "${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_cross_util.cpp"
        "${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_cross_c.cpp"
    )
    target_include_directories(spirvcross PUBLIC "${PROJECT_SOURCE_DIR}/third_party/spirv-cross")
    target_link_libraries(EngineCore PUBLIC spirvcross)
    target_compile_definitions(EngineCore PUBLIC ENGINE_HAS_SPIRV_CROSS)
    message(STATUS "SPIRV-Cross: integrated")
endif()

# ════════════════════════════════════════════════════════
# Shaderc 库 — GLSL/HLSL → SPIR-V 编译（可选）
# ════════════════════════════════════════════════════════
# 注意：Shaderc 依赖 glslang 和 spirv-tools，完整构建需要较多步骤。
# 此处先添加 libshaderc 接口头文件和 stub 库骨架。
# 完整构建需在 third_party/shaderc 中执行：
#   python3 utils/git-sync-deps
#   cmake -B build -DSHADERC_SKIP_TESTS=ON
#   cmake --build build
#
# 以下代码在 shaderc 库构建就绪后激活：
# if(EXISTS "${PROJECT_SOURCE_DIR}/third_party/shaderc/libshaderc/include/shaderc/shaderc.hpp")
#     add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/shaderc third_party/shaderc/build)
#     target_link_libraries(EngineCore PUBLIC shaderc_combined)
#     target_compile_definitions(EngineCore PUBLIC ENGINE_HAS_SHADERC)
#     message(STATUS "Shaderc: integrated")
# endif()

# ── Lua 脚本支持（可选） ──
# 如需启用 Lua 脚本功能，请将 Lua 源码放入 third_party/lua/
# 并取消下方注释
# if(EXISTS "${PROJECT_SOURCE_DIR}/third_party/lua/src/lua.h")
#     add_library(lua STATIC ...)
#     target_include_directories(lua PUBLIC third_party/lua/src)
#     target_link_libraries(EngineCore PUBLIC lua)
#     target_compile_definitions(EngineCore PRIVATE ENGINE_ENABLE_LUA)
# endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${ENGINE_SOURCES} ${ENGINE_HEADERS})