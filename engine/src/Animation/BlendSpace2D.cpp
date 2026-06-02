/**
 * @file BlendSpace2D.cpp
 * @brief 2D 混合空间实现 — 三角剖分 + 重心坐标混合
 */

#include "Engine/Animation/BlendSpace2D.h"
#include <cmath>
#include <limits>
#include <algorithm>

namespace Engine {

    // ════════════════════════════════════════════
    // 几何工具
    // ════════════════════════════════════════════

    float32 BlendSpace2D::Cross2D(const Vec2& a, const Vec2& b) {
        return a.x * b.y - a.y * b.x;
    }

    bool BlendSpace2D::PointInTriangle(const Vec2& p, const Vec2& a,
                                        const Vec2& b, const Vec2& c) {
        // 使用重心坐标法判断
        float32 w0, w1, w2;
        return Barycentric(p, a, b, c, w0, w1, w2);
    }

    bool BlendSpace2D::Barycentric(const Vec2& p, const Vec2& a,
                                    const Vec2& b, const Vec2& c,
                                    float32& w0, float32& w1, float32& w2) {
        // 计算向量
        Vec2 v0 = {c.x - a.x, c.y - a.y};   // AC
        Vec2 v1 = {b.x - a.x, b.y - a.y};   // AB
        Vec2 v2 = {p.x - a.x, p.y - a.y};   // AP

        // 点积和叉积
        float32 dot00 = v0.x * v0.x + v0.y * v0.y;
        float32 dot01 = v0.x * v1.x + v0.y * v1.y;
        float32 dot02 = v0.x * v2.x + v0.y * v2.y;
        float32 dot11 = v1.x * v1.x + v1.y * v1.y;
        float32 dot12 = v1.x * v2.x + v1.y * v2.y;

        float32 invDenom = 1.0f / (dot00 * dot11 - dot01 * dot01);
        if (!std::isfinite(invDenom)) {
            // 退化三角形（面积为零）
            w0 = w1 = w2 = 0.0f;
            return false;
        }

        w1 = (dot11 * dot02 - dot01 * dot12) * invDenom;
        w2 = (dot00 * dot12 - dot01 * dot02) * invDenom;
        w0 = 1.0f - w1 - w2;

        // 检查是否在三角形内部（允许微小容差）
        const float32 eps = 1e-6f;
        return (w0 >= -eps && w1 >= -eps && w2 >= -eps);
    }

    float32 BlendSpace2D::ProjectOnEdge(const Vec2& p, const Vec2& a,
                                         const Vec2& b, Vec2& outClosest) {
        Vec2 ab = {b.x - a.x, b.y - a.y};
        Vec2 ap = {p.x - a.x, p.y - a.y};

        float32 abLenSq = ab.x * ab.x + ab.y * ab.y;
        if (abLenSq < 1e-10f) {
            outClosest = a;
            return 0.0f;
        }

        float32 t = (ap.x * ab.x + ap.y * ab.y) / abLenSq;
        t = std::max(0.0f, std::min(1.0f, t));

        outClosest.x = a.x + t * ab.x;
        outClosest.y = a.y + t * ab.y;
        return t;
    }

    // ════════════════════════════════════════════
    // 采样点管理
    // ════════════════════════════════════════════

    void BlendSpace2D::AddSample(const Vec2& paramValue,
                                  std::shared_ptr<AnimationLocalTimeline> clip) {
        if (!clip) return;

        // 检查是否已存在相同坐标
        for (auto& sample : m_Samples) {
            float32 dx = sample.parameterValue.x - paramValue.x;
            float32 dy = sample.parameterValue.y - paramValue.y;
            if (std::abs(dx) < 1e-6f && std::abs(dy) < 1e-6f) {
                sample.clip = std::move(clip);
                m_TriangulationDirty = true;
                return;
            }
        }

        m_Samples.emplace_back(paramValue, std::move(clip));
        m_TriangulationDirty = true;
    }

    void BlendSpace2D::RemoveSample(int32 index) {
        if (index >= 0 && index < static_cast<int32>(m_Samples.size())) {
            m_Samples.erase(m_Samples.begin() + index);
            m_TriangulationDirty = true;
        }
    }

