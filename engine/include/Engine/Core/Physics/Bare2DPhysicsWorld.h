#pragma once

/**
 * @file Bare2DPhysicsWorld.h
 * @brief 纯自制 2D 物理世界 — 完整物理管线（支持多线程）
 *
 * 功能特性：
 *   - 固定/可变步长物理步进
 *   - Force Generators 框架（重力、阻力等）
 *   - 空间网格 + SAT 碰撞检测
 *   - Contact Manifold + Warm Starting
 *   - Sequential Impulse 约束求解器
 *   - Baumgarte 位置修正
 *   - 关节系统（距离、旋转、弹簧等）
 *   - Sleeping 休眠机制
 *   - CCD 连续碰撞检测
 *   - Shapecasting / Trigger / Layer filtering
 *   - 调试绘制
 *   - 多线程 Job 系统集成（ParallelFor 加速）
 */

#include "Engine/Core/Physics/IPhysicsWorld.h"
#include "Engine/Core/Physics/Bare2DPhysicsBody.h"
#include <memory>
#include <vector>
#include <map>
#include <array>
#include <utility>
#include <functional>

namespace Engine {

    // ============================================================
    // 前向声明
    // ============================================================
    class JobSystem;

    // ============================================================
    // 内部数据结构
    // ============================================================

    /// 碰撞对（宽阶段 + 窄阶段）
    struct ContactPair {
        Bare2DPhysicsBody* bodyA         = nullptr;
        Bare2DPhysicsBody* bodyB         = nullptr;
        Vec2               contactPoint  = {0.0f, 0.0f};
        Vec2               normal        = {0.0f, 0.0f};
        float32            penetration   = 0.0f;
        float32            normalImpulse = 0.0f;
        float32            tangentImpulse = 0.0f;
        bool               wasColliding  = false;
        bool               isSensor      = false;
    };

    /// 碰撞对键
    struct ContactPairKey {
        Bare2DPhysicsBody* a;
        Bare2DPhysicsBody* b;

        bool operator<(const ContactPairKey& o) const {
            if (a != o.a) return a < o.a;
            return b < o.b;
        }
    };

    using ContactMap = std::map<ContactPairKey, ContactPair>;

    /// 流形缓存（用于 Warm Starting）
    struct ManifoldCache {
        ContactManifold manifold;
    };

    /// 空间网格单元格
    struct GridCell {
        std::vector<Bare2DPhysicsBody*> bodies;
    };

    // ============================================================
    // 关节接口（内部实现，用于 Bare2DPhysicsWorld）
    // ============================================================
    class Bare2DJoint {
    public:
        virtual ~Bare2DJoint() = default;

        JointType type = JointType::Unknown;
        Bare2DPhysicsBody* bodyA = nullptr;
        Bare2DPhysicsBody* bodyB = nullptr;
        bool collideConnected = false;
        void* userData = nullptr;

        /// @brief 求解约束
        virtual void Solve(float32 dt) = 0;

        /// @brief 调试绘制
        virtual void DebugDraw(IPhysicsDebugDraw* draw) const = 0;
    };

    // ============================================================
    // Bare2DPhysicsWorld
    // ============================================================
    class Bare2DPhysicsWorld final : public IPhysicsWorld {
    public:
        static constexpr float32 FIXED_DT       = 1.0f / 60.0f;
        static constexpr int32   GRID_DIM       = 32;
        static constexpr float32 GRID_CELL_SIZE = 10.0f;

        explicit Bare2DPhysicsWorld(const Vec2& gravity);
        explicit Bare2DPhysicsWorld(const PhysicsWorldConfig& config);
        ~Bare2DPhysicsWorld() override;

        // ── IPhysicsWorld ──
        void Step(float32 dt,
                  int32 velocityIterations = 8,
                  int32 positionIterations = 3) override;

        std::shared_ptr<IPhysicsBody> CreateBody(const BodyDef& def) override;
        void DestroyBody(IPhysicsBody* body) override;

        std::shared_ptr<IJoint> CreateJoint(const JointDef& def) override;
        void DestroyJoint(IJoint* joint) override;

        std::vector<RayCastResult> RayCast(const Vec2& from, const Vec2& to) override;
        std::vector<IPhysicsBody*> QueryAABB(const Vec2& center, const Vec2& halfSize) override;

        void SetGravity(const Vec2& gravity) override { m_Gravity = gravity; }
        Vec2 GetGravity() const override { return m_Gravity; }

        void SetContactBeginCallback(ContactCallback callback) override { m_ContactBeginCallback = std::move(callback); }
        void SetContactEndCallback(ContactCallback callback) override { m_ContactEndCallback = std::move(callback); }
        void SetContactPreSolveCallback(ContactFilterCallback callback) override { m_ContactPreSolveCallback = std::move(callback); }
        void SetContactPersistCallback(ContactPersistCallback callback) override { m_ContactPersistCallback = std::move(callback); }

