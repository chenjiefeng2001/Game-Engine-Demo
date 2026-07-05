#pragma once

/**
 * @file Bare2DPhysicsBody.h
 * @brief 纯自制 2D 刚体 — 支持多 Fixture、力发生器、Sleeping
 */

#include "Engine/Core/Physics/IPhysicsBody.h"
#include "Engine/Core/Physics/PhysicsDefs.h"
#include <vector>
#include <algorithm>

namespace Engine {

    struct BarePolygon {
        std::vector<Vec2> vertices;   // 局部坐标系顶点（相对于质心）
        std::vector<Vec2> normals;    // 单位边法线（向外）
        int32 vertexCount = 0;

        void Set(const Vec2* verts, int32 count);
        void BuildNormals();

        // 计算多边形在轴上的投影
        void ProjectOntoAxis(const Vec2& axis, const Vec2& pos, float32 angle,
                             float32& outMin, float32& outMax) const;
    };

    /// 内部 Fixture 表示
    struct InternalFixture {
        ShapeDef         shape;
        BarePolygon      polygon;    // 仅 polygon 类型有效
        PhysicsMaterial  material;
        bool             isSensor = false;

        // 碰撞滤波
        uint16 categoryBits = 0x0001;
        uint16 maskBits     = 0xFFFF;
        int32  groupIndex   = 0;
        void*  userData     = nullptr;

        // AABB（世界空间，每帧更新）
        Vec2 aabbMin = {0.0f, 0.0f};
        Vec2 aabbMax = {0.0f, 0.0f};
    };

    class Bare2DPhysicsBody final : public IPhysicsBody {
    public:
        explicit Bare2DPhysicsBody(const BodyDef& def);
        ~Bare2DPhysicsBody() override = default;

        // ── IPhysicsBody ──
        void  SetTransform(const Vec2& position, float32 angle) override;
        Vec2  GetPosition() const override { return m_Position; }
        float32 GetAngle() const override { return m_Angle; }

        void  SetLinearVelocity(const Vec2& velocity) override { m_LinearVelocity = velocity; }
        Vec2  GetLinearVelocity() const override { return m_LinearVelocity; }
        void  SetAngularVelocity(float32 omega) override { m_AngularVelocity = omega; }
        float32 GetAngularVelocity() const override { return m_AngularVelocity; }

        void ApplyForce(const Vec2& force, const Vec2& point) override;
        void ApplyForceToCenter(const Vec2& force) override;
        void ApplyLinearImpulse(const Vec2& impulse, const Vec2& point) override;

        void  SetType(BodyType type) override;
        BodyType GetType() const override { return m_Type; }
        float32  GetMass() const override { return m_Mass; }
        float32  GetInertia() const override { return m_Inertia; }

        void  SetLinearDamping(float32 damping) override { m_LinearDamping = damping; }
        float32 GetLinearDamping() const override { return m_LinearDamping; }
        void  SetAngularDamping(float32 damping) override { m_AngularDamping = damping; }
        float32 GetAngularDamping() const override { return m_AngularDamping; }

        void* AddFixture(const FixtureDef& def) override;
        void  RemoveFixture(void* fixtureId) override;
        void  ClearFixtures() override;

        void  SetComponentRef(void* ref) override { m_ComponentRef = ref; }
        void* GetComponentRef() const override { return m_ComponentRef; }

        void SetFilterData(uint16 categoryBits, uint16 maskBits) override;
        void SetGroupIndex(int32 groupIndex) override;

        void  SetActive(bool active) override { m_Active = active; }
        bool  IsActive() const override { return m_Active; }

        void* GetUserData() const override { return m_UserData; }
        void  SetUserData(void* data) override { m_UserData = data; }

        void SetAwake(bool awake) override;
        bool IsAwake() const override { return m_Awake && m_Type == BodyType::Dynamic; }

        void* GetNativeBody() override { return nullptr; }

        // ── 内部接口（供 Bare2DPhysicsWorld 使用）──
        Vec2     GetForceAccum() const { return m_ForceAccum; }
        float32  GetTorqueAccum() const { return m_TorqueAccum; }
        void     ClearForceAccum() { m_ForceAccum = Vec2(0,0); m_TorqueAccum = 0; }
        void     AddTorque(float32 torque) { m_TorqueAccum += torque; }

        // Fixture 访问
        int32  GetFixtureCount() const { return static_cast<int32>(m_Fixtures.size()); }
        InternalFixture& GetFixture(int32 index) { return m_Fixtures[index]; }
        const InternalFixture& GetFixture(int32 index) const { return m_Fixtures[index]; }

