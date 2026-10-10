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
    # 统一 MSVC 运行时库为 DLL 模式，避免第三方库（如 Jolt）默认为静态的冲突。
    #
    # B5: CRT 必须按**配置**选择，而不是按 ENABLE_ASAN。
    #
    # 依据是 engine/CMakeLists.txt 已按配置选择预编译 shaderc：
    #   debug     -> shaderc_combined Debug   (/MDd, _ITERATOR_DEBUG_LEVEL=2)
    #   optimized -> shaderc_combined Release (/MD,  _ITERATOR_DEBUG_LEVEL=0)
    # CMake 的 `optimized` 覆盖 Release / MinSizeRel / RelWithDebInfo，且磁盘上
    # 不存在 RelWithDebInfo 版 shaderc，因此：
    #   Debug          必须 /MDd
    #   RelWithDebInfo 必须 /MD
    #   Release        必须 /MD
    #
    # 若沿用「ENABLE_ASAN=ON ⇒ 全配置 /MDd」，Release 会以 /MDd 去链接 /MD 的
    # shaderc，产生 492 条 LNK2038；而改成按 ENABLE_ASAN 之外的条件统一 /MDd
    # 又会让 RelWithDebInfo 出现同样的不匹配。ENABLE_ASAN 已通过
    # /fsanitize=address 的 genex 正确按 Debug/RelWithDebInfo 生效，
    # 不应再被当作 CRT 的配置条件。
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "$<$<CONFIG:Debug>:MultiThreadedDebugDLL>$<$<NOT:$<CONFIG:Debug>>:MultiThreadedDLL>")

    
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
        # B5-RD: MSVC 链接器不支持 /INCREMENTAL 与 /fsanitize=address 共存，
        # 会报 LNK4300: /INCREMENTAL ASAN。对 ASan 配置显式关闭增量链接，
        # 与上面的 ASan genex 保持同一配置范围。
        add_link_options(
            "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:/INCREMENTAL:NO>"
        )
        message(STATUS "[ASan] AddressSanitizer enabled globally (all targets, Debug/RelWithDebInfo); /INCREMENTAL disabled for those configs")
    else()
        message(STATUS "[ASan] AddressSanitizer disabled (ENABLE_ASAN=OFF)")
    endif()
endif()

# ── 测试框架选项 ──
option(ENABLE_TESTS "Build test suite" OFF)
option(ENABLE_COVERAGE "Enable code coverage" OFF)

# 包含四个子模块
add_subdirectory(third_party)
add_subdirectory(engine)
add_subdirectory(bridge)
add_subdirectory(sandbox)

# ── 测试套件 ──
if(ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── 被链入共享库的静态库需要 position-independent 目标文件 ──
#
# bridge/CMakeLists.txt 把 EngineCore 链进 libEditorBridge.so（SHARED），
# 而 EngineCore 又 PUBLIC 传递链接下列静态库。静态库目标文件若不是 PIC，
# x86-64 链接器会拒绝其中的 R_X86_64_TPOFF32 重定位：
#   "can not be used when making a shared object; recompile with -fPIC"
#
# EngineCore 自身已在 engine/CMakeLists.txt 中启用 PIC；这里补齐它 PUBLIC
# 传递出去、且已确认为 STATIC 且参与该共享库链接的静态库。设置放在所有
# add_subdirectory 之后，因此作用于已创建的真实目标，无需修改 vendored
# 第三方目录下的 CMakeLists，也不使用全局编译标志。
#
# 收录标准：EngineCore 的 PUBLIC/PRIVATE 链接中确为静态目标、且由本仓库从
# 源码构建（不是预编译 archive）的库。目标名取自 add_library 的真实名称，
# 命名空间别名（spdlog::spdlog → spdlog、box2d::box2d → box2d）已还原。
#
#   glad2    third_party/CMakeLists.txt     STATIC
#   lua      engine/CMakeLists.txt           STATIC
#   stb      third_party/stb                 STATIC，且已有真实链接器诊断
#   imguizmo third_party/imguizmo            STATIC
#   spdlog   third_party/spdlog              STATIC 分支（SPDLOG_BUILD_SHARED 未开），
#            命名空间别名 spdlog::spdlog 的真实目标，且已有真实链接器诊断
#
# 明确排除：
#   - fmt：spdlog 的 bundled fmt 以 header-only 方式参与，没有独立的
#     libfmt.a 目标，target 属性对它无意义。
#   - imgui：声明在 box2d/samples 下，受 if(BOX2D_SAMPLES) 控制，不在构建图内。
#   - box2d / glfw / freetype：库类型由变量或默认值决定（STATIC 或 SHARED 取决于
#     配置），未确认前不改；若后续链接诊断指向它们，再按实际证据补入。
#   - tracy / Jolt / vma / shaderc_wrapper：目前没有进入该共享库链接的证据。
foreach(_pic_target
        glad2
        lua
        stb
        imguizmo
        spdlog)
    if(TARGET ${_pic_target})
        set_target_properties(${_pic_target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
endforeach()
