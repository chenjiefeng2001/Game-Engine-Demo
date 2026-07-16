# 全引擎自动化测试框架架构报告

> **生成日期**: 2026-07-16  
> **分析范围**: 全引擎子系统 — Core/ECS/Asset/Renderer/Physics/Audio/Animation/Editor/Scripting  
> **适用框架**: Google Test (gtest) + Google Mock (gmock) / Doctest  
> **当前基础**: CMake + MSVC + ASan/UBSan/TSan 支持已就绪，GPU Headless 测试原型已验证

---

## 一、为什么需要全引擎测试框架？

当前引擎各子系统进展不一（物理 ~96%，脚本 ~20%），且缺乏统一的自动化验证手段：

| 现状 | 风险 |
|------|------|
| GPU 物理三环验证是手动运行的独立测试 | 无法在 CI 中自动触发 |
| ECS 系统无单元测试覆盖 | Component 变更可能导致运行时崩溃 |
| JobSystem 无压力测试 | 潜在的 Data Race 在偶发场景才暴露 |
| 物理/渲染接口变更无回归测试 | 重构时必须手动验证全部功能 |
| 脚本系统开发中 | 无绑定测试可能导致 C-ABI 布局错位 |

**核心目标**：构建一套从"单元测试 → 集成测试 → E2E 测试"的分层体系，覆盖引擎的全部子系统，并集成 Sanitizers 在 CI 中自动执行。

---

## 二、架构设计与目录划分

### 2.1 测试框架选型

| 框架 | 优势 | 劣势 | 推荐场景 |
|------|------|------|----------|
| **Google Test (gtest)** | 行业标准, Mock 内置, 参数化测试 | 编译较慢 (需链接静态库) | **主推荐: 全引擎测试** |
| **Doctest** | 编译极快, 单头文件 | Mock 需外部库 (trompeloeil) | 纯 Core 层轻量测试 |
| **Catch2** | 表达式宏简洁, BDD 风格 | 编译速度一般 | 可选替代 |

**推荐**: 使用 **Google Test + Google Mock** 作为主框架，Core 层可额外添加 Doctest 用于 TDD 阶段的快速反馈。

### 2.2 目录结构

```text
Game-Engine-Demo/
├── engine/                          # 引擎源码
│   ├── include/Engine/Core/         # 数学/内存/日志/字符串
│   ├── include/Engine/ECS/          # 实体组件系统
│   ├── include/Engine/Physics/      # CPU + GPU 物理
│   ├── include/Engine/RHI/          # 渲染硬件接口
│   ├── include/Engine/Renderer/     # 渲染管线
│   ├── include/Engine/Asset/        # 资产加载/序列化
│   ├── include/Engine/Audio/        # 音频系统
│   ├── include/Engine/Animation/    # 动画系统
│   ├── include/Engine/Editor/       # 编辑器框架
│   └── src/                         # 实现文件
├── tests/                           # 测试源码 (与 src 结构镜像)
│   ├── CMakeLists.txt               # 测试构建配置
│   ├── test_core/                   # L1: CPU 单元测试
│   │   ├── CMakeLists.txt
│   │   ├── math/
│   │   │   ├── Vector3Test.cpp
│   │   │   ├── Matrix4Test.cpp
│   │   │   └── QuaternionTest.cpp
│   │   ├── memory/
│   │   │   ├── StackAllocatorTest.cpp
│   │   │   └── PoolAllocatorTest.cpp
│   │   └── string/
│   │       └── StringIDTest.cpp
│   ├── test_ecs/                    # L1: ECS 逻辑测试
│   │   ├── CMakeLists.txt
│   │   ├── EntityManagerTest.cpp
│   │   ├── ComponentLifecycleTest.cpp
│   │   └── SystemSchedulingTest.cpp
│   ├── test_asset/                  # L2: 资产集成测试
│   │   ├── CMakeLists.txt
│   │   ├── MeshLoaderTest.cpp
│   │   ├── TextureLoaderTest.cpp
│   │   └── SceneSerializerTest.cpp
│   ├── test_physics/                # L2: 物理集成测试
│   │   ├── CMakeLists.txt
│   │   ├── CPUSimulatorTest.cpp     # 现有测试迁移
│   │   ├── GPUPhysicsRingTest.cpp   # 三环验证迁移
│   │   └── JoltIntegrationTest.cpp
│   ├── test_job/                    # L2: JobSystem 压力测试
│   │   ├── CMakeLists.txt
│   │   └── JobSystemStressTest.cpp
│   ├── test_renderer/               # L2: 无头 GPU 测试
│   │   ├── CMakeLists.txt
│   │   ├── GL46DeviceTest.cpp
│   │   ├── CommandListTest.cpp
│   │   └── ComputeShaderTest.cpp
│   ├── test_audio/                  # L2: 音频集成测试
│   │   ├── CMakeLists.txt
│   │   └── AudioEngineTest.cpp
│   ├── test_animation/              # L2: 动画逻辑测试
│   │   ├── CMakeLists.txt
│   │   └── BlendTreeTest.cpp
│   ├── test_scripting/              # L2: 脚本绑定测试
│   │   ├── CMakeLists.txt
│   │   └── CABIBindingTest.cpp
│   ├── test_e2e/                    # L3: 引擎端到端测试
│   │   ├── CMakeLists.txt
│   │   ├── EngineBootTest.cpp
│   │   ├── MultiFrameTickTest.cpp
│   │   └── SubsystemLifecycleTest.cpp
│   ├── mock/                        # 挡板与模拟类
│   │   ├── MockRenderer.h
│   │   ├── MockAudioEngine.h
│   │   ├── MockFileSystem.h
│   │   └── VirtualFileSystem.h
│   ├── utils/                       # 测试工具
│   │   ├── TestTime.h              # 模拟时间
│   │   ├── TestRandom.h            # 固定种子随机
│   │   └── ProceduralAssets.h      # 程序化生成测试资产
│   └── main.cpp                     # gtest main 入口
├── third_party/
│   └── googletest/                  # gtest submodule
├── assets/
│   └── test_assets/                 # Git LFS 管理的测试资产
├── CMakeLists.txt                   # 顶层 CMake
└── CMakePresets.json                # 预设: Debug/Release/ASan/TSan/UBSan
```

### 2.3 测试分层定义

```
L1: 纯 CPU 单元测试
    ├── 无任何外部依赖（无 GPU、无文件系统、无音频设备）
    ├── 毫秒级完成
    └── CI 每次提交全量执行

L2: 集成测试
    ├── 依赖特定子系统（如 GPU Headless Context、VFS、Mock）
    ├── 秒级完成
    └── CI 每晚执行 + 关键提交触发

L3: 端到端测试
    ├── 模拟完整引擎生命周期
    ├── 分钟级完成
    └── CI 每晚执行 + Release 候选触发
```

---

## 三、CMake 集成方案

### 3.1 顶层 CMakeLists.txt 修改

```cmake
# 在现有 CMakeLists.txt 中添加:
option(ENABLE_TESTS "Build test suite" OFF)
option(ENABLE_COVERAGE "Enable code coverage" OFF)

if(ENABLE_TESTS)
    enable_testing()
    add_subdirectory(third_party/googletest)
    add_subdirectory(tests)
endif()
```

### 3.2 tests/CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(EngineTests LANGUAGES C CXX)

# ── 链接引擎库 ──
# 测试可执行文件链接 engine 静态库，确保测试与引擎使用同一份代码
# 引擎库本身必须支持无窗口/无音频的 headless 模式

