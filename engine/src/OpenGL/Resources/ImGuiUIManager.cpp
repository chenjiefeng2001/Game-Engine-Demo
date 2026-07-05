#include "ImGuiUIManager.h"
#include "Engine/Core/Input.h"
#include "Engine/OpenGL/OpenGLContext.h"

// GLAD 必须在任何可能包含 gl.h 的头文件之前包含
#include <glad/gl.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

#include "Engine/Core/Log.h"

namespace {
    Engine::Logger s_Log("ImGuiUIManager");
    
    // 供 Renderer_RenderWindow 回调使用的 GladGLContext 指针
    // 在 Init() 中设置，用于子视口渲染时访问 OpenGL 函数
    GladGLContext* s_ViewportGL = nullptr;
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
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

        ApplyEngineStyle(1.0f);

        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 460");

        // ── 自定义 Viewport 渲染回调 ──
        // 替换默认的 Renderer_RenderWindow，确保每个子视口在渲染前都重置 2D 状态。
        // 默认回调 (imgui_impl_opengl3.cpp:1119) 只做 glClear(GL_COLOR_BUFFER_BIT)，
        // 不清除深度缓冲、不禁用深度测试。如果子视口的上下文被 3D 状态污染，
        // UI 将被深度剔除而消失。
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            // 从 apiContext 获取 OpenGL 上下文用于多视口渲染回调
            if (auto* oglCtx = static_cast<OpenGLContext*>(apiContext)) {
                s_ViewportGL = &oglCtx->GetGL();
            }

            ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
            platform_io.Renderer_RenderWindow = [](ImGuiViewport* viewport, void*) {
                if (!s_ViewportGL) return;

                // 清除颜色 + 深度缓冲（关键：深度也必须清，否则子视口 UI 被旧数据剔除）
                if (!(viewport->Flags & ImGuiViewportFlags_NoRendererClear)) {
                    s_ViewportGL->ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                    s_ViewportGL->Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                }

                // 【关键】强制 2D 渲染状态 — 每个视口都是干净的 UI 上下文
                s_ViewportGL->Disable(GL_DEPTH_TEST);
                s_ViewportGL->DepthMask(GL_FALSE);
                s_ViewportGL->Disable(GL_CULL_FACE);
                s_ViewportGL->Enable(GL_BLEND);
                s_ViewportGL->BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                s_ViewportGL->Enable(GL_SCISSOR_TEST);
                s_ViewportGL->Disable(GL_STENCIL_TEST);
                s_ViewportGL->PolygonMode(GL_FRONT_AND_BACK, GL_FILL);

                ImGui_ImplOpenGL3_RenderDrawData(viewport->DrawData);
            };

            ImGui::GetStyle().WindowRounding = 0.0f;
            ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        LoadFont(nullptr, 16.0f);

