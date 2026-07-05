#pragma once

/**
 * @file ShadowMapper.h
 * @brief 阴影映射器 — 渲染管线层抽象
 *
 * 位于 RHI 之上、Scene 之下，负责：
 *   A. 级联矩阵计算 (CSM Math)
 *   B. 阴影视锥体剔除 (Shadow Culling)
 *   C. 向 RenderGraph 注册 Shadow Depth Pass
 *   D. 输出 CSMUBOData 供后续 Pass 的 Shader 使用
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include "Engine/Core/RHI/RHITypes.h"
#include "Engine/Rendering/LightTypes.h"
#include <memory>
#include <vector>

namespace Engine {
    class PerspectiveCamera;
    class Scene;
    namespace RHI { class IRHIDevice; class IRHITexture; }
    namespace Rendering { class RenderGraph; }

namespace Rendering {

    // ============================================================
    // 阴影映射器配置
    // ============================================================
    struct ShadowMapperConfig {
        uint32 shadowMapSize = 2048;          ///< 阴影贴图分辨率
        uint32 numCascades   = 4;             ///< 级联数量 (CSM)
        float  cascadeSplitLambda = 0.95f;    ///< 对数/均匀分割混合因子
        float  shadowBias    = 0.005f;        ///< 阴影偏移
        float  lightDistance = 100.0f;        ///< 光源投影距离
        float  sceneRadius   = 50.0f;         ///< 场景包围盒半径
        Vec3   lightDir      = {0.577f, -0.577f, 0.577f};
        Vec3   lightPos      = {20.0f, 30.0f, 20.0f};
    };

    // ============================================================
    // ShadowMapper — 纯抽象接口
    // ============================================================
    class ShadowMapper {
    public:
        virtual ~ShadowMapper() = default;

        ShadowMapper(const ShadowMapper&) = delete;
        ShadowMapper& operator=(const ShadowMapper&) = delete;

        /**
         * @brief 初始化阴影映射器
         * @param device  RHI 设备（用于创建深度纹理）
         * @param config  配置参数
         * @return 是否成功
         */
        virtual bool Initialize(RHI::IRHIDevice& device,
                                const ShadowMapperConfig& config) = 0;

        /** 销毁 GPU 资源 */
        virtual void Shutdown() = 0;

        /** 是否已就绪 */
        virtual bool IsValid() const = 0;

        // ── 每帧更新 ──

        /**
         * @brief 准备阴影数据（每帧调用）
         *
         * 职责：
         *   1. 根据相机视锥体计算级联分割
         *   2. 计算每级联的光照 View-Projection 矩阵
         *   3. Texel Snap 防闪烁
         *   4. 用自定义视锥体剔除阴影投射物
         *
         * @param viewProj    主相机的 View-Projection 矩阵
         * @param cameraPos   相机世界位置
         * @param scene       场景（用于提取投射阴影的物体）
         */
        virtual void Prepare(const Mat4& viewProj,
                             const Vec3& cameraPos,
                             const Scene* scene) = 0;

        // ── RenderGraph 集成 ──

        /**
         * @brief 向 RenderGraph 注册阴影深度 Pass
         *
         * 此方法创建 "ShadowDepth" Pass，在 RenderGraph 中：
         *   1. 创建瞬态深度纹理资源
         *   2. 录制深度绘制命令
         *   3. 输出屏障供后续 Pass 采样
         *
         * @param graph 渲染图
         */
        virtual void RegisterPasses(RenderGraph& graph) = 0;

        // ── 数据访问 ──

        /** 获取 CSM UBO 数据（供 Shader 设置 uniform） */
        virtual const CSMUBOData& GetCSMData() const = 0;

        /** 获取阴影深度纹理 */
        virtual RHI::IRHITexture* GetShadowTexture() const = 0;

        /** 获取配置 */
        virtual const ShadowMapperConfig& GetConfig() const = 0;

        // ── 光源控制 ──
        virtual void SetLightPosition(const Vec3& pos) = 0;
        virtual void SetLightDirection(const Vec3& dir) = 0;
        virtual void SetLightDistance(float dist) = 0;
        virtual void SetBias(float bias) = 0;
    };

}} // namespace Engine::Rendering