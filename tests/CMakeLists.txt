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
    test_core/time/TimeTest.cpp
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
# 蒙皮像素验证的四个用例在这里（GPUSkinningPixelTest.cpp），用例与断言在
# SkinningPixelCases.cpp 里只写一份；GL 与 Vulkan 各提供一份后端实现。
# Vulkan 后端文件本身用 ENGINE_HAS_VULKAN / ENGINE_HAS_SHADERC 守卫，未编译
# Vulkan 时它只提供一个返回 nullptr 的桩，因此这里无条件列出。
add_executable(test_renderer
    test_renderer/GL46DeviceTest.cpp
    test_renderer/RenderGraphSG6Test.cpp
    test_renderer/GPUSkinningPixelTest.cpp
    test_renderer/SkinningPixelCases.cpp
    test_renderer/SkinningPixelBackend.cpp
    test_renderer/SkinningPixelBackendGL.cpp
    test_renderer/SkinningPixelBackendVulkan.cpp
    main.cpp
)
target_link_libraries(test_renderer PRIVATE ${COMMON_LIBS} glfw)
target_include_directories(test_renderer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test_renderer)
add_test(NAME test_renderer COMMAND test_renderer)
# GL 4.6 core context 创建 + compute dispatch + 两次 gl.Finish() 同步，
# 属真实 GPU 工作量（与 test_pick 同档）。此前本 target 是唯一没有
# CTest TIMEOUT 的 GPU target：一旦挂住，默认 `ctest` 会无限等待，
# 只能依赖调用方手动传 --timeout。历史上确实出现过一次 >150s 的
# 超时（当前无法复现，机制未确认），因此这里只补齐基础设施缺口：
# 给一个确定的上界，让挂起被确定性终止并报为 TIMEOUT/FAILED。
# 不加 retry、不加 delay、不改测试逻辑、不改 renderer。
# 上界因此放宽到 300s：新增的 Vulkan 后端会在 lavapipe 上真实光栅化并回读像素，
# 那是 CPU 光栅化的工作量（软件 Vulkan 单帧数十毫秒量级），180s 在慢 runner 上
# 余量不足。
set_tests_properties(test_renderer PROPERTIES TIMEOUT 300)

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
    test_content/ShaderCompileRuleTest.cpp
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
target_link_libraries(test_bridge PRIVATE EditorBridge GTest::gtest_main)# HRC-3 P1：production scene render 的像素证据需要读回 presentation target。
# P1 阶段刻意不新增 C 导出（那是 P2 的 additive Fetch op），因此该测试白色盒地
# 包含 bridge 内部头。这**不改变 C ABI**，只是测试取证通道。
target_include_directories(test_bridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../bridge/src)
add_test(NAME test_bridge COMMAND test_bridge)
set_tests_properties(test_bridge PROPERTIES TIMEOUT 120)

# ── HRC-3 D3：pick request/result transport contract（ABI v1.3）──
# 刻意**独立成 target**，不并入 test_bridge：
#   1) 这些用例会反复增删，放进 test_editor_capi.cpp（2000+ 行 P1/P2 综合证据）
#      会让任何 block 移动都有破坏既有 evidence 的风险；
#   2) 独立进程意味着本组用例若 abort，不会污染 test_bridge 的 44/44 信号。
add_executable(test_pick_transport
    test_bridge/test_pick_transport.cpp
)
target_link_libraries(test_pick_transport PRIVATE EditorBridge GTest::gtest_main)
# presentation readback seam 需要 EngineHost 内部类型（与 test_bridge 同一理由：
# 刻意只取 C++ 证据，不新增 C ABI）
target_include_directories(test_pick_transport PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../bridge/src)
# D3-H3 发现的 artifact 缺陷：本 target 原本**没有**自己的 DLL staging，
# 只能靠 test_bridge 的 POST_BUILD 副作用刷新，导致它可能加载**上一轮**的
# EditorBridge.dll —— 而 exe 仍能正常启动、输出看似可信。
# 与已记录的两次 stale-artifact 事件同类，因此这里自备 staging：
# test_pick_transport.exe 依赖的必须是**同一次构建**的 EditorBridge.dll。
add_custom_command(TARGET test_pick_transport POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:EditorBridge> $<TARGET_FILE_DIR:test_pick_transport>)
add_test(NAME test_pick_transport COMMAND test_pick_transport)
set_tests_properties(test_pick_transport PROPERTIES TIMEOUT 180)

