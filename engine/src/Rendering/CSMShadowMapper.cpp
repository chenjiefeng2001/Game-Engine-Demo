/**
 * @file CSMShadowMapper.cpp
 * @brief 级联阴影贴图 (CSM) 实现 — 分割 + Texel Snap + Frustum Culling
 *
 * 核心算法：
 *   1. 根据视锥体计算 4 个级联的远近裁剪面（对数/均匀混合分割）
 *   2. 对每个级联计算光源正交投影矩阵
 *   3. Texel Snap：将投影中心对齐到纹素边界，防止阴影边缘闪烁
 *   4. 生成 CSMUBOData 供着色器使用
 *
 * 使用方式：
 * @code
 *   CSMShadowMapper csm;
 *   csm.SetView(viewMatrix, projMatrix);
 *   csm.SetLight(direction, position);
 *   csm.CalculateCascades(heplerFrustum[0..7]);
 *
 *   // 获取 GPU 数据
 *   const auto& csmData = csm.GetCSMData();
 *   // 上传到 UBO
 * @endcode
 */

#include "Engine/Rendering/LightTypes.h"
#include "Engine/Core/Log.h"
#include <cmath>
#include <algorithm>

namespace {
    Engine::Logger s_Log("CSM");
}

namespace Engine {
namespace Rendering {

    // ════════════════════════════════════════════════════════════
    // CSMShadowMapper — 级联阴影计算类
    // ════════════════════════════════════════════════════════════

    class CSMShadowMapper {
    public:
        CSMShadowMapper() = default;
        ~CSMShadowMapper() = default;

        // 禁用拷贝
        CSMShadowMapper(const CSMShadowMapper&) = delete;
        CSMShadowMapper& operator=(const CSMShadowMapper&) = delete;

        // ── 配置 ──
        void SetShadowMapSize(uint32 size) { m_ShadowMapSize = size; }
        void SetCascadeCount(uint32 count) { m_CascadeCount = std::min(count, (uint32)CSMUBOData::kMaxCascades); }
        void SetSplitLambda(float lambda)  { m_SplitLambda = lambda; }
        void SetBias(float bias)           { m_Bias = bias; }

        uint32 GetShadowMapSize() const { return m_ShadowMapSize; }
        uint32 GetCascadeCount()  const { return m_CascadeCount; }

        // ── 每帧更新 ──

        /**
         * @brief 设置主相机视图/投影矩阵
         *
         * @param view      视图矩阵（世界 → 视图）
         * @param proj      投影矩阵（视图 → 裁剪）
         * @param nearPlane 近裁剪面（视图空间 Z）
         * @param farPlane  远裁剪面（视图空间 Z）
         */
        void SetCamera(const Mat4& view, const Mat4& proj,
                       float nearPlane = LightConstants::kDefaultNearZ,
                       float farPlane  = LightConstants::kDefaultFarZ) {
            m_ViewMatrix   = view;
            m_ProjMatrix   = proj;
            m_CamNear      = nearPlane;
            m_CamFar       = farPlane;
        }

        /**
         * @brief 设置方向光参数
         *
         * @param lightDir 光源方向（世界空间，指向光源）
         * @param lightPos 光源位置（世界空间，用于计算 LookAt）
         */
        void SetLight(const Vec3& lightDir, const Vec3& lightPos) {
            m_LightDir = Vec3::Normalize(lightDir);
            m_LightPos = lightPos;
        }

