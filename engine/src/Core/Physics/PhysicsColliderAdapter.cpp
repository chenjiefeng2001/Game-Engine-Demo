#include "Engine/Core/Physics/PhysicsColliderAdapter.h"
#include "Engine/Core/GameObject/ColliderComponent.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/Physics/IPhysicsBody.h"
#include "Engine/Core/Physics/IPhysicsWorld.h"
#include "Engine/Core/Log.h"

#include <algorithm>
#include <cstring>

namespace Engine {

    // ════════════════════════════════════════════
    // World 所有权（D1）
    // ════════════════════════════════════════════

    PhysicsColliderAdapter::~PhysicsColliderAdapter() {
        // World 存活时销毁全部 body；组件（声明层）原样存活。
        Clear();
    }

    void PhysicsColliderAdapter::SetWorld(std::shared_ptr<IPhysicsWorld> world) {
        // 换世界前先清空旧绑定：body 属于旧世界，不得残留到新世界。
        Clear();
        m_World = std::move(world);
    }

    void PhysicsColliderAdapter::ReleaseWorld() {
        // 销毁全部已绑 body（此时 World 仍存活），再释放 World 引用。
        // m_Tracked 保留 —— 组件存活，待新 World + RebuildAll 重建。
        Clear();
        m_World.reset();
    }

    // ════════════════════════════════════════════
    // Runtime 绑定
    // ════════════════════════════════════════════

    /// B6-8 负路径：非法几何必须干净失败（不 crash / 不 double-bind / 不产生 ghost body）。
    /// 半径 / 半宽高必须为有限正值；否则即便传给 Box2D 也可能产生退化 shape。
    static bool IsValidColliderGeometry(const ColliderComponent& collider) {
        switch (collider.GetShape()) {
            case ColliderShape::Circle:
                return collider.GetRadius() > 0.0f;
            case ColliderShape::Box:
                return collider.GetHalfExtentsX() > 0.0f && collider.GetHalfExtentsY() > 0.0f;
        }
        return false;
    }

    bool PhysicsColliderAdapter::Bind(const ColliderComponent& collider) {
        if (!m_World) {
            Log::Warn("[PhysicsColliderAdapter] Bind('{}') failed: no Physics World bound "
                      "(SetWorld first)", collider.GetComponentTypeName());
            return false;
        }
        if (!IsValidColliderGeometry(collider)) {
            Log::Warn("[PhysicsColliderAdapter] Bind('{}') failed: invalid geometry "
                      "(radius<=0 or half-extent<=0)", collider.GetComponentTypeName());
            return false;
        }
        if (m_Bodies.find(&collider) != m_Bodies.end()) {
            Log::Warn("[PhysicsColliderAdapter] Bind('{}') failed: already bound (idempotent)",
                      collider.GetComponentTypeName());
            return false;
        }

        const BodyDef def = BuildBodyDef(collider);
        std::shared_ptr<IPhysicsBody> body = m_World->CreateBody(def);
        if (!body) {
            Log::Warn("[PhysicsColliderAdapter] Bind('{}') failed: physics world rejected BodyDef",
                      collider.GetComponentTypeName());
            return false;
        }

        // GAP-1 规避：初始形状路径（CreateShapesFromBodyDef）丢失 isSensor —— 不照搬该错误路径。
        // 改经 Fixture 路径重建形状（Box2DPhysicsBody::CreateShapeFromDef），
        // sensor + category/mask 一并落到真实 Physics shape。
        body->ClearFixtures();
        FixtureDef fd;
        fd.shape        = def.shape;
        fd.isSensor     = collider.IsSensor();
        fd.categoryBits = collider.GetCategory();
        fd.maskBits     = collider.GetMask();
        fd.density      = 1.0f;
        fd.friction     = 0.3f;
        fd.restitution  = 0.2f;
        if (!body->AddFixture(fd)) {
            Log::Warn("[PhysicsColliderAdapter] Bind('{}') failed: fixture creation rejected",
                      collider.GetComponentTypeName());
            m_World->DestroyBody(body.get());
            return false;
        }

        // v1 契约（B2-4）：enabled → 即时 SetActive（唯一实时通道）
        body->SetActive(collider.IsEnabled());

        Track(collider);
        m_Bodies[&collider] = std::move(body);
        return true;
    }

    void PhysicsColliderAdapter::Track(const ColliderComponent& collider) {
        if (std::find(m_Tracked.begin(), m_Tracked.end(), &collider) == m_Tracked.end()) {
            m_Tracked.push_back(&collider);
        }
    }

    void PhysicsColliderAdapter::Unbind(const ColliderComponent& collider) {
        auto it = m_Bodies.find(&collider);
        if (it != m_Bodies.end()) {
            if (m_World) m_World->DestroyBody(it->second.get());
            m_Bodies.erase(it);
        }
        // Remove 语义：移出受管集合（之后 RebuildAll 不再重建它）
        auto tr = std::find(m_Tracked.begin(), m_Tracked.end(), &collider);
        if (tr != m_Tracked.end()) m_Tracked.erase(tr);
    }

