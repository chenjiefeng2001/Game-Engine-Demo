#pragma once

/**
 * @file ViewportPanel.h
 * @brief 工业级视口面板 — 悬浮 Overlay、ImGuizmo、鼠标拾取、Scene 桥接
 *
 * 架构：
 *   - ViewportConfig 纯数据结构管理所有开关和参数
 *   - EditorCamera 负责相机输入和矩阵计算
 *   - EditorOverlay 负责网格/轴等覆盖层绘制
 *   - Scene 传入当前编辑场景，Render3DScene 将场景渲染到 FBO
 *   - RHI 抽象：所有 GPU 资源通过 IGraphicsFactory 创建
 *   - 支持 ImGuizmo 物体变换、鼠标射线拾取、悬浮工具栏
 */

#include "Engine/Types.h"
#include "Engine/Editor/EditorCamera.h"
#include "Engine/Editor/EditorOverlay.h"
#include "Engine/Editor/ViewportConfig.h"
#include "Engine/Core/ViewMode.h"
#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/OpenGL/OpenGLFramebuffer.h"
#include <memory>
#include <string>
#include <functional>

#include <imgui.h>
struct GladGLContext;

namespace Engine {

    // 前置声明
    class Scene;

    class ViewportPanel {
    public:
        explicit ViewportPanel(const std::string& name);
        ~ViewportPanel();

        ViewportPanel(const ViewportPanel&) = delete;
        ViewportPanel& operator=(const ViewportPanel&) = delete;

        // ── 资源初始化 ──
        void InitResources(IGraphicsFactory* factory,
                           const std::string& vertPath,
                           const std::string& fragPath);

        // ── 外部依赖注入 ──
        void SetRenderContext(IRenderContext* ctx) { m_RenderContext = ctx; }
        void SetGLContext(GladGLContext* gl) { m_GL = gl; }
        void SetScene(std::shared_ptr<Scene> scene) { m_Scene = scene; }

        // ── 选中对象（用于 Gizmo 变换） ──
        template<typename T>
        void SetSelectedObject(std::shared_ptr<T> obj) { m_SelectedObject = std::static_pointer_cast<void>(obj); }
        std::shared_ptr<void> GetSelectedObject() const { return m_SelectedObject.lock(); }

        // ── 配置访问 ──
        ViewportConfig&       GetConfig()       { return m_Config; }
        const ViewportConfig& GetConfig() const { return m_Config; }

        void SetViewMode(ViewMode mode) { m_Config.CurrentMode = mode; }
        ViewMode GetViewMode() const { return m_Config.CurrentMode; }
        void SetShowGrid(bool show) { m_Config.ShowGrid = show; }
        bool IsShowGrid() const { return m_Config.ShowGrid; }

        // ── 帧生命周期 ──
        void OnUpdate(float32 dt, bool isFocusedViewport = false);
        void OnImGui();

        // ── 资源清理 ──
        void Cleanup();

        // ── 访问器 ──
        const std::string& GetName() const { return m_Config.Name; }
        EditorCamera& GetCamera() { return *m_Camera; }
        const EditorCamera& GetCamera() const { return *m_Camera; }
        float32 GetWidth() const  { return m_Width; }
        float32 GetHeight() const { return m_Height; }
        bool    IsHovered() const { return m_Hovered; }
        bool    IsFocused() const { return m_Focused; }

        /// 获取鼠标在视口内的 NDC 坐标 [-1, 1]（用于射线拾取）
        void GetMouseViewportSpace(float& outNDCX, float& outNDCY) const;

        /// 获取视口屏幕边界（绝对屏幕坐标）
        void GetViewportBounds(float& outMinX, float& outMinY, float& outMaxX, float& outMaxY) const;

        using ResizeCallback = std::function<void(int32 width, int32 height)>;
        void SetResizeCallback(ResizeCallback cb) { m_ResizeCallback = std::move(cb); }

