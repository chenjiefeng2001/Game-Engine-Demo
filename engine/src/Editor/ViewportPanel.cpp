#include "Engine/Editor/ViewportPanel.h"
#include "Engine/Core/IRenderContext.h"
#include <imgui.h>

namespace Engine {

    void ViewportPanel::OnImGui() {
        // 创建视口窗口（Docking 可停靠）
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Viewport", nullptr,
                     ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse);

        // 获取当前视口尺寸
        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        m_Hovered = ImGui::IsWindowHovered();
        m_Focused = ImGui::IsWindowFocused();

        // 获取视口在屏幕上的偏移（用于鼠标坐标转换）
        ImVec2 viewPos = ImGui::GetCursorScreenPos();
        m_ViewX = viewPos.x;
        m_ViewY = viewPos.y;

        // 检测尺寸变化
        float32 newWidth  = viewportSize.x;
        float32 newHeight = viewportSize.y;
        if ((newWidth != m_Width || newHeight != m_Height) &&
            newWidth > 0.0f && newHeight > 0.0f) {
            m_Width  = newWidth;
            m_Height = newHeight;
            if (m_ResizeCallback) {
                m_ResizeCallback(static_cast<int32>(m_Width),
                                 static_cast<int32>(m_Height));
            }
        }

        // 渲染占位纹理（由外部 Engine 驱动将渲染结果绑定到此区域）
        // 使用 ImGui::Image() 传递 OpenGL 纹理 ID 来显示渲染结果
        // 示例：ImGui::Image((ImTextureID)(uint64)m_FramebufferTextureID,
        //                     ImVec2(m_Width, m_Height), ImVec2(0,1), ImVec2(1,0));

        // 如果还没有帧缓冲纹理，显示一个提示背景
        if (m_Width > 0.0f && m_Height > 0.0f) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 posMin = ImGui::GetCursorScreenPos();
            ImVec2 posMax = ImVec2(posMin.x + m_Width, posMin.y + m_Height);
            drawList->AddRectFilled(posMin, posMax,
                                    IM_COL32(30, 30, 35, 255));
            drawList->AddText(posMin, IM_COL32(100, 100, 110, 255),
                              "Viewport - Game Output");
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

} // namespace Engine