    void BlendSpace2D::Clear() {
        m_Samples.clear();
        m_Triangles.clear();
        m_TriangulationDirty = false;
    }

    const BlendSpace2DSample& BlendSpace2D::GetSample(int32 index) const {
        return m_Samples[index];
    }

    BlendSpace2DSample& BlendSpace2D::GetSample(int32 index) {
        return m_Samples[index];
    }

    const BlendTriangle& BlendSpace2D::GetTriangle(int32 index) const {
        return m_Triangles[index];
    }

    // ════════════════════════════════════════════
    // 三角剖分
    // ════════════════════════════════════════════

    void BlendSpace2D::RebuildTriangulation() {
        m_Triangles.clear();
        int32 n = static_cast<int32>(m_Samples.size());

        if (n < 3) {
            // 少于 3 个点不需要三角形
            m_TriangulationDirty = false;
            return;
        }

        // 对每三个不共线的采样点构造三角形
        // 使用简单枚举法（对游戏动画的采样点数量级足够，通常 < 20）
        for (int32 i = 0; i < n - 2; ++i) {
            for (int32 j = i + 1; j < n - 1; ++j) {
                for (int32 k = j + 1; k < n; ++k) {
                    const Vec2& a = m_Samples[i].parameterValue;
                    const Vec2& b = m_Samples[j].parameterValue;
                    const Vec2& c = m_Samples[k].parameterValue;

                    // 检查是否共线（面积为零则跳过）
                    Vec2 ab = {b.x - a.x, b.y - a.y};
                    Vec2 ac = {c.x - a.x, c.y - a.y};
                    float32 area = std::abs(Cross2D(ab, ac));
                    if (area < 1e-8f)
                        continue;

                    // 检查是否其他点落在此三角形内部
                    // 如果是，则这个三角形不是凸包的面，跳过
                    bool hasInternalPoint = false;
                    for (int32 m = 0; m < n; ++m) {
                        if (m == i || m == j || m == k) continue;
                        if (PointInTriangle(m_Samples[m].parameterValue, a, b, c)) {
                            hasInternalPoint = true;
                            break;
                        }
                    }

                    if (!hasInternalPoint) {
                        m_Triangles.emplace_back(i, j, k);
                    }
                }
            }
        }

        m_TriangulationDirty = false;
    }

    // ════════════════════════════════════════════
    // 查找回退边（当点在凸包外时）
    // ════════════════════════════════════════════

    bool BlendSpace2D::FindFallbackEdge(const Vec2& paramValue,
                                         int32& outIdxA, int32& outIdxB,
                                         float32& outT) const {
        int32 n = static_cast<int32>(m_Samples.size());
        if (n < 2) return false;

        float32 bestDist = std::numeric_limits<float32>::max();
        bool found = false;

        // 检查所有由三角形边组成的边界
        // 由于我们没有显式的凸包边列表，遍历所有采样点对
        for (int32 i = 0; i < n - 1; ++i) {
            for (int32 j = i + 1; j < n; ++j) {
                const Vec2& a = m_Samples[i].parameterValue;
                const Vec2& b = m_Samples[j].parameterValue;

                Vec2 closest;
                float32 t = ProjectOnEdge(paramValue, a, b, closest);

                // 计算到投影点的距离
                float32 dx = paramValue.x - closest.x;
                float32 dy = paramValue.y - closest.y;
                float32 dist = dx * dx + dy * dy;

                // 边缘 = 所有可能的边。但我们只选择最近的
                // 更好的做法是先构建凸包边，但这里简化为所有点对
                if (dist < bestDist) {
                    // 额外检查：这条边是否是凸包的边
                    // 简单检查：所有其他点是否在边的同一侧
                    bool isEdge = true;
                    Vec2 edgeDir = {b.x - a.x, b.y - a.y};
                    float32 side = 0.0f;
                    for (int32 k = 0; k < n; ++k) {
                        if (k == i || k == j) continue;
                        Vec2 ak = {m_Samples[k].parameterValue.x - a.x,
                                   m_Samples[k].parameterValue.y - a.y};
                        float32 cross = Cross2D(edgeDir, ak);
                        if (side == 0.0f) {
                            side = cross;
                        } else if (cross * side < 0.0f) {
                            // 点分布在边的两侧，不是凸包边
                            isEdge = false;
                            break;
                        }
                    }

                    if (isEdge) {
                        bestDist = dist;
                        outIdxA = i;
                        outIdxB = j;
                        outT = t;
                        found = true;
                    }
                }
            }
        }

        return found;
    }

