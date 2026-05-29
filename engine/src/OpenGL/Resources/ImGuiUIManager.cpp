#include "ImGuiUIManager.h"
#include "Engine/Core/Input.h"

// GLAD 必须在任何可能包含 gl.h 的头文件之前包含
#include <glad/gl.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

#include "Engine/Core/Log.h"

namespace {
    Engine::Logger s_Log("ImGuiUIManager");
}
#include <cstdio>
#include <cmath>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Engine {

    // ============================================================
    // 构造 / 析构
    // ============================================================

    ImGuiUIManager::~ImGuiUIManager() {
        if (m_Initialized) {
            Shutdown();
        }
    }

    // ============================================================
    // 初始化
    // ============================================================

    bool ImGuiUIManager::Init(void* nativeWindow, void* apiContext) {
        if (m_Initialized) {
            s_Log.Error("Already initialized");
            return true;
        }

        GLFWwindow* window = static_cast<GLFWwindow*>(nativeWindow);
        if (!window) {
            s_Log.Error("Invalid window handle");
            return false;
        }
        (void)apiContext;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ApplyEngineStyle(1.0f);

        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 460");

        LoadFont(nullptr, 16.0f);

        m_Initialized = true;
        s_Log.Info("Dear ImGui initialized");
        return true;
    }

    void ImGuiUIManager::Shutdown() {
        if (!m_Initialized) return;

        s_Log.Info("Shutting down...");
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        m_Initialized = false;
        s_Log.Info("Shutdown complete");
    }

    // ============================================================
    // 帧生命周期
    // ============================================================

    void ImGuiUIManager::Begin() {
        if (!m_Initialized) return;

        if (m_PendingScale > 0.0f) {
            float pending = m_PendingScale;
            m_PendingScale = -1.0f;
            float newSize = std::roundf(16.0f * pending);
            LoadFont(m_FontPath.empty() ? nullptr : m_FontPath.c_str(), newSize);
            s_Log.Info("Scale set to {} (font: {}px)", pending, newSize);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        Input::SetBlockInput(
            m_Visible && ImGui::GetIO().WantCaptureMouse,
            m_Visible && ImGui::GetIO().WantCaptureKeyboard
        );
    }

    void ImGuiUIManager::End() {
        if (!m_Initialized) return;

        ImGui::Render();
        if (m_Visible) {
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
    }

    // ============================================================
    // 输入查询
    // ============================================================

    bool ImGuiUIManager::WantCaptureMouse() const {
        return m_Visible && ImGui::GetIO().WantCaptureMouse;
    }

    bool ImGuiUIManager::WantCaptureKeyboard() const {
        return m_Visible && ImGui::GetIO().WantCaptureKeyboard;
    }

    // ============================================================
    // 缩放
    // ============================================================

    void ImGuiUIManager::SetScale(float scale) {
        if (scale < 0.5f) scale = 0.5f;
        if (scale > 3.0f) scale = 3.0f;
        m_Scale = scale;

        if (m_Initialized) {
            m_PendingScale = scale;
        } else {
            ApplyEngineStyle(scale);
        }
    }

    // ============================================================
    // 字体
    //
    // 仅将字体注册到 ImGui Atlas。不要调用 Build()/GetTexData*()——
    // 交由后端在首次渲染时处理纹理上传。这样可以避免在后端纹理能力
    // 尚未一致设置时触发 ImGui 的断言。
    // ============================================================

    void ImGuiUIManager::LoadFont(const char* filePath, float sizePixels) {
        ImGuiIO& io = ImGui::GetIO();

        if (filePath && filePath[0] != '\0') {
            // 添加自定义 TTF 字体
            io.Fonts->AddFontFromFileTTF(filePath, sizePixels);
        } else {
            // 添加默认内嵌字体
            io.Fonts->AddFontDefault();
        }

        // 重要：不要主动调用 io.Fonts->Build() / GetTexDataAsRGBA32()
        // 后端会在首次 ImGui_ImplOpenGL3_RenderDrawData() 时自动上传字体纹理
    }

    // ============================================================
    // 引擎暗色主题
    // ============================================================

    void ImGuiUIManager::ApplyEngineStyle(float scale) {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        colors[ImGuiCol_Text]                   = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
        colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        colors[ImGuiCol_WindowBg]               = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
        colors[ImGuiCol_ChildBg]                = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
        colors[ImGuiCol_PopupBg]                = ImVec4(0.08f, 0.08f, 0.10f, 0.94f);
        colors[ImGuiCol_Border]                 = ImVec4(0.25f, 0.25f, 0.28f, 0.50f);
        colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg]                = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.22f, 0.22f, 0.25f, 1.00f);
        colors[ImGuiCol_FrameBgActive]          = ImVec4(0.28f, 0.28f, 0.32f, 1.00f);
        colors[ImGuiCol_TitleBg]                = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
        colors[ImGuiCol_TitleBgActive]          = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.08f, 0.08f, 0.10f, 0.75f);
        colors[ImGuiCol_MenuBarBg]              = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.30f, 0.30f, 0.34f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.40f, 0.40f, 0.45f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.50f, 0.50f, 0.57f, 1.00f);
        colors[ImGuiCol_CheckMark]              = ImVec4(0.90f, 0.70f, 0.10f, 1.00f);
        colors[ImGuiCol_SliderGrab]             = ImVec4(0.90f, 0.70f, 0.10f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]       = ImVec4(1.00f, 0.80f, 0.20f, 1.00f);
        colors[ImGuiCol_Button]                 = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
        colors[ImGuiCol_ButtonHovered]          = ImVec4(0.30f, 0.30f, 0.35f, 1.00f);
        colors[ImGuiCol_ButtonActive]           = ImVec4(0.40f, 0.40f, 0.45f, 1.00f);
        colors[ImGuiCol_Header]                 = ImVec4(0.20f, 0.20f, 0.25f, 1.00f);
        colors[ImGuiCol_HeaderHovered]          = ImVec4(0.30f, 0.30f, 0.38f, 1.00f);
        colors[ImGuiCol_HeaderActive]           = ImVec4(0.35f, 0.35f, 0.45f, 1.00f);
        colors[ImGuiCol_Separator]              = ImVec4(0.25f, 0.25f, 0.28f, 1.00f);
        colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.35f, 0.35f, 0.40f, 1.00f);
        colors[ImGuiCol_SeparatorActive]        = ImVec4(0.45f, 0.45f, 0.50f, 1.00f);
        colors[ImGuiCol_ResizeGrip]             = ImVec4(0.30f, 0.30f, 0.34f, 1.00f);
        colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.40f, 0.40f, 0.45f, 1.00f);
        colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.50f, 0.50f, 0.57f, 1.00f);
        colors[ImGuiCol_Tab]                    = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
        colors[ImGuiCol_TabHovered]             = ImVec4(0.28f, 0.28f, 0.32f, 1.00f);
        colors[ImGuiCol_TabActive]              = ImVec4(0.20f, 0.22f, 0.28f, 1.00f);
        colors[ImGuiCol_TabUnfocused]           = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.14f, 0.14f, 0.18f, 1.00f);
