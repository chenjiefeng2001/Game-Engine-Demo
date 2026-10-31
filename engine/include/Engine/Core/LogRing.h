#pragma once

/**
 * @file LogRing.h
 * @brief T-6-B Engine 日志环形缓冲区 — 进程级有界、线程安全、可轮询
 *
 * 契约：docs/T-6-B-Console-Logging-GateA-Contract.md（FROZEN）
 *   - 容量 4096 条；溢出保留最新、淘汰最旧，droppedTotal 进程级累计；
 *   - sequence 从 1 开始单调递增，是消费顺序的权威标识；
 *   - category 64B / source 256B / message 2048B，UTF-8 字符边界截断；
 *   - 单次 poll 最多 32 条；空队列成功返回 0 条；
 *   - 记录布局与 ABI 冻结的 EngineLogRecordV1 一致（2392B，自然对齐）。
 *
 * 写入路径只复制结构化字段，不调用 managed/UI、不阻塞。
 * 进程级实例由 Log::Init 创建并通过 Log::GetLogRing() 暴露给 Bridge。
 */

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <vector>

namespace Engine {

inline constexpr size_t kLogRingCapacity  = 4096;
inline constexpr size_t kLogCategoryBytes = 64;
inline constexpr size_t kLogSourceBytes   = 256;
inline constexpr size_t kLogMessageBytes  = 2048;
inline constexpr size_t kLogPollBatch     = 32;

inline constexpr uint8_t kLogFlagCategoryTruncated = 0x01;
inline constexpr uint8_t kLogFlagSourceTruncated   = 0x02;
inline constexpr uint8_t kLogFlagMessageTruncated  = 0x04;

/** 内部记录布局；字段偏移与 EngineLogRecordV1 冻结契约一致。 */
struct EngineLogRecord {
    uint64_t sequence{0};
    int64_t  timestampUtcUs{0};
    uint8_t  level{0};
    uint8_t  flags{0};
    uint16_t categoryLength{0};
    uint16_t sourceLength{0};
    uint16_t messageLength{0};
    char     category[kLogCategoryBytes]{};
    char     source[kLogSourceBytes]{};
    char     message[kLogMessageBytes]{};
};

static_assert(sizeof(EngineLogRecord) == 2392,
              "EngineLogRecord layout is frozen by T-6-B A-10");
static_assert(alignof(EngineLogRecord) == 8,
              "EngineLogRecord must use natural 8-byte alignment");
static_assert(offsetof(EngineLogRecord, sequence) == 0, "frozen offset");
static_assert(offsetof(EngineLogRecord, timestampUtcUs) == 8, "frozen offset");
static_assert(offsetof(EngineLogRecord, level) == 16, "frozen offset");
static_assert(offsetof(EngineLogRecord, flags) == 17, "frozen offset");
static_assert(offsetof(EngineLogRecord, categoryLength) == 18, "frozen offset");
static_assert(offsetof(EngineLogRecord, sourceLength) == 20, "frozen offset");
static_assert(offsetof(EngineLogRecord, messageLength) == 22, "frozen offset");
static_assert(offsetof(EngineLogRecord, category) == 24, "frozen offset");
static_assert(offsetof(EngineLogRecord, source) == 88, "frozen offset");
static_assert(offsetof(EngineLogRecord, message) == 344, "frozen offset");

class LogRing {
public:
    struct PollStats {
        uint64_t nextSequence{0};
        uint64_t droppedTotal{0};
        uint64_t oldestSequence{0};
        uint64_t newestSequence{0};
    };

    explicit LogRing(size_t capacity = kLogRingCapacity);

    LogRing(const LogRing&) = delete;
    LogRing& operator=(const LogRing&) = delete;

    /**
     * @brief 追加一条记录（多生产者安全）
     * @param level 0..5（Trace..Critical），超界钳制到 5
     * @param timestampUtcUs 源端 Unix epoch UTC 微秒
     * @param message 已格式化 payload；尾部 CR/LF 去除、内部 CR/LF 转空格
     */
    void Push(uint8_t level, int64_t timestampUtcUs,
              std::string_view category, std::string_view source,
              std::string_view message);

    /**
     * @brief 读取 sequence > afterSequence 的记录
     * @param afterSequence 0 表示从当前最旧可读记录开始
     * @param out 输出缓冲，至少可容纳 maxRecords 条
     * @param maxRecords 本次上限；内部再钳制到 kLogPollBatch
     * @param stats 输出统计；out 可为空（只探测范围）
     * @return 写入 out 的记录数（0..min(maxRecords, 32)）
     */
    size_t Poll(uint64_t afterSequence, EngineLogRecord* out,
                size_t maxRecords, PollStats& stats);

    size_t Capacity() const { return m_Capacity; }
    uint64_t DroppedTotal() const;

private:
    uint64_t OldestSequenceLocked() const;

    std::vector<EngineLogRecord> m_Records;
    size_t m_Capacity;
    size_t m_WriteIndex{0};
    size_t m_Count{0};
    uint64_t m_NextSequence{0};
    uint64_t m_DroppedTotal{0};
    mutable std::mutex m_Mutex;
};

} // namespace Engine
