/**
 * @file AtomicFile.cpp
 * @brief Content v2 M1-C：原子写入与不覆盖复制的实现
 */

#include "Engine/Core/Content/AtomicFile.h"

#include "Engine/Core/Content/Utf8Path.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace Engine::Content {

namespace {

std::atomic<uint64_t> g_TempCounter{0};
AtomicFaultHook g_AtomicFaultHook;

bool AtomicFault(AtomicFaultPoint point, const fs::path& temp,
                 const fs::path& target) {
    return g_AtomicFaultHook &&
           g_AtomicFaultHook(point, PathToUtf8(temp), PathToUtf8(target));
}

std::string UniqueToken() {
    const uint64_t n = g_TempCounter.fetch_add(1);
    return std::to_string(static_cast<unsigned long long>(n));
}

fs::path TempPathFor(const fs::path& target) {
    return target.parent_path() /
           PathFromUtf8(".m1tmp." + UniqueToken() +
                        PathToUtf8(target.filename()));
}

bool ReadAllBytes(const fs::path& path, std::string* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    std::string data((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    if (!f.good() && !f.eof()) return false;
    *out = std::move(data);
    return true;
}

bool VerifySameBytes(const fs::path& path, std::string_view expected,
                     std::string* diagnostic) {
    std::string actual;
    if (!ReadAllBytes(path, &actual)) {
        *diagnostic = "cannot read back temp file";
        return false;
    }
    if (actual.size() != expected.size()) {
        *diagnostic = "size mismatch after write";
        return false;
    }
    if (actual != expected) {
        *diagnostic = "content mismatch after write";
        return false;
    }
    return true;
}

#ifdef _WIN32
std::string WinError(DWORD code) { return "win32 error " + std::to_string(code); }
#endif

AtomicFileResult WriteBytes(std::string_view utf8Target, std::string_view bytes,
                            bool replaceExisting) {
    AtomicFileResult result;
    const fs::path target = PathFromUtf8(utf8Target);
    if (target.empty() || target.filename().empty()) {
        result.error = AtomicFileError::TempCreate;
        result.diagnostic = "invalid target path";
        return result;
    }
    const fs::path temp = TempPathFor(target);
    if (AtomicFault(AtomicFaultPoint::BeforeTempWrite, temp, target)) {
        result.error = AtomicFileError::InjectedFailure;
        result.diagnostic = "injected BeforeTempWrite failure";
        return result;
    }

#ifdef _WIN32
    const std::wstring tempW = temp.wstring();
    const std::wstring targetW = target.wstring();
    HANDLE handle = CreateFileW(tempW.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        result.error = AtomicFileError::TempCreate;
        result.diagnostic = WinError(GetLastError());
        return result;
    }
    bool ok = true;
    size_t written = 0;
    while (written < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(
            std::min<size_t>(bytes.size() - written, 1u << 20));
        DWORD done = 0;
        if (!WriteFile(handle, bytes.data() + written, chunk, &done, nullptr) ||
            done == 0) {
            ok = false;
            break;
        }
        written += done;
    }
    if (ok && !FlushFileBuffers(handle)) ok = false;
    CloseHandle(handle);
    if (!ok) {
        DeleteFileW(tempW.c_str());
        result.error = AtomicFileError::Write;
        result.diagnostic = WinError(GetLastError());
        return result;
    }

    if (AtomicFault(AtomicFaultPoint::AfterTempWriteBeforeVerify, temp,
                    target)) {
        DeleteFileW(tempW.c_str());
        result.error = AtomicFileError::InjectedFailure;
        result.diagnostic = "injected AfterTempWriteBeforeVerify failure";
        return result;
    }
    if (!VerifySameBytes(temp, bytes, &result.diagnostic)) {
        DeleteFileW(tempW.c_str());
        result.error = AtomicFileError::Verify;
        return result;
    }

    if (AtomicFault(AtomicFaultPoint::BeforeReplace, temp, target)) {
        DeleteFileW(tempW.c_str());
        result.error = AtomicFileError::InjectedFailure;
        result.diagnostic = "injected BeforeReplace failure";
        return result;
    }
    DWORD flags = MOVEFILE_WRITE_THROUGH;
    if (replaceExisting) flags |= MOVEFILE_REPLACE_EXISTING;
    if (!MoveFileExW(tempW.c_str(), targetW.c_str(), flags)) {
        const DWORD err = GetLastError();
        DeleteFileW(tempW.c_str());
        if (!replaceExisting &&
            (err == ERROR_ALREADY_EXISTS || err == ERROR_FILE_EXISTS)) {
            result.error = AtomicFileError::TargetExists;
        } else {
            result.error = AtomicFileError::Replace;
        }
        result.diagnostic = WinError(err);
        return result;
    }
#else
    const std::string tempName = temp.string();
    const int fd = ::open(tempName.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        result.error = AtomicFileError::TempCreate;
        result.diagnostic = std::strerror(errno);
        return result;
    }
    bool ok = true;
    size_t written = 0;
    while (written < bytes.size()) {
        const ssize_t n =
            ::write(fd, bytes.data() + written, bytes.size() - written);
        if (n <= 0) { ok = false; break; }
        written += static_cast<size_t>(n);
    }
    if (ok && ::fsync(fd) != 0) ok = false;
    ::close(fd);
    if (!ok) {
        ::unlink(tempName.c_str());
        result.error = AtomicFileError::Write;
        result.diagnostic = std::strerror(errno);
        return result;
    }

    if (AtomicFault(AtomicFaultPoint::AfterTempWriteBeforeVerify, temp,
                    target)) {
        ::unlink(tempName.c_str());
        result.error = AtomicFileError::InjectedFailure;
        result.diagnostic = "injected AfterTempWriteBeforeVerify failure";
        return result;
    }
    if (!VerifySameBytes(temp, bytes, &result.diagnostic)) {
        ::unlink(tempName.c_str());
        result.error = AtomicFileError::Verify;
        return result;
    }

    if (AtomicFault(AtomicFaultPoint::BeforeReplace, temp, target)) {
        ::unlink(tempName.c_str());
        result.error = AtomicFileError::InjectedFailure;
        result.diagnostic = "injected BeforeReplace failure";
        return result;
    }
    if (replaceExisting) {
        if (::rename(tempName.c_str(), target.string().c_str()) != 0) {
            result.error = AtomicFileError::Replace;
            result.diagnostic = std::strerror(errno);
            ::unlink(tempName.c_str());
            return result;
        }
    } else {
        if (::link(tempName.c_str(), target.string().c_str()) != 0) {
            result.error = (errno == EEXIST) ? AtomicFileError::TargetExists
                                             : AtomicFileError::Replace;
            result.diagnostic = std::strerror(errno);
            ::unlink(tempName.c_str());
            return result;
        }
        ::unlink(tempName.c_str());
    }
#endif

    result.ok = true;
    return result;
}

} // namespace

void SetAtomicFaultHookForTesting(AtomicFaultHook hook) {
    g_AtomicFaultHook = std::move(hook);
}

const char* ToString(AtomicFileError error) {
    switch (error) {
        case AtomicFileError::None: return "None";
        case AtomicFileError::CannotReadSource: return "CannotReadSource";
        case AtomicFileError::TempCreate: return "TempCreate";
        case AtomicFileError::Write: return "Write";
        case AtomicFileError::Flush: return "Flush";
        case AtomicFileError::Verify: return "Verify";
        case AtomicFileError::TargetExists: return "TargetExists";
        case AtomicFileError::Replace: return "Replace";
        case AtomicFileError::InjectedFailure: return "InjectedFailure";
    }
    return "Unknown";
}

AtomicFileResult WriteFileAtomic(std::string_view utf8Target,
                                 std::string_view bytes) {
    return WriteBytes(utf8Target, bytes, true);
}

AtomicFileResult CopyFileNoOverwrite(std::string_view utf8Source,
                                     std::string_view utf8Target) {
    AtomicFileResult result;
    std::string bytes;
    if (!ReadAllBytes(PathFromUtf8(utf8Source), &bytes)) {
        result.error = AtomicFileError::CannotReadSource;
        result.diagnostic = "cannot read source file";
        return result;
    }
    return WriteBytes(utf8Target, bytes, false);
}

} // namespace Engine::Content
