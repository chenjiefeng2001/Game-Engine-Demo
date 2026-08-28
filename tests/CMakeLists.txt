cmake_minimum_required(VERSION 3.20)
project(EngineTests LANGUAGES C CXX)

# ── 包含 Google Test ──
# googletest 已在 third_party/googletest 中作为 submodule 添加
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/googletest
                 ${CMAKE_BINARY_DIR}/googletest
                 EXCLUDE_FROM_ALL)

# 禁用 gtest 的 /W4 警告（MSVC 下过于冗长）
if(MSVC)
    target_compile_options(gtest PRIVATE /wd4996)
    target_compile_options(gtest_main PRIVATE /wd4996)
    target_compile_options(gmock PRIVATE /wd4996)
endif()

# ── 通用链接库 ──
# 所有测试 target 链接 EngineCore 静态库
set(COMMON_LIBS EngineCore GTest::gtest GTest::gmock)

# ── L1: Core 单元测试 ──
add_executable(test_core
    test_core/math/Vector3Test.cpp
    test_core/memory/StackAllocatorTest.cpp
    main.cpp
)
target_link_libraries(test_core PRIVATE ${COMMON_LIBS})
add_test(NAME test_core COMMAND test_core)

# ── L1: ECS 单元测试 ──
add_executable(test_ecs
    test_ecs/EntityManagerTest.cpp
    test_ecs/ComponentLifecycleTest.cpp
    main.cpp
)
target_link_libraries(test_ecs PRIVATE ${COMMON_LIBS})
add_test(NAME test_ecs COMMAND test_ecs)

# ── L2: 物理集成测试 ──
add_executable(test_physics
    test_physics/CPUSimulatorTest.cpp
    test_physics/GPUPhysicsRingTest.cpp
    main.cpp
)
target_link_libraries(test_physics PRIVATE ${COMMON_LIBS})
add_test(NAME test_physics COMMAND test_physics)

# ── L2: JobSystem 压力测试 ──
add_executable(test_job
    test_job/JobSystemStressTest.cpp
    main.cpp
)
target_link_libraries(test_job PRIVATE ${COMMON_LIBS})
add_test(NAME test_job COMMAND test_job)

# ── L2: 无头 GPU 测试 ──
add_executable(test_renderer
    test_renderer/GL46DeviceTest.cpp
    test_renderer/RenderGraphSG6Test.cpp
    main.cpp
)
target_link_libraries(test_renderer PRIVATE ${COMMON_LIBS} glfw)
add_test(NAME test_renderer COMMAND test_renderer)

# ── L2: 脚本 C-ABI 绑定测试 ──
add_executable(test_scripting
    test_scripting/CABIBindingTest.cpp
    test_scripting/ScriptingMVPTest.cpp
    test_scripting/ScriptingGameplayTest.cpp
    main.cpp
)
target_link_libraries(test_scripting PRIVATE ${COMMON_LIBS})
add_executable(test_content
    test_content/ContentPipelineTest.cpp
    test_content/Dogfood05Test.cpp
    test_content/Dogfood06Test.cpp
    test_content/Dogfood07Test.cpp
    test_content/Dogfood08Test.cpp
    test_content/Dogfood09Test.cpp
    test_content/VS01Test.cpp
    test_content/ResourceLifecycleTest.cpp
    test_content/DXGoldenGateTest.cpp
)
target_link_libraries(test_content PRIVATE ${COMMON_LIBS} GTest::gtest_main)
add_test(NAME test_content COMMAND test_content)

add_test(NAME test_scripting COMMAND test_scripting)

# ── L4: Game Production 契约测试（GP-P1，见 docs/GP-P1-Charter.md §11）──
add_executable(test_gp01
    test_gp01/GP01Test.cpp
    test_gp01/GP01CombatTest.cpp
    test_gp01/GP01ContentScaleTest.cpp
    test_gp01/GP01EditorWorkflowTest.cpp
    test_gp01/GP01SaveLoadTest.cpp
)
target_link_libraries(test_gp01 PRIVATE ${COMMON_LIBS} GTest::gtest_main)
add_test(NAME test_gp01 COMMAND test_gp01)

# ── L3: E2E 引擎生命周期测试 ──
add_executable(test_e2e
    test_e2e/EngineBootTest.cpp
    main.cpp
)
target_link_libraries(test_e2e PRIVATE ${COMMON_LIBS})
add_test(NAME test_e2e COMMAND test_e2e)

# ── AV-005: EditorBridge C-ABI 契约测试（Avalonia 迁移 Phase 0）──
add_executable(test_bridge
    test_bridge/test_editor_capi.cpp
)
target_link_libraries(test_bridge PRIVATE EditorBridge GTest::gtest_main)
add_test(NAME test_bridge COMMAND test_bridge)
if(TARGET EditorBridge AND WIN32)
    # DLL 与 exe 同目录，保证 add_test 直接可跑
    add_custom_command(TARGET test_bridge POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:EditorBridge> $<TARGET_FILE_DIR:test_bridge>)
endif()

# ════════════════════════════════════════════════════════════════
# CTest 运行环境固化（与 tools/integrity_gate.ps1 直跑语义一致）
# ════════════════════════════════════════════════════════════════
# 1) 工作目录：所有测试以仓库根为 CWD。测试内部大量使用相对路径
#    （assets/gp01/...、gp01_editor_scratch/...、assets/scenes/...），
#    ctest 缺省在 build/tests 下运行会全部落空（manifest load failed）。
get_property(_ctest_test_targets DIRECTORY PROPERTY TESTS)
foreach(_t IN LISTS _ctest_test_targets)
    set_tests_properties(${_t} PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endforeach()
unset(_ctest_test_targets)

# 2) MSVC ASan interception 缺陷防护（docs/Avalonia-Phase3-Freeze-ASan-Report.md）
# interception_win.cpp:193：ASan 构建的进程在启动早期对某模块做 inline hook 时
# 遇到无法解码的指令，缺省（未设 ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE=1）
# 直接 CHECK 硬失败 → 进程挂死（无响应、无法退出，需强制结束）。C++ 测试 exe
# 没有 Program.cs 式"首行自设"位置（env 必须在 CRT/ASan 初始化前生效，main()
# 内设置已太晚），因此 CTest 层固化 env，保证 `ctest` 直跑永不卡死
# （直接手跑请用 tools/run_test.cmd）。
if(ENABLE_ASAN AND MSVC)
    get_property(_asan_test_targets DIRECTORY PROPERTY TESTS)
    foreach(_t IN LISTS _asan_test_targets)
        set_tests_properties(${_t} PROPERTIES ENVIRONMENT
            "ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE=1")
    endforeach()
    unset(_asan_test_targets)
endif()
