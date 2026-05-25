#include "Engine/UIManager.h"
#include "Engine/Core/Input.h"
#include "Engine/Core/Resources/ResourceManager.h"
#include "Engine/Core/Resources/Font.h"

// GLAD 必须在任何可能包含 gl.h 的头文件之前包含
#include <glad/gl.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <iostream>
#include <filesystem>
#include <string>
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

    UIManager* UIManager::s_Instance = nullptr;
    std::unique_ptr<UIManager> UIManager::s_InstanceOwner = nullptr;

    // ============================================================
    // 单例生命周期
    // ============================================================

    void UIManager::Init(GLFWwindow* window, GladGLContext& glContext)
    {
        if (s_Instance)
        {
            std::cerr << "[UIManager] Already initialized" << std::endl;
            return;
        }

        if (!window)
        {
            std::cerr << "[UIManager] Invalid GLFW window handle" << std::endl;
            return;
        }

        auto instance = std::unique_ptr<UIManager>(new UIManager());

        // ── 1. 创建 ImGui 上下文 ──
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

// ── 2. 应用引擎暗色主题 ──
  instance->ApplyEngineStyle(1.0f);

  // Note: initialize GLFW + OpenGL3 backend before loading fonts so
  // ImGuiBackendFlags_RendererHasTextures is set and LoadFont detects
  // whether Build() must be called. Loading fonts before backend init
  // may cause a legacy Build path then backend switches to new path,
  // triggering assertions in ImGui (preloaded glyphs vs renderer textures).

        // ── 3. 初始化 GLFW + OpenGL3 后端 ──
        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 460");

        // ── 4. 加载字体（在后端初始化之后） ──
        instance->LoadFont(nullptr, 16.0f);

        instance->m_Initialized = true;

        s_InstanceOwner = std::move(instance);
        s_Instance = s_InstanceOwner.get();

        std::cout << "[UIManager] Dear ImGui initialized successfully" << std::endl;
    }

    void UIManager::Shutdown()
    {
        if (!s_Instance || !s_Instance->m_Initialized)
            return;

        std::cout << "[UIManager] Shutting down Dear ImGui..." << std::endl;

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        s_Instance->m_Initialized = false;
        s_Instance = nullptr;
        s_InstanceOwner.reset();

        std::cout << "[UIManager] Dear ImGui shutdown complete" << std::endl;
    }

    UIManager::~UIManager()
    {
        if (m_Initialized)
        {
            // 不应在此处调用 Shutdown，因为 ImGui 全局上下文已经可能被销毁
            // 使用 Shutdown() 静态方法进行有序清理
        }
    }

    // ============================================================
    // 帧生命周期
    // ============================================================

    void UIManager::Begin()
    {
        if (!m_Initialized) return;

        // 在有延迟缩放请求时，在 NewFrame 之前重载字体（帧间安全）
        if (m_PendingScale > 0.0f)
        {
            float pending = m_PendingScale;
            m_PendingScale = -1.0f;

            float newSize = std::roundf(16.0f * pending);
            LoadFont(m_FontPath.empty() ? nullptr : m_FontPath.c_str(), newSize);

            std::cout << "[UIManager] Scale set to " << pending
                      << " (font: " << newSize << "px)" << std::endl;
        }

        // 按 ImGui 要求的顺序开始新帧
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // 根据可见状态和 ImGui 捕获需求设置输入抢占
        Input::SetBlockInput(
            m_Visible && ImGui::GetIO().WantCaptureMouse,
            m_Visible && ImGui::GetIO().WantCaptureKeyboard
        );
    }

    void UIManager::End()
    {
        if (!m_Initialized) return;

        ImGui::Render();

        // 仅在可见时实际绘制，隐藏时维护内部状态但不输出
        if (m_Visible)
        {
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
    }

    // ============================================================
    // 状态查询
    // ============================================================

    bool UIManager::WantCaptureMouse() const
    {
        return m_Initialized && m_Visible && ImGui::GetIO().WantCaptureMouse;
    }

    bool UIManager::WantCaptureKeyboard() const
    {
        return m_Initialized && m_Visible && ImGui::GetIO().WantCaptureKeyboard;
    }

    // ============================================================
    // 全局缩放
    // ============================================================

    void UIManager::SetScale(float scale)
    {
        if (scale < 0.5f) scale = 0.5f;
        if (scale > 3.0f) scale = 3.0f;

        m_Scale = scale;
        if (!m_Initialized) return;

        // 立即更新样式（安全），但延迟字体重载到下一帧 Begin()
        ApplyEngineStyle(scale);
        m_PendingScale = scale;
    }

    // ============================================================
    // 字体管理（支持热重载）
    // ============================================================

   bool UIManager::LoadFont(const char* path, float size)
{
    auto& io = ImGui::GetIO();

    // 如果未指定路径，搜索系统中文 TTF
    if (!path || path[0] == '\0')
    {
        const char* fallbackPaths[] = {
            "C:/Windows/Fonts/msyh.ttc",
            "C:/Windows/Fonts/msyhbd.ttc",
            "C:/Windows/Fonts/simsun.ttc",
            "C:/Windows/Fonts/simhei.ttf",
            "C:/Windows/Fonts/deng.ttf",
        };

        for (auto fp : fallbackPaths)
        {
            if (std::filesystem::exists(fp))
            {
                path = fp;
                break;
            }
        }

        if (!path || path[0] == '\0')
        {
            io.Fonts->AddFontDefault();
            std::cout << "[UIManager] No font found, using default" << std::endl;
            io.Fonts->Build();
            return true;
        }
    }

    // ==========================================
    // 修复 1：确保 fontSize 变量被正确声明和赋值
    // ==========================================
    float fontSize = (size > 0.0f) ? size : m_FontSize;

    // 清空旧字体，加载新字体
    io.Fonts->Clear();
    ImFont* font = io.Fonts->AddFontFromFileTTF(
        path, fontSize, nullptr,
        io.Fonts->GetGlyphRangesChineseFull()
    );

    if (!font)
    {
        io.Fonts->AddFontDefault();
        std::cerr << "[UIManager] Failed to load font: " << path << std::endl;
        io.Fonts->Build();
        return false;
    }

    m_FontPath = path;
    m_FontSize = fontSize;
    std::cout << "[UIManager] Font loaded: " << path
              << " (" << fontSize << "px)" << std::endl;

    // 始终调用 Build() 生成像素数据
    io.Fonts->Build();

    // ==========================================
    // 修复 2：使用 ImGui 新版公开的 Device Objects 接口刷新纹理
    // ==========================================
    if (m_Initialized) 
    {
        // 销毁旧的 GPU 纹理和渲染对象
        ImGui_ImplOpenGL3_DestroyDeviceObjects();
        // 重建新的 GPU 纹理和渲染对象（会读取刚刚 Build 好的最新字体）
        ImGui_ImplOpenGL3_CreateDeviceObjects();
    }

    // ── 注册字体到统一资源管理器 ──
    if (auto* rm = ResourceManager::Get())
    {
        auto fontRes = std::make_shared<Font>(m_FontPath, fontSize);
        fontRes->SetImFont(font);
        fontRes->SetState(ResourceState::Loaded);
        rm->Register(fontRes);
    }

    return true;
}

    // ============================================================
    // 引擎暗色主题
    // ============================================================

    void UIManager::ApplyEngineStyle(float scale)
    {
        auto& style = ImGui::GetStyle();

        // ── 每次从默认值重新开始，用目标 scale 调用一次 ScaleAllSizes ──
        // 绝不用相对比例反复调用（ImTrunc 截断会导致小尺寸变 0）
        style = ImGuiStyle();
        style.ScaleAllSizes(scale);

        // ── 颜色主题：深色引擎风格 ──
        auto& colors = style.Colors;

        colors[ImGuiCol_WindowBg]          = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
        colors[ImGuiCol_ChildBg]           = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
        colors[ImGuiCol_PopupBg]           = ImVec4(0.10f, 0.10f, 0.12f, 0.94f);
        colors[ImGuiCol_Border]            = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
        colors[ImGuiCol_BorderShadow]      = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        colors[ImGuiCol_TitleBg]           = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
        colors[ImGuiCol_TitleBgActive]     = ImVec4(0.12f, 0.12f, 0.16f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]  = ImVec4(0.06f, 0.06f, 0.08f, 0.70f);
        colors[ImGuiCol_Tab]               = ImVec4(0.12f, 0.12f, 0.16f, 1.00f);
        colors[ImGuiCol_TabHovered]        = ImVec4(0.20f, 0.20f, 0.28f, 1.00f);
        colors[ImGuiCol_TabActive]         = ImVec4(0.25f, 0.25f, 0.35f, 1.00f);
        colors[ImGuiCol_TabUnfocused]      = ImVec4(0.10f, 0.10f, 0.14f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive]= ImVec4(0.15f, 0.15f, 0.22f, 1.00f);
        colors[ImGuiCol_MenuBarBg]         = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);

        colors[ImGuiCol_Button]            = ImVec4(0.20f, 0.20f, 0.28f, 1.00f);
        colors[ImGuiCol_ButtonHovered]     = ImVec4(0.28f, 0.28f, 0.38f, 1.00f);
        colors[ImGuiCol_ButtonActive]      = ImVec4(0.35f, 0.35f, 0.48f, 1.00f);

        colors[ImGuiCol_Header]            = ImVec4(0.20f, 0.20f, 0.30f, 1.00f);
        colors[ImGuiCol_HeaderHovered]     = ImVec4(0.28f, 0.28f, 0.40f, 1.00f);
        colors[ImGuiCol_HeaderActive]      = ImVec4(0.35f, 0.35f, 0.50f, 1.00f);
        colors[ImGuiCol_Separator]         = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
        colors[ImGuiCol_SeparatorHovered]  = ImVec4(0.30f, 0.30f, 0.40f, 1.00f);
        colors[ImGuiCol_SeparatorActive]   = ImVec4(0.40f, 0.40f, 0.55f, 1.00f);
        colors[ImGuiCol_CheckMark]         = ImVec4(0.60f, 0.60f, 1.00f, 1.00f);
        colors[ImGuiCol_NavHighlight]      = ImVec4(0.50f, 0.50f, 1.00f, 1.00f);
        colors[ImGuiCol_NavWindowingHighlight]= ImVec4(1.00f, 1.00f, 1.00f, 0.70f);

        colors[ImGuiCol_ScrollbarBg]       = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab]     = ImVec4(0.25f, 0.25f, 0.35f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered]= ImVec4(0.35f, 0.35f, 0.48f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]= ImVec4(0.45f, 0.45f, 0.60f, 1.00f);
        colors[ImGuiCol_SliderGrab]        = ImVec4(0.40f, 0.40f, 0.60f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]  = ImVec4(0.55f, 0.55f, 0.80f, 1.00f);
        colors[ImGuiCol_FrameBg]           = ImVec4(0.12f, 0.12f, 0.16f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]    = ImVec4(0.18f, 0.18f, 0.24f, 1.00f);
        colors[ImGuiCol_FrameBgActive]     = ImVec4(0.22f, 0.22f, 0.30f, 1.00f);
        colors[ImGuiCol_Text]              = ImVec4(0.85f, 0.85f, 0.90f, 1.00f);
        colors[ImGuiCol_TextDisabled]      = ImVec4(0.45f, 0.45f, 0.50f, 1.00f);
        colors[ImGuiCol_TextSelectedBg]    = ImVec4(0.35f, 0.35f, 0.60f, 1.00f);
        colors[ImGuiCol_PlotLines]         = ImVec4(0.60f, 0.60f, 1.00f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered]  = ImVec4(0.80f, 0.80f, 1.00f, 1.00f);
        colors[ImGuiCol_PlotHistogram]     = ImVec4(0.60f, 0.60f, 1.00f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered]= ImVec4(0.80f, 0.80f, 1.00f, 1.00f);
        colors[ImGuiCol_TableBorderLight]  = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
        colors[ImGuiCol_TableBorderStrong] = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
        colors[ImGuiCol_DragDropTarget]    = ImVec4(0.50f, 0.50f, 1.00f, 0.50f);
        colors[ImGuiCol_ResizeGrip]        = ImVec4(0.25f, 0.25f, 0.35f, 0.50f);
        colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.40f, 0.40f, 0.55f, 0.60f);
        colors[ImGuiCol_ResizeGripActive]  = ImVec4(0.50f, 0.50f, 0.70f, 0.80f);
        colors[ImGuiCol_ModalWindowDimBg]  = ImVec4(0.00f, 0.00f, 0.00f, 0.55f);
    }

} // namespace Engine
