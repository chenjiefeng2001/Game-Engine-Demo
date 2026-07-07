/**
 * @file PhysicsComponent3D.cpp
 * @brief 3D 物理组件实现
 */

#include "Engine/Core/GameObject/PhysicsComponent3D.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/TransformComponent.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>

namespace Engine {

    PhysicsComponent3D::~PhysicsComponent3D() {
        DestroyBody();
    }

    void PhysicsComponent3D::CreateBody(std::shared_ptr<IPhysicsWorld3D> world,
                                         const BodyDef3D& def) {
        if (!world) return;

        // 如果已有刚体，先销毁
        if (m_Body) {
            DestroyBody();
        }

        m_World = world;  // weak_ptr
        m_Body = world->CreateBody(def);

        // 将物理刚体的位置同步到 TransformComponent
        SyncPhysicsToTransform();
    }

    void PhysicsComponent3D::DestroyBody() {
        if (auto world = m_World.lock()) {
            if (m_Body) {
                m_Body->SetComponentRef(nullptr);
                world->DestroyBody(m_Body.get());
                m_Body.reset();
            }
        }
    }

    void PhysicsComponent3D::SyncPhysicsToTransform() {
        if (!m_Body) return;

        auto* owner = GetOwner();
        if (!owner) return;

        auto& transform = owner->GetTransform();
        transform.SetPosition(m_Body->GetPosition());
        // Use quaternion to avoid gimbal lock
        transform.SetRotationQuat(m_Body->GetRotationQuat());
    }

    void PhysicsComponent3D::SyncTransformToPhysics() {
        if (!m_Body) return;

        auto* owner = GetOwner();
        if (!owner) return;

        auto& transform = owner->GetTransform();
        m_Body->SetPosition(transform.GetPosition());
        m_Body->SetRotationQuat(transform.GetRotationQuat());
    }

    void PhysicsComponent3D::OnDestroy() {
        DestroyBody();
    }

} // namespace Engine