# ── L1: Core 单元测试 ──
add_executable(test_core
    test_core/math/Vector3Test.cpp
    test_core/math/Matrix4Test.cpp
    test_core/math/QuaternionTest.cpp
    test_core/memory/StackAllocatorTest.cpp
    test_core/memory/PoolAllocatorTest.cpp
    test_core/string/StringIDTest.cpp
    main.cpp
)
target_link_libraries(test_core PRIVATE engine GTest::gtest GTest::gmock)
add_test(NAME test_core COMMAND test_core)

# ── L1: ECS 测试 ──
add_executable(test_ecs
    test_ecs/EntityManagerTest.cpp
    test_ecs/ComponentLifecycleTest.cpp
    test_ecs/SystemSchedulingTest.cpp
    main.cpp
)
target_link_libraries(test_ecs PRIVATE engine GTest::gtest GTest::gmock)
add_test(NAME test_ecs COMMAND test_ecs)

# ── L2: 物理集成测试 ──
add_executable(test_physics
    test_physics/CPUSimulatorTest.cpp
    test_physics/GPUPhysicsRingTest.cpp
    test_physics/JoltIntegrationTest.cpp
    main.cpp
)
target_link_libraries(test_physics PRIVATE engine GTest::gtest GTest::gmock)
add_test(NAME test_physics COMMAND test_physics)

# ── L2: JobSystem 压力测试 ──
add_executable(test_job
    test_job/JobSystemStressTest.cpp
    main.cpp
)
target_link_libraries(test_job PRIVATE engine GTest::gtest GTest::gmock)
# JobSystem 压力测试需要用 TSan 运行
add_test(NAME test_job_tsan COMMAND ${CMAKE_COMMAND} -E env TSAN_OPTIONS="halt_on_error=1" test_job)

# ── L2: 无头 GPU 测试 ──
add_executable(test_renderer
    test_renderer/GL46DeviceTest.cpp
    test_renderer/CommandListTest.cpp
    test_renderer/ComputeShaderTest.cpp
    main.cpp
)
target_link_libraries(test_renderer PRIVATE engine GTest::gtest GTest::gmock glfw)
add_test(NAME test_renderer COMMAND test_renderer)

# ── L3: E2E 引擎生命周期测试 ──
add_executable(test_e2e
    test_e2e/EngineBootTest.cpp
    test_e2e/MultiFrameTickTest.cpp
    test_e2e/SubsystemLifecycleTest.cpp
    main.cpp
)
target_link_libraries(test_e2e PRIVATE engine GTest::gtest GTest::gmock)
add_test(NAME test_e2e COMMAND test_e2e)

# ── 代码覆盖率 (GCC/Clang) ──
if(ENABLE_COVERAGE)
    target_compile_options(test_core PRIVATE --coverage)
    target_link_options(test_core PRIVATE --coverage)
    # 其他 target 类似...
endif()
```

### 3.3 CMakePresets.json 扩展

```json
{
    "version": 3,
    "configurePresets": [
        {
            "name": "x64-Debug-Tests",
            "displayName": "x64 Debug with Tests",
            "generator": "Visual Studio 17 2022",
            "binaryDir": "${sourceDir}/build/tests",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Debug",
                "ENABLE_TESTS": "ON",
                "ENABLE_ASAN": "OFF"
            }
        },
        {
            "name": "x64-Debug-ASan-Tests",
            "displayName": "x64 Debug ASan with Tests",
            "inherits": "x64-Debug-Tests",
            "cacheVariables": {
                "ENABLE_ASAN": "ON"
            }
        },
        {
            "name": "x64-Release-Tests",
            "displayName": "x64 Release with Tests",
            "inherits": "x64-Debug-Tests",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "RelWithDebInfo"
            }
        }
    ],
    "buildPresets": [
        { "name": "x64-Debug-Tests", "configurePreset": "x64-Debug-Tests" },
        { "name": "x64-Debug-ASan-Tests", "configurePreset": "x64-Debug-ASan-Tests" },
        { "name": "x64-Release-Tests", "configurePreset": "x64-Release-Tests" }
    ],
    "testPresets": [
        {
            "name": "L1-Unit",
            "configurePreset": "x64-Debug-Tests",
            "filter": { "include": { "name": "test_core|test_ecs" } },
            "execution": { "noTestsAction": "error", "stopOnFailure": false }
        },
        {
            "name": "L2-Integration",
            "configurePreset": "x64-Debug-Tests",
            "filter": { "include": { "name": "test_physics|test_job|test_renderer|test_audio|test_animation|test_scripting" } },
            "execution": { "noTestsAction": "error", "stopOnFailure": false }
        },
        {
            "name": "L3-E2E",
            "configurePreset": "x64-Release-Tests",
            "filter": { "include": { "name": "test_e2e" } },
            "execution": { "noTestsAction": "error", "stopOnFailure": true }
        },
        {
            "name": "Full-ASan",
            "configurePreset": "x64-Debug-ASan-Tests",
            "execution": { "noTestsAction": "error", "stopOnFailure": false }
        }
    ]
}
```

---

## 四、分模块测试策略

### 4.1 Core 层 (基础库：数学/内存/字符串)

**测试重点**: 浮点精度、内存对齐、边界条件

#### 数学库: Vector3/Matrix4/Quaternion

```cpp
// tests/test_core/math/Vector3Test.cpp
#include <gtest/gtest.h>
#include "Engine/Core/Math/Vector3.h"

using namespace Engine;

TEST(Vector3Test, DefaultConstructorIsZero) {
    Vector3 v;
    EXPECT_FLOAT_EQ(v.x, 0.0f);
    EXPECT_FLOAT_EQ(v.y, 0.0f);
    EXPECT_FLOAT_EQ(v.z, 0.0f);
}

TEST(Vector3Test, CrossProduct) {
    Vector3 a(1, 0, 0);
    Vector3 b(0, 1, 0);
    Vector3 c = Cross(a, b);
    EXPECT_NEAR(c.x, 0.0f, 1e-6f);
    EXPECT_NEAR(c.y, 0.0f, 1e-6f);
    EXPECT_NEAR(c.z, 1.0f, 1e-6f);
}

// SIMD 边界测试: 确保不对齐的 Vector3 数组不会崩溃
TEST(Vector3Test, UnalignedArrayAccess) {
    // 故意不对齐的缓冲区
    alignas(4) char buffer[sizeof(Vector3) * 3 + 4];
    Vector3* vecs = reinterpret_cast<Vector3*>(buffer + 1);
    // 如果 SIMD 版本的 Vector3 要求 16 字节对齐，这里应该通过断言/异常捕获
    EXPECT_NO_FATAL_FAILURE({
        vecs[0] = Vector3(1, 2, 3);
        vecs[1] = Vector3(4, 5, 6);
        auto dot = Dot(vecs[0], vecs[1]);
        EXPECT_FLOAT_EQ(dot, 32.0f);
    });
}
```

#### 内存分配器: StackAllocator/PoolAllocator

```cpp
// tests/test_core/memory/StackAllocatorTest.cpp
#include <gtest/gtest.h>
#include "Engine/Core/Memory/StackAllocator.h"

class StackAllocatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        allocator = new StackAllocator(1024 * 1024); // 1MB
    }
    void TearDown() override {
        // 关键: 检查内存泄漏
        EXPECT_EQ(allocator->GetUsedBytes(), 0) 
            << "StackAllocator should have 0 used bytes after test";
        delete allocator;
    }
    StackAllocator* allocator;
};

TEST_F(StackAllocatorTest, AlignedAllocation) {
    void* ptr = allocator->Allocate(64, 16); // 16 字节对齐
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % 16, 0);
}

