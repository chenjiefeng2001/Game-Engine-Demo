#pragma once

/**
 * @file ViewportPanel.h
 * @brief 视口面板 — 可多次实例化的独立 3D 视口
 *
 * 架构：
 *   - ViewportConfig 纯数据结构管理所有开关和参数
 *   - EditorCamera 负责相机输入和矩阵计算
 *   - EditorOverlay 负责网格/轴等覆盖层绘制
 *   - 3D 场景物体由 SceneRenderer 提交，本类只负责框架层
 */

#include "Engine/Types.h"
#include "Engine/Editor/EditorCamera.h"
#include "Engine/Editor/EditorOverlay.h"
#include "Engine/Editor/ViewportConfig.h"
#include "Engine/Core/ViewMode.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/RenderResources/VertexArray.h"
#include "Engine/Core/RenderResources/VertexBuffer.h"
#include "Engine/Core/RenderResources/IndexBuffer.h"
#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/OpenGL/OpenGLFramebuffer.h"
#include <memory>
#include <string>
#include <functional>
#include <vector>

struct GladGLContext;

namespace Engine {

    class ViewportPanel {
    public:
        explicit ViewportPanel(const std::string& name);
        ~ViewportPanel();

        ViewportPanel(const ViewportPanel&) = delete;
        ViewportPanel& operator=(const ViewportPanel&) = delete;

        // ── 资源初始化（shader、网格、Overlay） ──
        void InitResources(IGraphicsFactory* factory,
                           const std::string& vertPath,
                           const std::string& fragPath);

        // ── 外部依赖注入 ──
        void SetRenderContext(IRenderContext* ctx) { m_RenderContext = ctx; }
        void SetGLContext(GladGLContext* gl) { m_GL = gl; }

        // ── 配置访问（外部直接读写配置来控制视口行为） ──
        ViewportConfig&       GetConfig()       { return m_Config; }
        const ViewportConfig& GetConfig() const { return m_Config; }

        // ── 兼容旧接口（委托到 ViewportConfig） ──
        void SetViewMode(ViewMode mode) { m_Config.CurrentMode = mode; }
        ViewMode GetViewMode() const { return m_Config.CurrentMode; }

        void SetShowGrid(bool show) { m_Config.ShowGrid = show; }
        bool IsShowGrid() const { return m_Config.ShowGrid; }

        // ── 帧生命周期 ──
        void OnUpdate(float32 dt, bool isFocusedViewport = false);
        void OnImGui();

        // ── 访问器 ──
        const std::string& GetName() const { return m_Config.Name; }
        EditorCamera& GetCamera() { return *m_Camera; }
        const EditorCamera& GetCamera() const { return *m_Camera; }
        float32 GetWidth() const  { return m_Width; }
        float32 GetHeight() const { return m_Height; }
        bool    IsHovered() const { return m_Hovered; }
        bool    IsFocused() const { return m_Focused; }

        float32 GetViewX() const { return m_ViewX; }
        float32 GetViewY() const { return m_ViewY; }

        using ResizeCallback = std::function<void(int32 width, int32 height)>;
        void SetResizeCallback(ResizeCallback cb) { m_ResizeCallback = std::move(cb); }

    private:
        void InitFBO();
        void Render3DScene();

        // ── 配置 ──
        ViewportConfig m_Config;

        // ── 窗口状态 ──
        float32 m_Width  = 0.0f;
        float32 m_Height = 0.0f;
        float32 m_ViewX  = 0.0f;
        float32 m_ViewY  = 0.0f;
        bool    m_Hovered = false;
        bool    m_Focused = false;

        // ── 外部依赖 ──
        IRenderContext*      m_RenderContext = nullptr;
        IGraphicsFactory*    m_Factory = nullptr;
        GladGLContext*       m_GL = nullptr;
        std::unique_ptr<OpenGLFramebuffer> m_FBO;

        // ── 编辑器系统 ──
        std::unique_ptr<EditorCamera> m_Camera;
        EditorOverlay                m_Overlay;

        // ── 演示 3D 资源（后续移至 SceneRenderer） ──
        std::shared_ptr<Shader>      m_3DShader;
        std::shared_ptr<VertexArray> m_CubeVAO;

        // ── 回调 ──
        ResizeCallback m_ResizeCallback;
    };

} // namespace Engine
