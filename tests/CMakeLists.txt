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
    main.cpp
)
target_link_libraries(test_ecs PRIVATE ${COMMON_LIBS})
add_test(NAME test_ecs COMMAND test_ecs)

# ── L2: 物理集成测试 ──
add_executable(test_physics
    test_physics/CPUSimulatorTest.cpp
    test_physics/GPUPhysicsRingTest.cpp
    test_physics/ColliderBindingTest.cpp
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


# ── P1: Animation 子系统测试 ──
# 依据 docs/Engine-Capability-Audit.md §4：Animation 有 58 文件 / ~13k 行，
# 声称完整但**零测试** —— 风险加权最高的缺口（未测试的复杂代码只会静默失败）。
# 本 target 以**不变量**为断言依据（骨骼数守恒、权重和归一、末端误差阈值），
# 不得以"函数返回非零"之类的弱断言充数。
add_executable(test_animation
    test_animation/SkeletonTest.cpp
    test_animation/IKTest.cpp
    test_animation/BlendTreeTest.cpp
    test_animation/AnimationRetargetTest.cpp
    test_animation/AnimationCompressionTest.cpp
    main.cpp
)
target_link_libraries(test_animation PRIVATE ${COMMON_LIBS})
add_test(NAME test_animation COMMAND test_animation)

# ── P1: Audio 子系统测试 ──
# 依据 docs/Engine-Capability-Audit.md：Audio 声称完整但零测试。
# 首批只覆盖不依赖 OpenAL 设备的实现（AudioBusManager）。
# 该 target 链接 ${COMMON_LIBS}，因此沿用既有 DLL staging 规则。
add_executable(test_audio
    test_audio/AudioBusManagerTest.cpp
    test_audio/SpatialAudioManagerTest.cpp
    test_audio/AudioAssetManagerTest.cpp
    main.cpp
)
target_link_libraries(test_audio PRIVATE ${COMMON_LIBS})
add_test(NAME test_audio COMMAND test_audio)
# test_audio 引用 OpenAL 符号（AudioBusManager / SpatialAudioManager 均在
# EngineCore 内且带 OpenAL 导入），因此 PE 导入表需要 OpenAL32.dll。
# 其它测试（如 test_animation）不引用任何 OpenAL 符号，导入表里没有该条目，
# 所以从未暴露这个缺失 —— 这正是"只有新 target 才会撞上"的坑。
# Debug 配置下 MSBuild 会自动随引用拷贝，Release 不会，故显式 staging。
if(TARGET OpenAL::OpenAL)
  add_custom_command(TARGET test_audio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:OpenAL::OpenAL> $<TARGET_FILE_DIR:test_audio>)
endif()

# ── P1: IO/Config 子系统测试 ──
# 依据 docs/Engine-Capability-Audit.md：Config / FileSystem / UserSettings
# 此前零直接测试。首批覆盖 Config（纯逻辑 + nlohmann/json，无设备依赖）。
add_executable(test_io
    test_io/IoConfigTest.cpp
    test_io/FileSystemTest.cpp
    main.cpp
)
target_link_libraries(test_io PRIVATE ${COMMON_LIBS})
add_test(NAME test_io COMMAND test_io)

# NOTE: this block must stay at the END of the file.
# get_property(... DIRECTORY PROPERTY TESTS) only returns the tests
# registered so far. Placed mid-file, later targets (test_animation /
# test_audio) never receive the env var, so a plain `ctest` run fails
# while a hand-run with the env set passes -- a CI-only trap.
if(ENABLE_ASAN AND MSVC)
    get_property(_asan_test_targets DIRECTORY PROPERTY TESTS)
    foreach(_t IN LISTS _asan_test_targets)
        set_tests_properties(${_t} PROPERTIES ENVIRONMENT
            "ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE=1")
    endforeach()
    unset(_asan_test_targets)
endif()