        // 形状快捷访问（兼容旧 API）
        const ShapeDef& GetShape() const { return m_Fixtures.empty() ? m_DefaultShape : m_Fixtures[0].shape; }
        float32 GetCircleRadius() const {
            if (m_Fixtures.empty()) return m_DefaultShape.circleRadius;
            return m_Fixtures[0].shape.circleRadius;
        }
        const Vec2& GetBoxHalfSize() const {
            static Vec2 defaultBox(0.5f, 0.5f);
            if (m_Fixtures.empty()) return defaultBox;
            return m_Fixtures[0].shape.boxSize;
        }

        const BarePolygon& GetPolygon() const {
            static BarePolygon empty;
            if (m_Fixtures.empty()) return empty;
            return m_Fixtures[0].polygon;
        }
        BarePolygon& GetPolygon() {
            static BarePolygon empty;
            if (m_Fixtures.empty()) return empty;
            return m_Fixtures[0].polygon;
        }

        float32 GetRestitution() const {
            return m_Material.restitution;
        }
        float32 GetFriction() const {
            return m_Material.friction;
        }

        float32 GetInvMass() const { return m_InvMass; }
        float32 GetInvInertia() const { return m_InvInertia; }
        bool    IsFixedRotation() const { return m_FixedRotation; }

        // ── CCD 相关 ──
        Vec2    GetPrevPosition() const { return m_PrevPosition; }
        float32 GetPrevAngle() const { return m_PrevAngle; }
        void    SavePrevState() { m_PrevPosition = m_Position; m_PrevAngle = m_Angle; }

        // ── 滤波 ──
        uint16 GetCategoryBits() const { return m_CategoryBits; }
        uint16 GetMaskBits()     const { return m_MaskBits; }
        int32  GetGroupIndex()   const { return m_GroupIndex; }

        // ── 插值 ──
        void SetInterpState(Vec2 prevPos, float32 prevAngle) {
            m_PrevPosition = prevPos; m_PrevAngle = prevAngle;
        }
        Vec2  GetInterpPosition(float32 alpha) const {
            return m_PrevPosition + (m_Position - m_PrevPosition) * alpha;
        }
        float32 GetInterpAngle(float32 alpha) const {
            return m_PrevAngle + (m_Angle - m_PrevAngle) * alpha;
        }

        // ── 休眠相关 ──
        float32 GetSleepTimer() const { return m_SleepTimer; }
        void    SetSleepTimer(float32 t) { m_SleepTimer = t; }
        bool    IsBullet() const { return m_IsBullet; }

        // ── 休眠阈值（公开供其他模块使用） ──
        static constexpr float32 SLEEP_TIME_THRESHOLD = 0.5f;  // 秒
        static constexpr float32 SLEEP_SPEED_THRESHOLD = 0.1f; // m/s

        // ── 材料 ──
        const PhysicsMaterial& GetMaterial() const { return m_Material; }
        void SetMaterial(const PhysicsMaterial& mat) { m_Material = mat; }

        // 更新 AABB
        void UpdateAABB();

    private:
        void RecalculateMass();
        void ComputeAABBForFixture(InternalFixture& fix);

        // 物理状态
        Vec2     m_Position        = Vec2(0,0);
        float32  m_Angle           = 0.0f;
        Vec2     m_LinearVelocity  = Vec2(0,0);
        float32  m_AngularVelocity = 0.0f;

        // 力累加器
        Vec2     m_ForceAccum   = Vec2(0,0);
        float32  m_TorqueAccum  = 0.0f;

        // 质量
        float32  m_Mass         = 1.0f;
        float32  m_InvMass      = 1.0f;
        float32  m_Inertia      = 1.0f;
        float32  m_InvInertia   = 1.0f;

        // 材料
        PhysicsMaterial m_Material;

        // 阻尼
        float32  m_LinearDamping  = 0.0f;
        float32  m_AngularDamping = 0.0f;

        // 类型
        BodyType m_Type          = BodyType::Dynamic;
        bool     m_FixedRotation = false;
        bool     m_AllowSleep    = true;
        bool     m_Awake         = true;
        bool     m_Active        = true;
        bool     m_IsBullet      = false;

        // 休眠计时器
        float32  m_SleepTimer    = 0.0f;

        // 滤波
        uint16 m_CategoryBits = 0x0001;
        uint16 m_MaskBits     = 0xFFFF;
        int32  m_GroupIndex   = 0;

        // Fixtures
        std::vector<InternalFixture> m_Fixtures;
        ShapeDef m_DefaultShape; // 当没有 Fixture 时的 fallback

        // 用户数据
        void*  m_UserData     = nullptr;
        void*  m_ComponentRef = nullptr;

        // 前一帧状态（用于插值）
        Vec2    m_PrevPosition = Vec2(0,0);
        float32 m_PrevAngle    = 0.0f;
    };

} // namespace Engine