        m_Initialized = true;
        s_Log.Info("Dear ImGui initialized");
        return true;
    }

    void ImGuiUIManager::Shutdown() {
        if (!m_Initialized) return;

        if (Log::IsInitialized()) {
            s_Log.Info("Shutting down...");
        }

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        m_Initialized = false;
        if (Log::IsInitialized()) {
            s_Log.Info("Shutdown complete");
        }
    }

    // ============================================================
    // 帧生命周期
    // ============================================================

    void ImGuiUIManager::Begin() {
        if (!m_Initialized) return;
        if (!ImGui::GetCurrentContext()) return;

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
        if (!ImGui::GetCurrentContext()) return;

        ImGui::Render();
        if (m_Visible) {
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }

        // ── 官方多视口渲染（ImGui docking 分支） ──
        // 当面板被拖出主窗口时，ImGui 自动创建辅助 GLFW 窗口并在各自的
        // OpenGL 上下文中渲染。无需自定义子窗口管理代码。
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();

            // 恢复主窗口 OpenGL 上下文
            GLFWwindow* mainWin = static_cast<GLFWwindow*>(
                ImGui::GetMainViewport()->PlatformHandle);
            if (mainWin)
                glfwMakeContextCurrent(mainWin);
        }
    }

    // ============================================================
    // 输入查询
    // ============================================================

    bool ImGuiUIManager::WantCaptureMouse() const {
        return m_Visible && ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
    }

    bool ImGuiUIManager::WantCaptureKeyboard() const {
        return m_Visible && ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard;
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
    // 优先加载支持 CJK（中文/日文/韩文）的字体以正确渲染汉字。
    // 策略：
    //   1. 若指定了自定义字体路径 → 直接加载该字体
    //   2. 否则尝试加载 Windows 系统 CJK 字体（微软雅黑）
    //   3. 若均失败 → 回退到默认内嵌字体（不支持中文）
    //
    // 重要安全说明：
    //   - 不要调用 io.Fonts->Clear()，否则会销毁当前帧还在引用的图集。
    //   - 不要直接调用 GetTexDataAsRGBA32()/Build()，后端会在首次渲染时
    //     自动处理纹理上传；提前调用可能因后端纹理能力未就绪而触发断言。
    //   - 使用 m_CjkFontAttempted 标志确保 CJK 字体只尝试加载一次。
    // ============================================================

    void ImGuiUIManager::LoadFont(const char* filePath, float sizePixels) {
        ImGuiIO& io = ImGui::GetIO();

        if (filePath && filePath[0] != '\0') {
            // ── 用户指定了自定义字体路径 ──
            if (io.Fonts->AddFontFromFileTTF(filePath, sizePixels)) {
                s_Log.Info("Loaded custom font: {} ({}px)", filePath, sizePixels);
            } else {
                s_Log.Warn("Failed to load custom font: {}, falling back to default", filePath);
                io.Fonts->AddFontDefault();
            }
            m_CjkFontAttempted = true; // 自定义字体，不再尝试 CJK
        } else if (!m_CjkFontAttempted) {
            // ── 仅首次调用时尝试自动加载 CJK 字体 ──
            m_CjkFontAttempted = true;

            // 先加载默认字体，但指定显式 SizePixels。
            // 若不设显式大小，AddFontDefault 会将字体标记为 ImFontFlags_ImplicitRefSize，
            // 导致后续 MergeMode 的 CJK 字体加载时触发断言冲突。
            {
                ImFontConfig defaultConfig;
                defaultConfig.SizePixels = sizePixels;
                io.Fonts->AddFontDefault(&defaultConfig);
            }

            // 尝试从 Windows 系统字体目录合并 CJK 字形
            const char* cjkCandidates[] = {
                "C:/Windows/Fonts/msyh.ttc",     // Microsoft YaHei
                "C:/Windows/Fonts/msyhbd.ttc",   // Microsoft YaHei Bold
                "C:/Windows/Fonts/simhei.ttf",   // SimHei
                "C:/Windows/Fonts/yahei.ttf",    // YaHei fallback
                "C:/Windows/Fonts/msjh.ttc",     // Microsoft JhengHei (繁体)
            };

            bool cjkLoaded = false;
            for (auto& candidate : cjkCandidates) {
                ImFontConfig config;
                config.MergeMode = true;
                config.SizePixels = sizePixels;  // 与默认字体保持一致
                // 仅加载 CJK 和符号字形 —— 不包含 Basic Latin / Latin-1，
                // 以便 ProggyClean 的清晰位图字体继续渲染英文。
                static const ImWchar cjkRanges[] = {
                    0x4E00, 0x9FFF,     // CJK Unified Ideographs (汉字)
                    0x3400, 0x4DBF,     // CJK Extension A (生僻字)
                    0x3000, 0x303F,     // CJK Symbols and Punctuation（。、）
                    0xFF00, 0xFFEF,     // Fullwidth Forms（！＂）
                    0x2010, 0x205F,     // General Punctuation（–—'）
                    0x2300, 0x27BF,     // Misc Technical / Symbols / Dingbats / Geometric Shapes
                                        //  (⏸ ⏩ ▶ ◀ ■ ◆ ★ etc.)
                    0,
                };
                if (io.Fonts->AddFontFromFileTTF(candidate, sizePixels, &config, cjkRanges)) {
                    s_Log.Info("Merged CJK font: {} ({}px)", candidate, sizePixels);
                    cjkLoaded = true;
                    break;
                }
            }

            if (!cjkLoaded) {
                s_Log.Warn("No CJK font found, Chinese characters may show as '?'");
            }

            // ── Font Awesome 图标字体（MergeMode 到主字体的 FreeType 渲染器中） ──
            // 注意: FontAwesome 7 提供 .otf 格式，ImGui 的 AddFontFromFileTTF 能加载 OTF。
            // 仅合并图标私有使用区字形，不覆盖拉丁字母。
            {
                // 候选路径优先级:
                //   1. assets/fonts/ (工作目录相对路径，兼容开发环境)
                //   2. third_party/fontawesome/otfs/ (子模块原始路径)
                //   3. ../ (从某些构建输出目录回溯)
                const char* faCandidates[] = {
                    "assets/fonts/fa-solid-900.otf",
                    "../assets/fonts/fa-solid-900.otf",
                    "third_party/fontawesome/otfs/Font Awesome 7 Free-Solid-900.otf",
                };

                // FontAwesome 图标的 Unicode 私有使用区范围
                // 这些码点对应 ICON_FA_* 宏的 UTF-8 编码。
                static const ImWchar faRanges[] = {
                    0xE005, 0xF8FF,  // Private Use Area
                    0,
                };

                ImFontConfig faConfig;
                faConfig.MergeMode = true;
                faConfig.PixelSnapH = true;
                faConfig.GlyphMinAdvanceX = sizePixels; // 图标等宽
                faConfig.SizePixels = sizePixels;

                bool faLoaded = false;
                for (auto& candidate : faCandidates) {
                    if (io.Fonts->AddFontFromFileTTF(candidate, sizePixels, &faConfig, faRanges)) {
                        s_Log.Info("Merged FontAwesome: {} ({}px)", candidate, sizePixels);
                        faLoaded = true;
                        break;
                    }
                }

                if (!faLoaded) {
                    s_Log.Warn("FontAwesome not found, editor icons will show as text fallback. "
                               "Run: git submodule update --init --recursive");
                }
            }
        }
        // 非首次调用且无自定义路径：不做任何事（字体已存在，避免重复添加）
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
