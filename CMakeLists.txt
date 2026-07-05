cmake_minimum_required(VERSION 3.20)
project(GameEngineDemo LANGUAGES C CXX)

# ── AddressSanitizer (ASan) 选项：默认关闭，需要时在 CMake 中开启 ──
# 开启方式：
#   cmake -S . -B build -DENABLE_ASAN=ON
# 或通过 CMakePresets.json 选择 "x64-Debug-ASan" 预设
option(ENABLE_ASAN "Enable AddressSanitizer (MSVC /fsanitize=address)" OFF)

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

    # ── AddressSanitizer (ASan) ──
    # 所有目标（包括第三方静态库）统一启用 ASan，避免 STL 标注
    # （annotate_string/annotate_vector）在带/不带 ASan 的 .obj 间不匹配。
    #
    # openal-soft 的 bsinc_tables.cpp 在静态初始化时存在已知的栈越界
    # （al::number_base 构造时写越界 8 字节），仅影响 DLL 加载阶段，
    # 不影响运行时行为。ASan 会在首次加载时报告但程序可继续运行。
    #
    # 要求：Visual Studio 2019 16.9+ 或 VS 2022
    # 注意：ASan 与 /RTC 冲突，CMake 的 Debug 配置默认会启用 /RTC1，
    #       添加 /fsanitize=address 后编译器会自动禁用冲突的 /RTC 标志。
    if(ENABLE_ASAN)
        add_compile_options(
            "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:/fsanitize=address>"
        )
        message(STATUS "[ASan] AddressSanitizer enabled globally (all targets, Debug/RelWithDebInfo)")
    else()
        message(STATUS "[ASan] AddressSanitizer disabled (ENABLE_ASAN=OFF)")
    endif()
endif()

# 包含三个子模块
add_subdirectory(third_party)
add_subdirectory(engine)
add_subdirectory(sandbox)