TEST_F(StackAllocatorTest, OverAllocation) {
    // 尝试分配超出预算
    EXPECT_DEATH(allocator->Allocate(2 * 1024 * 1024), "out of memory");
}
```

#### 日志系统: Logger

```cpp
// 测试日志系统: 使用 Mock Sink 捕获日志
class MockLogSink : public spdlog::sinks::base_sink<std::mutex> {
public:
    std::vector<std::string> messages;
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        messages.push_back(std::string(msg.payload.data(), msg.payload.size()));
    }
    void flush_() override {}
};

TEST(LoggerTest, LogLevelFiltering) {
    auto sink = std::make_shared<MockLogSink>();
    auto logger = std::make_shared<spdlog::logger>("test", sink);
    logger->set_level(spdlog::level::warn);
    
    logger->info("should not appear");
    logger->warn("should appear");
    
    EXPECT_EQ(sink->messages.size(), 1);
    EXPECT_EQ(sink->messages[0], "should appear");
}
```

### 4.2 ECS 层 (实体组件系统)

**测试重点**: 组件生命周期、System 调度、大规模实体压力

```cpp
// tests/test_ecs/EntityManagerTest.cpp
#include <gtest/gtest.h>
#include "Engine/ECS/EntityManager.h"
#include "Engine/ECS/Component.h"

using namespace Engine::ECS;

struct Position : public Component<Position> {
    float x, y, z;
};

struct Velocity : public Component<Velocity> {
    float vx, vy, vz;
};

class EntityManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        manager = std::make_unique<EntityManager>();
        // 注册组件类型
        manager->RegisterComponent<Position>();
        manager->RegisterComponent<Velocity>();
    }
    
    std::unique_ptr<EntityManager> manager;
};

TEST_F(EntityManagerTest, CreateEntityHasUniqueID) {
    Entity e1 = manager->CreateEntity();
    Entity e2 = manager->CreateEntity();
    EXPECT_NE(e1.GetID(), e2.GetID());
}

TEST_F(EntityManagerTest, AddAndGetComponent) {
    Entity e = manager->CreateEntity();
    
    Position& pos = manager->AddComponent<Position>(e);
    pos.x = 10.0f;
    pos.y = 20.0f;
    pos.z = 30.0f;
    
    Position* retrieved = manager->GetComponent<Position>(e);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_FLOAT_EQ(retrieved->x, 10.0f);
}

// 大规模压力测试: 创建 10000 个实体
TEST_F(EntityManagerTest, TenThousandEntities) {
    std::vector<Entity> entities;
    entities.reserve(10000);
    
    for (int i = 0; i < 10000; ++i) {
        Entity e = manager->CreateEntity();
        manager->AddComponent<Position>(e);
        entities.push_back(e);
    }
    
    // 验证所有实体存在
    for (auto& e : entities) {
        EXPECT_NE(manager->GetComponent<Position>(e), nullptr);
    }
    
    // 删除所有实体
    for (auto& e : entities) {
        manager->DestroyEntity(e);
    }
    
    // 验证所有实体已被删除
    for (auto& e : entities) {
        EXPECT_EQ(manager->GetComponent<Position>(e), nullptr);
    }
}

// 迭代器失效测试: 在迭代时添加/删除组件不崩溃
TEST_F(EntityManagerTest, NoIteratorInvalidationOnRemove) {
    std::vector<Entity> entities;
    for (int i = 0; i < 100; ++i) {
        Entity e = manager->CreateEntity();
        manager->AddComponent<Position>(e);
        if (i % 2 == 0) manager->AddComponent<Velocity>(e);
        entities.push_back(e);
    }
    
    // 在迭代所有实体时移除部分组件的 Velocity
    // 这不应导致迭代器失效
    for (auto& e : entities) {
        if (manager->HasComponent<Velocity>(e)) {
            manager->RemoveComponent<Velocity>(e);
        }
    }
    
    // 验证所有 Velocity 被移除
    for (auto& e : entities) {
        EXPECT_FALSE(manager->HasComponent<Velocity>(e));
    }
}
```

### 4.3 物理层: CPU + GPU

**测试重点**: 物理模拟精度、GPU 计算一致性、多后端一致性

#### CPU Simulator 测试 (现有 CPUSimulator 迁移)

```cpp
// tests/test_physics/CPUSimulatorTest.cpp
// 将 sandbox/src/GPUPhysicsTest 中的 CPUSimulator 验证逻辑迁移到 gtest

#include <gtest/gtest.h>
#include "Engine/Core/Physics/GPUParticle.h"

// 从 sandbox 移植的 CPUSimulator（应提取为独立测试工具类）
class CPUSimulator {
public:
    static void Step(std::vector<GPUParticleData>& particles, float dt,
                     const float gravity[3], float restitution, float damping,
                     const float boxMin[3], const float boxMax[3]) {
        for (auto& p : particles) {
            // 半隐式欧拉
            p.velocity[0] += gravity[0] * dt;
            p.velocity[1] += gravity[1] * dt;
            p.velocity[2] += gravity[2] * dt;
            p.velocity[0] *= (1.0f - damping * dt);
            p.velocity[1] *= (1.0f - damping * dt);
            p.velocity[2] *= (1.0f - damping * dt);
            p.position[0] += p.velocity[0] * dt;
            p.position[1] += p.velocity[1] * dt;
            p.position[2] += p.velocity[2] * dt;
            // ... 边界碰撞处理
        }
    }
};

TEST(CPUSimulatorTest, GravityAffectsVelocity) {
    std::vector<GPUParticleData> particles(1);
    particles[0].position[1] = 100.0f;
    particles[0].velocity[1] = 0.0f;
    particles[0].radius = 1.0f;
    
    float gravity[3] = {0, -9.8f, 0};
    CPUSimulator::Step(particles, 1.0f/60.0f, gravity, 0.8f, 0.02f,
                       (float[]){-50,0,-50}, (float[]){50,100,50});
    
    // 重力使速度向下增加
    EXPECT_LT(particles[0].velocity[1], 0.0f);
    EXPECT_NEAR(particles[0].velocity[1], -9.8f/60.0f, 0.001f);
}

TEST(CPUSimulatorTest, BoundaryRestitution) {
    std::vector<GPUParticleData> particles(1);
    particles[0].position[0] = -49.0f; // 靠近边界
    particles[0].velocity[0] = -10.0f;  // 向边界移动
    particles[0].radius = 1.0f;
    
    float gravity[3] = {0, 0, 0}; // 无重力，专注边界
    CPUSimulator::Step(particles, 0.1f, gravity, 0.5f, 0.0f,  // restitution=0.5
                       (float[]){-50,0,-50}, (float[]){50,100,50});
    
    // 反弹后速度方向反转，大小减半
    EXPECT_GT(particles[0].velocity[0], 0.0f);
    EXPECT_NEAR(particles[0].velocity[0], 5.0f, 0.001f); // 10 * 0.5 = 5
}
```

#### GPU 物理三环验证 (现有 GPUPhysicsTest 迁移)

```cpp
// tests/test_physics/GPUPhysicsRingTest.cpp
// 将现有的三环验证移植为可重复执行的 gtest 用例

#include <gtest/gtest.h>
#include "Engine/Core/Physics/GPUParticle.h"
#include "Engine/Core/RHI/GL46AZDODevice.h"

class GPUPhysicsTest : public ::testing::Test {
protected:
    void SetUp() override {
        device = std::make_unique<RHI::GL46Device>();
        if (!device->InitializeWithGLFWHeadless()) {
            GTEST_SKIP() << "No GL 4.6 GPU available, skipping GPU physics tests";
        }
        engine = std::make_unique<GPUPhysicsEngine>();
        GPUPhysicsConfig config;
        config.particleCount = 1024;
        ASSERT_TRUE(engine->Initialize(device.get(), config));
        
        cmdList = std::unique_ptr<RHI::IRHICommandList>(device->CreateCommandList());
        ASSERT_NE(cmdList, nullptr);
    }
    
