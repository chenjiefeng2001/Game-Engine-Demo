/**
 * @file RevisionStore.cpp
 * @brief Content v2 M1-D：current/Revision 的实现（含跨进程写锁与基线复验）
 */

#include "Engine/Core/Content/RevisionStore.h"

#include "Engine/Core/Content/AtomicFile.h"
#include "Engine/Core/Content/ContentAsset.h"
#include "Engine/Core/Content/SceneSerializerV1.h"
#include "Engine/Core/Content/Utf8Path.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace Engine::Content {

namespace {

constexpr const char* kMetaDir = ".engine-content";

RevisionFaultHook g_RevisionFaultHook;
ContentFaultHook g_ContentFaultHook;

void RevisionFault(RevisionFaultPoint point) {
    if (g_RevisionFaultHook) g_RevisionFaultHook(point);
}
constexpr const char* kVersionedMarker = "versioned";
constexpr int64_t kCurrentFormat = 1;

std::string JoinPath(const std::string& base, const std::string& relative) {
    if (base.empty()) return relative;
    if (base.back() == '/') return base + relative;
    return base + "/" + relative;
}

bool ReadAll(const std::string& utf8Path, std::string* out) {
    std::ifstream f(PathFromUtf8(utf8Path), std::ios::binary);
    if (!f.is_open()) return false;
    std::string data((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    if (!f.good() && !f.eof()) return false;
    *out = std::move(data);
    return true;
}

bool IsHex16(std::string_view value) {
    if (value.size() != 16) return false;
    for (char c : value) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    }
    return true;
}

/// 项目级跨进程写锁：Windows 以不共享的 CreateFileW 持有；POSIX 用 flock。
class ProjectWriteLock {
public:
    ProjectWriteLock() = default;
    ~ProjectWriteLock() { Release(); }
    ProjectWriteLock(const ProjectWriteLock&) = delete;
    ProjectWriteLock& operator=(const ProjectWriteLock&) = delete;

    bool Acquire(const std::string& utf8Path, std::string* diagnostic) {
        Release();
#if defined(_WIN32)
        const std::wstring wide = PathFromUtf8(utf8Path).wstring();
        m_Handle = CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                               nullptr);
        if (m_Handle == INVALID_HANDLE_VALUE) {
            if (diagnostic)
                *diagnostic = "project write lock busy (win32 " +
                              std::to_string(GetLastError()) + ")";
            return false;
        }
        return true;
#else
        const std::string native = PathFromUtf8(utf8Path).string();
        m_Fd = ::open(native.c_str(), O_CREAT | O_RDWR, 0666);
        if (m_Fd < 0) {
            if (diagnostic) *diagnostic = std::strerror(errno);
            return false;
        }
        if (::flock(m_Fd, LOCK_EX | LOCK_NB) != 0) {
            if (diagnostic) *diagnostic = std::strerror(errno);
            ::close(m_Fd);
            m_Fd = -1;
            return false;
        }
        return true;
#endif
    }

private:
    void Release() {
#if defined(_WIN32)
        if (m_Handle != INVALID_HANDLE_VALUE) {
            CloseHandle(m_Handle);
            m_Handle = INVALID_HANDLE_VALUE;
        }
#else
        if (m_Fd >= 0) {
            ::flock(m_Fd, LOCK_UN);
            ::close(m_Fd);
            m_Fd = -1;
        }
#endif
    }

#if defined(_WIN32)
    HANDLE m_Handle = INVALID_HANDLE_VALUE;
#else
    int m_Fd = -1;
#endif
};

bool ReadCurrent(const std::string& meta, bool* exists, std::string* bytes,
                 std::string* diagnostic) {
    const std::string path = JoinPath(meta, "current");
    std::error_code ec;
    *exists = fs::exists(PathFromUtf8(path), ec);
    if (ec) {
        *diagnostic = "cannot probe current: " + ec.message();
        return false;
    }
    if (!*exists) return true;
    if (!ReadAll(path, bytes)) {
        *diagnostic = "cannot read current";
        return false;
    }
    return true;
}

} // namespace

void SetRevisionFaultHookForTesting(RevisionFaultHook hook) {
    g_RevisionFaultHook = std::move(hook);
}

void SetContentFaultHookForTesting(ContentFaultHook hook) {
    g_ContentFaultHook = std::move(hook);
}

void InvokeContentFaultForTesting(ContentFaultPoint point) {
    if (g_ContentFaultHook) g_ContentFaultHook(point);
}

std::string RevisionStore::HashBytes(std::string_view bytes) {
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : bytes) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    char buf[17] = {};
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(hash));
    return std::string(buf);
}

