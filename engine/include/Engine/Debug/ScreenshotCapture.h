#pragma once

/**
 * @file ScreenshotCapture.h
 * @brief 帧缓冲区预捕获 — 崩溃时无需调用 OpenGL 即可获得截屏。
 *
 * 设计原则：
 *   正常运行中每帧由 Application 将渲染结果喂入 CaptureFrame()，
 *   崩溃时 CrashHandler 调用 SaveLastFrame() 写入文件。
 *   避免在 VEH 中调用 OpenGL（上下文可能已损坏）。
 *
 * 使用方式：
 * @code
 *   // 每帧渲染完成后（Application::Run 中）
 *   int vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
 *   std::vector<uint8_t> pixels(vp[2] * vp[3] * 3);
 *   glReadPixels(0, 0, vp[2], vp[3], GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
 *   ScreenshotCapture::CaptureFrame(vp[2], vp[3], pixels.data());
 *
 *   // 崩溃时（CrashHandler 自动调用）
 *   ScreenshotCapture::SaveLastFrame("crash_xxx.png");
 * @endcode
 */

#include "Engine/Types.h"
#include <string>
#include <vector>
#include <mutex>

namespace Engine {

    class ScreenshotCapture {
    public:
        // ── 写入（每帧由 Application 调用） ──

        /**
         * @brief 将当前帧像素保存到环形缓冲区（最新 1 帧）
         * @param width    帧宽度（像素）
         * @param height   帧高度（像素）
         * @param pixels   RGB888 像素数据（width × height × 3 字节）
         * @param channels 通道数（3=RGB, 4=RGBA）
         */
        static void CaptureFrame(int width, int height,
                                 const uint8_t* pixels, int channels = 3);

        // ── 读取（崩溃时由 CrashHandler 调用） ──

        /**
         * @brief 将最新一帧保存为 TGA 文件
         * @param path  输出路径（不含扩展名，自动加 .tga）
         * @return 是否成功
         */
        static bool SaveLastFrame(const std::string& path);

        /** @brief 是否已有捕获的帧 */
        static bool HasFrame();

        /** @brief 清空缓冲区 */
        static void Clear();

    private:
        struct Frame {
            int width  = 0;
            int height = 0;
            std::vector<uint8_t> pixels;
        };

        static std::mutex&      Mutex();
        static Frame&           LastFrame();
    };

} // namespace Engine
