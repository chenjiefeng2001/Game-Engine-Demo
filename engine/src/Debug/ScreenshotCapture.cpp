#include "Engine/Debug/ScreenshotCapture.h"
#include <fstream>

namespace Engine {

// ============================================================
// 静态辅助
// ============================================================

std::mutex& ScreenshotCapture::Mutex() {
    static std::mutex m;
    return m;
}

ScreenshotCapture::Frame& ScreenshotCapture::LastFrame() {
    static Frame f;
    return f;
}

// ============================================================
// 写入
// ============================================================

void ScreenshotCapture::CaptureFrame(int width, int height,
                                     const uint8_t* pixels, int channels) {
    if (width <= 0 || height <= 0 || !pixels) return;

    std::lock_guard lock(Mutex());
    auto& frame = LastFrame();

    frame.width  = width;
    frame.height = height;

    size_t srcStride = static_cast<size_t>(width) * static_cast<size_t>(channels);
    size_t dstStride = static_cast<size_t>(width) * 3;  // 统一存 RGB

    frame.pixels.resize(dstStride * static_cast<size_t>(height));

    if (channels == 3) {
        // RGB → RGB 直接拷贝
        std::memcpy(frame.pixels.data(), pixels, dstStride * static_cast<size_t>(height));
    } else if (channels == 4) {
        // RGBA → RGB 去除 A 通道
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                size_t srcIdx = static_cast<size_t>(y) * srcStride + static_cast<size_t>(x) * 4;
                size_t dstIdx = static_cast<size_t>(y) * dstStride + static_cast<size_t>(x) * 3;
                frame.pixels[dstIdx + 0] = pixels[srcIdx + 0];
                frame.pixels[dstIdx + 1] = pixels[srcIdx + 1];
                frame.pixels[dstIdx + 2] = pixels[srcIdx + 2];
            }
        }
    }
    // 其他通道数 — 忽略
}

// ============================================================
// 读取
// ============================================================

bool ScreenshotCapture::SaveLastFrame(const std::string& path) {
    std::lock_guard lock(Mutex());
    const auto& frame = LastFrame();
    if (frame.pixels.empty()) return false;

    std::string outPath = path;
    // 确保有扩展名
    if (outPath.find('.') == std::string::npos)
        outPath += ".tga";

    // ── 写入 TGA 文件（无需外部依赖） ──
    // 注意：TGA 默认原点为左下角（bit 5 = 0），与 glReadPixels
    // 的输出一致，因此不要翻转 Y 轴。
    std::ofstream f(outPath, std::ios::binary);
    if (!f) return false;

    int w = frame.width, h = frame.height;
    size_t stride = static_cast<size_t>(w) * 3;

    uint8_t tgaHeader[18] = {};
    tgaHeader[2]  = 2;          // 未压缩 RGB
    tgaHeader[12] = w & 0xFF;
    tgaHeader[13] = (w >> 8) & 0xFF;
    tgaHeader[14] = h & 0xFF;
    tgaHeader[15] = (h >> 8) & 0xFF;
    tgaHeader[16] = 24;         // 24bpp

    f.write(reinterpret_cast<const char*>(tgaHeader), sizeof(tgaHeader));

    // 直接写入全部像素数据（行序 0→h-1，匹配 glReadPixels 的输出顺序）
    f.write(reinterpret_cast<const char*>(frame.pixels.data()),
            static_cast<std::streamsize>(frame.pixels.size()));

    return true;
}

bool ScreenshotCapture::HasFrame() {
    std::lock_guard lock(Mutex());
    return !LastFrame().pixels.empty();
}

void ScreenshotCapture::Clear() {
    std::lock_guard lock(Mutex());
    LastFrame() = Frame{};
}

} // namespace Engine
