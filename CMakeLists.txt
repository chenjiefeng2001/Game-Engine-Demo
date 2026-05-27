if(MSVC)
    # 强制 MSVC 使用 UTF-8 编码读取源文件并生成执行文件
    add_compile_options(/utf-8)
endif()
cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
project(GameEngineDemo LANGUAGES C CXX)

# ============================================================================
# Visual Studio / MSVC 兼容性设置
# ============================================================================
if(MSVC)
    # 使用 UTF-8 源码编码
    add_compile_options(/utf-8)
    # 多处理器编译
    add_compile_options(/MP)
    # 禁用部分非关键警告
    add_compile_options(/wd4201 /wd4100 /wd4189 /wd4505)
endif()

# 包含三个子模块
add_subdirectory(third_party)
add_subdirectory(engine)
add_subdirectory(sandbox)
