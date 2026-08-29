#pragma once

/**
 * @file PhysicsColliderAdapter.h
 * @brief F2-B 唯一 Runtime 映射层：ColliderComponent（声明事实源）→ Physics Body（派生对象）
 *
 * 所有权模型（F2-B D1，与 F2-A Closure §7 一致）：
 *
 * ```text
 * PhysicsColliderAdapter
 *  ├── PhysicsWorld   （显式拥有/引用，SetWorld/ReleaseWorld 掌控生命周期）
 *  ├── m_Tracked      （受管 ColliderComponent 集合 —— 跨 World 销毁存活）
 *  └── m_Bodies       （ColliderComponent* → shared_ptr<IPhysicsBody>，已绑 body）
 *
 * ColliderComponent   （declaration only，零物理引用 —— F2-A 已冻结）
 * Physics Body        （runtime-derived，绝无 Body→Component 反向同步）
 * ```
 *
 * 【明确不复制 PhysicsComponent 的缺陷】
 *   B1 审计发现 `PhysicsComponent` 自持 `shared_ptr<world>` → 组件反向钉住 World
 *   生命周期。Adapter 反其道而行：**Component 不拥有 World**，World 生命周期由
 *   Adapter 显式管理（SetWorld / ReleaseWorld），组件无感知。
 *
 * 【B3 World Lifecycle 语义（D3：RebuildAll = Foundation v1 正式 Runtime contract）】
 *   - `Bind(c)`：组件进入受管集合并创建 body（幂等；无 World / 创建失败 → 干净失败，
 *     不产生任何副作用，Log 可诊断）。
 *   - `ReleaseWorld()`：销毁全部已绑 body、释放 World 引用，**受管集合保留** →
 *     组件存活；之后 `SetWorld(newWorld)` + `RebuildAll()` 即可从组件重建全部 body。
 *   - `RebuildAll()`：对受管集合逐一 Destroy+Create，**参数完全来自 Component 当前状态**
 *     （不是旧 Body 快照）。
 *   - `Unbind(c)`：销毁 body 并移出受管集合（Remove 语义）。
 *   - `Clear()`：销毁全部已绑 body（保留 World 与受管集合）。
 *
 * 【v1 Mutation 契约（B2-4 表）】
 *   - `enabled`       → 即时 `SetActive`（唯一实时通道）
 *   - `shape/radius/halfX/halfY/sensor/category/mask` → `Rebuild`（Destroy/Recreate）
 *   不引入任何隐式实时同步机制。
 *
 * 【GAP-1 规避（B1 审计：CreateShapesFromBodyDef 丢失 isSensor）】
 *   Adapter 不照搬该错误路径：Bind 时经 Fixture 路径重建形状，
 *   sensor + category/mask 一并落到真实 Physics shape（Box2DPhysicsBody::CreateShapeFromDef）。
 *   GAP-1 本身保持登记（EF-F2B-002），不顺手修旧路径。
 *
 * 注意：本头文件**不** include ColliderComponent.h / IPhysicsBody.h（仅前置声明），
 * 保持分层：Contract（GameObject 域）不依赖 Adapter，Adapter 依赖 Contract。
 */

#include "Engine/Core/Physics/PhysicsDefs.h"
#include <memory>
#include <unordered_map>
#include <vector>

namespace Engine {

    class ColliderComponent;
    class GameObject;
    class IPhysicsBody;
    class IPhysicsWorld;

    class PhysicsColliderAdapter {
    public:
        PhysicsColliderAdapter() = default;
        ~PhysicsColliderAdapter();                 // 销毁全部 body（world 存活时），不触碰 Component

        PhysicsColliderAdapter(const PhysicsColliderAdapter&) = delete;
        PhysicsColliderAdapter& operator=(const PhysicsColliderAdapter&) = delete;

        // ════════════════════════════════════════════
        // World 所有权（D1）
        // ════════════════════════════════════════════

        /** 绑定世界。换世界前先 Clear（旧 body 属旧世界，不得残留）。 */
        void SetWorld(std::shared_ptr<IPhysicsWorld> world);

