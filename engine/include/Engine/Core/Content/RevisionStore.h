#pragma once

/**
 * @file RevisionStore.h
 * @brief Content v2 M1-D：K1 单一提交点（current）与不可变 revision
 *
 * 布局：
 *   <ProjectRoot>/.engine-content/current
 *   <ProjectRoot>/.engine-content/revisions/<N>/manifest.json
 *   <ProjectRoot>/.engine-content/revisions/<N>/scene.scene
 *
 * 读取端只认 current；禁止扫描 revision 目录决定当前版本。
 * 写入端先写候选文件并全量校验，最后单次原子替换 current；失败旧 current 权威。
 */

#include "Engine/Core/Content/ProjectPaths.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace Engine::Content {

/// 测试专用确定性故障注入点（生产默认为空，不改变正常语义）。
enum class RevisionFaultPoint : uint8_t {
    BeforeCandidateManifestWrite = 0,
    AfterManifestWrite,
    AfterSceneWrite,
    AfterCandidateValidation,
    BeforeMarkerWrite,
    AfterMarkerWrite,
    BeforeCurrentReplace,
    AfterCurrentReplaceBeforeReadback,
};

using RevisionFaultHook = std::function<void(RevisionFaultPoint)>;
void SetRevisionFaultHookForTesting(RevisionFaultHook hook);

enum class ContentFaultPoint : uint8_t { BeforeRenameSourceCleanup };
using ContentFaultHook = std::function<void(ContentFaultPoint)>;
void SetContentFaultHookForTesting(ContentFaultHook hook);
void InvokeContentFaultForTesting(ContentFaultPoint point);

enum class RevisionOpenState : uint8_t {
    Unversioned = 0,   ///< 没有 current：旧工程/首次打开
    Current,           ///< current 有效且引用的 revision 文件与校验和一致
    RecoveryRequired,  ///< current 损坏、引用缺失或校验失败
};

struct RevisionOpenResult {
    RevisionOpenState state = RevisionOpenState::RecoveryRequired;
    int64_t revision = 0;
    std::string manifestPath;   ///< 绝对 UTF-8（Current 时）
    std::string scenePath;      ///< 绝对 UTF-8（Current 时）
    std::string diagnostic;
};

struct RevisionPublishResult {
    bool ok = false;
    bool committed = false;   ///< current 已替换；false = NotCommitted
    /// 提交状态不可判定（D-2）：调用方必须进入 RecoveryRequired。
    bool recoveryRequired = false;
    int64_t revision = 0;
    std::string diagnostic;
};

class RevisionStore {
public:
    /// 只读 current。缺失 → Unversioned；损坏/校验失败 → RecoveryRequired。
    RevisionOpenResult Open(const ProjectRoot& root);

    bool IsVersioned() const { return m_State == RevisionOpenState::Current; }
    int64_t Revision() const { return m_Revision; }
    const std::string& CommittedManifestPath() const { return m_ManifestPath; }
    const std::string& CommittedScenePath() const { return m_ScenePath; }

    /// rename 提交复用“已提交 Scene”：Current → revision 内场景；否则读旧场景文件。
    bool ReadCommittedScene(const std::string& legacyScenePath,
                            std::string* out) const;

    /// 候选 revision：写 manifest+scene → 严格校验 → 原子替换 current。
    RevisionPublishResult Publish(const ProjectRoot& root,
                                  const std::string& manifestJson,
                                  const std::string& sceneBytes);

    /// FNV-1a 64（current 指针中的文件校验和）。
    static std::string HashBytes(std::string_view bytes);

private:
    RevisionOpenState m_State = RevisionOpenState::Unversioned;
    int64_t m_Revision = 0;
    /// 打开/上次提交时的 current 字节哈希；提交前必须与持久化 current 一致。
    bool m_HasBaseline = false;
    std::string m_BaselineHash;
    /// 下一次分配的编号；失败候选的编号不复用（允许空洞）。
    int64_t m_NextRevision = 1;
    std::string m_ManifestPath;
    std::string m_ScenePath;
};

} // namespace Engine::Content
