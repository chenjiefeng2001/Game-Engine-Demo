#pragma once

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <memory>
#include <vector>
#include <unordered_map>

namespace Engine {

    class Mesh;
    class Shader;
    class PerspectiveCamera;
    class GameObject;
    class IGraphicsFactory;
    class IRenderContext;
    class PotentiallyVisibleSet;
    class ISceneGraph;
    class IPrimitiveBatch;
    class ShadowMapper;
    class GBuffer;
    struct Frustum;

    /// 渲染模式
    enum class RenderMode : uint8 {
        Forward  = 0,   // 前向渲染 (直接光照)
        Deferred = 1,   // 延迟渲染 (G-Buffer + 光照通道)
        Default  = Forward,
    };

    /**
     * @brief 3D 网格渲染器 — 将 MeshComponent 渲染到屏幕
     *
     * 负责：
     *   1. 遍历场景中的 MeshComponent
     *   2. 管理 GPU 资源（VBO/IBO/VAO）
     *   3. 绑定 3D Shader 并设置光源、相机 uniform
     *   4. 提交绘制调用
     */
    class MeshRenderer {
    public:
        MeshRenderer(IGraphicsFactory& factory, IRenderContext& context);
        ~MeshRenderer();

        void SetShader(std::shared_ptr<Shader> shader) { m_Shader = std::move(shader); }
        std::shared_ptr<Shader> GetShader() const { return m_Shader; }

        void SetCamera(PerspectiveCamera* camera) { m_Camera = camera; }
        PerspectiveCamera* GetCamera() const { return m_Camera; }

        // ── 多光源 ──
        struct Light {
            Vec3  position  = {5, 10, 5};
            Vec3  color     = {1, 1, 1};
            float intensity = 1.0f;
        };

        void ClearLights() { m_Lights.clear(); }
        void AddLight(const Light& light) { if (m_Lights.size() < 4) m_Lights.push_back(light); }
        size_t GetLightCount() const { return m_Lights.size(); }
        Light& GetLight(size_t i) { return m_Lights[i]; }
        const std::vector<Light>& GetLights() const { return m_Lights; }

        // ── 向后兼容的单光源接口 ──
        void SetLightPosition(const Vec3& pos)   { if (m_Lights.empty()) m_Lights.push_back({}); m_Lights[0].position = pos; }
        void SetLightColor(const Vec3& color)    { if (m_Lights.empty()) m_Lights.push_back({}); m_Lights[0].color = color; }
        void SetLightIntensity(float intensity)  { if (m_Lights.empty()) m_Lights.push_back({}); m_Lights[0].intensity = intensity; }
        void SetAmbientColor(const Vec3& color)  { m_AmbientColor = color; }

        const Vec3& GetLightPosition()  const { return m_Lights.empty() ? m_AmbientColor : m_Lights[0].position; }
        const Vec3& GetLightColor()     const { return m_Lights.empty() ? m_AmbientColor : m_Lights[0].color; }
        float       GetLightIntensity() const { return m_Lights.empty() ? 1.0f : m_Lights[0].intensity; }
        const Vec3& GetAmbientColor()   const { return m_AmbientColor; }

        // ── 世界坐标轴辅助（调试用）──
        struct AxisVert {
            Vec3 position;
            Vec3 color;
        };
        struct AxisMesh {
            std::vector<AxisVert> vertices;
            std::vector<uint32> indices;
        };

        /**
         * @brief 生成世界坐标轴网格（3 条带颜色的线段: R=X, G=Y, B=Z）
         * @param length 轴的长度
         * @return AxisMesh 可用于 GL_LINES 绘制
         */
        static AxisMesh GenerateAxes(float length = 10.0f);

        /**
         * @brief 生成半透明网格辅助线（用于可视化世界地面）
         * @param size  网格大小
         * @param steps 分段数
         * @return AxisMesh 可用于 GL_LINES 绘制
         */
        static AxisMesh GenerateGrid(float size = 10.0f, int32 steps = 10);

        // ── 潜在可见集 (PVS) ──
        void SetPVS(const PotentiallyVisibleSet* pvs) { m_PVS = pvs; }
        const PotentiallyVisibleSet* GetPVS() const { return m_PVS; }

        /**
         * @brief 使用 PVS 加速渲染
         * @param objects   所有场景物体列表
         * @param cameraPos 相机世界坐标（用于 PVS 查询）
         */
        void RenderWithPVS(const std::vector<GameObject*>& objects,
                           const Vec3& cameraPos);

        /**
         * @brief 使用批处理器渲染 — 所有网格合并到单个 DrawCall
         * @param objects  物体列表
         * @param batch    图元批处理器
         */
        void RenderBatched(const std::vector<GameObject*>& objects,
                           IPrimitiveBatch& batch);

        // ── 深度预渲染 (Depth Pre-Pass) ──
        /**
         * @brief 设置深度预渲染着色器
         * @param shader 深度只写着色器（assets/shaders/depth_only）
         */
        void SetDepthShader(std::shared_ptr<Shader> shader) { m_DepthShader = std::move(shader); }

        /** 是否启用深度预渲染（减少 overdraw） */
        void SetDepthPrePassEnabled(bool enable) { m_DepthPrePassEnabled = enable; }
        bool IsDepthPrePassEnabled() const { return m_DepthPrePassEnabled; }

        /**
         * @brief 执行深度预渲染 + 主渲染（两遍法）
         * @param objects 物体列表
         */
        void RenderWithDepthPrePass(const std::vector<GameObject*>& objects);

