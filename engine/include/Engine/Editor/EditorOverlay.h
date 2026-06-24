#pragma once

/**
 * @file EditorOverlay.h
 * @brief 编辑器覆盖层 — 无限网格、坐标轴指示器、场景图标
 *
 * 在 FBO 的 3D 场景渲染完成后绘制，叠加到最终图像上。
 * 所有绘制通过 GladGLContext 的命令缓冲完成，不参与场景光照管线。
 */

#include "Engine/Types.h"
#include "Engine/Editor/ViewportConfig.h"
#include <memory>

struct GladGLContext;

namespace Engine {

    class Shader;
    class VertexArray;
    class IGraphicsFactory;

    class EditorOverlay {
    public:
        EditorOverlay();
        ~EditorOverlay();

        EditorOverlay(const EditorOverlay&) = delete;
        EditorOverlay& operator=(const EditorOverlay&) = delete;

        // ── 生命周期 ──
        /** 创建网格/轴所需的 GPU 资源 */
        void Init(IGraphicsFactory* factory, GladGLContext* gl);

        /** 释放 GPU 资源 */
        void Shutdown();

        // ── 绘制（在 3D 场景渲染完成后、FBO Unbind 之前调用） ──
        /**
         * @brief 绘制无限网格（根据相机距离自动 LOD 线条密度）
         * @param gl        GL 上下文（用于绘制命令）
         * @param viewProj  当前帧的 ViewProjection 矩阵
         * @param cameraPos 相机世界位置（用于网格淡出计算）
         * @param config    视口配置
         */
        void DrawGrid(GladGLContext* gl,
                      const float* viewProj,
                      const float* cameraPos,
                      const ViewportConfig& config);

        /**
         * @brief 绘制世界坐标轴指示器（Viewport 左下角小图标）
         * @param gl        GL 上下文
         * @param view      当前帧 View 矩阵（仅旋转部分）
         * @param config    视口配置
         */
        void DrawAxisIndicator(GladGLContext* gl,
                               const float* view,
                               const ViewportConfig& config);

        /** 是否已初始化 */
        bool IsInitialized() const { return m_Initialized; }

    private:
        /** 创建网格顶点缓冲（LOD0 密集 / LOD1 粗略） */
        bool CreateGridMesh(IGraphicsFactory* factory);

        /** 创建坐标轴箭头网格 */
        bool CreateAxisMesh(IGraphicsFactory* factory);

        // ── GPU 资源 ──
        bool m_Initialized = false;

        // 网格（两个 LOD 级别）
        std::shared_ptr<VertexArray> m_GridVAO[2];
        uint32 m_GridVertexCount[2] = { 0, 0 };

        // 坐标轴指示器
        std::shared_ptr<VertexArray> m_AxisVAO;
        uint32 m_AxisVertexCount = 0;

        // Shader
        std::shared_ptr<Shader> m_GridShader;
        std::shared_ptr<Shader> m_AxisShader;

        GladGLContext* m_GL = nullptr;
    };

} // namespace Engine