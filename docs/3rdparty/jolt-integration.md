# Jolt Physics 集成指南

## 1. 添加子模块

```bash
# 在项目根目录下执行
cd <project-root>
git submodule add https://github.com/jrouwe/JoltPhysics.git third_party/jolt
git submodule update --init --recursive
```

## 2. 更新 `third_party/CMakeLists.txt`

在 `third_party/CMakeLists.txt` 末尾添加：

```cmake
# =============================================================================
# Jolt Physics — 3D 物理引擎
# =============================================================================
# 选项说明：
#   JPH_CROSS_PLATFORM_DETERMINISTIC — 跨平台确定性（ON）
#   JPH_DOUBLE_PRECISION — 使用 double 精度（OFF）
#   JPH_USE_AVX2 — 启用 AVX2 优化（默认自动检测）
#   JPH_USE_SSE4_1 — 启用 SSE4.1 优化（默认自动检测）
option(JPH_BUILD_TESTS "Build Jolt tests" OFF)
option(JPH_BUILD_BENCHMARKS "Build Jolt benchmarks" OFF)
add_subdirectory(jolt)
```

## 3. 创建 Jolt 实现文件

创建以下文件实现 `IPhysicsWorld3D` / `IPhysicsBody3D` / `IJoint3D`：

```
engine/src/Jolt/
├── JoltPhysicsWorld.h     # IPhysicsWorld3D 的 Jolt 实现
├── JoltPhysicsWorld.cpp
├── JoltPhysicsBody.h      # IPhysicsBody3D 的 Jolt 实现
├── JoltPhysicsBody.cpp
├── JoltJoint.h            # IJoint3D 的 Jolt 实现
├── JoltJoint.cpp
├── JoltDebugDraw.h        # IPhysicsDebugDraw3D 的 Jolt 实现
└── JoltDebugDraw.cpp
```

### 3a. JoltPhysicsWorld.h 骨架

```cpp
#pragma once
#include "Engine/Core/Physics/IPhysicsWorld3D.h"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>

namespace Engine {

class JoltPhysicsWorld : public IPhysicsWorld3D {
public:
    JoltPhysicsWorld();
    ~JoltPhysicsWorld() override;

    bool Init(const PhysicsWorldConfig3D& config) override;
    void Shutdown() override;
    void Step(float32 dt, int32 collisionSteps = 1) override;

    std::shared_ptr<IPhysicsBody3D> CreateBody(const BodyDef3D& def) override;
    void DestroyBody(IPhysicsBody3D* body) override;
    // ... 其余接口实现
private:
    JPH::PhysicsSystem* m_System = nullptr;
    JPH::TempAllocator* m_TempAllocator = nullptr;
    JPH::JobSystem*     m_JobSystem = nullptr;
    // 映射表: JPH::BodyID ⇔ IPhysicsBody3D*
    std::unordered_map<JPH::BodyID, std::shared_ptr<JoltPhysicsBody>> m_Bodies;
};

} // namespace Engine
```

### 3b. 核心映射逻辑

```cpp
// 将 BodyDef3D → JPH::BodyCreationSettings
JPH::BodyCreationSettings JoltPhysicsWorld::ToJoltBodySettings(const BodyDef3D& def) {
    JPH::ShapeRefC shape = CreateJoltShape(def.shape);
    JPH::EMotionType motionType = ToJoltMotionType(def.type);
    JPH::ObjectLayer layer = (def.type == BodyType3D::Static)
        ? Layers::NON_MOVING : Layers::MOVING;

    JPH::Vec3 pos(def.position.x, def.position.y, def.position.z);
    JPH::Quat rot = JPH::Quat::sEulerAngles(
        JPH::Vec3(def.rotation.x, def.rotation.y, def.rotation.z));

    JPH::BodyCreationSettings settings(shape, pos, rot, motionType, layer);
    settings.mFriction = def.friction;
    settings.mRestitution = def.restitution;
    settings.mLinearDamping = def.linearDamping;
    settings.mAngularDamping = def.angularDamping;
    settings.mAllowSleeping = def.allowSleep;
    settings.mIsSensor = def.shape.isSensor;
    return settings;
}
```

## 4. 注册到 PhysicsSystemManager

```cpp
// 在 Application 初始化时
#include "Engine/Jolt/JoltPhysicsWorld.h"

auto joltWorld = std::make_shared<JoltPhysicsWorld>();
PhysicsWorldConfig3D config;
config.gravity = {0, -9.81f, 0};
joltWorld->Init(config);

PhysicsSystemManager mgr;
mgr.SetWorld3D(joltWorld);
```

## 5. 物理→渲染同步

```cpp
// 每帧步进后，遍历所有 PhysicsComponent3D
for (auto& obj : scene.GetObjects()) {
    auto* phys = obj->GetComponent<PhysicsComponent3D>();
    if (phys && phys->HasBody()) {
        phys->SyncPhysicsToTransform();
    }
}
```

## 6. 构建验证

```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
ninja _3DTest  # 或其他使用 3D 物理的 target
```
