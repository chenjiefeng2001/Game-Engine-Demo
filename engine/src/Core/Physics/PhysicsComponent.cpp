#include "Engine/Core/Physics/PhysicsComponent.h"
#include "Engine/Core/Scene/Serializer.h"
#include <nlohmann/json.hpp>

// ── 注册到 JsonSerializer 反序列化工厂（文件作用域，静态初始化） ──
namespace {
    bool registerPhysics = []() -> bool {
        Engine::JsonSerializer::RegisterComponentType<Engine::PhysicsComponent>("PhysicsComponent");
        return true;
    }();
}

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

    // ============================================================
    // Serialize / Deserialize — JSON
    // ============================================================

    void PhysicsComponent::Serialize(nlohmann::json& json) const {
        json["type"] = "PhysicsComponent";
        auto& data = json["data"];
        data["hasBody"] = (m_Body != nullptr);
        // TODO: 完整序列化 BodyDef（fixture 形状、密度、摩擦等）
        // 当前仅存有/无标记，物理体重建依赖外部逻辑
    }

    bool PhysicsComponent::Deserialize(const nlohmann::json& json) {
        if (!json.contains("data")) return true;
        const auto& data = json["data"];
        bool hasBody = false;
        if (data.contains("hasBody")) hasBody = data["hasBody"];
        (void)hasBody; // 物理体重建需要外部物理世界，此处仅标记
        // TODO: 完整反序列化 BodyDef 并在外部物理世界中重建
        return true;
    }

} // namespace Engine
