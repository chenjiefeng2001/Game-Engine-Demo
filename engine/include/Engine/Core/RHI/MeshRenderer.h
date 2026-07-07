#pragma once

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include "Engine/Core/RHI/Handle.h"
#include "Engine/Core/RHI/RenderPacket.h"
#include "Engine/Core/RHI/IRHIVertexArray.h"
#include "Engine/Core/RHI/IRHIVertexBuffer.h"
#include "Engine/Core/RHI/IRHIIndexBuffer.h"
#include "Engine/Rendering/ShadowMapper.h"
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
    namespace Rendering { class ShadowMapper; }
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
     *
     * RHI 路径（v2）：使用 IRHIVertexArray/IRHIVertexBuffer/IRHIIndexBuffer，
     *   完全不依赖 OpenGL 具体类型，支持多后端。
     * 旧路径（v1）：使用 VertexArray/VertexBuffer/IndexBuffer（保留向后兼容）。
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

        static AxisMesh GenerateAxes(float length = 10.0f);
        static AxisMesh GenerateGrid(float size = 10.0f, int32 steps = 10);

        // ── 潜在可见集 (PVS) ──
        void SetPVS(const PotentiallyVisibleSet* pvs) { m_PVS = pvs; }
        const PotentiallyVisibleSet* GetPVS() const { return m_PVS; }

        void RenderWithPVS(const std::vector<GameObject*>& objects,
                           const Vec3& cameraPos);

        void RenderBatched(const std::vector<GameObject*>& objects,
                           IPrimitiveBatch& batch);

        // ── 深度预渲染 (Depth Pre-Pass) ──
        void SetDepthShader(std::shared_ptr<Shader> shader) { m_DepthShader = std::move(shader); }
        void SetDepthPrePassEnabled(bool enable) { m_DepthPrePassEnabled = enable; }
        bool IsDepthPrePassEnabled() const { return m_DepthPrePassEnabled; }
        void RenderWithDepthPrePass(const std::vector<GameObject*>& objects);

        // ── 场景图管理 ──
        void SetSceneGraph(const ISceneGraph* sceneGraph) { m_SceneGraph = sceneGraph; }
        const ISceneGraph* GetSceneGraph() const { return m_SceneGraph; }
        void SetSceneGraphThreshold(uint32 threshold) { m_SGThreshold = threshold; }
        uint32 GetSceneGraphThreshold() const { return m_SGThreshold; }
        IPrimitiveBatch* GetBatch() const { return m_Batch.get(); }
        void RenderWithSceneGraph(const std::vector<GameObject*>& objects,
                                  const Frustum* frustum = nullptr,
                                  bool forceDepthPrePass = false);

        // ── 阴影映射 ──
        void SetShadowMapper(Rendering::ShadowMapper* sm) { m_ShadowMapper = sm; }
        Rendering::ShadowMapper* GetShadowMapper() { return m_ShadowMapper; }
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

        // ── GBuffer ──
        void SetGBuffer(GBuffer* gbuf) { m_GBuffer = gbuf; }
        GBuffer* GetGBuffer() const { return m_GBuffer; }
        void RenderDeferred(const std::vector<GameObject*>& objects,
                            std::shared_ptr<Shader> lightShader);

        // ── 主渲染入口 ──
        void Render(const std::vector<GameObject*>& objects);

        // ═══════════════════════════════════════════════
        // 【RHI v2 新路径】
        // ═══════════════════════════════════════════════

        /** 启用/禁用 RHI 渲染路径 */
        void SetRHIEnabled(bool enable) { m_RHIEnabled = enable; }
        bool IsRHIEnabled() const { return m_RHIEnabled; }

        /**
         * @brief 使用 RHI 路径渲染主场景
         * @param objects 物体列表
         */
        void Render_RHI(const std::vector<GameObject*>& objects);

        // ═══════════════════════════════════════════════
        // 【v3 提取+执行分离】
        // ═══════════════════════════════════════════════

        /// 设置渲染中使用的槽位映射（必须在使用 ExtractScene 前设置）
        void SetMeshSlotMap(RHI::MeshSlotMap* map) { m_MeshSlotMap = map; }
        void SetTextureSlotMap(RHI::TextureSlotMap* map) { m_TextureSlotMap = map; }

        /**
         * @brief 从 GameObject 列表中提取渲染数据包到 SceneExtraction
         * @param objects 物体列表
         * @param outExtraction 输出提取结果
         *
         * 此方法不绑定任何 shader / 不产生任何 glDraw 调用。
         * 纯粹从 ECS 中拷贝变换矩阵和材质信息。
         */
        void ExtractScene(const std::vector<GameObject*>& objects,
                          RHI::SceneExtraction& outExtraction);

        /**
         * @brief 执行提取后的渲染数据包
         * @param extraction 由 ExtractScene 填充的提取结果
         *
         * 遍历 packets，绑定 shader 并发出 DrawIndexed 调用。
         * 可在线程安全的情况下调用（此时 packets 是只读快照）。
         */
        void ExecuteRenderPackets(const RHI::SceneExtraction& extraction);

    private:
        const PotentiallyVisibleSet* m_PVS = nullptr;
        std::unique_ptr<IPrimitiveBatch> m_Batch;

        void InitFullscreenQuad();
        void RenderFullscreenQuad();

        // ── 深度预渲染 ──
        std::shared_ptr<Shader> m_DepthShader;
        bool m_DepthPrePassEnabled = true;

        // ── 场景图 ──
        const ISceneGraph* m_SceneGraph = nullptr;
        uint32 m_SGThreshold = 50;

        // ── 阴影 ──
        Rendering::ShadowMapper* m_ShadowMapper = nullptr;
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
        std::shared_ptr<Shader> m_GeomShader;

        // ── 统计 ──
        mutable uint32 m_LastTotalObjects = 0;
        mutable uint32 m_LastVisibleObjects = 0;

        // ════════════════════════════════════
        // v1 旧路径 GPU 资源（向后兼容）
        // ════════════════════════════════════

        /**
         * @brief 为 Mesh 创建或获取旧路径 GPU 资源
         */
        uint64 UploadMesh(const std::shared_ptr<Mesh>& mesh);

        struct CachedMeshData {
            std::shared_ptr<class VertexArray> vao;
            std::shared_ptr<class VertexBuffer> vbo;
            std::shared_ptr<class IndexBuffer>  ibo;
            uint32 indexCount = 0;
        };
        std::unordered_map<uint64, CachedMeshData> m_MeshCache;

        // ════════════════════════════════════
        // v2 RHI 新路径 GPU 资源
        // ════════════════════════════════════

        /**
         * @brief 为 Mesh 创建或获取 RHI GPU 资源
         * @return RHI 资源哈希键
         */
        uint64 UploadMesh_RHI(const std::shared_ptr<Mesh>& mesh);

        struct CachedMeshData_RHI {
            std::shared_ptr<RHI::IRHIVertexBuffer> vertexBuffer;
            std::shared_ptr<RHI::IRHIIndexBuffer>  indexBuffer;
            std::shared_ptr<RHI::IRHIVertexArray>   vertexArray;
            uint32 indexCount = 0;
        };
        std::unordered_map<uint64, CachedMeshData_RHI> m_MeshCache_RHI;

    private:
        IGraphicsFactory& m_Factory;
        IRenderContext& m_Context;
        std::shared_ptr<Shader> m_Shader;
        PerspectiveCamera* m_Camera = nullptr;

        Vec3  m_AmbientColor  = {0.15f, 0.15f, 0.20f};
        std::vector<Light> m_Lights;

        bool m_RHIEnabled = false;

        // ── v3 SplotMap 指针（由外部设置，不拥有所有权） ──
        RHI::MeshSlotMap*    m_MeshSlotMap    = nullptr;
        RHI::TextureSlotMap* m_TextureSlotMap = nullptr;
    };

} // namespace Engine
