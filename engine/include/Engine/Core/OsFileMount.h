#pragma once

/**
 * @file OsFileMount.h
 * @brief OS 本地目录挂载后端 — 直接映射到文件系统目录
 *
 * 这是最基础的 VFS 后端，用于开发模式下直接访问散落文件。
 * 在 Shipping 版本中，通常会替换为 PakArchiveMount。
 */

#include "Engine/Core/IFile.h"
#include <string>
#include <string_view>
#include <cstdio>
#include <filesystem>

namespace Engine {

    // ============================================================
    // OsFile — C FILE* 包装的 IFile
    // ============================================================
    class OsFile final : public IFile {
    public:
        explicit OsFile(FILE* f, size_t size) : m_File(f), m_Size(size) {}
        ~OsFile() override { if (m_File) std::fclose(m_File); }

        OsFile(const OsFile&) = delete;
        OsFile& operator=(const OsFile&) = delete;

        size_t Read(void* buffer, size_t size) override {
            if (!m_File) return 0;
            return std::fread(buffer, 1, size, m_File);
        }

        void Seek(size_t offset) override {
            if (m_File) {
            #ifdef _WIN32
                _fseeki64(m_File, static_cast<int64_t>(offset), SEEK_SET);
            #else
                fseeko(m_File, static_cast<off_t>(offset), SEEK_SET);
            #endif
            }
        }

        size_t Tell() const override {
            if (!m_File) return 0;
        #ifdef _WIN32
            return static_cast<size_t>(_ftelli64(m_File));
        #else
            return static_cast<size_t>(ftello(m_File));
        #endif
        }

        size_t GetSize() const override { return m_Size; }

    private:
        FILE*  m_File;
        size_t m_Size;
    };

    // ============================================================
    // OsDirectoryMount — 本地目录挂载
    // ============================================================
    class OsDirectoryMount final : public IMountBackend {
    public:
        explicit OsDirectoryMount(std::string_view realPath)
            : m_RealPath(std::filesystem::absolute(std::filesystem::path(realPath))
                         .lexically_normal().string())
        {}

        const char* GetName() const override { return "OsDirectory"; }

        bool FileExists(std::string_view path) override {
            auto full = ResolveReal(path);
            return std::filesystem::exists(full) && std::filesystem::is_regular_file(full);
        }

        IFilePtr OpenFile(std::string_view path) override {
            auto full = ResolveReal(path);

            FILE* fp = nullptr;
        #ifdef _WIN32
            fopen_s(&fp, full.c_str(), "rb");
        #else
            fp = fopen(full.c_str(), "rb");
        #endif
            if (!fp) return nullptr;

            // 获取文件大小
            size_t size = 0;
        #ifdef _WIN32
            _fseeki64(fp, 0, SEEK_END);
            size = static_cast<size_t>(_ftelli64(fp));
            _fseeki64(fp, 0, SEEK_SET);
        #else
            fseeko(fp, 0, SEEK_END);
            size = static_cast<size_t>(ftello(fp));
            fseeko(fp, 0, SEEK_SET);
        #endif

            return std::make_unique<OsFile>(fp, size);
        }

        size_t GetFileSize(std::string_view path) override {
            auto full = ResolveReal(path);
            try {
                return static_cast<size_t>(std::filesystem::file_size(full));
            } catch (...) { return 0; }
        }

    private:
        std::string ResolveReal(std::string_view subPath) const {
            return (std::filesystem::path(m_RealPath) / subPath).lexically_normal().string();
        }

        std::string m_RealPath;
    };

} // namespace Engine