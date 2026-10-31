/**
 * @file LogRing.cpp
 * @brief T-6-B Engine 日志环形缓冲区实现
 */

#include "Engine/Core/LogRing.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace Engine {
namespace {

/**
 * 返回不超过 limit 的最大前缀长度，且不切断 UTF-8 codepoint。
 * 无效序列按 lead byte 声明的长度处理；无法容纳时退回 lead byte 之前。
 */
size_t Utf8TruncateLength(const char* data, size_t size, size_t limit) {
    if (size <= limit) return size;

    size_t n = limit;
    while (n > 0 && (static_cast<unsigned char>(data[n - 1]) & 0xC0) == 0x80) {
        --n;
    }
    if (n == 0) return 0;

    const unsigned char lead = static_cast<unsigned char>(data[n - 1]);
    size_t seq = 1;
    if ((lead & 0xE0) == 0xC0) {
        seq = 2;
    } else if ((lead & 0xF0) == 0xE0) {
        seq = 3;
    } else if ((lead & 0xF8) == 0xF0) {
        seq = 4;
    }
    if (n - 1 + seq > limit) {
        --n;
    }
    return n;
}

/** 复制并截断字段；未使用空间填零；截断时置 flags。 */
uint16_t CopyField(char* dst, size_t capacity, std::string_view src,
                   uint8_t& flags, uint8_t flagBit) {
    const size_t n = Utf8TruncateLength(src.data(), src.size(), capacity);
    if (n > 0) {
        std::memcpy(dst, src.data(), n);
    }
    std::memset(dst + n, 0, capacity - n);
    if (n < src.size()) {
        flags |= flagBit;
    }
    return static_cast<uint16_t>(n);
}

} // namespace

LogRing::LogRing(size_t capacity)
    : m_Records(capacity == 0 ? 1 : capacity),
      m_Capacity(capacity == 0 ? 1 : capacity) {}

void LogRing::Push(uint8_t level, int64_t timestampUtcUs,
                   std::string_view category, std::string_view source,
                   std::string_view message) {
    std::string normalized;
    if (message.find_first_of("\r\n") != std::string_view::npos) {
        normalized.assign(message);
        while (!normalized.empty() &&
               (normalized.back() == '\r' || normalized.back() == '\n')) {
            normalized.pop_back();
        }
        for (char& c : normalized) {
            if (c == '\r' || c == '\n') {
                c = ' ';
            }
        }
        message = normalized;
    }

    std::lock_guard<std::mutex> lock(m_Mutex);

    EngineLogRecord& rec = m_Records[m_WriteIndex];
    ++m_NextSequence;
    rec.sequence = m_NextSequence;
    rec.timestampUtcUs = timestampUtcUs;
    rec.level = level > 5 ? 5 : level;
    rec.flags = 0;
    rec.categoryLength = CopyField(rec.category, kLogCategoryBytes, category,
                                   rec.flags, kLogFlagCategoryTruncated);
    rec.sourceLength = CopyField(rec.source, kLogSourceBytes, source,
                                 rec.flags, kLogFlagSourceTruncated);
    rec.messageLength = CopyField(rec.message, kLogMessageBytes, message,
                                  rec.flags, kLogFlagMessageTruncated);

    m_WriteIndex = (m_WriteIndex + 1) % m_Capacity;
    if (m_Count < m_Capacity) {
        ++m_Count;
    } else {
        ++m_DroppedTotal;
    }
}

size_t LogRing::Poll(uint64_t afterSequence, EngineLogRecord* out,
                     size_t maxRecords, PollStats& stats) {
    std::lock_guard<std::mutex> lock(m_Mutex);

    stats.nextSequence = afterSequence;
    stats.droppedTotal = m_DroppedTotal;
    stats.oldestSequence = OldestSequenceLocked();
    stats.newestSequence = m_Count == 0 ? 0 : m_NextSequence;

    if (m_Count == 0 || out == nullptr || maxRecords == 0) {
        return 0;
    }

    uint64_t startSeq = (afterSequence == 0) ? stats.oldestSequence
                                             : afterSequence + 1;
    if (startSeq < stats.oldestSequence) {
        startSeq = stats.oldestSequence;
    }
    if (startSeq > m_NextSequence) {
        return 0;
    }

    const size_t available =
        static_cast<size_t>(m_NextSequence - startSeq + 1);
    size_t n = (std::min)(available, maxRecords);
    n = (std::min)(n, kLogPollBatch);

    for (size_t i = 0; i < n; ++i) {
        const size_t index =
            static_cast<size_t>((startSeq - 1 + i) % m_Capacity);
        out[i] = m_Records[index];
    }

    stats.nextSequence = startSeq + n - 1;
    return n;
}

uint64_t LogRing::DroppedTotal() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_DroppedTotal;
}

uint64_t LogRing::OldestSequenceLocked() const {
    if (m_Count == 0) return 0;
    return m_NextSequence - static_cast<uint64_t>(m_Count) + 1;
}

} // namespace Engine