    // ════════════════════════════════════════════
    // 三路姿势混合
    // ════════════════════════════════════════════

    void BlendSpace2D::BlendThreePoses(Skeleton& skeleton,
                                        AnimationPose& poseEval,
                                        const BlendSpace2DSample& s0,
                                        const BlendSpace2DSample& s1,
                                        const BlendSpace2DSample& s2,
                                        const std::array<float32, 3>& weights,
                                        float32 time) const {
        size_t boneCount = skeleton.GetBoneCount();
        if (boneCount == 0) return;

        // 对三个采样点分别求值并保存骨骼局部姿势快照
        // 使用 seek 方式在不修改时间线内部状态的情况下求值

        // 保存原始时间
        float32 savedTime0 = s0.clip->GetLocalTime();
        float32 savedTime1 = s1.clip->GetLocalTime();
        float32 savedTime2 = s2.clip->GetLocalTime();

        // 求值采样点 0
        s0.clip->Seek(time);
        poseEval.EvaluateFromTimeline(*s0.clip);
        std::vector<Mat4> pose0(boneCount);
        for (size_t i = 0; i < boneCount; ++i) {
            pose0[i] = skeleton.GetBone(static_cast<int32>(i)).localPoseMatrix;
        }

        // 求值采样点 1
        s1.clip->Seek(time);
        poseEval.EvaluateFromTimeline(*s1.clip);
        std::vector<Mat4> pose1(boneCount);
        for (size_t i = 0; i < boneCount; ++i) {
            pose1[i] = skeleton.GetBone(static_cast<int32>(i)).localPoseMatrix;
        }

        // 求值采样点 2
        s2.clip->Seek(time);
        poseEval.EvaluateFromTimeline(*s2.clip);
        std::vector<Mat4> pose2(boneCount);
        for (size_t i = 0; i < boneCount; ++i) {
            pose2[i] = skeleton.GetBone(static_cast<int32>(i)).localPoseMatrix;
        }

        // 恢复原始时间
        s0.clip->Seek(savedTime0);
        s1.clip->Seek(savedTime1);
        s2.clip->Seek(savedTime2);

        // 对每个骨骼做三路加权混合
        float32 w0 = weights[0];
        float32 w1 = weights[1];
        float32 w2 = weights[2];

        for (size_t i = 0; i < boneCount; ++i) {
            Bone& outBone = skeleton.GetBone(static_cast<int32>(i));

            // 平移：三路加权和
            Vec3 transA = AnimationBlend::DecomposeTranslation(pose0[i]);
            Vec3 transB = AnimationBlend::DecomposeTranslation(pose1[i]);
            Vec3 transC = AnimationBlend::DecomposeTranslation(pose2[i]);
            Vec3 trans(
                transA.x * w0 + transB.x * w1 + transC.x * w2,
                transA.y * w0 + transB.y * w1 + transC.y * w2,
                transA.z * w0 + transB.z * w1 + transC.z * w2
            );

            // 旋转：三路 SLERP（逐步进行）
            Quat rot0 = AnimationBlend::DecomposeRotation(pose0[i]);
            Quat rot1 = AnimationBlend::DecomposeRotation(pose1[i]);
            Quat rot2 = AnimationBlend::DecomposeRotation(pose2[i]);

            // 三路四元数混合：先混合 0 和 1，再混合结果和 2
            // 权重需要重新归一化到每一步的 [0,1] 范围
            float32 sum01 = w0 + w1;
            float32 t01 = (sum01 > 1e-10f) ? w1 / sum01 : 0.0f;
            Quat blended01 = AnimationBlend::SLERP(rot0, rot1, t01);
            Quat finalRot = AnimationBlend::SLERP(blended01, rot2, w2);

            // 缩放：三路加权
            Vec3 scale0 = AnimationBlend::DecomposeScale(pose0[i]);
            Vec3 scale1 = AnimationBlend::DecomposeScale(pose1[i]);
            Vec3 scale2 = AnimationBlend::DecomposeScale(pose2[i]);
            Vec3 finalScale(
                scale0.x * w0 + scale1.x * w1 + scale2.x * w2,
                scale0.y * w0 + scale1.y * w1 + scale2.y * w2,
                scale0.z * w0 + scale1.z * w1 + scale2.z * w2
            );

            // 重组矩阵: T * R * S
            AnimationPose::ComposeMatrix(
                trans,
                AnimationBlend::ToEuler(finalRot),
                finalScale,
                outBone.localPoseMatrix);
        }

        // 更新世界矩阵和蒙皮矩阵
        skeleton.UpdateWorldPoses();
    }

