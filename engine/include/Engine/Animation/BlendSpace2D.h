#pragma once

/**
 * @file BlendSpace2D.h
 * @brief 2D 混合空间 — 在二维参数平面上对多个动画片段做三角化混合
 *
 * 2D BlendSpace（二维混合空间）是 1D 混合空间的泛化，允许在两个维度上
 * 同时混合动画。典型应用：
 *   - 速度 + 方向（如 locomotion 2D blend）
 *   - 仰角 + 方位角（如瞄准空间）
 *   - 移动速度 + 转向速度
 *
 * ════════════════════════════════════════════
 * 混合策略
 * ════════════════════════════════════════════
 *
 *   1. 采样点散布在二维平面上（每个点关联一个动画片段）
 *   2. 给定输入点 P = (x, y)，找到包含 P 的三角形
 *   3. 计算 P 在三角形中的重心坐标 (w0, w1, w2)
 *   4. 以此作为权重混合三个顶点的动画：
 *      Translation → LERP（三路加权）
 *      Rotation    → SLERP（三路加权）
 *      Scale       → LERP（三路加权）
 *
 * ════════════════════════════════════════════
 * 边界处理
 * ════════════════════════════════════════════
 *
 *   如果 P 不在任何三角形内部（位于凸包外）：
 *     1. 找到凸包上离 P 最近的边
 *     2. 将 P 投影到该边上做 1D 回退混合
 *
 * ════════════════════════════════════════════
 * 使用示例
 * ════════════════════════════════════════════
 *
 *   BlendSpace2D blendSpace2D;
 *
 *   // 添加采样点（速度, 角度, 动画片段）
 *   blendSpace2D.AddSample(Vec2(0,   0),   idleClip);
 *   blendSpace2D.AddSample(Vec2(3,  -45),  walkFwdLeftClip);
 *   blendSpace2D.AddSample(Vec2(3,   0),   walkFwdClip);
 *   blendSpace2D.AddSample(Vec2(3,  45),   walkFwdRightClip);
 *   blendSpace2D.AddSample(Vec2(6,   0),   runFwdClip);
 *   blendSpace2D.AddSample(Vec2(6,  45),   runFwdRightClip);
 *
 *   // 每帧采样
 *   Vec2 input(speed, directionAngle);
 *   blendSpace2D.Sample(skeleton, poseEvaluator, input, deltaTime);
 *
 *   // 上传蒙皮矩阵到 GPU（通过渲染器接口）
 */

#include "Engine/Animation/AnimationLocalTimeline.h"
#include "Engine/Animation/Skeleton.h"
#include "Engine/Animation/AnimationPose.h"
#include "Engine/Animation/AnimationBlend.h"
#include <vector>
#include <memory>
#include <algorithm>
#include <array>

namespace Engine {

    // ============================================================
    // 2D 混合采样点
    // ============================================================
    struct BlendSpace2DSample {
        Vec2    parameterValue = Vec2(0.0f, 0.0f);          ///< 2D 参数坐标
        std::shared_ptr<AnimationLocalTimeline> clip;       ///< 对应动画片段

        BlendSpace2DSample() = default;
        BlendSpace2DSample(const Vec2& pv, std::shared_ptr<AnimationLocalTimeline> c)
            : parameterValue(pv), clip(std::move(c)) {}
    };

    // ============================================================
    // 三角形（三个采样点索引）
    // ============================================================
    struct BlendTriangle {
        int32 i0 = -1;  ///< 顶点 0 在采样点列表中的索引
        int32 i1 = -1;  ///< 顶点 1 在采样点列表中的索引
        int32 i2 = -1;  ///< 顶点 2 在采样点列表中的索引

        BlendTriangle() = default;
        BlendTriangle(int32 a, int32 b, int32 c) : i0(a), i1(b), i2(c) {}
    };

    // ============================================================
    // 2D 混合空间
    // ============================================================
    class BlendSpace2D {
    public:
        BlendSpace2D() = default;
        ~BlendSpace2D() = default;

        // 禁止拷贝
        BlendSpace2D(const BlendSpace2D&) = delete;
        BlendSpace2D& operator=(const BlendSpace2D&) = delete;

        // ── 采样点管理 ──

        /**
         * @brief 添加一个 2D 采样点
         * @param paramValue 二维参数坐标 (x, y)
         * @param clip       对应的动画片段
         *
         * 添加后需要调用 RebuildTriangulation() 重建三角形。
         * 如果已存在相同坐标的采样点，会被替换。
         */
        void AddSample(const Vec2& paramValue,
                       std::shared_ptr<AnimationLocalTimeline> clip);