    void TearDown() override {
        engine->Shutdown();
    }
    
    std::unique_ptr<RHI::GL46Device> device;
    std::unique_ptr<GPUPhysicsEngine> engine;
    std::unique_ptr<RHI::IRHICommandList> cmdList;
};

// Ring 1: 内存完整性
TEST_F(GPUPhysicsTest, Ring1_MemoryIntegrity) {
    engine->ResetParticles();
    
    // 上传→读回逐字节比对
    std::vector<GPUParticleData> uploaded(1024), readback(1024);
    engine->ReadbackParticles(0, 1024, readback.data());
    
    for (uint32_t i = 0; i < 1024; ++i) {
        EXPECT_EQ(memcmp(&uploaded[i], &readback[i], sizeof(GPUParticleData)), 0);
    }
}

// Ring 2: 动力学变化
TEST_F(GPUPhysicsTest, Ring2_DynamicChange) {
    engine->ResetParticles();
    cmdList->Begin();
    engine->Update(1.0f/60.0f, cmdList.get());
    cmdList->End();
    
    std::vector<GPUParticleData> before(1), after(1);
    engine->ReadbackParticles(0, 1, before.data());
    // 执行 60 帧
    for (int i = 0; i < 60; ++i) {
        cmdList->Begin();
        engine->Update(1.0f/60.0f, cmdList.get());
        cmdList->End();
    }
    engine->ReadbackParticles(0, 1, after.data());
    
    // 重力使速度向下
    EXPECT_GT(after[0].velocity[1] - before[0].velocity[1], 0.01f);
}

// Ring 3: CPU/GPU 一致性
TEST_F(GPUPhysicsTest, Ring3_CPUConsistency) {
    // 相同初始条件: CPU 模拟 60 帧 vs GPU 模拟 60 帧
    // 最大差异 < 0.01
    engine->ResetParticles();
    for (int i = 0; i < 60; ++i) {
        cmdList->Begin();
        engine->Update(1.0f/60.0f, cmdList.get());
        cmdList->End();
    }
    
    std::vector<GPUParticleData> gpuResult(1024);
    engine->ReadbackParticles(0, 1024, gpuResult.data());
    
    // CPU 端用相同初始条件模拟
    std::vector<GPUParticleData> cpuResult(1024);
    CPUSimulator::Run(1024, 60, 1.0f/60.0f, cpuResult.data());
    
    float maxDiff = 0.0f;
    for (uint32_t i = 0; i < 1024; ++i) {
        for (int j = 0; j < 3; ++j) {
            maxDiff = std::max(maxDiff, 
                std::abs(gpuResult[i].position[j] - cpuResult[i].position[j]));
        }
    }
    EXPECT_LT(maxDiff, 0.01f);
}
```

### 4.4 JobSystem 压力与 Data Race 测试

**测试重点**: 线程安全、任务完整性、TSan 无警告

```cpp
// tests/test_job/JobSystemStressTest.cpp
#include <gtest/gtest.h>
#include "Engine/Core/JobSystem.h"

using namespace Engine;

// 压力测试: 1000 个随机任务
TEST(JobSystemStressTest, Dispatch1000Tasks) {
    JobSystem jobSys;
    jobSys.Initialize(4); // 4个工作线程
    
    constexpr int kTaskCount = 1000;
    std::atomic<int> counter{0};
    
    for (int i = 0; i < kTaskCount; ++i) {
        jobSys.Dispatch([&counter]() {
            // 模拟随机计算量
            volatile int sum = 0;
            for (int j = 0; j < rand() % 1000; ++j) sum += j;
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }
    
    jobSys.WaitForAll();
    
    EXPECT_EQ(counter.load(), kTaskCount);
}

// 此测试必须在 TSan 下运行
// 如果存在 Data Race, TSan 会输出警告并终止测试
TEST(JobSystemStressTest, NoDataRaceWithTSan) {
    JobSystem jobSys;
    jobSys.Initialize(8); // 8个工作线程
    
    struct SharedData {
        std::atomic<int> atomicCounter{0};
        // 注意: 非原子变量在多线程写入时如果有锁保护则安全
        // 这里故意使用 atomic 验证设计正确
    };
    
    SharedData data;
    constexpr int kIterations = 100;
    
    for (int i = 0; i < kIterations; ++i) {
        jobSys.Dispatch([&data]() {
            // 使用 atomic 操作是安全的
            data.atomicCounter.fetch_add(1);
            
            // 如果这里有非原子 int 的 ++ 操作, TSan 会报错
            // 这里故意不加, 保持测试无警告
        });
    }
    
    jobSys.WaitForAll();
    EXPECT_EQ(data.atomicCounter.load(), kIterations);
}

// 死锁检测: 任务间等待
TEST(JobSystemStressTest, NoDeadlockWithSubTasks) {
    JobSystem jobSys;
    jobSys.Initialize(4);
    
    std::atomic<bool> task1Done{false};
    std::atomic<bool> task2Done{false};
    
    // 两个任务相互不等待, 验证调度器不会死锁
    auto task1 = jobSys.Dispatch([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        task1Done.store(true);
    });
    
    auto task2 = jobSys.Dispatch([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        task2Done.store(true);
    });
    
    // 等待两个任务
    task1->Wait();
    task2->Wait();
    
    EXPECT_TRUE(task1Done.load());
    EXPECT_TRUE(task2Done.load());
}
```

### 4.5 资产层: 虚拟文件系统 (VFS) 测试

**测试重点**: 反序列化正确性、内存文件系统、资产管道错误处理

```cpp
// tests/test_asset/MeshLoaderTest.cpp
#include <gtest/gtest.h>
#include "Engine/Asset/MeshLoader.h"
#include "tests/mock/VirtualFileSystem.h"

using namespace Engine::Asset;

class MeshLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 注入内存文件系统, 不依赖实际硬盘文件
        vfs = std::make_shared<VirtualFileSystem>();
        
        // 在内存中构造一个最小 glTF 2.0 文件
        std::string minimalGltf = R"(
        {
            "asset": {"version": "2.0"},
            "meshes": [{
                "primitives": [{
                    "attributes": {"POSITION": 0},
                    "indices": 1
                }]
            }],
            "accessors": [
                {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
                {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}
            ],
            "bufferViews": [
                {"buffer": 0, "byteOffset": 0, "byteLength": 36},
                {"buffer": 0, "byteOffset": 36, "byteLength": 6}
            ],
            "buffers": [{"byteLength": 42, "uri": "data:application/octet-stream;base64,..."}]
        })";
        
        vfs->RegisterFile("models/test_cube.gltf", minimalGltf);
        
        AssetContext ctx;
        ctx.vfs = vfs;
        loader = std::make_unique<MeshLoader>(ctx);
    }
    
    std::shared_ptr<VirtualFileSystem> vfs;
    std::unique_ptr<MeshLoader> loader;
};

TEST_F(MeshLoaderTest, LoadMinimalMesh) {
    auto mesh = loader->Load("models/test_cube.gltf");
    ASSERT_NE(mesh, nullptr);
    EXPECT_GT(mesh->GetVertexCount(), 0);
    EXPECT_GT(mesh->GetIndexCount(), 0);
}

TEST_F(MeshLoaderTest, LoadNonExistentFile) {
    EXPECT_THROW(loader->Load("models/nonexistent.gltf"), AssetNotFoundException);
}