    void PhysicsColliderAdapter::Rebuild(const ColliderComponent& collider) {
        if (!m_World) return;   // 无 World：no-op，保持受管状态（待 RebuildAll）
        Unbind(collider);
        Bind(collider);
    }

    void PhysicsColliderAdapter::Clear() {
        for (auto& [comp, body] : m_Bodies) {
            (void)comp;
            if (m_World) m_World->DestroyBody(body.get());
        }
        m_Bodies.clear();
        // 保留 m_Tracked：受管组件集合跨 Clear/ReleaseWorld 存活
    }

    void PhysicsColliderAdapter::RebuildAll() {
        if (!m_World) return;   // 无 World：no-op
        const std::vector<const ColliderComponent*> tracked = m_Tracked;  // 快照（Rebuild 会改动容器）
        for (const ColliderComponent* comp : tracked) {
            Rebuild(*comp);
        }
    }

    // ════════════════════════════════════════════
    // GameObject 集成（F2-B4）
    // ════════════════════════════════════════════

    void PhysicsColliderAdapter::InstallMutationHook(ColliderComponent& collider) {
        collider.SetMutationHook([this](ColliderComponent& c, const char* prop) {
            if (std::strcmp(prop, "enabled") == 0) {
                // 唯一实时通道（B2-4 契约）：enabled → 即时 SetActive
                if (IPhysicsBody* body = GetBody(c)) body->SetActive(c.IsEnabled());
                return;
            }
            // 其余声明式属性（shape/radius/halfX/halfY/isSensor/category/mask）：Destroy/Recreate
            Rebuild(c);
        });
    }

    void PhysicsColliderAdapter::ObserveGameObject(GameObject& go) {
        for (const auto& obs : m_Observations) {
            if (obs.go == &go) return;   // 已观察，幂等
        }

        const uint32 listenerId = go.AddComponentLifecycleListener(
            [this](Component& comp, bool attached) {
                auto* collider = dynamic_cast<ColliderComponent*>(&comp);
                if (!collider) return;
                if (attached) {
                    Track(*collider);          // 无条件受管（世界未就绪时也可晚到恢复）
                    InstallMutationHook(*collider);
                    Bind(*collider);           // 无世界 → 干净失败，待 RebuildAll
                } else {
                    Unbind(*collider);
                    collider->SetMutationHook(nullptr);
                }
            });
        m_Observations.push_back({&go, listenerId});

        // 观察前已挂载的 Collider：立即接管（含变更钩子）
        if (auto* comp = go.GetComponentByName("Collider")) {
            if (auto* collider = dynamic_cast<ColliderComponent*>(comp)) {
                Track(*collider);
                InstallMutationHook(*collider);
                Bind(*collider);
            }
        }
    }

    void PhysicsColliderAdapter::UnobserveGameObject(GameObject& go) {
        for (auto it = m_Observations.begin(); it != m_Observations.end(); ++it) {
            if (it->go != &go) continue;
            go.RemoveComponentLifecycleListener(it->listenerId);
            // 清理该 GameObject 上全部 Collider 的绑定与变更钩子
            if (auto* comp = go.GetComponentByName("Collider")) {
                if (auto* collider = dynamic_cast<ColliderComponent*>(comp)) {
                    collider->SetMutationHook(nullptr);
                    Unbind(*collider);
                }
            }
            m_Observations.erase(it);
            return;
        }
    }

    bool PhysicsColliderAdapter::IsBound(const ColliderComponent& collider) const {
        return m_Bodies.find(&collider) != m_Bodies.end();
    }

    IPhysicsBody* PhysicsColliderAdapter::GetBody(const ColliderComponent& collider) const {
        auto it = m_Bodies.find(&collider);
        return it != m_Bodies.end() ? it->second.get() : nullptr;
    }

    // ════════════════════════════════════════════
    // 单向映射：Component → BodyDef
    // ════════════════════════════════════════════

    BodyDef PhysicsColliderAdapter::BuildBodyDef(const ColliderComponent& collider) {
        BodyDef def;

        // 适配器策略：Collider 契约不含 body type（RigidBody 属 F2 范围外，F2 红线），
        // 固定映射为 Static（碰撞区域语义）；动态类型待 RigidBody 契约进入后定义。
        def.type = BodyType::Static;

        switch (collider.GetShape()) {
            case ColliderShape::Circle:
                def.shape.type         = ShapeType::Circle;
                def.shape.circleRadius = collider.GetRadius();
                break;
            case ColliderShape::Box:
                def.shape.type   = ShapeType::Box;
                def.shape.boxSize = { collider.GetHalfExtentsX(), collider.GetHalfExtentsY() };
                break;
        }
        def.shape.isSensor = collider.IsSensor();
        def.categoryBits   = collider.GetCategory();
        def.maskBits       = collider.GetMask();

        // 世界位置属 Transform 所有权（F2-A 裁定），B2/B3 不映射；后续 Gate 接 Transform 同步。
        // （本轮 position 取 BodyDef 默认 {0,0}，角度 0）
        return def;
    }

} // namespace Engine