#ifdef ImGuiCol_DockingPreview
        colors[ImGuiCol_DockingPreview]         = ImVec4(0.30f, 0.40f, 0.60f, 0.70f);
        colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
#endif
        colors[ImGuiCol_PlotLines]              = ImVec4(0.60f, 0.60f, 0.70f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.70f, 0.10f, 1.00f);
        colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.10f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.80f, 0.20f, 1.00f);
        colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
        colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.25f, 0.25f, 0.28f, 1.00f);
        colors[ImGuiCol_TableBorderLight]       = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
        colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
        colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.30f, 0.40f, 0.60f, 0.50f);
        colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
        colors[ImGuiCol_NavHighlight]           = ImVec4(0.45f, 0.55f, 0.80f, 0.80f);
        colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
        colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);

        style.WindowPadding         = ImVec2(8.0f, 8.0f);
        style.FramePadding          = ImVec2(6.0f, 4.0f);
        style.ItemSpacing           = ImVec2(6.0f, 4.0f);
        style.ItemInnerSpacing      = ImVec2(4.0f, 4.0f);
        style.IndentSpacing         = 20.0f;
        style.ScrollbarSize         = 14.0f;
        style.GrabMinSize           = 10.0f;
        style.WindowRounding        = 4.0f;
        style.ChildRounding         = 4.0f;
        style.FrameRounding         = 3.0f;
        style.PopupRounding         = 4.0f;
        style.ScrollbarRounding     = 4.0f;
        style.GrabRounding          = 3.0f;
        style.TabRounding           = 4.0f;
        style.WindowTitleAlign      = ImVec2(0.02f, 0.50f);

        style.ScaleAllSizes(scale);
    }

} // namespace Engine
