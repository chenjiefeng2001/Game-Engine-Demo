#pragma once

/**
 * @file PakFile.h
 * @brief 自定义 Pak 打包文件格式 — TOC 索引 + LZ4/Zstd 压缩预留 + 内存映射
 *
 * Pak 文件格式（二进制）：
 *
 * ┌─────────────────────────────────────────────────────┐
 * │ Header (32 bytes)                                  │
 * │   Magic:  "EGNP" (4 bytes, Engine Pak)             │
 * │   Version: uint32                                  │
 * │   TocOffset: uint64   (TOC 在文件中的绝对偏移)      │
 * │   TocSize:   uint64   (TOC 压缩后大小; 0 = 未压缩)  │
 * │   Reserved:  [8 bytes]                             │
 * ├─────────────────────────────────────────────────────┤
 * │ File Entry Data (按写入顺序排列)                    │
 * │   [file0 data][file1 data][...]                    │
 * ├─────────────────────────────────────────────────────┤
 * │ TOC (Table of Contents — 目录表)                   │
 * │   EntryCount: uint32                               │
 * │   [                                            ]   │
 * │     StringID hash  : uint64                       │
 * │     StringID path  : uint64  (调试用)              │
 * │     Offset         : uint64  (数据区绝对偏移)       │
 * │     OriginalSize   : uint64  (未压缩大小)          │
 * │     StoredSize     : uint64  (存储区内大小)         │
 * │     Flags          : uint32                       │
 * │            bit 0: compressed (LZ4)                │
 * │            bit 1: compressed (Zstd) — 预留         │
 * │            bit 2: encrypted — 预留                  │
 * │     Reserved       : [4 bytes]                    │
 * │     OriginalPath   : [variable]  null-terminated  │
 * │       ...  (下一个条目)                             │
 * │   [End]                                           │
 * ├─────────────────────────────────────────────────────┤
 * │ Footer (8 bytes)                                  │
 * │   Magic:  "EOPK" (End Of Pack, 4 bytes)           │
 * │   Version: uint32                                 │
 * └─────────────────────────────────────────────────────┘
 */

