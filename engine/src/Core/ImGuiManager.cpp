#include "Engine/ImGuiManager.h"

// GLAD 必须在任何可能包含 gl.h 的头文件之前包含
#include <glad/gl.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <iostream>

namespace Engine {

    ImGuiManager::~ImGuiManager()
    {
        if (m_Initialized)
        {
            Shutdown();
        }
    }

    bool ImGuiManager::Init(GLFWwindow* window, GladGLContext& glContext)
    {
        if (m_Initialized)
        {
            std::cerr << "[ImGuiManager] Already initialized" << std::endl;
            return true;
        }

        if (!window)
        {
            std::cerr << "[ImGuiManager] Invalid GLFW window handle" << std::endl;
            return false;
        }

        // ── 1. 创建 ImGui 上下文 ──
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // 键盘导航

        // ── 2. 设置样式 ──
        ImGui::StyleColorsDark();
        // ImGui::StyleColorsClassic();

        // ── 3. 初始化后端 ──
        // GLFW + OpenGL3
        ImGui_ImplGlfw_InitForOpenGL(window, true);    // true = 安装回调
        ImGui_ImplOpenGL3_Init("#version 460");         // 匹配 OpenGL 4.6

        std::cout << "[ImGuiManager] Dear ImGui initialized successfully" << std::endl;

        m_Initialized = true;
        return true;
    }

    void ImGuiManager::NewFrame()
    {
        if (!m_Initialized) return;

        // ImGui 规定顺序：OpenGL3 → GLFW → ImGui
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void ImGuiManager::Render()
    {
        if (!m_Initialized) return;

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void ImGuiManager::Shutdown()
    {
        if (!m_Initialized) return;

        std::cout << "[ImGuiManager] Shutting down Dear ImGui..." << std::endl;

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        m_Initialized = false;

        std::cout << "[ImGuiManager] Dear ImGui shutdown complete" << std::endl;
    }

} // namespace Engine
