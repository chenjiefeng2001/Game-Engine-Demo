#include "Engine/UIManager.h"
#include "Engine/Core/Input.h"

// GLAD 必须在任何可能包含 gl.h 的头文件之前包含
#include <glad/gl.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <iostream>
#include <string>
#include <cstdio>

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

        // ── 2. 设置样式 ──
        ImGui::StyleColorsDark();

  // ── 3. 加载中文字体 ──
        // ImGui 默认字体不包含 CJK 字符，直接加载系统字体作为唯一字体
        {
            // 注意：不要调用 AddFontDefault()，否则 ProggyClean 会占据 Fonts[0]
            // 成为默认字体，后续加载的微软雅黑虽包含 CJK 但不会被使用。
            // 直接加载中文字体即可，它会同时包含 ASCII 和 CJK 字符。
            const char* chineseFontPaths[] = {
                "C:/Windows/Fonts/msyh.ttc",     
                "C:/Windows/Fonts/msyhbd.ttc",   
                "C:/Windows/Fonts/simsun.ttc",   
                "C:/Windows/Fonts/simhei.ttf",    
                "C:/Windows/Fonts/deng.ttf",      
            };
            bool fontLoaded = false;
            for (const char* fontPath : chineseFontPaths)
            {
                FILE* f = nullptr;
                if (fopen_s(&f, fontPath, "r") == 0 && f)
                {
                    fclose(f);
                    // 使用完整 CJK 字库（~21000 字），确保所有汉字都能正确显示
                    ImFont* font = io.Fonts->AddFontFromFileTTF(
                        fontPath, 16.0f, nullptr,
                        io.Fonts->GetGlyphRangesChineseFull()
                    );
                    if (font)
                    {
                        std::cout << "[UIManager] Loaded Chinese font: " << fontPath << std::endl;
                        fontLoaded = true;
                        break;
                    }
                }
            }

            if (!fontLoaded)
            {
                std::cout << "[UIManager] No Chinese font found, using default font" << std::endl;
            }
            // 注意：不要手动调用 io.Fonts->Build()，
            // ImGui_ImplOpenGL3_Init() 会在设置 backend flag 后自动调用
        }

        // ── 4. 初始化 GLFW + OpenGL3 后端 ──
        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 460");

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

} // namespace Engine