#include "Engine/Types.h"
#include "Engine/Core/IFile.h"
#include "Engine/Core/StringID.h"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace Engine {

    // ============================================================
    // Pak 文件常量
    // ============================================================
    constexpr uint32_t PAK_MAGIC   = 0x504E4745U;  // "EGNP" (little-endian)
    constexpr uint32_t PAK_FOOTER_MAGIC = 0x4B504F45U;  // "EOPK"
    constexpr uint32_t PAK_VERSION = 1;

    // ============================================================
    // Pak 条目标志位
    // ============================================================
    namespace PakFlags {
        constexpr uint32_t None       = 0;
        constexpr uint32_t Compressed = 1 << 0;  // LZ4 压缩
        constexpr uint32_t Zstd       = 1 << 1;  // Zstd 压缩（预留）
        constexpr uint32_t Encrypted  = 1 << 2;  // AES 加密（预留）
    }

    // ============================================================
    // 目录表条目（TOC Entry）
    // ============================================================
    struct PakEntry {
        uint64_t hash           = 0;     // 文件路径的 StringID 哈希
        uint64_t offset         = 0;     // 数据在 Pak 文件中的绝对偏移
        uint64_t originalSize   = 0;     // 未压缩前的大小
        uint64_t storedSize     = 0;     // 存储区大小（等于 originalSize 当未压缩时）
        uint32_t flags          = 0;     // PakFlags 组合
        char     path[256]      = {};    // 原始路径（调试用，null-terminated）

        bool IsCompressed() const { return (flags & PakFlags::Compressed) != 0; }
    };

    // ============================================================
    // Pak 文件头
    // ============================================================
    struct PakHeader {
        uint32_t magic       = PAK_MAGIC;
        uint32_t version     = PAK_VERSION;
        uint64_t tocOffset   = 0;
        uint64_t tocSize     = 0;
        uint8_t  reserved[8] = {};
    };

    // ============================================================
    // PakFile — 单个 Pak 文件的 IFile（用于 TOC 加载后打开内部条目）
    // ============================================================
    /**
     * @brief 对 Pak 内部的一个条目提供 IFile 接口
     *
     * 引用外部打开的 FILE* + PakEntry 的 offset/size。
     * 生命周期短：通常在一次 Read 调用后由调用方释放。
     */
    class PakEntryFile final : public IFile {
    public:
        PakEntryFile(FILE* pakFile, const PakEntry& entry)
            : m_PakFile(pakFile)
            , m_BaseOffset(entry.offset)
            , m_StoredSize(entry.storedSize)
            , m_OriginalSize(entry.originalSize)
            , m_Flags(entry.flags)
            , m_Position(0)
        {
            // 定位到条目数据起始处
        #ifdef _WIN32
            _fseeki64(m_PakFile, static_cast<int64_t>(m_BaseOffset), SEEK_SET);
        #else
            fseeko(m_PakFile, static_cast<off_t>(m_BaseOffset), SEEK_SET);
        #endif
        }

        size_t Read(void* buffer, size_t size) override {
            if (!m_PakFile || m_Position >= m_StoredSize) return 0;

            size_t remaining = m_StoredSize - m_Position;
            size_t toRead   = std::min(size, remaining);

            // 从当前位置读取（fread 会推进 FILE* 位置）
            size_t actual = std::fread(buffer, 1, toRead, m_PakFile);
            m_Position += actual;

            // 对压缩数据不解压，返回原始存储字节
            // 真实的 LZ4 解压在此处插入解压逻辑
            return actual;
        }

        void Seek(size_t offset) override {
            if (!m_PakFile) return;
            if (offset > m_StoredSize) offset = m_StoredSize;

            m_Position = offset;
        #ifdef _WIN32
            _fseeki64(m_PakFile, static_cast<int64_t>(m_BaseOffset + offset), SEEK_SET);
        #else
            fseeko(m_PakFile, static_cast<off_t>(m_BaseOffset + offset), SEEK_SET);
        #endif
        }

        size_t Tell() const override { return m_Position; }
        size_t GetSize() const override { return m_OriginalSize; }

    private:
        FILE*    m_PakFile;
        uint64_t m_BaseOffset;
        uint64_t m_StoredSize;
        uint64_t m_OriginalSize;
        uint32_t m_Flags;
        size_t   m_Position;
    };

    // ============================================================
    // PakArchiveMount — 将一个 .pak 文件挂载为 VFS 后端
    // ============================================================
    /**
     * @brief 从 .pak 文件中读取 TOC 表并提供 IMountBackend 接口
     *
     * 初始化时读取并解析 TOC 表，之后 OpenFile 操作在 TOC 中查找。
     *
     * 使用方式：
     * @code
     *   auto pak = std::make_unique<PakArchiveMount>("C:/Game/Base.pak");
     *   if (pak->IsValid()) {
     *       FileSystem::Mount("base:", std::move(pak));
     *   }
     * @endcode
     */
    class PakArchiveMount final : public IMountBackend {
    public:
        explicit PakArchiveMount(std::string_view pakPath);
        ~PakArchiveMount() override;

        PakArchiveMount(const PakArchiveMount&) = delete;
        PakArchiveMount& operator=(const PakArchiveMount&) = delete;

        const char* GetName() const override { return "PakArchive"; }

        bool FileExists(std::string_view path) override;
        IFilePtr OpenFile(std::string_view path) override;
        size_t GetFileSize(std::string_view path) override;
        bool SupportsEnumeration() const override { return false; }

        /** Pak 文件是否有效（成功加载 TOC） */
        bool IsValid() const { return m_Valid; }

        /** 获取 TOC 中的条目总数 */
        size_t GetEntryCount() const { return m_Entries.size(); }

        /** 调试：列出 TOC 中所有路径 */
        std::vector<std::string> ListPaths() const;

    private:
        /** @brief 从文件末尾读取 TOC */
        bool LoadTOC();

        /** @brief 解析 TOC 二进制格式 */
        bool ParseTOC(const std::vector<uint8_t>& tocData);

        // ── 数据成员 ──
        FILE*   m_PakFile = nullptr;
        bool    m_Valid   = false;

        // TOC 索引：hash → PakEntry
        std::unordered_map<uint64_t, PakEntry> m_Entries;

        // 反向索引：path → hash（方便从路径查找）
        static std::string  m_CachedPakPath;
        static uint64_t     HashPath(std::string_view path);
    };

} // namespace Engine