RevisionOpenResult RevisionStore::Open(const ProjectRoot& root) {
    RevisionOpenResult result;
    m_State = RevisionOpenState::RecoveryRequired;
    m_Revision = 0;
    m_NextRevision = 1;
    m_HasBaseline = false;
    m_BaselineHash.clear();
    m_ManifestPath.clear();
    m_ScenePath.clear();

    if (!root.IsValid()) {
        result.diagnostic = "project root is invalid";
        return result;
    }

    const std::string meta = JoinPath(root.Path(), kMetaDir);
    const std::string currentAbs = JoinPath(meta, "current");
    std::error_code ec;
    const bool exists = fs::exists(PathFromUtf8(currentAbs), ec);
    if (ec) {
        result.diagnostic = "cannot probe current: " + ec.message();
        return result;
    }
    if (!exists) {
        // Unversioned 仅用于“从未进入 RevisionStore”的合法旧工程。
        // 一旦存在 versioned 标记而 current 缺失，就是事务元数据丢失。
        const std::string marker = JoinPath(meta, kVersionedMarker);
        const bool versioned = fs::exists(PathFromUtf8(marker), ec);
        if (ec) {
            result.diagnostic = "cannot probe versioned marker: " +
                                ec.message();
            return result;
        }
        if (versioned) {
            result.diagnostic =
                "versioned project is missing current (RecoveryRequired)";
            return result;
        }
        m_State = RevisionOpenState::Unversioned;
        result.state = RevisionOpenState::Unversioned;
        return result;
    }

    std::string text;
    if (!ReadAll(currentAbs, &text)) {
        result.diagnostic = "cannot read current";
        return result;
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(text);
    } catch (const std::exception&) {
        result.diagnostic = "current is not valid JSON";
        return result;
    }
    if (!j.is_object() || !j.contains("format") ||
        !j["format"].is_number_integer() || !j.contains("revision") ||
        !j["revision"].is_number_integer()) {
        result.diagnostic = "current has invalid format/revision";
        return result;
    }
    int64_t format = 0;
    int64_t revision = 0;
    try {
        format = j["format"].get<int64_t>();
        revision = j["revision"].get<int64_t>();
    } catch (const std::exception&) {
        result.diagnostic = "current format/revision out of range";
        return result;
    }
    if (format != kCurrentFormat) {
        result.diagnostic = "current has unsupported format";
        return result;
    }
    if (revision <= 0) {
        result.diagnostic = "current revision must be positive";
        return result;
    }
    if (!j.contains("manifest") || !j["manifest"].is_string() ||
        !j.contains("scene") || !j["scene"].is_string() ||
        !j.contains("manifestHash") || !j["manifestHash"].is_string() ||
        !j.contains("sceneHash") || !j["sceneHash"].is_string()) {
        result.diagnostic = "current is missing required fields";
        return result;
    }

    const std::string expectedManifest =
        "revisions/" + std::to_string(revision) + "/manifest.json";
    const std::string expectedScene =
        "revisions/" + std::to_string(revision) + "/scene.scene";
    if (j["manifest"].get<std::string>() != expectedManifest ||
        j["scene"].get<std::string>() != expectedScene) {
        result.diagnostic = "current paths do not match revision layout";
        return result;
    }
    const std::string manifestHash = j["manifestHash"].get<std::string>();
    const std::string sceneHash = j["sceneHash"].get<std::string>();
    if (!IsHex16(manifestHash) || !IsHex16(sceneHash)) {
        result.diagnostic = "current has malformed checksums";
        return result;
    }

    const std::string manifestAbs = JoinPath(meta, expectedManifest);
    const std::string sceneAbs = JoinPath(meta, expectedScene);
    std::string manifestBytes;
    std::string sceneBytes;
    if (!ReadAll(manifestAbs, &manifestBytes)) {
        result.diagnostic = "current references a missing manifest";
        return result;
    }
    if (!ReadAll(sceneAbs, &sceneBytes)) {
        result.diagnostic = "current references a missing scene";
        return result;
    }
    if (HashBytes(manifestBytes) != manifestHash ||
        HashBytes(sceneBytes) != sceneHash) {
        result.diagnostic = "revision file checksum mismatch";
        return result;
    }

    m_State = RevisionOpenState::Current;
    m_Revision = revision;
    m_NextRevision = revision + 1;
    m_HasBaseline = true;
    m_BaselineHash = HashBytes(text);
    m_ManifestPath = manifestAbs;
    m_ScenePath = sceneAbs;
    result.state = RevisionOpenState::Current;
    result.revision = revision;
    result.manifestPath = manifestAbs;
    result.scenePath = sceneAbs;
    return result;
}

