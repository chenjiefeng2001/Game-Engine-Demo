#include "Engine/Core/FileStream.h"
#include "Engine/Core/FileSystem.h"
#include "Engine/Core/Log.h"
#include <fstream>

namespace {
    Engine::Logger s_Log("FileStream");
}

namespace Engine {

    // ============================================================
    // PIMPL 实现
    // ============================================================

    struct FileStream::Impl {
        std::ifstream stream;
        size_t        fileSize = 0;
        bool          open = false;
    };

    // ============================================================
    // 构造 / 析构 / 移动
    // ============================================================

    FileStream::~FileStream() = default;

    FileStream::FileStream(FileStream&& other) noexcept
        : m_Impl(std::move(other.m_Impl)) {}

    FileStream& FileStream::operator=(FileStream&& other) noexcept {
        if (this != &other) {
            Close();
            m_Impl = std::move(other.m_Impl);
        }
        return *this;
    }

    // ============================================================
    // 打开 / 关闭
    // ============================================================

    bool FileStream::Open(const std::string& virtualPath) {
        Close();

        std::string realPath = FileSystem::ResolvePath(virtualPath);

        auto impl = std::make_unique<Impl>();
        impl->stream.open(realPath, std::ios::binary);
        if (!impl->stream.is_open()) {
            s_Log.Error("Failed to open: {}", realPath);
            return false;
        }

        // 计算文件大小
        impl->stream.seekg(0, std::ios::end);
        impl->fileSize = static_cast<size_t>(impl->stream.tellg());
        impl->stream.seekg(0, std::ios::beg);
        impl->open = true;

        m_Impl = std::move(impl);
        return true;
    }

    void FileStream::Close() {
        if (m_Impl && m_Impl->open) {
            m_Impl->stream.close();
            m_Impl->open = false;
        }
        m_Impl.reset();
    }

    bool FileStream::IsOpen() const {
        return m_Impl && m_Impl->open;
    }

    // ============================================================
    // 读取 / 定位
    // ============================================================

    size_t FileStream::Read(void* buffer, size_t size) {
        if (!IsOpen() || !buffer || size == 0) return 0;
        m_Impl->stream.read(static_cast<char*>(buffer),
                           static_cast<std::streamsize>(size));
        return static_cast<size_t>(m_Impl->stream.gcount());
    }

    void FileStream::Skip(int n) {
        if (!IsOpen()) return;
        m_Impl->stream.seekg(n, std::ios::cur);
    }

    bool FileStream::Seek(int64 offset, int origin) {
        if (!IsOpen()) return false;
        std::ios::seekdir dir;
        switch (origin) {
            case 0:  dir = std::ios::beg; break;
            case 1:  dir = std::ios::cur; break;
            case 2:  dir = std::ios::end; break;
            default: return false;
        }
        m_Impl->stream.seekg(static_cast<std::streamoff>(offset), dir);
        return !m_Impl->stream.fail();
    }

    int64 FileStream::Tell() const {
        if (!IsOpen()) return 0;
        return static_cast<int64>(m_Impl->stream.tellg());
    }

    size_t FileStream::GetSize() const {
        return IsOpen() ? m_Impl->fileSize : 0;
    }

    bool FileStream::IsEof() const {
        return IsOpen() && m_Impl->stream.eof();
    }

    // ============================================================
    // 第三方库回调结构体工厂
    // ============================================================

    FileStream::STBIOCallbacks FileStream::GetSTBICallbacks() const {
        return STBIOCallbacks{&STBI_Read, &STBI_Skip, &STBI_Eof};
    }

    FileStream::DRWavCallbacks FileStream::GetDRWavCallbacks() const {
        return DRWavCallbacks{&DRWav_OnRead, &DRWav_OnSeek};
    }

    // ============================================================
    // 静态回调函数
    // ============================================================

    int FileStream::STBI_Read(void* user, char* data, int size) {
        auto* self = static_cast<FileStream*>(user);
        return static_cast<int>(self->Read(data, static_cast<size_t>(size)));
    }

    void FileStream::STBI_Skip(void* user, int n) {
        static_cast<FileStream*>(user)->Skip(n);
    }

    int FileStream::STBI_Eof(void* user) {
        return static_cast<FileStream*>(user)->IsEof() ? 1 : 0;
    }

    bool FileStream::DRWav_OnRead(void* user, void* bufferOut,
                                   size_t bytesToRead, size_t* bytesRead) {
        auto* self = static_cast<FileStream*>(user);
        size_t actual = self->Read(bufferOut, bytesToRead);
        if (bytesRead) *bytesRead = actual;
        return actual > 0 || bytesToRead == 0;
    }

    bool FileStream::DRWav_OnSeek(void* user, int offset, int origin) {
        return static_cast<FileStream*>(user)->Seek(offset, origin);
    }

} // namespace Engine