        // ── 场景图管理（支持 Flat / BVH / Grid 切换） ──
        /**
         * @brief 设置场景图（用于平截头体剔除加速）
         * @param sceneGraph 场景图接口指针（可运行时切换）
         */
        void SetSceneGraph(const ISceneGraph* sceneGraph) { m_SceneGraph = sceneGraph; }
        const ISceneGraph* GetSceneGraph() const { return m_SceneGraph; }

        /** 自动切换阈值：物体数超过此值启用场景图剔除 */
        void SetSceneGraphThreshold(uint32 threshold) { m_SGThreshold = threshold; }
        uint32 GetSceneGraphThreshold() const { return m_SGThreshold; }

        /** 获取内部批处理器（用于粒子/贴花等） */
        IPrimitiveBatch* GetBatch() const { return m_Batch.get(); }

        /** 使用场景图渲染（自动平截头体剔除 + 深度预渲染） */
        void RenderWithSceneGraph(const std::vector<GameObject*>& objects,
                                  const Frustum* frustum = nullptr,
                                  bool forceDepthPrePass = false);

        // ── 阴影映射 ──
        void SetShadowMapper(ShadowMapper* sm) { m_ShadowMapper = sm; }
        ShadowMapper* GetShadowMapper() { return m_ShadowMapper; }
        void SetShadowEnabled(bool enable) { m_ShadowEnabled = enable; }
        bool IsShadowEnabled() const { return m_ShadowEnabled; }
        void RenderShadowPass(const std::vector<GameObject*>& objects);

        // ── SSAO ──
        void SetSSAOEnabled(bool enable) { m_SSAOEnabled = enable; }
        bool IsSSAOEnabled() const { return m_SSAOEnabled; }
        void SetSSAOStrength(float strength) { m_SSAOStrength = strength; }

        // ── 焦散 ──
        void SetCausticsEnabled(bool enable) { m_CausticsEnabled = enable; }
        bool IsCausticsEnabled() const { return m_CausticsEnabled; }
        void SetCausticStrength(float s) { m_CausticStrength = s; }

        // ── 渲染模式 ──
        void SetRenderMode(RenderMode mode) { m_RenderMode = mode; }
        RenderMode GetRenderMode() const { return m_RenderMode; }
        bool IsDeferred() const { return m_RenderMode == RenderMode::Deferred; }

        // ── GBuffer（延迟渲染用） ──
        void SetGBuffer(GBuffer* gbuf) { m_GBuffer = gbuf; }
        GBuffer* GetGBuffer() const { return m_GBuffer; }

        /** 延迟渲染入口 */
        void RenderDeferred(const std::vector<GameObject*>& objects,
                            std::shared_ptr<Shader> lightShader);

        // ── 主渲染入口（无 PVS 剔除） ──
        void Render(const std::vector<GameObject*>& objects);

    private:
        const PotentiallyVisibleSet* m_PVS = nullptr;
        std::unique_ptr<IPrimitiveBatch> m_Batch;

        // ── 全屏四边形（延迟渲染光照通道用） ──
        void InitFullscreenQuad();
        void RenderFullscreenQuad();

        // ── 深度预渲染 ──
        std::shared_ptr<Shader> m_DepthShader;
        bool m_DepthPrePassEnabled = true;

        // ── 场景图 ──
        const ISceneGraph* m_SceneGraph = nullptr;
        uint32 m_SGThreshold = 50;

        // ── 阴影 ──
        ShadowMapper* m_ShadowMapper = nullptr;
        bool m_ShadowEnabled = false;

        // ── SSAO ──
        bool m_SSAOEnabled = false;
        float m_SSAOStrength = 1.0f;
        std::shared_ptr<class Shader> m_SSAOShader;
        uint32 m_SSAOFBO = 0, m_SSAOTex = 0;

        // ── 焦散 ──
        bool m_CausticsEnabled = false;
        float m_CausticStrength = 0.3f;

        // ── 延迟渲染 ──
        RenderMode m_RenderMode = RenderMode::Forward;
        GBuffer*   m_GBuffer = nullptr;
        std::shared_ptr<Shader> m_GeomShader;  // 几何通道着色器

        // ── 统计 ──
        mutable uint32 m_LastTotalObjects = 0;
        mutable uint32 m_LastVisibleObjects = 0;

        // ── GPU 资源管理 ──
        /**
         * @brief 为 Mesh 创建或获取 GPU 资源
         * 返回一个不透明的 GPU 资源句柄（由具体实现管理）
         */
        uint64 UploadMesh(const std::shared_ptr<Mesh>& mesh);

    private:
        IGraphicsFactory& m_Factory;
        IRenderContext& m_Context;
        std::shared_ptr<Shader> m_Shader;
        PerspectiveCamera* m_Camera = nullptr;

        Vec3  m_AmbientColor  = {0.15f, 0.15f, 0.20f};
        std::vector<Light> m_Lights;

        // 向后兼容单光源（由 SetLightPosition 等填充）

        // GPU 资源缓存
        struct CachedMeshData {
            std::shared_ptr<class VertexArray> vao;
            std::shared_ptr<class VertexBuffer> vbo;
            std::shared_ptr<class IndexBuffer>  ibo;
            uint32 indexCount = 0;
        };
        std::unordered_map<uint64, CachedMeshData> m_MeshCache;
    };

} // namespace Engine
