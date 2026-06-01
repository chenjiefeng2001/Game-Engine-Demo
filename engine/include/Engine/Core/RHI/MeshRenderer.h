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
    struct Frustum;

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

        // ── 光源参数 ──
        void SetLightPosition(const Vec3& pos)   { m_LightPos = pos; }
        void SetLightColor(const Vec3& color)    { m_LightColor = color; }
        void SetLightIntensity(float intensity)  { m_LightIntensity = intensity; }
        void SetAmbientColor(const Vec3& color)  { m_AmbientColor = color; }

        // ── 光源参数查询（供调试 UI 访问） ──
        const Vec3& GetLightPosition()  const { return m_LightPos; }
        const Vec3& GetLightColor()     const { return m_LightColor; }
        float       GetLightIntensity() const { return m_LightIntensity; }
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

        /** 使用场景图渲染（自动平截头体剔除 + 深度预渲染） */
        void RenderWithSceneGraph(const std::vector<GameObject*>& objects,
                                  const Frustum* frustum = nullptr,
                                  bool forceDepthPrePass = false);

        // ── 主渲染入口（无 PVS 剔除） ──
        void Render(const std::vector<GameObject*>& objects);

    private:
        const PotentiallyVisibleSet* m_PVS = nullptr;
        std::unique_ptr<IPrimitiveBatch> m_Batch;  // 内部批处理器

        // ── 深度预渲染 ──
        std::shared_ptr<Shader> m_DepthShader;
        bool m_DepthPrePassEnabled = true;

        // ── 场景图 ──
        const ISceneGraph* m_SceneGraph = nullptr;
        uint32 m_SGThreshold = 50;  // 物体超过此数启用场景图剔除

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

        Vec3  m_LightPos      = {5.0f, 10.0f, 5.0f};
        Vec3  m_LightColor    = {1.0f, 1.0f, 1.0f};
        float m_LightIntensity = 1.0f;
        Vec3  m_AmbientColor  = {0.15f, 0.15f, 0.20f};

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
