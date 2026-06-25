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
    src/Debug/*.cpp
    src/Editor/*.cpp
    src/OpenAL/*.cpp
    src/OpenGL/*.cpp
    src/OpenGL/Resources/*.cpp
    src/Platform/*.cpp
    src/Platform/glfw/*.cpp
    src/Rendering/*.cpp
    src/Scripting/*.cpp
)
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
    include/Engine/Audio/*.h
    include/Engine/OpenGL/*.h
    include/Engine/OpenAL/*.h
    include/Engine/Box2D/*.h
    include/Engine/Platform/*.h
    include/Engine/Rendering/*.h
    include/Engine/Scripting/*.h
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
target_link_libraries(EngineCore PUBLIC glad2 glfw stb box2d OpenAL::OpenAL imgui spdlog::spdlog freetype imguizmo)

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
    target_link_libraries(EngineCore PRIVATE dbghelp.lib)
endif()

# ── 预编译头文件加速 ──
target_precompile_headers(EngineCore PRIVATE include/Engine/PCH.h)

# 确保第三方 include 路径可用（仅添加实际需要的子目录）
target_include_directories(EngineCore PUBLIC
    "${PROJECT_SOURCE_DIR}/third_party/box2d/include"
    "${PROJECT_SOURCE_DIR}/third_party"
)

# ── Lua 脚本支持（可选） ──
# 如需启用 Lua 脚本功能，请将 Lua 源码放入 third_party/lua/
# 并取消下方注释
# if(EXISTS "${PROJECT_SOURCE_DIR}/third_party/lua/src/lua.h")
#     add_library(lua STATIC
#         third_party/lua/src/lapi.c
#         third_party/lua/src/lcode.c
#         third_party/lua/src/lctype.c
#         third_party/lua/src/ldebug.c
#         third_party/lua/src/ldo.c
#         third_party/lua/src/ldump.c
#         third_party/lua/src/lfunc.c
#         third_party/lua/src/lgc.c
#         third_party/lua/src/llex.c
#         third_party/lua/src/lmem.c
#         third_party/lua/src/lobject.c
#         third_party/lua/src/lopcodes.c
#         third_party/lua/src/lparser.c
#         third_party/lua/src/lstate.c
#         third_party/lua/src/lstring.c
#         third_party/lua/src/ltable.c
#         third_party/lua/src/ltm.c
#         third_party/lua/src/lundump.c
#         third_party/lua/src/lvm.c
#         third_party/lua/src/lzio.c
#         third_party/lua/src/lauxlib.c
#         third_party/lua/src/lbaselib.c
#         third_party/lua/src/ldblib.c
#         third_party/lua/src/liolib.c
#         third_party/lua/src/lmathlib.c
#         third_party/lua/src/loslib.c
#         third_party/lua/src/lstrlib.c
#         third_party/lua/src/ltablib.c
#     )
#     target_include_directories(lua PUBLIC third_party/lua/src)
#     target_link_libraries(EngineCore PUBLIC lua)
#     target_compile_definitions(EngineCore PRIVATE ENGINE_ENABLE_LUA)
# endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${ENGINE_SOURCES} ${ENGINE_HEADERS})