        void SetDebugDraw(IPhysicsDebugDraw* draw) override { m_DebugDraw = draw; }
        void DebugDraw() override;
        void* GetNativeWorld() override { return nullptr; }

        // ── 力发生器管理 ──
        void AddForceGenerator(std::shared_ptr<IForceGenerator> generator) {
            m_ForceGenerators.push_back(std::move(generator));
        }

        void RemoveForceGenerator(IForceGenerator* gen) {
            auto it = std::remove_if(m_ForceGenerators.begin(), m_ForceGenerators.end(),
                [gen](const auto& ptr) { return ptr.get() == gen || !ptr->IsValid(); });
            m_ForceGenerators.erase(it, m_ForceGenerators.end());
        }

        void ClearForceGenerators() { m_ForceGenerators.clear(); }

        // ── 形状投射（Shapecast） ──
        std::vector<ShapeCastResult> ShapeCast(const Vec2& from, const Vec2& to,
                                                ShapeType type, const Vec2& size);

        // ── Trigger 查询 ──
        std::vector<IPhysicsBody*> GetTriggerOverlaps(IPhysicsBody* body);

        // ── 世界配置 ──
        void SetAllowSleeping(bool allow) { m_AllowSleep = allow; }
        bool GetAllowSleeping() const { return m_AllowSleep; }
        void SetCCDEnabled(bool enable) { m_EnableCCD = enable; }
        bool GetCCDEnabled() const { return m_EnableCCD; }

        // ── 多线程配置 ──
        /**
         * @brief 设置 Job 系统（启用多线程物理模拟）
         * @param js JobSystem 实例指针；传入 nullptr 则退化为单线程
         *
         * 启用后，IntegrateForces / IntegrateVelocity / UpdateAABB / UpdateSleeping
         * 等阶段将通过 JobSystem::ParallelFor 并行执行。
         * 碰撞检测和约束求解仍保持单线程（Sequential Impulses 的迭代特性使然）。
         */
        void SetJobSystem(JobSystem* js) { m_JobSystem = js; }

        /// 启用/禁用多线程（需要先通过 SetJobSystem 设置 JobSystem）
        void SetMultithreadingEnabled(bool enabled) { m_UseMultithreading = enabled; }
        bool IsMultithreadingEnabled() const { return m_UseMultithreading; }

        // ── 内部步进管线 ──
        void FixedStep(float32 dt, int32 velocityIterations, int32 positionIterations);

        // ── 同步方法 ──
        static void SyncTransformToBody(IPhysicsBody* body, const Vec2& position, float32 angle);
        static void SyncBodyToTransform(const IPhysicsBody* body, Vec2& outPosition, float32& outAngle);
        Vec2  InterpolatePosition(const IPhysicsBody* body, float32 alpha);
        float32 InterpolateAngle(const IPhysicsBody* body, float32 alpha);

    private:
        // ── 多线程辅助（每体操作，供 ParallelFor 调用） ──
        /**
         * @brief 对单个刚体执行力发生器施加 + 速度积分
         * @note 线程安全：不同 body 实例之间无数据竞争
         */
        void IntegrateBodyForces(Bare2DPhysicsBody* body, float32 dt);

        /**
         * @brief 对单个刚体执行位置积分
         * @note 线程安全：不同 body 实例之间无数据竞争
         */
        void IntegrateBodyVelocity(Bare2DPhysicsBody* body, float32 dt);

        /**
         * @brief 更新单个刚体的所有 Fixture AABB
         * @note 线程安全：不同 body 实例之间无数据竞争
         */
        void UpdateBodyAABB(Bare2DPhysicsBody* body);

        /**
         * @brief 更新单个刚体的休眠状态
         * @note 线程安全：不同 body 实例之间无数据竞争
         */
        void UpdateBodySleeping(Bare2DPhysicsBody* body, float32 dt);

        // ── 空间网格 ──
        int32 WorldToGrid(float32 coord);
        void  ClearGrid();
        void  InsertIntoGrid(Bare2DPhysicsBody* body);
        void  GetPotentialPairs(std::vector<std::pair<Bare2DPhysicsBody*, Bare2DPhysicsBody*>>& pairs);
        void  DetectCollisions(std::vector<ContactPair>& contacts);

