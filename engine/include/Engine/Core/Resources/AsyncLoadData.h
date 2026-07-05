#pragma once

/**
 * @file AsyncLoadData.h
 * @brief 异步加载数据结构 — 后台 I/O 线程与主线程间的数据传递
 *
 * 加载流水线：
 *   LoadAsync<T>(path)
 *   ┌─ 后台线程 ─────────────────┐
 *   │  1. 从磁盘读取文件          │
 *   │  2. 解码（stbi / 音频解码）│
 *   │  3. 填充 AsyncLoadData      │
 *   └──────────┬─────────────────┘
 *              ▼ 队列传递
 *   ┌─ 主线程 ───────────────────┐
 *   │  4. GPU/API 上传（OpenGL等）│
 *   │  5. 设置 ResourceState     │
 *   └────────────────────────────┘
 */

#include "Engine/Types.h"
#include "Engine/Core/Audio/AudioDefs.h"
#include <string>
#include <vector>
#include <memory>

namespace Engine {

    // ============================================================
    // 纹理异步加载数据
    // ============================================================
    struct TextureLoadData {
        std::string    path;
        int            width  = 0;
        int            height = 0;
        int            channels = 0;
        unsigned char* pixels = nullptr;  

        ~TextureLoadData() { Free(); }
        TextureLoadData() = default;

        // 禁止拷贝，允许移动
        TextureLoadData(const TextureLoadData&) = delete;
        TextureLoadData& operator=(const TextureLoadData&) = delete;

        TextureLoadData(TextureLoadData&& other) noexcept
            : path(std::move(other.path))
            , width(other.width)
            , height(other.height)
            , channels(other.channels)
            , pixels(other.pixels)
        {
            other.pixels = nullptr;
            other.width = other.height = other.channels = 0;
        }

        TextureLoadData& operator=(TextureLoadData&& other) noexcept {
            if (this != &other) {
                Free();
                path     = std::move(other.path);
                width    = other.width;
                height   = other.height;
                channels = other.channels;
                pixels   = other.pixels;
                other.pixels = nullptr;
                other.width = other.height = other.channels = 0;
            }
            return *this;
        }

        bool IsValid() const noexcept { return pixels != nullptr && width > 0 && height > 0; }

        void Free() {
            // stbi_image_free can handle nullptr
            // We include stb_image.h in the .cpp, we just store the raw pointer here
            // Actual freeing happens via a custom deleter
            if (pixels) {
                // stb_image_free will be called from the .cpp where stb_image.h is included
                // We use a raw pointer and free it explicitly
                ::free(pixels);  // stbi_load uses malloc internally
                pixels = nullptr;
            }
            width = height = channels = 0;
        }
    };

    // ============================================================
    // 音频异步加载数据
    // ============================================================
    // 复用 AudioData（在 AudioLoader 中定义），但为统一接口作类型别名
    // AudioData 已包含 pcmData vector + AudioClipInfo

    // ============================================================
    // 着色器异步加载数据
    // ============================================================
    struct ShaderLoadData {
        std::string vertexPath;
        std::string fragmentPath;
        std::string vertexSource;
        std::string fragmentSource;

        bool IsValid() const noexcept {
            return !vertexSource.empty() && !fragmentSource.empty();
        }
    };

    // ============================================================
    // 异步加载任务完成后的上传函数签名
    // ============================================================
    // 每个资源类型提供一个 "upload" lambda:
    //   [](void* decodedData, Resource* resource) → bool
    //
    // decodedData 指向 TextureLoadData / AudioData / ShaderLoadData
    // resource 指向已创建的 Resource 对象（状态为 Loading）
    // 返回 true 表示成功

} // namespace Engine