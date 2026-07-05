#pragma once

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

    enum class ViewportTool { Select, Hand, Translate, Rotate, Scale };

    class ViewportPanel {
    public:
        explicit ViewportPanel(const std::string& name);
        ~ViewportPanel();
        ViewportPanel(const ViewportPanel&) = delete;
        ViewportPanel& operator=(const ViewportPanel&) = delete;

        void InitResources(IGraphicsFactory* factory, const std::string& vertPath, const std::string& fragPath);
        void SetRenderContext(IRenderContext* ctx) { m_RenderContext = ctx; }
        void SetGLContext(GladGLContext* gl) { m_GL = gl; }
        void SetSelectedObject(std::shared_ptr<GameObject> obj) { m_SelectedObject = obj; }

        ViewportConfig&       GetConfig()       { return m_Config; }
        const ViewportConfig& GetConfig() const { return m_Config; }
        void SetViewMode(ViewMode mode) { m_Config.CurrentMode = mode; }
        ViewMode GetViewMode() const { return m_Config.CurrentMode; }
        void SetShowGrid(bool show) { m_Config.ShowGrid = show; }
        bool IsShowGrid() const { return m_Config.ShowGrid; }

        void SetTool(ViewportTool tool) { m_CurrentTool = tool; }
        ViewportTool GetTool() const { return m_CurrentTool; }
        void CycleGizmoTool();

        void SetSelectionMode(SelectionMode mode) { m_SelectionMode = mode; }
        SelectionMode GetSelectionMode() const { return m_SelectionMode; }
        void SetSnapMode(SnapMode mode);
        SnapMode GetSnapMode() const;

        void OnUpdate(float32 dt, bool isFocusedViewport = false);
        void OnImGui();
        void Cleanup();

        const std::string& GetName() const { return m_Config.Name; }
        EditorCamera& GetCamera() { return *m_Camera; }
        const EditorCamera& GetCamera() const { return *m_Camera; }
        float32 GetWidth() const  { return (float32)m_Width; }
        float32 GetHeight() const { return (float32)m_Height; }
        bool    IsHovered() const { return m_Hovered; }
        bool    IsFocused() const { return m_Focused; }

        // ── 渲染注入器回调 (3-param: viewProj, camPos, isPicking) ──
        using SceneRenderCallback = std::function<void(const float* viewProj, const float* camPos, bool isPicking)>;
        void SetSceneRenderCallback(SceneRenderCallback cb) { m_SceneRenderCallback = std::move(cb); }
        using PickCallback = std::function<void(float, float, int32)>;
        void SetPickCallback(PickCallback cb) { m_PickCallback = std::move(cb); }
        using SceneCreateCallback = std::function<void(const std::string&)>;
        using SceneDeleteCallback = std::function<void()>;
        using DropAssetCallback = std::function<void(const std::string&, const float*)>;
        void SetSceneCreateCallback(SceneCreateCallback cb) { m_SceneCreateCallback = std::move(cb); }
        void SetSceneDeleteCallback(SceneDeleteCallback cb) { m_SceneDeleteCallback = std::move(cb); }
        void SetDropAssetCallback(DropAssetCallback cb) { m_DropAssetCallback = std::move(cb); }
        using ResizeCallback = std::function<void(int32, int32)>;
        void SetResizeCallback(ResizeCallback cb) { m_ResizeCallback = std::move(cb); }
        using LayerDrawCallback = std::function<void(EditorCamera*, float32)>;
        void SetLayerDrawCallback(LayerDrawCallback cb) { m_LayerDrawCallback = std::move(cb); }
        using SceneRefreshCallback = std::function<void()>;
        void SetSceneRefreshCallback(SceneRefreshCallback cb) { m_SceneRefreshCallback = std::move(cb); }

        void MarkDirty() { m_RenderDirty = true; }

    private:
        void InitMRTFramebuffer();
        void Render3DScene();
        void HandleMousePicking(int localX, int localY);
        void HandleViewportContextMenu();
        void DrawGizmos();
        void DrawOverlay();
        void DrawViewportStats();
        void FocusOnSelected();

        ViewportConfig m_Config;
        ViewportTool m_CurrentTool = ViewportTool::Select;

        uint32 m_Width = 0, m_Height = 0;
        bool m_Hovered = false, m_Focused = false;
        ImVec2 m_ViewportBounds[2];

        IRenderContext*   m_RenderContext = nullptr;
        IGraphicsFactory* m_Factory = nullptr;
        GladGLContext*    m_GL = nullptr;

        // ★ MRT FBO (RAII)
        uint32 m_FBO_ID = 0, m_ColorTexture = 0, m_IdTexture = 0, m_DepthBuffer = 0;
        bool m_NeedsFBOUpdate = false;

        // ── ★ Dirty Rendering（按需重绘：仅 Camera/Scene 变化时渲染） ──
        bool m_RenderDirty = true;

        // ── ★ FBO Resize 防抖 (100ms) ──
        float m_ResizeTimer = 0.0f;
        uint32 m_PendingWidth = 0, m_PendingHeight = 0;

        // ── 分辨率缩放（全屏时降低内部渲染分辨率提升性能） ──
        float m_ResolutionScale = 1.0f;
        uint32 m_DisplayWidth = 0, m_DisplayHeight = 0;  // 实际显示尺寸
        uint32 m_RenderWidth = 0, m_RenderHeight = 0;     // FBO 渲染尺寸

        std::unique_ptr<EditorCamera> m_Camera;
        EditorOverlay                m_Overlay;
        std::weak_ptr<GameObject>    m_SelectedObject;

        ResizeCallback      m_ResizeCallback;
        SceneRenderCallback m_SceneRenderCallback;
        PickCallback        m_PickCallback;
        SceneCreateCallback m_SceneCreateCallback;
        SceneDeleteCallback m_SceneDeleteCallback;
        DropAssetCallback   m_DropAssetCallback;
        LayerDrawCallback   m_LayerDrawCallback;
        SceneRefreshCallback m_SceneRefreshCallback;

        SelectionMode m_SelectionMode = SelectionMode::Object;
        bool m_GizmoLocal = false, m_SnapEnabled = false;
        float m_SnapValue = 0.5f;
    };

} // namespace Engine