# ── HRC-3 D3 Step 3.1：native known-pixel pick proof ──
# 刻意**不**链接 EditorBridge：这是 native-only 证明，不经过 C ABI，
# 因此 D3 的 spatial semantics 不会被 ABI 层"顺手兜住"或掩盖。
# 链接 EngineCore 以直接使用 Application / Scene / OpenGLFramebuffer。
add_executable(test_pick
    test_pick/test_pick.cpp
)
target_link_libraries(test_pick PRIVATE EngineCore GTest::gtest_main)
add_test(NAME test_pick COMMAND test_pick)
# GL context 创建 + 双精灵渲染 + 逐像素 readback，比纯 CPU 测试慢
set_tests_properties(test_pick PROPERTIES TIMEOUT 180)
add_executable(test_hrc
    test_hrc/test_hrc.cpp
)
target_link_libraries(test_hrc PRIVATE EditorBridge GTest::gtest_main)
add_test(NAME test_hrc COMMAND test_hrc)
set_tests_properties(test_hrc PROPERTIES TIMEOUT 120)
if(TARGET EditorBridge AND WIN32)
    # DLL 与 exe 同目录，保证 add_test 直接可跑
    add_custom_command(TARGET test_bridge POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:EditorBridge> $<TARGET_FILE_DIR:test_bridge>)
    add_custom_command(TARGET test_hrc POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:EditorBridge> $<TARGET_FILE_DIR:test_hrc>)
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
    test_audio/ListenerTest.cpp
    test_audio/AudioLoaderTest.cpp
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
    test_io/UserSettingsTest.cpp
    test_io/EngineSettingsTest.cpp
    test_io/JsonSerializerTest.cpp
    test_io/ViewportSerializerTest.cpp
    main.cpp
)
target_link_libraries(test_io PRIVATE ${COMMON_LIBS})
add_test(NAME test_io COMMAND test_io)

# ── Phase 7-D: EditorCamera external input path ──
# 依据 docs/Avalonia-Phase7-Viewport-Charter.md:37：7-D 的 orbit/pan/fly
# 在此之前**零测试覆盖**（rg -i "editorcamera|orbit|externalinput" tests/ 命中 0）。
# 刻意**不**链接 EditorBridge：这些是引擎内纯逻辑，bridge 只是转发通道。
# 本 target 的价值在于全程不创建 ImGui context —— 修复前 orbit 分支硬编码
# ImGui::IsMouseDown(ImGuiMouseButton_Left)，生产路径（headless EngineHost）
# 没有 context，把 ImGui 依赖改回来会立刻在这里暴露。
add_executable(test_editor_camera
    test_editor_camera/test_editor_camera.cpp
)
target_link_libraries(test_editor_camera PRIVATE EngineCore GTest::gtest_main)
add_test(NAME test_editor_camera COMMAND test_editor_camera)

# ── Phase 7-C: Gizmo ABI ↔ ViewportPanel state ──
# 依据 docs/Avalonia-Phase7-Viewport-Charter.md §3.4：9 个 Gizmo ABI 此前只写
# EditorSession 自己的成员，既不触达 ViewportPanel 也无消费者。本 target 锁住
# 修复后的映射与状态往返。
# 重点是 ABI 的 4 值协议（Select/Translate/Rotate/Scale）与 ViewportTool 的
# 5 值枚举（多一个 Hand）下标不对齐 —— 直接强转会静默错位一档。
# 刻意不构造 Application 来验证转发：那需要 GL factory + headless 初始化，
# 成本远高于本组用例能证明的东西。
add_executable(test_viewport_gizmo
    test_viewport_gizmo/test_viewport_gizmo.cpp
)
target_link_libraries(test_viewport_gizmo PRIVATE EngineCore EditorBridge GTest::gtest_main)
target_include_directories(test_viewport_gizmo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../bridge/include)
add_test(NAME test_viewport_gizmo COMMAND test_viewport_gizmo)
if(TARGET EditorBridge AND WIN32)
    add_custom_command(TARGET test_viewport_gizmo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:EditorBridge> $<TARGET_FILE_DIR:test_viewport_gizmo>)
endif()

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
