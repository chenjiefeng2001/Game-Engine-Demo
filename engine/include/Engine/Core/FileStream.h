#pragma once

/**
 * @file FileStream.h
 * @brief 文件流包装器 — 将 FileSystem（含 VFS）适配为第三方库的 I/O 回调
 *
 * 解决的问题：
 *   - stb_image、dr_wav、stb_vorbis 等库使用 FILE* 或文件名直接加载
 *   - 它们提供回调接口，但需要适配到我们的 VFS 文件系统
 *   - FileStream 统一管理文件流生命周期，并提供静态回调函数
 *
 * 使用示例：
 * @code
 *   // 加载纹理（VFS 路径）
 *   FileStream stream;
 *   if (stream.Open("assets:textures/player.png")) {
 *       stbi_io_callbacks cbs = stream.GetSTBICallbacks();
 *       int w, h, n;
 *       unsigned char* data = stbi_load_from_callbacks(&cbs, &stream, &w, &h, &n, 0);
 *       // ...
 *   }  // stream 析构时自动关闭
 * @endcode
 */

#include "Engine/Types.h"
#include <cstdint>
#include <memory>

namespace Engine {

    class FileStream {
    public:
        FileStream() = default;
        ~FileStream();

        FileStream(const FileStream&) = delete;
        FileStream& operator=(const FileStream&) = delete;

        // 移动（转移文件流所有权）
        FileStream(FileStream&& other) noexcept;
        FileStream& operator=(FileStream&& other) noexcept;

        // ── 打开 / 关闭 ──

        /**
         * @brief 打开文件（路径经过 FileSystem::ResolvePath 解析）
         * @param virtualPath 虚拟路径（支持 "mount:sub/path" 和相对路径）
         * @return 是否成功打开
         */
        bool Open(const std::string& virtualPath);

        /** 关闭文件 */
        void Close();

        /** 是否已打开 */
        bool IsOpen() const;

        // ── 读取 / 定位 ──

        /** 读取指定字节数，返回实际读取的字节数 */
        size_t Read(void* buffer, size_t size);

        /** 跳过指定字节数 */
        void Skip(int n);

        /** 定位到指定偏移 */
        bool Seek(int64 offset, int origin);

        /** 获取当前位置 */
        int64 Tell() const;

        /** 获取文件总大小 */
        size_t GetSize() const;

        /** 是否到达文件末尾 */
        bool IsEof() const;

        // ── 第三方库回调适配 ──

        /**
         * @brief stb_image 回调接口（stbi_io_callbacks）
         *
         * 用法：
         *   stbi_io_callbacks cbs = stream.GetSTBICallbacks();
         *   int w, h, n;
         *   stbi_load_from_callbacks(&cbs, &stream, &w, &h, &n, 0);
         */
        struct STBIOCallbacks {
            int (*read)(void* user, char* data, int size);
            void (*skip)(void* user, int n);
            int (*eof)(void* user);
        };

        STBIOCallbacks GetSTBICallbacks() const;

        /**
         * @brief dr_wav 回调接口（drwav_read_callbacks 结构）
         *
         * 用法：
         *   auto callbacks = stream.GetDRWavCallbacks();
         *   drwav_init(&wav, &callbacks, &stream);
         */
        struct DRWavCallbacks {
            bool (*onRead)(void* pWav, void* pBufferOut, size_t bytesToRead, size_t* pBytesRead);
            bool (*onSeek)(void* pWav, int offset, int origin);
        };

        DRWavCallbacks GetDRWavCallbacks() const;

        // ── 静态回调函数（作为上述结构体的函数指针） ──

        /** stb_image: read callback */
        static int STBI_Read(void* user, char* data, int size);

        /** stb_image: skip callback */
        static void STBI_Skip(void* user, int n);

        /** stb_image: eof callback */
        static int STBI_Eof(void* user);

        /** dr_wav: onRead callback */
        static bool DRWav_OnRead(void* user, void* bufferOut, size_t bytesToRead, size_t* bytesRead);

        /** dr_wav: onSeek callback */
        static bool DRWav_OnSeek(void* user, int offset, int origin);

    private:
        // PIMPL — 隐藏 std::ifstream 依赖
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

} // namespace Engine
