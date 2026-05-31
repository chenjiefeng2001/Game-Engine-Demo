#pragma once

/**
 * @file IPhysicsWorld3D.h
 * @brief 3D 物理世界接口 — 不依赖任何第三方物理库
 *
 * Jolt/PhysX/Bullet 各自实现此接口。
 * 设计原则与 2D IPhysicsWorld 一致。
 */

#include "Engine/Core/Physics/PhysicsDefs3D.h"
#include "Engine/Core/Physics/IPhysicsBody3D.h"
#include "Engine/Core/Physics/IJoint3D.h"
#include <memory>
#include <vector>

namespace Engine {

    class IJoint3D;
    class IPhysicsDebugDraw3D;

    class IPhysicsWorld3D {
    public:
        virtual ~IPhysicsWorld3D() = default;

        // ── 初始化 ──
        /**
         * @brief 使用配置初始化物理世界
         * @param config 世界配置（重力、最大物体数等）
         * @return 是否成功
         */
        virtual bool Init(const PhysicsWorldConfig3D& config) = 0;

        /**
         * @brief 关闭物理世界，释放所有资源
         */
        virtual void Shutdown() = 0;

        // ── 步进 ──
        /**
         * @brief 模拟物理世界
         * @param dt 时间步长（秒），建议固定为 1/60
         * @param collisionSteps 子步数（越大越精确，1=默认）
         */
        virtual void Step(float32 dt, int32 collisionSteps = 1) = 0;

        // ── 刚体管理 ──
        virtual std::shared_ptr<IPhysicsBody3D> CreateBody(const BodyDef3D& def) = 0;
        virtual void DestroyBody(IPhysicsBody3D* body) = 0;

        // ── 关节管理 ──
        virtual std::shared_ptr<IJoint3D> CreateJoint(const JointDef3D& def) = 0;
        virtual void DestroyJoint(IJoint3D* joint) = 0;

        // ── 查询 ──
        /**
         * @brief 光线投射
         * @param from 起点（世界空间）
         * @param to   终点（世界空间）
         * @return 按距离排序的所有被击中物体
         */
        virtual std::vector<RayCastResult3D> RayCast(const Vec3& from,
                                                       const Vec3& to) = 0;

        /**
         * @brief 范围查询（AABB）
         * @param center   查询中心
         * @param halfSize 半长宽高
         * @return 包围盒与查询区域重叠的所有物体
         */
        virtual std::vector<IPhysicsBody3D*> QueryAABB(const Vec3& center,
                                                         const Vec3& halfSize) = 0;

        /**
         * @brief 球体重叠查询
         * @param center 球心
         * @param radius 半径
         * @return 与球体重叠的所有物体
         */
        virtual std::vector<IPhysicsBody3D*> QuerySphere(const Vec3& center,
                                                           float32 radius) = 0;

        // ── 重力 ──
        virtual void  SetGravity(const Vec3& gravity) = 0;
        virtual Vec3  GetGravity() const = 0;

        // ── 碰撞事件 ──
        virtual void SetContactBeginCallback(ContactCallback3D callback) = 0;
        virtual void SetContactEndCallback(ContactCallback3D callback) = 0;
        virtual void SetContactPreSolveCallback(ContactFilterCallback3D callback) = 0;
        virtual void SetContactPersistCallback(ContactPersistCallback3D callback) = 0;

        // ── 调试绘制 ──
        virtual void SetDebugDraw(IPhysicsDebugDraw3D* draw) = 0;
        virtual void DebugDraw() = 0;

        // ── 性能统计 ──
        struct Stats {
            int32 activeBodyCount = 0;
            int32 contactCount = 0;
            int32 constraintCount = 0;
            float32 stepTimeMs = 0.0f;
        };
        virtual Stats GetStats() const = 0;

        // ── 底层访问 ──
        virtual void* GetNativeWorld() = 0;
    };

} // namespace Engine