TEST_F(MeshLoaderTest, LoadCorruptedFile) {
    vfs->RegisterFile("models/corrupted.gltf", "not valid json");
    EXPECT_THROW(loader->Load("models/corrupted.gltf"), AssetParseException);
}
```

### 4.6 渲染层: 无头 GPU 测试

**测试重点**: GPU 设备创建、CommandList 执行、Compute Shader 正确性

```cpp
// tests/test_renderer/GL46DeviceTest.cpp
#include <gtest/gtest.h>
#include "Engine/Core/RHI/GL46AZDODevice.h"

using namespace Engine::RHI;

class GL46DeviceTest : public ::testing::Test {
protected:
    void SetUp() override {
        device = std::make_unique<GL46Device>();
        if (!device->InitializeWithGLFWHeadless()) {
            GTEST_SKIP() << "No GL 4.6 GPU available, skipping renderer tests";
        }
    }
    
    void TearDown() override {
        device->Shutdown();
    }
    
    std::unique_ptr<GL46Device> device;
};

TEST_F(GL46DeviceTest, CreateBuffer) {
    RHIBufferDesc desc;
    desc.size = 1024;
    desc.memoryUsage = MemoryUsage::GPU_Only;
    
    auto buffer = device->CreateBuffer(desc);
    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(buffer->GetDesc().size, 1024);
}

TEST_F(GL46DeviceTest, CreateComputePSO) {
    ComputePSODesc desc;
    desc.computeShader = StringID::Runtime("test_shader");
    
    auto pso = device->CreateComputePSO(desc);
    EXPECT_NE(pso, nullptr); // 即使着色器编译失败也返回 non-null (stub 模式)
}

TEST_F(GL46DeviceTest, BufferPersistentMapping) {
    RHIBufferDesc desc;
    desc.size = 64;
    desc.memoryUsage = MemoryUsage::GPU_Only;
    
    auto buffer = device->CreateBuffer(desc);
    auto* gl46Buf = dynamic_cast<GL46Buffer*>(buffer.get());
    ASSERT_NE(gl46Buf, nullptr);
    
    void* mapped = gl46Buf->GetPersistentPtr();
    ASSERT_NE(mapped, nullptr);
    
    // 写入 + 读回验证
    const char* testData = "Hello GPU Physics!";
    std::memcpy(mapped, testData, 18);
    
    char readback[18];
    std::memcpy(readback, mapped, 18);
    EXPECT_EQ(std::memcmp(readback, testData, 18), 0);
}

TEST_F(GL46DeviceTest, ComputeShaderExecution) {
    // 创建 CommandList
    auto cmdList = std::unique_ptr<IRHICommandList>(device->CreateCommandList());
    ASSERT_NE(cmdList, nullptr);
    
    // 创建 SSBO
    RHIBufferDesc bufDesc;
    bufDesc.size = 64; // 1 个 GPUParticleData
    bufDesc.memoryUsage = MemoryUsage::GPU_Only;
    auto buffer = device->CreateBuffer(bufDesc);
    
    // 创建 Integrate PSO
    ComputePSODesc psoDesc;
    psoDesc.computeShader = StringID::Runtime("gpu_physics_integrate");
    auto pso = device->CreateComputePSO(psoDesc);
    ASSERT_NE(pso, nullptr);
    
    // Dispatch
    cmdList->Begin();
    cmdList->SetPipelineState(pso);
    cmdList->SetUnorderedAccess(0, buffer.get());
    cmdList->SetComputeFloat("u_Dt", 1.0f/60.0f);
    cmdList->Dispatch(1, 1, 1);
    cmdList->End();
    
    // 读回验证
    auto* gl46Buf = dynamic_cast<GL46Buffer*>(buffer.get());
    ASSERT_NE(gl46Buf, nullptr);
    
    GPUParticleData result;
    device->GetGL().Finish(); // 确保 GPU 完成
    device->GetGL().GetNamedBufferSubData(gl46Buf->GetGLHandle(), 0, sizeof(result), &result);
    
    // 验证着色器写入了数据 (非零)
    EXPECT_NE(result.position[0], 0.0f);
}
```

### 4.7 脚本层: C-ABI 绑定测试

**测试重点**: 函数签名正确性、内存布局一致性、跨语言调用边界

```cpp
// tests/test_scripting/CABIBindingTest.cpp
#include <gtest/gtest.h>
#include "Engine/Scripting/engine_api.h"

using namespace Engine::Scripting;

class CABITest : public ::testing::Test {
protected:
    void SetUp() override {
        // 初始化 ECS 管理器
        ecs = std::make_unique<EntityManager>();
        ecs->RegisterComponent<Transform>();
        ecs->RegisterComponent<RigidBody>();
        
        // 设置全局 ECS 上下文 (C-ABI 需要)
        SetGlobalECSContext(ecs.get());
    }
    
    void TearDown() override {
        SetGlobalECSContext(nullptr);
    }
    
    std::unique_ptr<EntityManager> ecs;
};

// 验证 Engine_GetComponentPtr 返回正确的指针
TEST_F(CABITest, GetComponentPtrReturnsCorrectData) {
    Entity e = ecs->CreateEntity();
    Transform& t = ecs->AddComponent<Transform>(e);
    t.position[0] = 10.0f;
    t.position[1] = 20.0f;
    t.position[2] = 30.0f;
    
    // 通过 C-ABI 获取指针
    void* ptr = Engine_GetComponentPtr(e.GetID(), ComponentID<Transform>::Value);
    ASSERT_NE(ptr, nullptr);
    
    // 按布局读取
    float* pos = static_cast<float*>(ptr);
    EXPECT_FLOAT_EQ(pos[0], 10.0f);
    EXPECT_FLOAT_EQ(pos[1], 20.0f);
    EXPECT_FLOAT_EQ(pos[2], 30.0f);
}

// 验证 Engine_QueryEntities 的正确性
TEST_F(CABITest, QueryEntitiesReturnsCorrectCount) {
    // 创建 100 个实体, 其中 60 个带 Transform, 40 个带 Transform+RigidBody
    for (int i = 0; i < 100; ++i) {
        Entity e = ecs->CreateEntity();
        ecs->AddComponent<Transform>(e);
        if (i >= 60) ecs->AddComponent<RigidBody>(e);
    }
    
    uint32_t componentMask[] = { ComponentID<Transform>::Value };
    uint32_t outCount = 0;
    uint64_t* results = Engine_QueryEntities(componentMask, 1, &outCount);
    
    EXPECT_EQ(outCount, 100);
}

// 验证 C-ABI 结构体布局与 C++ 结构体一致
TEST_F(CABITest, StructLayoutMatchesCpp) {
    // 编译期断言: C-ABI 结构体与 C++ 结构体大小一致
    static_assert(sizeof(EngineTransform) == sizeof(Transform),
                  "C-ABI Transform size mismatch");
    static_assert(offsetof(EngineTransform, position) == offsetof(Transform, position),
                  "C-ABI Transform position offset mismatch");
    static_assert(offsetof(EngineTransform, rotation) == offsetof(Transform, rotation),
                  "C-ABI Transform rotation offset mismatch");
}

// 模拟 WASM 环境调用 C-ABI
TEST_F(CABITest, SimulatedWASMBinding) {
    // WASM 环境下无法直接运行此测试, 但可以验证 C-ABI 函数的行为
    // 确保 WASM 导入函数签名与引擎端一致
    
    // 验证输入函数可用
    EXPECT_TRUE(Engine_Input_GetKey != nullptr);
    
    // 验证日志函数可用 (不崩溃)
    EXPECT_NO_FATAL_FAILURE(Engine_Log("C-ABI test log message"));
    
    // 验证时间函数可用
    float dt = Engine_Time_GetDeltaTime();
    EXPECT_GE(dt, 0.0f);
}
```

### 4.8 E2E 引擎生命周期测试

**测试重点**: 子系统初始化/销毁顺序、Headless 模式、多帧稳定性

```cpp
// tests/test_e2e/EngineBootTest.cpp
#include <gtest/gtest.h>
#include "Engine/Engine.h"