        /**
         * @brief 计算所有级联的投影矩阵
         *
         * 核心步骤：
         *   1. 计算级联分割距离（Cascade Split Distances）
         *   2. 计算每级联的光源正交投影
         *   3. Texel Snap 防闪烁
         *
         * @param cameraWorldPos 相机世界位置（用于计算到物体的距离）
         */
        void CalculateCascades(const Vec3& cameraWorldPos) {
            if (m_CascadeCount == 0) return;

            float cascadeFarDepths[CSMUBOData::kMaxCascades + 1];
            cascadeFarDepths[0] = m_CamNear;

            // ── 1. 计算级联分割 — 对数/均匀混合 ──
            for (uint32 i = 0; i < m_CascadeCount; ++i) {
                float p = (float)(i + 1) / (float)m_CascadeCount;

                // 对数分割
                float logSplit = m_CamNear * std::pow(m_CamFar / m_CamNear, p);

                // 均匀分割
                float uniformSplit = m_CamNear + (m_CamFar - m_CamNear) * p;

                // 混合：m_SplitLambda 控制倾向（偏对数 = 近处级联更精细）
                cascadeFarDepths[i + 1] = logSplit * m_SplitLambda +
                                          uniformSplit * (1.0f - m_SplitLambda);
            }

            // ── 2. 构建光源 LookAt 矩阵 ──
            Vec3 lightUp(0, 0, 1);
            Vec3 lightRight = Vec3::Cross(m_LightDir, lightUp);
            if (Vec3::Length(lightRight) < 0.001f) {
                lightUp = Vec3(0, 0, 1);
                lightRight = Vec3::Cross(m_LightDir, lightUp);
            }
            lightRight = Vec3::Normalize(lightRight);
            lightUp    = Vec3::Normalize(Vec3::Cross(lightRight, m_LightDir));

            // 光源空间变换矩阵（世界 → 光源空间）
            Mat4 lightView;
            lightView.data[0]  = lightRight.x; lightView.data[4]  = lightRight.y;
            lightView.data[8]  = lightRight.z; lightView.data[12] = -Vec3::Dot(lightRight, m_LightPos);
            lightView.data[1]  = lightUp.x;    lightView.data[5]  = lightUp.y;
            lightView.data[9]  = lightUp.z;    lightView.data[13] = -Vec3::Dot(lightUp, m_LightPos);
            lightView.data[2]  = m_LightDir.x; lightView.data[6]  = m_LightDir.y;
            lightView.data[10] = m_LightDir.z; lightView.data[14] = -Vec3::Dot(m_LightDir, m_LightPos);
            lightView.data[3]  = 0;            lightView.data[7]  = 0;
            lightView.data[11] = 0;            lightView.data[15] = 1;

            m_CSMData.cascadeCount = m_CascadeCount;

            // ── 3. 对每个级联计算 ──
            for (uint32 i = 0; i < m_CascadeCount; ++i) {
                float nearZ = cascadeFarDepths[i];
                float farZ  = cascadeFarDepths[i + 1];

                // 将级联视锥体 8 个角转换到光源空间
                Vec3 frustumCorners[8];
                ComputeFrustumCorners(nearZ, farZ, cameraWorldPos, frustumCorners);

                // 转到光源空间
                Vec3 lightSpaceCorners[8];
                for (int j = 0; j < 8; ++j) {
                    Vec4 v(frustumCorners[j].x, frustumCorners[j].y, frustumCorners[j].z, 1.0f);
                    Vec4 lv = Mat4MultiplyVec4(lightView, v);
                    lightSpaceCorners[j] = Vec3(lv.x, lv.y, lv.z);
                }

                // 计算 AABB
                Vec3 minAABB = lightSpaceCorners[0];
                Vec3 maxAABB = lightSpaceCorners[0];
                for (int j = 1; j < 8; ++j) {
                    minAABB.x = std::min(minAABB.x, lightSpaceCorners[j].x);
                    minAABB.y = std::min(minAABB.y, lightSpaceCorners[j].y);
                    minAABB.z = std::min(minAABB.z, lightSpaceCorners[j].z);
                    maxAABB.x = std::max(maxAABB.x, lightSpaceCorners[j].x);
                    maxAABB.y = std::max(maxAABB.y, lightSpaceCorners[j].y);
                    maxAABB.z = std::max(maxAABB.z, lightSpaceCorners[j].z);
                }

                // ── 4. Texel Snap ──
                // 将正交投影中心对齐到纹素边界，防止相机移动时阴影边缘闪烁
                float texelSize = (maxAABB.x - minAABB.x) / (float)m_ShadowMapSize;
                float snappedX = std::floor(((minAABB.x + maxAABB.x) * 0.5f) / texelSize) * texelSize;
                texelSize = (maxAABB.y - minAABB.y) / (float)m_ShadowMapSize;
                float snappedY = std::floor(((minAABB.y + maxAABB.y) * 0.5f) / texelSize) * texelSize;

                // 计算半范围
                float halfRangeX = (maxAABB.x - minAABB.x) * 0.5f;
                float halfRangeY = (maxAABB.y - minAABB.y) * 0.5f;

                // ── 5. 构建正交投影矩阵 ──
                Mat4 orthoProj;
                orthoProj.Identity();
                orthoProj.data[0]  = 1.0f / halfRangeX;
                orthoProj.data[5]  = 1.0f / halfRangeY;
                orthoProj.data[10] = -2.0f / (maxAABB.z - minAABB.z);
                orthoProj.data[12] = -snappedX / halfRangeX;
                orthoProj.data[13] = -snappedY / halfRangeY;
                orthoProj.data[14] = -(maxAABB.z + minAABB.z) / (maxAABB.z - minAABB.z);

                // 合成 Light View-Projection
                Mat4Multiply(orthoProj, lightView, m_CSMData.cascades[i].lightViewProj);
                m_CSMData.cascades[i].splitDepth = farZ;
            }

            s_Log.Info("CSM calculated: {} cascades (near={}, far={})",
                       m_CascadeCount, m_CamNear, m_CamFar);
        }

