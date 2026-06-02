#pragma once

/**
 * @file ConstraintTarget.h
 * @brief 约束目标 — 定义约束引用的目标（骨骼/定位器/世界空间）
 *
 * 约束目标是约束系统的核心数据，它指明了约束作用于哪个目标：
 *
 *   Locator   — 引用一个已命名的定位器（用于跨物体对接）
 *   Bone      — 引用同一骨骼上的另一个骨骼
 *   WorldSpace— 固定的世界空间变换
 *
 * 每个目标还可以包含偏移量、权重和逐轴控制。
 */

#include "Engine/Core/RHI/MathTypes.h"
#include "Engine/Types.h"
#include <string>
#include <vector>

namespace Engine {

    // ============================================================
    // 约束目标
    // ============================================================
    struct ConstraintTarget {
        enum class Type : uint8 {
            Locator,    ///< 引用命名定位器（跨物体或同物体）
            Bone,       ///< 引用骨骼索引
            WorldSpace  ///< 固定世界空间变换
        };

        Type type = Type::WorldSpace;

        // ── Locator 模式 ──
        std::string locatorName;    ///< 定位器名称（type == Locator 时生效）

        // ── Bone 模式 ──
        int32 boneIndex = -1;       ///< 骨骼索引（type == Bone 时生效）

        // ── 偏移量（对所有模式生效） ──
        Vec3 positionOffset{0, 0, 0};
        Quat rotationOffset{0, 0, 0, 1};
        Vec3 scaleOffset{1, 1, 1};

        // ── 混合权重 ──
        float32 weight = 1.0f;

        // ── 逐轴控制（Parent 约束使用） ──
        bool constrainX = true;
        bool constrainY = true;
        bool constrainZ = true;

        ConstraintTarget() = default;

        /** 创建定位器类型目标 */
        static ConstraintTarget FromLocator(const std::string& name, float32 w = 1.0f) {
            ConstraintTarget t;
            t.type = Type::Locator;
            t.locatorName = name;
            t.weight = w;
            return t;
        }

        /** 创建骨骼类型目标 */
        static ConstraintTarget FromBone(int32 bone, float32 w = 1.0f) {
            ConstraintTarget t;
            t.type = Type::Bone;
            t.boneIndex = bone;
            t.weight = w;
            return t;
        }

        static ConstraintTarget FromBoneName(const std::string& boneName, float32 w = 1.0f) {
            ConstraintTarget t;
            t.type = Type::Bone;
            // boneIndex 会在约束求解时通过骨骼名称查找设置
            t.locatorName = boneName; // 暂存名称，求解时解析
            t.weight = w;
            return t;
        }

        /** 创建世界空间类型目标 */
        static ConstraintTarget FromWorld(const Vec3& pos, const Quat& rot = Quat::Identity()) {
            ConstraintTarget t;
            t.type = Type::WorldSpace;
            t.positionOffset = pos;
            t.rotationOffset = rot;
            return t;
        }
    };

} // namespace Engine