        // ── Fixture 级碰撞检测 ──
        bool TestFixtureCollision(const InternalFixture& fixA, Bare2DPhysicsBody* bodyA,
                                   const InternalFixture& fixB, Bare2DPhysicsBody* bodyB,
                                   ContactPair& outPair);
        bool TestCircleVsCircle(const InternalFixture& fixA, Bare2DPhysicsBody* bodyA,
                                 const InternalFixture& fixB, Bare2DPhysicsBody* bodyB,
                                 ContactPair& outPair);
        bool TestCircleVsAABB(const InternalFixture& fixCircle, Bare2DPhysicsBody* bodyCircle,
                               const InternalFixture& fixBox, Bare2DPhysicsBody* bodyBox,
                               ContactPair& outPair);
        bool TestCircleVsPolygon(const InternalFixture& fixCircle, Bare2DPhysicsBody* bodyCircle,
                                  const InternalFixture& fixPoly, Bare2DPhysicsBody* bodyPoly,
                                  ContactPair& outPair);
        bool TestAABBVsAABB(const InternalFixture& fixA, Bare2DPhysicsBody* bodyA,
                             const InternalFixture& fixB, Bare2DPhysicsBody* bodyB,
                             ContactPair& outPair);
        bool TestAABBVsPolygon(const InternalFixture& fixBox, Bare2DPhysicsBody* bodyBox,
                                const InternalFixture& fixPoly, Bare2DPhysicsBody* bodyPoly,
                                ContactPair& outPair);
        bool TestPolygonVsPolygon(const InternalFixture& fixA, Bare2DPhysicsBody* bodyA,
                                   const InternalFixture& fixB, Bare2DPhysicsBody* bodyB,
                                   ContactPair& outPair);
        float32 ProjectPolygonOnAxis(const BarePolygon& poly, const Vec2& axis,
                                      const Vec2& pos, float32 angle,
                                      float32& outMin, float32& outMax);
        bool SATOverlap(const BarePolygon& polyA, Bare2DPhysicsBody* bodyA,
                         const BarePolygon& polyB, Bare2DPhysicsBody* bodyB,
                         ContactPair& outPair);

        // ── 碰撞响应 ──
        void ResolveCollisions(std::vector<ContactPair>& contacts, float32 dt);
        void ResolveContact(Bare2DPhysicsBody* a, Bare2DPhysicsBody* b,
                             const Vec2& contactPoint, const Vec2& normal,
                             float32 penetration, float32& normalImpulse,
                             float32& tangentImpulse, float32 dt);
        void DispatchCallbacks(const std::vector<ContactPair>& newContacts, float32 dt);

        // ── 约束求解器 ──
        void SolveConstraints(std::vector<ContactPair>& contacts, float32 dt, int32 iterations);
        void WarmStartContacts(std::vector<ContactPair>& contacts);

        // ── CCD ──
        void ComputeCCD(float32 dt);
        bool RayVsAABB(const Vec2& from, const Vec2& to,
                       const Vec2& aabbMin, const Vec2& aabbMax,
                       float32& outT);

        // ── 积分 ──
        void IntegrateForces(float32 dt);
        void IntegrateVelocity(float32 dt);

        // ── 休眠 ──
        void UpdateSleeping(float32 dt);

        // ── 成员 ──
        std::vector<std::shared_ptr<Bare2DPhysicsBody>> m_Bodies;
        std::vector<std::unique_ptr<Bare2DJoint>> m_Joints;
        std::vector<std::shared_ptr<IForceGenerator>> m_ForceGenerators;

        std::array<std::array<GridCell, GRID_DIM>, GRID_DIM> m_Grid;
        Vec2    m_Gravity = {0.0f, -9.8f};
        float32 m_Accumulator = 0.0f;

        // 配置
        int32  m_VelocityIterations = 8;
        int32  m_PositionIterations = 3;
        int32  m_SolverIterations   = 10;
        bool   m_UseFixedDt = true;
        bool   m_AllowSleep = true;
        bool   m_EnableCCD  = false;

        // 多线程
        JobSystem* m_JobSystem           = nullptr;
        bool       m_UseMultithreading   = false;

        // 调试绘制
        IPhysicsDebugDraw* m_DebugDraw = nullptr;

        // 碰撞回调
        ContactCallback        m_ContactBeginCallback;
        ContactCallback        m_ContactEndCallback;
        ContactFilterCallback  m_ContactPreSolveCallback;
        ContactPersistCallback m_ContactPersistCallback;

        // 活跃碰撞映射（用于检测 Enter / Exit / Persist + Warm Starting）
        ContactMap m_ActiveContacts;

        // 流形缓存（按帧缓存 ContactManifold 用于 Warm Starting）
        std::map<ContactPairKey, ContactManifold> m_ManifoldCache;

        // 前一帧活跃对（用于 Exit 检测）
        ContactMap m_PrevActiveContacts;
    };

} // namespace Engine