#pragma once

/**
 * @file ViewportPanel.h
 * @brief 工业级视口面板 — 交互状态机 + MRT 双附件渲染 + 像素级拾取
 *
 * 架构对标 Unity/Blender：
 *   - ViewportTool 枚举驱动的交互状态机
 *   - MRT 单次 Pass 渲染（颜色 + ID）
 *   - 手型/选择/变换工具分离，互不干扰
 */

#include "Engine/Types.h"
#include "Engine/Editor/EditorCamera.h"
#include "Engine/Editor/EditorOverlay.h"
#include "Engine/Editor/ViewportConfig.h"
#include "Engine/Core/ViewMode.h"
#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/IRenderContext.h"
#include <memory>
#include <string>
#include <functional>

#include <imgui.h>
struct GladGLContext;

namespace Engine {

    class GameObject;

    // ── 交互工具模式（对标 Unity 工具架） ──
    enum class ViewportTool {
        Select,     // 选择/拾取 (Q)
        Hand,       // 手型平移 (H)
        Translate,  // 位移 (W)
        Rotate,     // 旋转 (E)
        Scale       // 缩放 (R)
    };

    // ── 视口可视化配置（消除硬编码） ──
    struct ViewportStyle {
        ImVec4  ToolbarBgColor       = ImVec4(0.12f, 0.12f, 0.12f, 0.85f);
        float   ToolbarHeight        = 32.0f;
        float   GizmoSnapTranslate   = 0.5f;
        float   GizmoSnapRotate      = 45.0f;
        float   GizmoSnapScale       = 0.1f;
        ImU32   SelectionBorderColor = IM_COL32(255, 200, 50, 180);
        ImU32   HoveredBorderColor   = IM_COL32(100, 150, 255, 100);
    };

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
        void SetSelectedObject(std::shared_ptr<GameObject> obj) { m_SelectedObject = obj; }

        // ── 配置访问 ──
        ViewportConfig&       GetConfig()       { return m_Config; }
        const ViewportConfig& GetConfig() const { return m_Config; }

        void SetViewMode(ViewMode mode) { m_Config.CurrentMode = mode; }
        ViewMode GetViewMode() const { return m_Config.CurrentMode; }
        void SetShowGrid(bool show) { m_Config.ShowGrid = show; }
        bool IsShowGrid() const { return m_Config.ShowGrid; }

        // ── 当前工具 ──
        void SetTool(ViewportTool tool) { m_CurrentTool = tool; }
        ViewportTool GetTool() const { return m_CurrentTool; }
        void CycleGizmoTool();

        // ── 选择模式 ──
        void SetSelectionMode(SelectionMode mode) { m_SelectionMode = mode; }
        SelectionMode GetSelectionMode() const { return m_SelectionMode; }

        // ── 吸附 ──
        void SetSnapMode(SnapMode mode);
        SnapMode GetSnapMode() const;

        // ── 样式访问 ──
        ViewportStyle& GetStyle() { return m_Style; }
        const ViewportStyle& GetStyle() const { return m_Style; }

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

        // ── 场景渲染回调（MRT 单次 Pass，同时写入颜色 + ID） ──
        using SceneRenderCallback = std::function<void(const float* viewProj16, const float* camPos3)>;
        void SetSceneRenderCallback(SceneRenderCallback cb) { m_SceneRenderCallback = std::move(cb); }

        // ── ID 缓冲区拾取回调 ──
        using PickCallback = std::function<void(float ndcX, float ndcY, int32 entityId)>;
        void SetPickCallback(PickCallback cb) { m_PickCallback = std::move(cb); }

        // ── 场景编辑回调 ──
        using SceneCreateCallback = std::function<void(const std::string& type)>;
        using SceneDeleteCallback = std::function<void()>;
        using DropAssetCallback = std::function<void(const std::string& assetPath, const float* dropPos3)>;
        void SetSceneCreateCallback(SceneCreateCallback cb) { m_SceneCreateCallback = std::move(cb); }
        void SetSceneDeleteCallback(SceneDeleteCallback cb) { m_SceneDeleteCallback = std::move(cb); }
        void SetDropAssetCallback(DropAssetCallback cb) { m_DropAssetCallback = std::move(cb); }

        // ── 视口尺寸变化回调 ──
        using ResizeCallback = std::function<void(int32 width, int32 height)>;
        void SetResizeCallback(ResizeCallback cb) { m_ResizeCallback = std::move(cb); }

        // ── 视口叠加层回调 ──
        using LayerDrawCallback = std::function<void(EditorCamera* camera, float32 dt)>;
        void SetLayerDrawCallback(LayerDrawCallback cb) { m_LayerDrawCallback = std::move(cb); }

    private:
        void InitMRTFramebuffer();
        void Render3DScene();
        void DestroyFBO();

        // ── ImGui 管线阶段 ──
        void UpdateBoundsAndFocus();       // 1. 边界与尺寸
        void Draw3DImage();                // 2. FBO 纹理显示
        void DrawOverlay();                // 3. 工具栏仪表盘
        void DrawGizmos();                 // 4. ImGuizmo（依工具模式）
        void HandleCameraNavigation();     // 5. 手型/中键平移
        void HandleDragAndDrop();          // 6. 资源拖拽
        void HandleMousePicking();         // 7. 像素级拾取
        void HandleViewportInteraction();  // 8. 右键菜单 + 快捷键
        void FocusOnSelected();            // 9. F 键聚焦

        // ── 配置 ──
        ViewportConfig m_Config;
        ViewportStyle  m_Style;

        // ── 工具状态 ──
        ViewportTool m_CurrentTool = ViewportTool::Select;

        // ── 窗口状态 ──
        float32 m_Width  = 0.0f;
        float32 m_Height = 0.0f;
        bool    m_Hovered = false;
        bool    m_Focused = false;
        ImVec2  m_ViewportBounds[2];

        // ── 外部依赖 ──
        IRenderContext*      m_RenderContext = nullptr;
        IGraphicsFactory*    m_Factory = nullptr;
        GladGLContext*       m_GL = nullptr;

        // ── MRT FBO ──
        uint32 m_FBO          = 0;
        uint32 m_ColorTexture = 0;
        uint32 m_IdTexture    = 0;
        uint32 m_DepthBuffer  = 0;
        bool   m_NeedsFBOUpdate = false;

        // ── 编辑器系统 ──
        std::unique_ptr<EditorCamera> m_Camera;
        EditorOverlay                m_Overlay;
        std::weak_ptr<GameObject>    m_SelectedObject;

        // ── 拾取 hover 预读 ──
        int32 m_HoveredEntityId = -1;

        // ── 回调 ──
        ResizeCallback       m_ResizeCallback;
        SceneRenderCallback  m_SceneRenderCallback;
        PickCallback         m_PickCallback;
        SceneCreateCallback  m_SceneCreateCallback;
        SceneDeleteCallback  m_SceneDeleteCallback;
        DropAssetCallback    m_DropAssetCallback;
        LayerDrawCallback    m_LayerDrawCallback;

        // ── 选择模式（Object/Vertex/Edge/Face） ──
        SelectionMode m_SelectionMode = SelectionMode::Object;

        // ── Gizmo 状态 ──
        bool  m_GizmoLocal  = false;
        bool  m_SnapEnabled = false;
        float m_SnapValue   = 0.5f;
    };

} // namespace Engine