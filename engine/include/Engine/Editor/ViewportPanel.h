#pragma once

/**
 * @file ViewportPanel.h
 * @brief 视口面板 — 可多次实例化的独立 3D 视口
 */

#include "Engine/Types.h"
#include "Engine/Editor/EditorCamera.h"
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

        void InitResources(IGraphicsFactory* factory,
                           const std::string& vertPath,
                           const std::string& fragPath);

        void SetRenderContext(IRenderContext* ctx) { m_RenderContext = ctx; }
        void SetGLContext(GladGLContext* gl) { m_GL = gl; }

        void SetViewMode(ViewMode mode) { m_ViewMode = mode; }
        ViewMode GetViewMode() const { return m_ViewMode; }

        void OnUpdate(float32 dt, bool isFocusedViewport = false);
        void OnImGui();

        const std::string& GetName() const { return m_Name; }
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

        std::string m_Name;
        float32 m_Width  = 0.0f;
        float32 m_Height = 0.0f;
        float32 m_ViewX  = 0.0f;
        float32 m_ViewY  = 0.0f;
        bool    m_Hovered = false;
        bool    m_Focused = false;
        ViewMode m_ViewMode = ViewMode::Normal;

        IRenderContext*      m_RenderContext = nullptr;
        IGraphicsFactory*    m_Factory = nullptr;
        GladGLContext*       m_GL = nullptr;
        std::unique_ptr<OpenGLFramebuffer> m_FBO;

        std::unique_ptr<EditorCamera> m_Camera;

        std::shared_ptr<Shader>     m_3DShader;
        std::shared_ptr<VertexArray> m_CubeVAO;
        std::shared_ptr<VertexArray> m_GridVAO;
        uint32 m_GridVertexCount = 0;

        ResizeCallback m_ResizeCallback;
    };

} // namespace Engine