    // ════════════════════════════════════════════
    // 核心采样
    // ════════════════════════════════════════════

    void BlendSpace2D::Sample(Skeleton& skeleton,
                               AnimationPose& poseEval,
                               const Vec2& paramValue,
                               float32 time) {
        // ── 自动重建三角剖分（如果需要） ──
        if (m_TriangulationDirty) {
            const_cast<BlendSpace2D*>(this)->RebuildTriangulation();
        }

        int32 n = static_cast<int32>(m_Samples.size());

        if (n == 0) {
            return;  // 无采样点
        }

        if (n == 1) {
            // ── 纯态：只有一个采样点 ──
            const auto& sample = m_Samples[0];
            if (sample.clip) {
                float32 saved = sample.clip->GetLocalTime();
                sample.clip->Seek(time);
                poseEval.EvaluateAndUpdate(*sample.clip);
                sample.clip->Seek(saved);
            }
            return;
        }

        if (n == 2) {
            // ── 退化为 1D 混合 ──
            const auto& s0 = m_Samples[0];
            const auto& s1 = m_Samples[1];
            Vec2 closest;
            float32 t = ProjectOnEdge(paramValue, s0.parameterValue,
                                       s1.parameterValue, closest);
            AnimationBlend::BlendTimelinePose(
                skeleton, poseEval, *s0.clip, *s1.clip, t, time);
            return;
        }

        // ── 3+ 个采样点：三角化混合 ──

        // 在所有三角形中查找包含 paramValue 的那个
        for (const auto& tri : m_Triangles) {
            const Vec2& a = m_Samples[tri.i0].parameterValue;
            const Vec2& b = m_Samples[tri.i1].parameterValue;
            const Vec2& c = m_Samples[tri.i2].parameterValue;

            float32 w0, w1, w2;
            if (Barycentric(paramValue, a, b, c, w0, w1, w2)) {
                // 归一化权重
                float32 sum = w0 + w1 + w2;
                if (sum > 1e-10f) {
                    w0 /= sum;
                    w1 /= sum;
                    w2 /= sum;
                }

                BlendThreePoses(skeleton, poseEval,
                                m_Samples[tri.i0],
                                m_Samples[tri.i1],
                                m_Samples[tri.i2],
                                {w0, w1, w2}, time);
                return;
            }
        }

        // ── 参数点不在任何三角形内（在凸包外） ──
        // 回退到最近边的 1D 混合
        int32 edgeA, edgeB;
        float32 edgeT;
        if (FindFallbackEdge(paramValue, edgeA, edgeB, edgeT)) {
            AnimationBlend::BlendTimelinePose(
                skeleton, poseEval,
                *m_Samples[edgeA].clip,
                *m_Samples[edgeB].clip,
                edgeT, time);
        } else {
            // 最终回退：使用最近采样点
            float32 bestDist = std::numeric_limits<float32>::max();
            int32 bestIdx = 0;
            for (int32 i = 0; i < n; ++i) {
                float32 dx = paramValue.x - m_Samples[i].parameterValue.x;
                float32 dy = paramValue.y - m_Samples[i].parameterValue.y;
                float32 d = dx * dx + dy * dy;
                if (d < bestDist) {
                    bestDist = d;
                    bestIdx = i;
                }
            }
            if (m_Samples[bestIdx].clip) {
                float32 saved = m_Samples[bestIdx].clip->GetLocalTime();
                m_Samples[bestIdx].clip->Seek(time);
                poseEval.EvaluateAndUpdate(*m_Samples[bestIdx].clip);
                m_Samples[bestIdx].clip->Seek(saved);
            }
        }
    }

} // namespace Engine