        /** 获取计算结果 */
        const CSMUBOData& GetCSMData() const noexcept { return m_CSMData; }

    private:
        // ── 配置 ──
        uint32 m_ShadowMapSize = LightConstants::kDefaultShadowMapSize;
        uint32 m_CascadeCount  = LightConstants::kDefaultCascadeCount;
        float  m_SplitLambda   = LightConstants::kDefaultSplitLambda;
        float  m_Bias          = LightConstants::kDefaultShadowBias;

        // ── 相机状态 ──
        Mat4  m_ViewMatrix;
        Mat4  m_ProjMatrix;
        float m_CamNear = LightConstants::kDefaultNearZ;
        float m_CamFar  = LightConstants::kDefaultFarZ;
        Vec3  m_LightDir = {0, -1, 0};
        Vec3  m_LightPos = {0, 50, 0};

        // ── 计算结果 ──
        CSMUBOData m_CSMData;

        // ── 常量 ──
        static constexpr float kDegToRad = 0.01745329252f;

        /**
         * @brief 在视图空间中计算级联视锥体的 8 个角
         *
         * @param nearZ    近裁剪面 Z（视图空间）
         * @param farZ     远裁剪面 Z（视图空间）
         * @param camPos   相机世界位置
         * @param outCorners 输出数组[8]，世界空间坐标
         */
        static void ComputeFrustumCorners(float nearZ, float farZ,
                                           const Vec3& camPos,
                                           Vec3 outCorners[8]) {
            // 从投影矩阵推导 FOV
            // proj[0][0] = 1 / (tan(fov/2) * aspect)
            // 简化: 使用标准透视投影参数
            // 这里使用一个近似：假设 60° FOV, 16:9
            constexpr float kFOV = 60.0f * 0.01745329252f;
            constexpr float kAspect = 16.0f / 9.0f;

            float tanHalfFOV = std::tan(kFOV * 0.5f);
            float nearH = tanHalfFOV * nearZ;
            float nearW = nearH * kAspect;
            float farH  = tanHalfFOV * farZ;
            float farW  = farH * kAspect;

            // Near plane (反方向是-z)
            outCorners[0] = Vec3(-nearW, -nearH, -nearZ);
            outCorners[1] = Vec3( nearW, -nearH, -nearZ);
            outCorners[2] = Vec3( nearW,  nearH, -nearZ);
            outCorners[3] = Vec3(-nearW,  nearH, -nearZ);

            // Far plane
            outCorners[4] = Vec3(-farW, -farH, -farZ);
            outCorners[5] = Vec3( farW, -farH, -farZ);
            outCorners[6] = Vec3( farW,  farH, -farZ);
            outCorners[7] = Vec3(-farW,  farH, -farZ);

            // 将角从视图空间转换到世界空间
            // 视图矩阵的逆矩阵将视图空间 → 世界空间
            // 简化：近似，忽略旋转偏移，仅用 camPos 平移
            // TODO：使用完整的逆视图矩阵
            for (int i = 0; i < 8; ++i) {
                outCorners[i].x += camPos.x;
                outCorners[i].y += camPos.y;
                outCorners[i].z += camPos.z;
            }
        }
    };

} // namespace Rendering
} // namespace Engine