        /**
         * @brief 移除指定索引的采样点
         */
        void RemoveSample(int32 index);

        /** 清空所有采样点 */
        void Clear();

        /** 获取采样点数量 */
        size_t GetSampleCount() const noexcept { return m_Samples.size(); }

        /** 获取指定索引的采样点 */
        const BlendSpace2DSample& GetSample(int32 index) const;
        BlendSpace2DSample& GetSample(int32 index);

        // ── 三角剖分 ──

        /**
         * @brief 重建三角剖分
         *
         * 在添加/删除采样点后调用。
         * 使用 Delaunay 风格的三角形生成（对每个不共线的三点组构造成三角形，
         * 并检查点是否在内）。
         *
         * @note 当采样点少于 3 个时，退化为 1D 或单点混合。
         */
        void RebuildTriangulation();

        /** 获取三角形数量 */
        size_t GetTriangleCount() const noexcept { return m_Triangles.size(); }

        /** 获取指定索引的三角形 */
        const BlendTriangle& GetTriangle(int32 index) const;

        // ── 核心采样 ──

        /**
         * @brief 在给定参数点处采样 2D 混合空间
         * @param skeleton   输出骨骼（局部姿势矩阵和蒙皮矩阵会被更新）
         * @param poseEval   姿势求值器
         * @param paramValue 当前参数点 (x, y)
         * @param time       动画采样时间点
         *
         * 采样流程：
         *   1. 在所有三角形中查找包含 paramValue 的那个
         *   2. 计算重心坐标作为混合权重
         *   3. 对三个顶点做带权重的姿势混合
         *   4. 更新 skeleton 的世界矩阵和蒙皮矩阵
         *
         * 边界情况：
         *   - 0 个采样点 → 不操作
         *   - 1 个采样点 → 纯态
         *   - 2 个采样点 → 退化为 1D 混合（在两点连线上投影）
         *   - 3+ 个采样点 → 三角化混合
         *   - 点在外围 → 投影到最近边做 1D 混合
         */
        void Sample(Skeleton& skeleton,
                    AnimationPose& poseEval,
                    const Vec2& paramValue,
                    float32 time);

    private:
        // ── 几何工具 ──

        /** 计算二维叉积（z 分量），用于判断方向 */
        static float32 Cross2D(const Vec2& a, const Vec2& b);

        /** 判断点 P 是否在三角形 (A, B, C) 内部 */
        static bool PointInTriangle(const Vec2& p, const Vec2& a,
                                     const Vec2& b, const Vec2& c);

        /**
         * @brief 计算重心坐标
         * @param p  查询点
         * @param a  三角形顶点 0
         * @param b  三角形顶点 1
         * @param c  三角形顶点 2
         * @param w0 输出：顶点 a 的权重
         * @param w1 输出：顶点 b 的权重
         * @param w2 输出：顶点 c 的权重
         * @return 点是否在三角形内部（或边上）
         */
        static bool Barycentric(const Vec2& p, const Vec2& a,
                                 const Vec2& b, const Vec2& c,
                                 float32& w0, float32& w1, float32& w2);

        /**
         * @brief 计算点 P 到线段 AB 的最短距离和投影参数
         * @return 投影参数 t：[0,1] 内表示在 A 到 B 之间
         */
        static float32 ProjectOnEdge(const Vec2& p, const Vec2& a,
                                      const Vec2& b, Vec2& outClosest);

        /**
         * @brief 当参数点不在任何三角形内时，找最近边做 1D 回退
         * @return (idxA, idxB, t) — 边上的两个采样点索引和投影参数
         */
        bool FindFallbackEdge(const Vec2& paramValue,
                              int32& outIdxA, int32& outIdxB,
                              float32& outT) const;

        // ── 三路姿势混合（内部） ──

        /**
         * @brief 将三个采样点的动画混合到 skeleton
         * @param weights 三个权重（需已归一化，和为 1.0）
         */
        void BlendThreePoses(Skeleton& skeleton,
                             AnimationPose& poseEval,
                             const BlendSpace2DSample& s0,
                             const BlendSpace2DSample& s1,
                             const BlendSpace2DSample& s2,
                             const std::array<float32, 3>& weights,
                             float32 time) const;

        // ── 数据 ──
        std::vector<BlendSpace2DSample> m_Samples;
        std::vector<BlendTriangle>      m_Triangles;  ///< 缓存的三角剖分
        bool m_TriangulationDirty = false;             ///< 是否需要重建三角形
    };

} // namespace Engine
