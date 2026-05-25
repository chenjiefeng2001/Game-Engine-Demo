/**
 * @file main.cpp
 * @brief ImGuiDemo — 演示 Dear ImGui 集成
 *
 * 功能：
 *   - 显示 ImGui 内置的示例窗口（ShowDemoWindow）
 *   - 显示引擎信息面板（帧率、窗口尺寸）
 */

#include <Engine/OpenGL/OpenGLGraphicsFactory.h>
#include "ImGuiDemoApp.h"

int main() {
    Engine::OpenGLGraphicsFactory factory;
    Engine::ImGuiDemoApp app(factory);
    app.Run();
    return 0;
}
