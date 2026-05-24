#include "Engine/Core/Physics/PhysicsComponent.h"

namespace Engine {

    PhysicsComponent::~PhysicsComponent() {
        DestroyBody();
    }

    void PhysicsComponent::CreateBody(std::shared_ptr<IPhysicsWorld> world,
                                       const BodyDef& def) {
        // 如果已有 body，先销毁
        if (m_Body) {
            DestroyBody();
        }

        m_World = std::move(world);
        m_Body = m_World->CreateBody(def);

        // 设置组件引用，用于碰撞事件路由
        if (m_Body) {
            m_Body->SetComponentRef(this);
        }
    }

    void PhysicsComponent::DestroyBody() {
        // 使用 shared_ptr 持有世界引用，确保调用 DestroyBody 时世界仍存活。
        // 如果世界已被其他途径释放，则 m_World 为空，只释放 body 引用。
        if (m_Body && m_World) {
            m_World->DestroyBody(m_Body.get());
        }
        m_Body.reset();
        m_World.reset();
    }

    void PhysicsComponent::SyncTransformToPhysics(const Vec2& position, float32 angle) {
        if (m_Body) {
            m_Body->SetTransform(position, angle);
        }
    }

    void PhysicsComponent::SyncPhysicsToTransform(Vec2& outPosition, float32& outAngle) const {
        if (m_Body) {
            outPosition = m_Body->GetPosition();
            outAngle    = m_Body->GetAngle();
        } else {
            outPosition = Vec2(0.0f, 0.0f);
            outAngle    = 0.0f;
        }
    }

} // namespace Engine
