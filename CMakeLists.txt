if(MSVC)
    # 强制 MSVC 使用 UTF-8 编码读取源文件并生成执行文件
    add_compile_options(/utf-8)
endif()
cmake_minimum_required(VERSION 3.20)

# ── Ninja 依赖格式：将编译器输出的头文件依赖写入 .d 文件而非控制台 ──
# MSVC 的 /showIncludes 会向 stdout 输出每条包含路径，导致编译输出杂乱。
# 设为 "deps" 后 Ninja 从 .d 文件中读取依赖，大幅减少编译日志量。
set(CMAKE_NINJA_DEPS_INTERFACE "deps" CACHE STRING "Ninja dependency format")

# ── Tracy Profiler：延迟初始化到 main() 之后，避免静态初始化期间栈空间不足 ──
# 某些 MSVC 版本在静态初始化阶段栈空间有限，Tracy 的定时器校准函数
# CalibrateTimer() 可能因栈溢出而崩溃。添加 TRACY_DELAYED_INIT 让 Tracy
# 推迟到首次调用 Tracy 宏时再初始化。
if(MSVC)
    add_compile_definitions(TRACY_DELAYED_INIT)
endif()

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