using namespace Engine;

class EngineE2ETest : public ::testing::Test {
protected:
    void SetUp() override {
        config = EngineConfig::Default();
        config.headless = true;     // 无窗口模式
        config.audio_enabled = false;
        config.physics_enabled = true;
        config.scripting_enabled = false;
    }
    
    EngineConfig config;
};

// 测试引擎完整启动→销毁
TEST_F(EngineE2ETest, BootAndShutdown) {
    Engine engine;
    
    ASSERT_TRUE(engine.Init(config));
    EXPECT_TRUE(engine.IsRunning());
    
    engine.Shutdown();
    EXPECT_FALSE(engine.IsRunning());
}

// 测试固定步长多帧运行
TEST_F(EngineE2ETest, Tick10FramesFixedDelta) {
    Engine engine;
    ASSERT_TRUE(engine.Init(config));
    
    for (int i = 0; i < 10; ++i) {
        engine.Tick(1.0f / 60.0f); // 固定 60fps
    }
    
    auto stats = engine.GetStats();
    EXPECT_EQ(stats.frameCount, 10);
    EXPECT_GT(stats.physicsSteps, 0); // 物理应在运行
    
    engine.Shutdown();
}

// 测试子系统顺序初始化
TEST_F(EngineE2ETest, SubsystemInitializationOrder) {
    Engine engine;
    
    // 记录初始化顺序
    std::vector<std::string> initOrder;
    engine.SetInitCallback([&](const std::string& name) {
        initOrder.push_back(name);
    });
    
    engine.Init(config);
    
    // 验证关键子系统初始化顺序
    ASSERT_GE(initOrder.size(), 3);
    EXPECT_EQ(initOrder[0], "Core");      // 核心库最先
    EXPECT_EQ(initOrder[1], "JobSystem");  // JobSystem 第二
    
    engine.Shutdown();
}

// 测试引擎崩溃恢复 (如果支持)
TEST_F(EngineE2ETest, GracefulShutdownOnInitFailure) {
    EngineConfig badConfig = config;
    badConfig.audio_enabled = true; // 但无音频设备
    
    Engine engine;
    // 不应崩溃, 应返回 false
    EXPECT_FALSE(engine.Init(badConfig));
}
```

---

## 五、Mock 机制与依赖注入

### 5.1 核心 Mock 接口

#### MockRenderer

```cpp
// tests/mock/MockRenderer.h
#include <gmock/gmock.h>
#include "Engine/Renderer/IRenderer.h"

class MockRenderer : public IRenderer {
public:
    MOCK_METHOD(bool, Initialize, (const RenderConfig&), (override));
    MOCK_METHOD(void, Shutdown, (), (override));
    MOCK_METHOD(void, BeginFrame, (), (override));
    MOCK_METHOD(void, EndFrame, (), (override));
    MOCK_METHOD(void, DrawMesh, (Mesh* mesh, const Matrix4& transform), (override));
    MOCK_METHOD(void, SetCamera, (const Camera& cam), (override));
    MOCK_METHOD(void, SetLighting, (const LightingConfig& cfg), (override));
    MOCK_METHOD(uint32_t, GetDrawCallCount, (), (const, override));
};
```

#### MockAudioEngine

```cpp
// tests/mock/MockAudioEngine.h
#include <gmock/gmock.h>
#include "Engine/Audio/IAudioEngine.h"

class MockAudioEngine : public IAudioEngine {
public:
    MOCK_METHOD(bool, Initialize, (const AudioConfig&), (override));
    MOCK_METHOD(void, Shutdown, (), (override));
    MOCK_METHOD(AudioSourceID, Play, (const std::string& clipName, float volume), (override));
    MOCK_METHOD(void, Stop, (AudioSourceID id), (override));
    MOCK_METHOD(void, SetVolume, (AudioSourceID id, float volume), (override));
    MOCK_METHOD(void, SetPosition, (AudioSourceID id, const Vector3& pos), (override));
};
```

#### VirtualFileSystem

```cpp
// tests/mock/VirtualFileSystem.h
#pragma once
#include <string>
#include <unordered_map>
#include <vector>

class VirtualFileSystem {
public:
    void RegisterFile(const std::string& path, const std::string& content) {
        files[path] = content;
    }
    
    void RegisterFile(const std::string& path, const std::vector<uint8_t>& data) {
        binaryFiles[path] = data;
    }
    
    std::string ReadText(const std::string& path) {
        auto it = files.find(path);
        if (it == files.end())
            throw std::runtime_error("File not found: " + path);
        return it->second;
    }
    
    std::vector<uint8_t> ReadBinary(const std::string& path) {
        auto it = binaryFiles.find(path);
        if (it == binaryFiles.end())
            throw std::runtime_error("File not found: " + path);
        return it->second;
    }
    
    bool Exists(const std::string& path) const {
        return files.find(path) != files.end() || 
               binaryFiles.find(path) != binaryFiles.end();
    }

private:
    std::unordered_map<std::string, std::string> files;
    std::unordered_map<std::string, std::vector<uint8_t>> binaryFiles;
};
```

### 5.2 Gameplay 测试示例 (使用 Mock)

```cpp
// 测试游戏逻辑: "角色射击生成子弹"
// 注入 MockRenderer, 验证 DrawCall 数量

class WeaponSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        mockRenderer = std::make_shared<MockRenderer>();
        engineContext.renderer = mockRenderer.get();
        
        // 创建玩家实体
        player = ecs.CreateEntity();
        ecs.AddComponent<Transform>(player);
        ecs.AddComponent<WeaponComponent>(player);
    }
    
    Entity player;
    EntityManager ecs;
    std::shared_ptr<MockRenderer> mockRenderer;
    EngineContext engineContext;
};

TEST_F(WeaponSystemTest, ShootingCreatesBulletEntity) {
    // 期望: 射击后渲染器收到一个子弹的 DrawCall
    EXPECT_CALL(*mockRenderer, DrawMesh(testing::_, testing::_)).Times(1);
    
    WeaponSystem::Fire(engineContext, player);
    
    // 验证子弹实体被创建
    auto bullets = ecs.Query<BulletComponent>();
    EXPECT_EQ(bullets.size(), 1);
}

TEST_F(WeaponSystemTest, MuzzleFlashDuration) {
    // 测试枪口闪光持续正确帧数
    WeaponSystem::Fire(engineContext, player);
    
    // 闪光在第 1 帧存在
    EXPECT_TRUE(ecs.HasComponent<MuzzleFlash>(player));
    
    // 模拟时间推进
    for (int i = 0; i < 5; ++i) {
        WeaponSystem::Update(engineContext, 1.0f/60.0f);
    }
    
    // 闪光应在 5 帧后消失
    EXPECT_FALSE(ecs.HasComponent<MuzzleFlash>(player));
}
```

---

## 六、控制时间与随机性

### 6.1 测试时间: `TestTime`

```cpp
// tests/utils/TestTime.h
#pragma once
#include <chrono>

class TestTime {
public:
    static double GetNow() { return currentTime; }
    static void SetTime(double t) { currentTime = t; }
    static void Advance(double dt) { currentTime += dt; }
    
    // 替换引擎内部的 Time::Now()
    static double Now() { return currentTime; }

private:
    static inline double currentTime = 0.0;
};

// 引擎内部:
// float Time::GetDeltaTime() {
//     if (TestMode::IsActive())
//         return TestTime::GetDeltaTime();  // 返回手动控制的 dt
//     else
//         return realDeltaTime;
// }
```

### 6.2 固定种子随机: `TestRandom`

```cpp
// tests/utils/TestRandom.h
#pragma once
#include <random>

