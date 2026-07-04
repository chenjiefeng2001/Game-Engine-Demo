/**
 * @file PakFile.cpp
 * @brief Pak 文件格式 TOC 加载与条目访问实现
 */

#include "Engine/Core/PakFile.h"
#include "Engine/Core/Log.h"
#include <cstring>
#include <algorithm>

namespace {
    Engine::Logger s_Log("PakFile");
}

namespace Engine {

    // ============================================================
    // 路径哈希辅助
    // ============================================================

    uint64_t PakArchiveMount::HashPath(std::string_view path) {
        return HashString64(path);
    }

    // ============================================================
    // 构造 / 析构
    // ============================================================

    PakArchiveMount::PakArchiveMount(std::string_view pakPath) {
        // 打开 Pak 文件
    #ifdef _WIN32
        fopen_s(&m_PakFile, pakPath.data(), "rb");
    #else
        m_PakFile = fopen(pakPath.data(), "rb");
    #endif
        if (!m_PakFile) {
            s_Log.Error("Failed to open Pak file: {}", pakPath);
            return;
        }

        if (!LoadTOC()) {
            s_Log.Error("Failed to load TOC from: {}", pakPath);
            std::fclose(m_PakFile);
            m_PakFile = nullptr;
            return;
        }

        m_Valid = true;
        s_Log.Info("Pak loaded: {} ({} entries)", pakPath, m_Entries.size());
    }

    PakArchiveMount::~PakArchiveMount() {
        if (m_PakFile) {
            std::fclose(m_PakFile);
            m_PakFile = nullptr;
        }
    }

    // ============================================================
    // TOC 加载
    // ============================================================

    bool PakArchiveMount::LoadTOC() {
        if (!m_PakFile) return false;

        // ── 1. 读取文件末尾的 Footer（8 bytes） ──
    #ifdef _WIN32
        _fseeki64(m_PakFile, -8, SEEK_END);
    #else
        fseeko(m_PakFile, -8, SEEK_END);
    #endif

        uint32_t footerMagic = 0, footerVersion = 0;
        if (std::fread(&footerMagic, sizeof(uint32_t), 1, m_PakFile) != 1 ||
            std::fread(&footerVersion, sizeof(uint32_t), 1, m_PakFile) != 1) {
            s_Log.Error("Failed to read Pak footer");
            return false;
        }

        if (footerMagic != PAK_FOOTER_MAGIC) {
            s_Log.Error("Invalid Pak footer magic: 0x{:08X} (expected 0x{:08X})",
                        footerMagic, PAK_FOOTER_MAGIC);
            return false;
        }

        // ── 2. 读取 Header（文件开头 32 bytes） ──
        PakHeader header;
    #ifdef _WIN32
        _fseeki64(m_PakFile, 0, SEEK_SET);
    #else
        fseeko(m_PakFile, 0, SEEK_SET);
    #endif

        if (std::fread(&header, sizeof(PakHeader), 1, m_PakFile) != 1) {
            s_Log.Error("Failed to read Pak header");
            return false;
        }

        if (header.magic != PAK_MAGIC) {
            s_Log.Error("Invalid Pak magic: 0x{:08X} (expected 0x{:08X})",
                        header.magic, PAK_MAGIC);
            return false;
        }

        if (header.version != PAK_VERSION) {
            s_Log.Error("Pak version mismatch: {} (expected {})",
                        header.version, PAK_VERSION);
            return false;
        }

        if (header.tocSize == 0) {
            s_Log.Error("Pak TOC size is zero");
            return false;
        }

        // ── 3. 读取 TOC 数据 ──
        std::vector<uint8_t> tocData(static_cast<size_t>(header.tocSize));

    #ifdef _WIN32
        _fseeki64(m_PakFile, static_cast<int64_t>(header.tocOffset), SEEK_SET);
    #else
        fseeko(m_PakFile, static_cast<off_t>(header.tocOffset), SEEK_SET);
    #endif

        if (std::fread(tocData.data(), 1, static_cast<size_t>(header.tocSize), m_PakFile)
            != static_cast<size_t>(header.tocSize)) {
            s_Log.Error("Failed to read TOC data (offset={}, size={})",
                        header.tocOffset, header.tocSize);
            return false;
        }

        // ── 4. 解析 TOC ──
        return ParseTOC(tocData);
    }

    bool PakArchiveMount::ParseTOC(const std::vector<uint8_t>& tocData) {
        if (tocData.size() < 4) return false;

        const uint8_t* ptr = tocData.data();
        const uint8_t* end = ptr + tocData.size();

        // EntryCount
        uint32_t count = 0;
        std::memcpy(&count, ptr, sizeof(uint32_t));
        ptr += sizeof(uint32_t);

        m_Entries.reserve(count);

        for (uint32_t i = 0; i < count; ++i) {
            if (ptr + 48 > end) {  // 最小条目大小：8+8+8+8+8+4+4 = 48 bytes（不含 path）
                s_Log.Error("TOC truncated at entry {}/{}", i, count);
                return false;
            }

            PakEntry entry;

            std::memcpy(&entry.hash, ptr, sizeof(uint64_t));     ptr += 8;
            /* pathHash (debug) */                               ptr += 8;  // 跳过
            std::memcpy(&entry.offset, ptr, sizeof(uint64_t));   ptr += 8;
            std::memcpy(&entry.originalSize, ptr, sizeof(uint64_t)); ptr += 8;
            std::memcpy(&entry.storedSize, ptr, sizeof(uint64_t));   ptr += 8;
            std::memcpy(&entry.flags, ptr, sizeof(uint32_t));     ptr += 4;
            /* reserved */                                        ptr += 4;  // 跳过

            // 读取 null-terminated 路径字符串
            size_t maxPathBytes = static_cast<size_t>(end - ptr);
            size_t pathLen = strnlen(reinterpret_cast<const char*>(ptr), maxPathBytes);
            if (pathLen >= sizeof(entry.path)) pathLen = sizeof(entry.path) - 1;

            std::memcpy(entry.path, ptr, pathLen);
            entry.path[pathLen] = '\0';
            ptr += pathLen + 1;  // 跳过 null 终止符

            // 已含该条目的尾随填充
            m_Entries[entry.hash] = entry;
        }

        if (m_Entries.size() != count) {
            s_Log.Warn("TOC entry count mismatch: declared={}, parsed={}",
                       count, m_Entries.size());
        }

        return true;
    }

    // ============================================================
    // IMountBackend 接口实现
    // ============================================================

    bool PakArchiveMount::FileExists(std::string_view path) {
        uint64_t hash = HashPath(path);
        return m_Entries.find(hash) != m_Entries.end();
    }

    IFilePtr PakArchiveMount::OpenFile(std::string_view path) {
        uint64_t hash = HashPath(path);
        auto it = m_Entries.find(hash);
        if (it == m_Entries.end()) return nullptr;

        return std::make_unique<PakEntryFile>(m_PakFile, it->second);
    }

    size_t PakArchiveMount::GetFileSize(std::string_view path) {
        uint64_t hash = HashPath(path);
        auto it = m_Entries.find(hash);
        return (it != m_Entries.end()) ? it->second.originalSize : 0;
    }

    std::vector<std::string> PakArchiveMount::ListPaths() const {
        std::vector<std::string> paths;
        paths.reserve(m_Entries.size());
        for (const auto& [hash, entry] : m_Entries) {
            paths.push_back(entry.path);
        }
        std::sort(paths.begin(), paths.end());
        return paths;
    }

} // namespace Engine