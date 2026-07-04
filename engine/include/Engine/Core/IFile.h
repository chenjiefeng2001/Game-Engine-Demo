#pragma once

/**
 * @file IFile.h
 * @brief 虚拟文件系统抽象接口 — IFile + IMountBackend
 *
 * 设计理念：
 *   - 所有文件 I/O 通过 IFile 接口完成，上层不感知底层存储方式
 *   - IMountBackend 统一了 OS 目录、Pak 打包文件、网络流等后端
 *   - 经 FileSystem 挂载后，所有 VFS 路径透明解析到手头的后端
 *
 * 架构层次：
 *   Application → FileSystem (ResolvePath) → IMountBackend → IFile
 *                                        → IMountBackend → IFile (pak)
 *
 * 使用示例：
 * @code
 *   // 获取文件
 *   auto file = FileSystem::OpenFile("assets:textures/hero.png");
 *   if (file) {
 *       std::vector<uint8_t> buf(file->GetSize());
 *       file->Read(buf.data(), buf.size());
 *   }
 *
 *   // 挂载不同类型后端
 *   FileSystem::Mount("data:", std::make_unique<OsDirectoryMount>("C:/Game/Data"));
 *   FileSystem::Mount("base:", std::make_unique<PakArchiveMount>("C:/Game/Base.pak"));
 * @endcode
 */

#include "Engine/Types.h"
#include <string>
#include <memory>
#include <string_view>
#include <cstdint>

namespace Engine {

    // ============================================================
    // IFile — 文件句柄抽象
    // ============================================================
    /**
     * @brief 所有文件 I/O 的统一接口
     *
     * 每个 IFile 实例对应一个打开的文件（无论是 OS 文件还是 Pak 中的条目）。
     * 生命周期由 shared_ptr 管理。
     */
    class IFile {
    public:
        virtual ~IFile() = default;

        /**
         * @brief 从当前位置读取指定字节数
         * @param buffer 目标缓冲区
         * @param size   请求读取的字节数
         * @return 实际读取的字节数（0 = EOF 或错误）
         */
        virtual size_t Read(void* buffer, size_t size) = 0;

        /**
         * @brief 定位到指定偏移（绝对定位）
         * @param offset 从文件开头的字节偏移
         */
        virtual void Seek(size_t offset) = 0;

        /** @brief 获取当前读取位置 */
        virtual size_t Tell() const = 0;

        /** @brief 获取文件总大小（字节） */
        virtual size_t GetSize() const = 0;

        /** @brief 是否已到达文件末尾 */
        virtual bool IsEOF() const { return Tell() >= GetSize(); }
    };

    /** IFile 实例的所有权指针 */
    using IFilePtr = std::unique_ptr<IFile>;

    // ============================================================
    // IMountBackend — 挂载后端接口
    // ============================================================
    /**
     * @brief 文件存储后端的统一接口
     *
     * 每个挂载点对应一个 IMountBackend 实现。
     * Engine::FileSystem 维护 name → backend 的映射。
     *
     * 后端类型：
     *   - OsDirectoryMount：普通目录（当前实现）
     *   - PakArchiveMount：打包文件（规划实现）
     */
    class IMountBackend {
    public:
        virtual ~IMountBackend() = default;

        /** 挂载点名称（用于日志和调试） */
        virtual const char* GetName() const = 0;

        /** 文件是否存在于此外部 */
        virtual bool FileExists(std::string_view path) = 0;

        /** 打开文件并返回 IFile 句柄 */
        virtual IFilePtr OpenFile(std::string_view path) = 0;

        /** 获取文件大小（可选重写，默认返回 0） */
        virtual size_t GetFileSize(std::string_view path) {
            auto f = OpenFile(path);
            return f ? f->GetSize() : 0;
        }

        /** 是否支持目录扫描（可选，Pak 后端可能返回 false） */
        virtual bool SupportsEnumeration() const { return true; }
    };

    using MountBackendPtr = std::unique_ptr<IMountBackend>;

} // namespace Engine