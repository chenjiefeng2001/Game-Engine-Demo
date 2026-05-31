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
    class IPrimitiveBatch;

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

        // ── 主渲染入口（无 PVS 剔除） ──
        void Render(const std::vector<GameObject*>& objects);

    private:
        const PotentiallyVisibleSet* m_PVS = nullptr;
        std::unique_ptr<IPrimitiveBatch> m_Batch;  // 内部批处理器

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
