#include "Engine/Core/Physics/PhysicsRuntime2D.h"

#include "Engine/Box2D/Box2DPhysicsWorld.h"
#include "Engine/Core/GameObject/ColliderComponent.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/RigidBodyComponent.h"
#include "Engine/Core/GameObject/TransformComponent.h"
#include "Engine/Core/Physics/IPhysicsBody.h"
#include "Engine/Core/Physics/PhysicsColliderAdapter.h"
#include "Engine/Core/Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace Engine {

namespace {
    constexpr float32 kDegToRad = 0.017453292519943295f;

    void CollectObjects(const GameObject& go,
                        std::vector<std::shared_ptr<GameObject>>& out) {
        // 只收集可观察对象；shared_ptr 由场景/父对象持有，函数结束即释放。
        for (const auto& child : go.GetChildren()) {
            if (!child) continue;
            out.push_back(child);
            CollectObjects(*child, out);
        }
    }

    std::vector<std::shared_ptr<GameObject>> SnapshotScene(Scene& scene) {
        std::vector<std::shared_ptr<GameObject>> objects;
        for (const auto& object : scene.GetObjects()) {
            if (!object) continue;
            objects.push_back(object);
            CollectObjects(*object, objects);
        }
        return objects;
    }

    float32 TransformAngleRadians(const TransformComponent& transform) {
        return transform.GetRotationEuler().z * kDegToRad;
    }

    void SetTransformFromBody(TransformComponent& transform,
                              const IPhysicsBody& body) {
        const Vec2 position = body.GetPosition();
        const float32 angle = body.GetAngle();
        const Vec3 old = transform.GetPosition();
        transform.SetPosition(position.x, position.y, old.z);
        transform.SetRotationQuat(
            Quat(0.0f, 0.0f, std::sin(angle * 0.5f), std::cos(angle * 0.5f)));
    }
}

PhysicsRuntime2D::PhysicsRuntime2D() = default;

PhysicsRuntime2D::~PhysicsRuntime2D() {
    Stop();
}

bool PhysicsRuntime2D::Start(Scene& scene, std::string* error) {
    Stop();
    m_LastError.clear();

    auto world = std::make_shared<Box2DPhysicsWorld>(scene.GetGravity());
    if (!world) {
        m_LastError = "cannot create 2D physics world";
        if (error) *error = m_LastError;
        return false;
    }

    auto adapter = std::make_unique<PhysicsColliderAdapter>();
    adapter->SetWorld(world);
    m_World = std::move(world);
    m_Adapter = std::move(adapter);
    m_Scene = &scene;
    m_Accumulator = 0.0f;
    m_Running = true;

    RefreshObservations();
    return true;
}

void PhysicsRuntime2D::Stop() {
    if (m_Adapter) {
        // 先清理仍存活对象上的 lifecycle/mutation hooks，避免 adapter 销毁后悬垂。
        for (auto& weak : m_Observed) {
            if (auto object = weak.lock()) {
                m_Adapter->UnobserveGameObject(*object);
            }
        }
        m_Adapter->ReleaseWorld();
        m_Adapter.reset();
    }
    m_World.reset();
    m_Observed.clear();
    m_Accumulator = 0.0f;
    m_Scene = nullptr;
    m_Running = false;
}

bool PhysicsRuntime2D::Step(float32 realDt, std::string* error) {
    if (!m_Running || !m_World || !m_Adapter) {
        m_LastError = "physics runtime is not running";
        if (error) *error = m_LastError;
        return false;
    }
    if (!std::isfinite(realDt) || realDt < 0.0f) {
        m_LastError = "invalid physics frame delta";
        if (error) *error = m_LastError;
        return false;
    }

    // 运行时可能由 Lua 生成/销毁实体，每帧刷新观察集合。
    RefreshObservations();

    m_Accumulator += (std::min)(realDt, kMaxFrameDt);
    int32 steps = static_cast<int32>(m_Accumulator / kFixedDt);
    steps = (std::min)(steps, kMaxSubSteps);
    if (steps <= 0) return true;

    SyncKinematicBodies();
    for (int32 i = 0; i < steps; ++i) {
        m_World->Step(kFixedDt);
    }
    m_Accumulator -= static_cast<float32>(steps) * kFixedDt;
    if (m_Accumulator < 0.0f) m_Accumulator = 0.0f;

    SyncPhysicsToTransforms();
    return true;
}

size_t PhysicsRuntime2D::GetBodyCount() const {
    return m_Adapter ? m_Adapter->GetBodyCount() : 0u;
}

void PhysicsRuntime2D::RefreshObservations() {
    if (!m_Scene || !m_Adapter) return;

    const auto objects = SnapshotScene(*m_Scene);
    std::unordered_set<GameObject*> live;
    live.reserve(objects.size());
    for (const auto& object : objects) live.insert(object.get());

    // 移除已销毁对象（adapter 已通过 GameObject 析构回调 Unbind）。
    m_Observed.erase(
        std::remove_if(m_Observed.begin(), m_Observed.end(),
                       [&live](const std::weak_ptr<GameObject>& weak) {
                           auto object = weak.lock();
                           return !object || live.find(object.get()) == live.end();
                       }),
        m_Observed.end());

    std::unordered_set<GameObject*> observed;
    observed.reserve(m_Observed.size());
    for (const auto& weak : m_Observed) {
        if (auto object = weak.lock()) observed.insert(object.get());
    }

    for (const auto& object : objects) {
        GameObject* raw = object.get();
        if (observed.find(raw) != observed.end()) continue;
        m_Adapter->ObserveGameObject(*object);
        m_Observed.push_back(object);
    }
}

void PhysicsRuntime2D::SyncKinematicBodies() {
    for (const auto& weak : m_Observed) {
        auto object = weak.lock();
        if (!object) continue;
        auto* collider = dynamic_cast<ColliderComponent*>(
            object->GetComponentByName("Collider"));
        if (!collider) continue;
        IPhysicsBody* body = m_Adapter->GetBody(*collider);
        if (!body || body->GetType() != BodyType::Kinematic) continue;
        const Vec3 position = object->GetTransform().GetPosition();
        body->SetTransform(Vec2(position.x, position.y),
                           TransformAngleRadians(object->GetTransform()));
    }
}

void PhysicsRuntime2D::SyncPhysicsToTransforms() {
    for (const auto& weak : m_Observed) {
        auto object = weak.lock();
        if (!object) continue;
        auto* collider = dynamic_cast<ColliderComponent*>(
            object->GetComponentByName("Collider"));
        if (!collider) continue;
        IPhysicsBody* body = m_Adapter->GetBody(*collider);
        if (!body || body->GetType() == BodyType::Static) continue;
        SetTransformFromBody(object->GetTransform(), *body);
    }
}

} // namespace Engine