class TestRandom {
public:
    // 使用固定种子, 确保每次测试结果一致
    static std::mt19937& GetEngine() {
        static std::mt19937 engine(42); // 固定种子 42
        return engine;
    }
    
    // 便捷函数: 生成 [min, max) 范围内的均匀浮点
    static float Float(float min = 0.0f, float max = 1.0f) {
        std::uniform_real_distribution<float> dist(min, max);
        return dist(GetEngine());
    }
};
```

### 6.3 测试中的时间/随机控制

```cpp
// 使用示例
TEST(PhysicsTest, DeterminisiticSimulation) {
    TestTime::SetTime(0.0);
    TestRandom::Reset(42); // 重置种子
    
    std::vector<GPUParticleData> particles(64);
    // 使用 TestRandom 初始化粒子位置（确定性）
    for (auto& p : particles) {
        p.position[0] = TestRandom::Float(-10.0f, 10.0f);
        p.position[1] = TestRandom::Float(0.0f, 20.0f);
        p.position[2] = TestRandom::Float(-10.0f, 10.0f);
    }
    
    // 模拟 60 帧
    for (int i = 0; i < 60; ++i) {
        CPUSimulator::Step(particles, 1.0f/60.0f, ...);
        TestTime::Advance(1.0f/60.0f);
    }
    
    // 每次跑测试时, 结果完全一致
    EXPECT_NEAR(particles[0].position[1], expected_y, 1e-6f);
}
```

---

## 七、Sanitizer 集成

### 7.1 CMake 配置

当前项目的 `CMakeLists.txt` 已支持 ASan（通过 `ENABLE_ASAN=ON`），需要扩展至 UBSan 和 TSan：

```cmake
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

# ASan (已有)
if(ENABLE_ASAN AND MSVC)
    add_compile_options($<$<CONFIG:Debug>:/fsanitize=address>)