        // ── 场景渲染回调（外部传入相机参数进行场景绘制） ──
        // 使用 const void* 避免在头文件中暴露 glm 细节
        using SceneRenderCallback = std::function<void(const float* viewProj16, const float* camPos3)>;
        void SetSceneRenderCallback(SceneRenderCallback cb) { m_SceneRenderCallback = std::move(cb); }

        // ── ID 缓冲区拾取回调 ──
        /**
         * @brief 拾取回调（由 HandleMousePicking 在鼠标点击时触发）
         * @param ndcX    鼠标在视口的 NDC X [-1, 1]
         * @param ndcY    鼠标在视口的 NDC Y [-1, 1]
         * @param entityId 从拾取纹理读取的 GameObject ID（0=无物体）
         */
        using PickCallback = std::function<void(float ndcX, float ndcY, uint32 entityId)>;
        void SetPickCallback(PickCallback cb) { m_PickCallback = std::move(cb); }

        /// 执行 ID 缓冲区拾取：读取鼠标位置像素的 GameObject ID
        uint32 PickAtMouse(float mouseScreenX, float mouseScreenY) const;

        /**
         * @brief 拾取渲染回调 — 在 Render3DScene 的拾取 Pass 中被调用
         * @param viewProj16 View-Projection 矩阵
         * @param pickTextureID 拾取纹理的 OpenGL ID（用于绑定 frame buffer）
         *
         * 实现此回调时，应将每个 GameObject 的 ID 写入 layout(location=1) 输出。
         * 使用 FBO::BindForPickRendering() 切换到拾取模式。
         */
        using PickRenderCallback = std::function<void(const float* viewProj16)>;
        void SetPickRenderCallback(PickRenderCallback cb) { m_PickRenderCallback = std::move(cb); }

    private:
        void InitFBO();
        void Render3DScene();
        void UpdateFBOIfNeeded();

        // ── ImGui 渲染管线阶段（SRP 拆分） ──
        void UpdateBoundsAndFocus();  // 1. 计算视口边界、焦点与尺寸
        void Draw3DImage();           // 2. 绘制 3D FBO 画面（底层）
        void DrawOverlay();           // 3. 绘制悬浮工具栏（顶层，使用屏幕绝对坐标）
        void DrawGizmos();            // 4. 绘制 ImGuizmo 变换控件（中层）
        void HandleDragAndDrop();     // 5. 处理资源拖拽
        void HandleMousePicking();    // 6. 处理鼠标射线拾取

        // ── 配置 ──
        ViewportConfig m_Config;

        // ── 窗口状态 ──
        float32 m_Width  = 0.0f;
        float32 m_Height = 0.0f;
        bool    m_Hovered = false;
        bool    m_Focused = false;

        // ── 视口边界（绝对屏幕坐标） ──
        ImVec2 m_ViewportBounds[2];

        // ── 外部依赖 ──
        IRenderContext*      m_RenderContext = nullptr;
        IGraphicsFactory*    m_Factory = nullptr;
        GladGLContext*       m_GL = nullptr;
        std::shared_ptr<Scene> m_Scene;
        std::unique_ptr<OpenGLFramebuffer> m_FBO;

        // ── 编辑器系统 ──
        std::unique_ptr<EditorCamera> m_Camera;
        EditorOverlay                m_Overlay;

        // ── 选中对象 ──
        std::weak_ptr<void> m_SelectedObject;

        // ── 延迟 FBO 更新标记 ──
        bool m_NeedsFBOUpdate = false;

        // ── 回调 ──
        ResizeCallback       m_ResizeCallback;
        PickCallback         m_PickCallback;
        SceneRenderCallback  m_SceneRenderCallback;
        PickRenderCallback   m_PickRenderCallback;

        // ── Gizmo 状态 ──
        int  m_GizmoType = 0;   // -1=None, 0=Translate, 1=Rotate, 2=Scale
        bool m_GizmoLocal = false;
        bool m_SnapEnabled = false;
        float m_SnapValue = 0.5f;
    };

} // namespace Engine