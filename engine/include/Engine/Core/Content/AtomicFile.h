#pragma once

/**
 * @file AtomicFile.h
 * @brief Content v2 M1-C：源文件可靠写入原语（K2.2/K2.10）
 *
 * 同一目录临时文件 → 写入/flush → 回读校验 → 原子发布。
 * 失败时目标文件保持不变；不覆盖复制用于 rename 的 prepare 阶段。
 */

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace Engine::Content {

enum class AtomicFileError : uint8_t {
    None = 0,
    CannotReadSource,
    TempCreate,
    Write,
    Flush,
    Verify,
    TargetExists,
    Replace,
    /// 测试专用确定性注入（生产路径不会产生）。
    InjectedFailure,
};

/// 测试专用故障注入点；生产默认空。
enum class AtomicFaultPoint : uint8_t {
    BeforeTempWrite = 0,
    AfterTempWriteBeforeVerify,
    BeforeReplace,
};

using AtomicFaultHook = std::function<bool(AtomicFaultPoint,
                                            const std::string& tempPath,
                                            const std::string& targetPath)>;
void SetAtomicFaultHookForTesting(AtomicFaultHook hook);

const char* ToString(AtomicFileError error);

struct AtomicFileResult {
    bool ok = false;
    AtomicFileError error = AtomicFileError::None;
    std::string diagnostic;
};

/// 原子替换目标文件（临时文件 + flush + 回读校验 + 单次替换）。
AtomicFileResult WriteFileAtomic(std::string_view utf8Target,
                                 std::string_view bytes);

/// 不覆盖地复制源到目标：源 → 目标目录临时文件 → 校验 → 非覆盖发布。
/// 目标已存在 → TargetExists，源/目标均不变。
AtomicFileResult CopyFileNoOverwrite(std::string_view utf8Source,
                                     std::string_view utf8Target);

} // namespace Engine::Content