elseif(ENABLE_ASAN AND (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU"))
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

# UBSan (仅 Clang/GCC)
if(ENABLE_UBSAN AND (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU"))
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined)
endif()

# TSan (仅 Clang/GCC, 与 ASan 互斥)
if(ENABLE_TSAN AND (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU"))
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()
```

### 7.2 Sanitizer 配置补充

当前项目已处理 ASan 的基础配置，还需补充：

```cpp
// tests/main.cpp — ASan 泄漏检测配置
extern "C" {
    // Windows 上启用 ASan 泄漏检测
    __declspec(dllexport) int __asan_default_options() {
        return "detect_leaks=1:alloc_dealloc_mismatch=1:"
               "check_initialization_order=1:"
               "strict_init_order=1";
    }
}

// Windows CRT 内存泄漏检测（非 ASan 环境）
#if defined(_MSC_VER) && !defined(__clang__)
#include <crtdbg.h>
struct LeakDetector {
    LeakDetector() {
        _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
        // 设置断点在第 n 次分配（用于追踪已知泄漏）
        // _CrtSetBreakAlloc(12345);
    }
};
static LeakDetector s_LeakDetector;
#endif
```

### 7.3 CI/CD 集成 (GitHub Actions)

```yaml
# .github/workflows/tests.yml
name: Engine Tests

on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main]
  schedule:
    - cron: '0 2 * * *'  # 每晚运行

jobs:
  L1-unit-tests:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v3
        with:
          submodules: recursive
      - name: Configure (Debug + Tests)
        run: cmake -B build -G "Visual Studio 17 2022" -DENABLE_TESTS=ON
      - name: Build L1 tests
        run: cmake --build build --target test_core test_ecs
      - name: Run L1 tests
        run: ctest --test-dir build -R "test_core|test_ecs" --output-on-failure

  ASan-tests:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v3
        with:
          submodules: recursive
      - name: Configure (ASan)
        run: cmake -B build -G "Visual Studio 17 2022" -DENABLE_TESTS=ON -DENABLE_ASAN=ON
      - name: Build ASan tests
        run: cmake --build build --target test_core test_ecs test_job
      - name: Run ASan tests
        run: ctest --test-dir build -R "test_core|test_ecs|test_job" --output-on-failure

  L2-integration-tests:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v3
        with:
          submodules: recursive
      - name: Configure (Release + Tests)
        run: cmake -B build -G "Visual Studio 17 2022" -DENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
      - name: Build L2 tests
        run: cmake --build build --target test_physics test_renderer test_job --config RelWithDebInfo
      - name: Run L2 tests
        run: ctest --test-dir build -R "test_physics|test_renderer|test_job" --output-on-failure
        # GPU 测试需要 NVIDIA GPU，在 GitHub Actions 上需要通过 self-hosted runner
        continue-on-error: true

  L3-e2e-tests:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v3
      - name: Configure (Release)
        run: cmake -B build -G "Visual Studio 17 2022" -DENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Release
      - name: Build E2E
        run: cmake --build build --target test_e2e --config Release
      - name: Run E2E
        run: ctest --test-dir build -R "test_e2e" --output-on-failure
```

---

## 八、反模式警告 (Anti-Patterns)

### ⚠️ 1. 黄金图像对比 (Golden Image Testing)

**危险**: 渲染一帧截图后与基准图逐像素对比。

| 问题 | 说明 | 严重程度 |
|------|------|----------|
| GPU 驱动差异 | N 卡/A 卡/核显的抗锯齿边缘像素不同 | 🔴 几乎每次都失败 |
| 驱动版本差异 | 同一张卡升级驱动后光栅化结果微妙变化 | 🔴 持续维护噩梦 |
| 分辨率/窗口尺寸 | 测试环境与开发环境窗口大小不同 | 🟡 可规避 |
| 调试覆盖 | 覆盖 DX12/Vulkan/GL46 多后端时截图不同 | 🔴 多后端 C.I. 不可行 |

**推荐替代**:

```cpp
// ✅ 逻辑断言: 检查 DrawCall 数量
EXPECT_EQ(renderer->GetDrawCallCount(), 42);

// ✅ 直方图对比: 只检查整体亮度/颜色分布, 不强对比像素
Histogram h1 = ComputeHistogram(screenshot);
Histogram h2 = ComputeHistogram(reference);
EXPECT_TRUE(HistogramSimilar(h1, h2, 0.95f));

// ✅ 检查 Shader 参数绑定
EXPECT_TRUE(shader->HasUniform("u_ViewProj"));
EXPECT_FLOAT_EQ(shader->GetUniformFloat("u_Exposure"), 1.0f);
```

### ⚠️ 2. 将大体积测试资产塞入 Git

**危险**: 测试资产（模型/贴图/音频）的体积可能远超代码本身。

| 问题 | 示例 | 影响 |
|------|------|------|
| Git 仓库膨胀 | 50MB × 100 个测试资产 = 5GB | 克隆时间骤增 |
| 二进制 diff 爆炸 | 每次修改资产 → 全量重新存储 | 仓库历史无限增长 |
| 权限管理 | 部分资产可能受版权保护 | 法律风险 |

**推荐替代**:

```cpp
// ✅ 程序化生成测试资产
Mesh CreateTestCube(float size = 1.0f) {
    Mesh mesh;
    mesh.vertices = {
        {-size, -size, -size}, { size, -size, -size},
        { size,  size, -size}, {-size,  size, -size},
        {-size, -size,  size}, { size, -size,  size},
        { size,  size,  size}, {-size,  size,  size},
    };
    mesh.indices = { 0,1,2, 2,3,0, 1,5,6, 6,2,1, /* ... */ };
    return mesh;
}

Texture CreateTestTexture(uint32_t width, uint32_t height, uint32_t color = 0xFF888888) {
    Texture tex;
    tex.width = width;
    tex.height = height;
    tex.data.resize(width * height * 4);
    std::fill(tex.data.begin(), tex.data.end(), color);
    return tex;
}

// 在测试中使用:
TEST(MeshLoaderTest, LoadProgrammaticMesh) {
    auto mesh = CreateTestCube(2.0f);
    EXPECT_EQ(mesh.vertices.size(), 8);
    EXPECT_EQ(mesh.indices.size(), 36);
}
```

### ⚠️ 3. Flaky Tests (不稳定的测试)

**危险**: 测试结果时好时坏，导致开发者失去对测试的信任。

| 原因 | 典型场景 | 解决方案 |
|------|----------|----------|
| 竞态条件 | 多线程测试中 assert 在错误时机触发 | 使用 `std::atomic` + `WaitForAll` |
| 时间依赖 | 依赖 `sleep()` 等待异步操作完成 | 使用 `TestTime` 手动控制时间 |
| 随机性 | RNG 产生边界值 (如 0 或 NaN) | 使用 `TestRandom` 固定种子 |
| 外部资源 | 网络/数据库/显卡驱动不可用 | 使用 `GTEST_SKIP()` + Mock |

```cpp
// ❌ Flaky: 依赖实际时间
TEST(JobSystemTest, DISABLED_TaskCompletion) {
    jobSys.Dispatch([]() { /* 工作 */ });
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 假设 100ms 内完成
    EXPECT_TRUE(taskDone);
}

// ✅ 稳定: 使用同步原语
TEST(JobSystemTest, TaskCompletion) {
    std::atomic<bool> done{false};
    auto future = jobSys.Dispatch([&done]() { done.store(true); });
    future->Wait(); // 阻塞直到完成
    EXPECT_TRUE(done.load());
}
```

### ⚠️ 4. 过度 Mock (Over-Mocking)

**危险**: Mock 了所有依赖，测试了 Mock 而非真实代码。

```cpp
// ❌ 过度 Mock: 测试的是 Mock 的行为而非武器系统的逻辑
TEST(WeaponTest, OverMocked) {
    MockRenderer mockRenderer;
    MockAudio mockAudio;
    MockPhysics mockPhysics;
    
    EXPECT_CALL(mockRenderer, DrawMesh).Times(1);  // 只是验证了 Mock 的调用配置
    EXPECT_CALL(mockAudio, Play).Times(1);          // 不是真正的武器系统行为
    
    WeaponSystem::Fire(engineContext, player);
}

// ✅ 合理 Mock: 只 Mock 无关子系统，核心逻辑用真实实现
TEST(WeaponTest, ReasonableMock) {
    // 渲染器和音频用 Mock（与武器逻辑无关）
    MockRenderer mockRenderer;
    MockAudio mockAudio;
    
    // 但 ECS、物理、碰撞检测用真实实现
    EntityManager ecs;
    PhysicsWorld physics;
    ecs.RegisterComponent<Transform>();
    ecs.RegisterComponent<RigidBody>();
    
    EngineContext ctx;
    ctx.ecs = &ecs;
    ctx.physics = &physics;
    ctx.renderer = &mockRenderer;
    ctx.audio = &mockAudio;
    
    WeaponSystem::Fire(ctx, player);
    
    // 验证: 子弹实体被创建, 物理世界中有子弹刚体
    auto bullets = ecs.Query<BulletComponent>();
    EXPECT_EQ(bullets.size(), 1);
}
```

---

## 九、实施路径建议

### Phase 0: 基础设施 (1 天)

- [ ] 添加 `googletest` submodule: `git submodule add https://github.com/google/googletest.git third_party/googletest`
- [ ] 创建 `tests/` 目录结构 (CMakeLists.txt + main.cpp + mock/ + utils/)
- [ ] 在顶层 CMakeLists.txt 添加 `ENABLE_TESTS` 选项
- [ ] 验证 `cmake -DENABLE_TESTS=ON` 可构建空测试套件

### Phase 1: Core 层测试 (2 天)

- [ ] Vector3/Matrix4/Quaternion 单元测试 (20+ 用例)
- [ ] StackAllocator/PoolAllocator 内存泄漏检测测试
- [ ] StringID 测试
- [ ] Logger Mock Sink 测试
- [ ] CI 集成 L1 测试 (每次提交自动运行)

### Phase 2: ECS + JobSystem 测试 (2 天)

- [ ] EntityManager CRUD 测试
- [ ] Component Lifecycle 测试 (10K 实体压力)
- [ ] System Scheduling 测试
- [ ] JobSystem 1000 任务压力测试
- [ ] TSan 配置 + 无 Data Race 验证

### Phase 3: 物理测试迁移 (1 天)

- [ ] 将 sandbox CPUSimulator 测试迁移到 `test_physics/`
- [ ] 将 GPU 三环验证迁移到 `test_physics/` (含 `GTEST_SKIP()` 处理)
- [ ] JoltIntegration 测试 (体素查询/碰撞事件/关节)

### Phase 4: Mock + VFS + 资产测试 (2 天)

- [ ] MockRenderer / MockAudioEngine 实现
- [ ] VirtualFileSystem 实现
- [ ] MeshLoader 测试 (内存 glTF 构建)
- [ ] Script C-ABI 绑定测试

### Phase 5: E2E + CI 硬化 (2 天)

- [ ] EngineBoot + MultiFrameTick 测试
- [ ] GitHub Actions 多 Job 配置 (L1/L2/L3 + ASan)
- [ ] `continue-on-error` 策略 (GPU 测试在非 GPU Runner 上跳过)
- [ ] 测试覆盖率报告 (Codecov / SonarQube)

**总计工作量**: ~10 天  
**并行建议**: Phase 1+2 可并行, Phase 3 依赖 Phase 1, Phase 4 依赖 Phase 2

---

## 十、与现有工程的衔接

### 10.1 现有 GPUPhysicsTest 的迁移策略

当前 `sandbox/src/GPUPhysicsTest/main.cpp` 包含：

- 三环验证 (Ring 1/2/3) → 迁移至 `test_physics/GPUPhysicsRingTest.cpp`
- CPUSimulator → 提取为独立工具类，用于 Ring 3 验证
- GLFW 隐藏窗口初始化 → 迁移至 `GL46Device::InitializeWithGLFWHeadless()`

### 10.2 现有 ASan 配置的复用

当前 `CMakeLists.txt` 已支持 `ENABLE_ASAN` 选项，测试框架直接复用此配置。不需要额外修改。

### 10.3 Headless 模式要求

引擎需要支持 `EngineConfig::headless = true` 模式：

- 不创建 GLFW 窗口
- 渲染器创建 GL46 Headless Context
- 音频子系统不初始化
- ECS/物理/JobSystem 正常运行

此模式已在 `GPUPhysicsTest` 中验证通过，可直接用于 `test_e2e` 测试。

---

## 十一、期望收益

| 指标 | 实施前 | 实施后 |
|------|--------|--------|
| 测试覆盖范围 | GPU 物理 + CPU Simulator (~500 行) | 全引擎 7 个子系统 (~3000+ 行) |
| 自动执行频率 | 手动运行 | CI 每次提交 + 每晚全量 |
| Bug 检出时机 | 运行时崩溃或视觉观察 | 编译期断言 + CI 自动捕获 |
| 重构安全网 | 无 | ASan + UBSan + 单元测试三重保护 |
| 多线程安全 | 手动 Code Review | TSan 自动化检测 |
| 回归测试 | 无 | 每次提交自动验证 |
| 新子系统集成信心 | 低 (脚本系统开发中) | 高 (C-ABI 绑定测试保证 ABI 兼容) |