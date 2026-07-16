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
    main.cpp
)
target_link_libraries(test_renderer PRIVATE ${COMMON_LIBS} glfw)
add_test(NAME test_renderer COMMAND test_renderer)

# ── L2: 脚本 C-ABI 绑定测试 ──
add_executable(test_scripting
    test_scripting/CABIBindingTest.cpp
    main.cpp
)
target_link_libraries(test_scripting PRIVATE ${COMMON_LIBS})
add_test(NAME test_scripting COMMAND test_scripting)

# ── L3: E2E 引擎生命周期测试 ──
add_executable(test_e2e
    test_e2e/EngineBootTest.cpp
    main.cpp
)
target_link_libraries(test_e2e PRIVATE ${COMMON_LIBS})
add_test(NAME test_e2e COMMAND test_e2e)