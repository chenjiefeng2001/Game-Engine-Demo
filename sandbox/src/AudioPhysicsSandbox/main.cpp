/**
 * @file main.cpp
 * @brief 音频物理沙盒 — 入口点
 *
 * 功能概述：
 *   - 程序化生成的背景音乐循环播放
 *   - 数字键 1~5 触发不同音效（钢琴/鼓/贝斯/扫弦/镲）
 *   - 鼠标左键拖动物体，碰撞时根据材质播放冲击音效
 *   - 控制台每秒输出 FPS、碰撞次数、活跃音效数、各资源计数
 *   - 退出时打印资源释放统计，验证无泄漏
 */

#include <Engine/OpenGL/OpenGLGraphicsFactory.h>
#include "AudioPhysicsSandboxApp.h"
#include <clocale>
#ifdef _WIN32
#include <windows.h>
#endif

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::setlocale(LC_ALL, "en_US.UTF-8");

    Engine::OpenGLGraphicsFactory factory;
    Engine::AudioPhysicsSandboxApp app(factory);
    app.Run();
    return 0;
}