        std::shared_ptr<IPhysicsWorld> GetWorld() const { return m_World; }

        /** 销毁 Runtime：先 Clear（销毁全部 body），再释放 World 引用。
         *  受管 ColliderComponent 集合保留 —— 组件原样存活，
         *  之后 SetWorld + RebuildAll 即可从组件完整重建。 */
        void ReleaseWorld();

        // ════════════════════════════════════════════
        // GameObject 集成（F2-B4）：组件生命周期自动跟随
        // ════════════════════════════════════════════

        /**
         * 观察一个 GameObject：其 ColliderComponent 的 Add/Remove/属性变更将被自动跟随
         * （Add→Bind / Remove→Unbind / enabled→SetActive，其余属性→Rebuild）。
         * 观察前已挂载的 Collider 立即接管。事件驱动（GameObject 组件生命周期监听器），
         * 零轮询。生命周期纪律：Adapter 必须先于被观察 GameObject 销毁，或先 Unobserve。
         */
        void ObserveGameObject(GameObject& go);

        /** 停止观察：移除监听器并解绑该 GameObject 上全部 Collider（含变更钩子）。 */
        void UnobserveGameObject(GameObject& go);

        // ════════════════════════════════════════════
        // Runtime 绑定
        // ════════════════════════════════════════════

        /** Create：组件进入受管集合并创建 Physics Body（参数全部来自 Component）。
         *  已绑定 / 无世界 / 创建失败返回 false（幂等；失败无副作用，Log 可诊断）。 */
        bool  Bind(const ColliderComponent& collider);

        /** Destroy：销毁该组件的 Runtime Body 并移出受管集合。组件不受影响。 */
        void  Unbind(const ColliderComponent& collider);

        /** Destroy + Create：Runtime 重建，参数完全来自 Component 当前状态（B2-4 契约）。
         *  无 World 时为 no-op（保持受管状态，待 World 就绪后 RebuildAll）。 */
        void  Rebuild(const ColliderComponent& collider);

        /** 对受管集合逐一 Destroy+Create（D3：World 重建 / 冷重启后的显式重建入口）。
         *  无 World 时为 no-op。 */
        void  RebuildAll();

        /** 销毁全部已绑 body（保留 World 与受管集合）。 */
        void  Clear();

        bool  IsBound(const ColliderComponent& collider) const;

        /** Runtime 查询：Body 指针（不暴露给 Component，仅供 Runtime/测试/审计）。 */
        IPhysicsBody* GetBody(const ColliderComponent& collider) const;

        /** 已绑 body 数（≠ 受管组件数：ReleaseWorld 后为 0）。 */
        size_t GetBodyCount() const { return m_Bodies.size(); }

        /** 受管组件数（跨 World 销毁存活）。 */
        size_t GetTrackedCount() const { return m_Tracked.size(); }

        /** 单向映射：Component → BodyDef（public 供测试/审计查验映射本身）。 */
        static BodyDef BuildBodyDef(const ColliderComponent& collider);

    private:
        /** 安装变更钩子：enabled → 即时 SetActive；其余属性 → Rebuild（B2-4 契约） */
        void InstallMutationHook(ColliderComponent& collider);

        /** 加入受管集合（幂等；世界未就绪时也登记，供晚到 RebuildAll 恢复） */
        void Track(const ColliderComponent& collider);

        std::shared_ptr<IPhysicsWorld> m_World;
        // 已绑 body：组件为键（observer，不拥有），body 为值（adapter 拥有）
        std::unordered_map<const ColliderComponent*, std::shared_ptr<IPhysicsBody>> m_Bodies;
        // 受管组件集合（跨 ReleaseWorld 存活；RebuildAll 的重建依据）
        std::vector<const ColliderComponent*> m_Tracked;

        // F2-B4 集成：被观察 GameObject 及其监听器 ID
        struct GameObjectObservation {
            GameObject* go = nullptr;
            uint32 listenerId = 0;
        };
        std::vector<GameObjectObservation> m_Observations;
    };

} // namespace Engine