bool RevisionStore::ReadCommittedScene(const std::string& legacyScenePath,
                                       std::string* out) const {
    if (!out) return false;
    const std::string& path =
        m_State == RevisionOpenState::Current ? m_ScenePath : legacyScenePath;
    return ReadAll(path, out);
}

RevisionPublishResult RevisionStore::Publish(const ProjectRoot& root,
                                             const std::string& manifestJson,
                                             const std::string& sceneBytes) {
    RevisionPublishResult result;
    if (!root.IsValid()) {
        result.diagnostic = "project root is invalid";
        return result;
    }

    const std::string meta = JoinPath(root.Path(), kMetaDir);
    const std::string revisions = JoinPath(meta, "revisions");
    std::error_code ec;
    fs::create_directories(PathFromUtf8(revisions), ec);
    if (ec) {
        result.diagnostic = "cannot create revisions directory: " + ec.message();
        return result;
    }

    // D-1：项目级跨进程写锁；所有写入路径共用同一提交纪律。
    ProjectWriteLock lock;
    if (!lock.Acquire(JoinPath(meta, "write.lock"), &result.diagnostic)) {
        result.diagnostic = "NotCommitted: " + result.diagnostic;
        return result;
    }

    // D-2：提交前重新读取持久化 current，确认仍与打开时的基线一致。
    bool currentExists = false;
    std::string currentBytes;
    if (!ReadCurrent(meta, &currentExists, &currentBytes,
                     &result.diagnostic)) {
        result.recoveryRequired = true;
        result.diagnostic = "RecoveryRequired: " + result.diagnostic;
        return result;
    }
    if (m_HasBaseline) {
        if (!currentExists || HashBytes(currentBytes) != m_BaselineHash) {
            result.diagnostic =
                "NotCommitted: stale baseline (current changed since open)";
            return result;
        }
    } else if (currentExists) {
        result.diagnostic =
            "NotCommitted: stale baseline (current appeared after open)";
        return result;
    }

    // 写端分配：跳过已存在的 revision 目录；失败候选的编号不复用。
    int64_t next = std::max<int64_t>(m_NextRevision, m_Revision + 1);
    if (next < 1) next = 1;
    for (;;) {
        const std::string candidateDir =
            JoinPath(revisions, std::to_string(next));
        if (!fs::exists(PathFromUtf8(candidateDir), ec)) {
            if (ec) {
                result.diagnostic = "cannot probe revision dir: " + ec.message();
                return result;
            }
            break;
        }
        ++next;
    }

    const std::string revDir = JoinPath(revisions, std::to_string(next));
    fs::create_directories(PathFromUtf8(revDir), ec);
    if (ec) {
        result.diagnostic = "cannot create revision dir: " + ec.message();
        return result;
    }
    // 从这一刻起编号已分配；失败候选保留在隔离目录，编号不复用。
    m_NextRevision = next + 1;
    const std::string manifestAbs = JoinPath(revDir, "manifest.json");
    const std::string sceneAbs = JoinPath(revDir, "scene.scene");

    RevisionFault(RevisionFaultPoint::BeforeCandidateManifestWrite);
    const AtomicFileResult manifestWritten =
        WriteFileAtomic(manifestAbs, manifestJson);
    if (!manifestWritten.ok) {
        result.diagnostic = "cannot write candidate manifest: " +
                            std::string(ToString(manifestWritten.error));
        return result;
    }
    RevisionFault(RevisionFaultPoint::AfterManifestWrite);
    const AtomicFileResult sceneWritten =
        WriteFileAtomic(sceneAbs, sceneBytes);
    if (!sceneWritten.ok) {
        result.diagnostic = "cannot write candidate scene: " +
                            std::string(ToString(sceneWritten.error));
        return result;
    }

    // K1.4：发布前全量校验候选（严格清单 + 场景解析）。
    ContentRegistry verify;
    ManifestLoadOptions options;
    options.root = &root;
    options.allowLegacyAbsolute = false;
    options.caseMode = root.CaseMode();
    if (!verify.LoadManifest(manifestAbs, options)) {
        result.diagnostic = "candidate manifest failed strict validation";
        return result;
    }
    RevisionFault(RevisionFaultPoint::AfterSceneWrite);
    SceneSnapshot snapshot;
    std::string sceneError;
    if (!LoadSnapshotFromFile(sceneAbs, snapshot, sceneError)) {
        result.diagnostic = "candidate scene failed validation: " + sceneError;
        return result;
    }
    RevisionFault(RevisionFaultPoint::AfterCandidateValidation);

    // D-2：指针替换前在锁内再次确认基线未被破坏。
    bool finalExists = false;
    std::string finalBytes;
    if (!ReadCurrent(meta, &finalExists, &finalBytes, &result.diagnostic)) {
        result.recoveryRequired = true;
        result.diagnostic = "RecoveryRequired: " + result.diagnostic;
        return result;
    }
    const bool baselineIntact =
        m_HasBaseline
            ? (finalExists && HashBytes(finalBytes) == m_BaselineHash)
            : !finalExists;
    if (!baselineIntact) {
        result.diagnostic =
            "NotCommitted: stale baseline detected before pointer replace";
        return result;
    }

    // 首次发布：在 current 之前写 versioned 标记；若 current 失败则回滚标记。
    const std::string marker = JoinPath(meta, kVersionedMarker);
    const bool markerExisted = fs::exists(PathFromUtf8(marker), ec);
    if (ec) {
        result.recoveryRequired = true;
        result.diagnostic =
            "RecoveryRequired: cannot probe versioned marker";
        return result;
    }
    RevisionFault(RevisionFaultPoint::BeforeMarkerWrite);
    if (!markerExisted) {
        const auto markerWritten =
            WriteFileAtomic(marker, R"({"format":1})");
        if (!markerWritten.ok) {
            result.diagnostic = "cannot write versioned marker: " +
                                std::string(ToString(markerWritten.error));
            return result;
        }
    }

    RevisionFault(RevisionFaultPoint::AfterMarkerWrite);
    nlohmann::json current;
    current["format"] = kCurrentFormat;
    current["revision"] = next;
    current["manifest"] =
        "revisions/" + std::to_string(next) + "/manifest.json";
    current["scene"] = "revisions/" + std::to_string(next) + "/scene.scene";
    current["manifestHash"] = HashBytes(manifestJson);
    current["sceneHash"] = HashBytes(sceneBytes);
    const std::string currentText = current.dump(2);
    const std::string currentAbs = JoinPath(meta, "current");

    RevisionFault(RevisionFaultPoint::BeforeCurrentReplace);
    bool committed = false;
    const AtomicFileResult currentWritten =
        WriteFileAtomic(currentAbs, currentText);
    if (currentWritten.ok) {
        RevisionFault(RevisionFaultPoint::AfterCurrentReplaceBeforeReadback);
        // 提交结果以持久化 current 为准：回读确认指向本次 revision。
        bool verifyExists = false;
        std::string verifyBytes;
        if (!ReadCurrent(meta, &verifyExists, &verifyBytes,
                         &result.diagnostic) ||
            !verifyExists ||
            HashBytes(verifyBytes) != HashBytes(currentText)) {
            result.recoveryRequired = true;
            result.diagnostic =
                "RecoveryRequired: current readback does not match the "
                "candidate after replace";
            return result;
        }
        committed = true;
    } else {
        // 替换失败：重新读取 current 判定实际提交状态。
        bool afterExists = false;
        std::string afterBytes;
        if (!ReadCurrent(meta, &afterExists, &afterBytes,
                         &result.diagnostic)) {
            result.recoveryRequired = true;
            result.diagnostic = "RecoveryRequired: cannot read current after "
                                "failed replace";
            return result;
        }
        if (afterExists && HashBytes(afterBytes) == HashBytes(currentText)) {
            committed = true;
        } else {
            const bool oldStill =
                m_HasBaseline
                    ? (afterExists && HashBytes(afterBytes) == m_BaselineHash)
                    : !afterExists;
            if (!oldStill) {
                result.recoveryRequired = true;
                result.diagnostic =
                    "RecoveryRequired: cannot determine commit state after "
                    "failed current replace";
                return result;
            }
            if (!markerExisted && !m_HasBaseline) {
                // 仅首次发布失败需要回滚标记；版本化工程即使标记曾被删，
                // 也不能在失败时把项目降级成旧格式。
                std::error_code removeEc;
                fs::remove(PathFromUtf8(marker), removeEc);
                if (removeEc) {
                    result.recoveryRequired = true;
                    result.diagnostic =
                        "RecoveryRequired: first-publish marker rollback "
                        "failed";
                    return result;
                }
            }
            result.diagnostic = "NotCommitted: current replace failed";
            return result;
        }
    }

    if (!committed) {
        result.recoveryRequired = true;
        result.diagnostic = "RecoveryRequired: undetermined commit state";
        return result;
    }

    m_State = RevisionOpenState::Current;
    m_Revision = next;
    m_NextRevision = next + 1;
    m_HasBaseline = true;
    m_BaselineHash = HashBytes(currentText);
    m_ManifestPath = manifestAbs;
    m_ScenePath = sceneAbs;
    result.ok = true;
    result.committed = true;
    result.revision = next;
    return result;
}

} // namespace Engine::Content
