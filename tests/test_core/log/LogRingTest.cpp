// T-6-B B1: native LogRing 原型测试
//
// 契约：docs/T-6-B-Console-Logging-GateA-Contract.md（FROZEN）
// 计划：docs/T-6-B-Console-Logging-GateB-Experiment-Plan.md §B1
//
// 覆盖：
//   - sequence 从 1 开始、单调、无重复；
//   - 4096 容量语义（本文件用小容量等价覆盖溢出/淘汰）；
//   - droppedTotal 累计；poll cursor / 32 条批量上限 / 空队列；
//   - UTF-8 边界截断与 flags；未使用空间填零；空字段；
//   - CR/LF 归一化；多线程写入；
//   - Log::Init 安装 sink：bootstrap、default、category、安装前已有 logger、
//     后续注册 logger 均被捕获且无重复。
//
// 不接 Bridge / managed；本文件只依赖 EngineCore。

#include <gtest/gtest.h>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "Engine/Core/Log.h"
#include "Engine/Core/LogRing.h"

namespace {

using Engine::EngineLogRecord;
using Engine::LogRing;

constexpr size_t kBatch = Engine::kLogPollBatch;

EngineLogRecord PollOne(LogRing& ring, uint64_t afterSequence) {
    EngineLogRecord out[kBatch];
    LogRing::PollStats stats;
    const size_t n = ring.Poll(afterSequence, out, kBatch, stats);
    EXPECT_EQ(n, 1u);
    return out[0];
}

std::vector<EngineLogRecord> DrainAll(const std::shared_ptr<LogRing>& ring) {
    std::vector<EngineLogRecord> records;
    LogRing::PollStats stats;
    uint64_t cursor = 0;
    std::vector<EngineLogRecord> batch(kBatch);
    for (;;) {
        const size_t n = ring->Poll(cursor, batch.data(), batch.size(), stats);
        if (n == 0) break;
        records.insert(records.end(), batch.begin(), batch.begin() + n);
        cursor = stats.nextSequence;
    }
    return records;
}

size_t CountMessageContaining(const std::shared_ptr<LogRing>& ring,
                              std::string_view text) {
    size_t count = 0;
    for (const auto& rec : DrainAll(ring)) {
        if (std::string_view(rec.message, rec.messageLength).find(text) !=
            std::string_view::npos) {
            ++count;
        }
    }
    return count;
}

bool WaitForMessageContaining(const std::shared_ptr<LogRing>& ring,
                              std::string_view text, int timeoutMs = 3000) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
        if (CountMessageContaining(ring, text) > 0) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (std::chrono::steady_clock::now() < deadline);
    return CountMessageContaining(ring, text) > 0;
}

std::optional<EngineLogRecord> FindMessageContaining(
    const std::shared_ptr<LogRing>& ring, std::string_view text) {
    for (const auto& rec : DrainAll(ring)) {
        if (std::string_view(rec.message, rec.messageLength).find(text) !=
            std::string_view::npos) {
            return rec;
        }
    }
    return std::nullopt;
}

} // namespace

// ══════════════════════════════════════════════════════════════════════
// 直接 ring 单元测试（独立实例，不依赖进程级 Log 状态）
// ══════════════════════════════════════════════════════════════════════

TEST(LogRingTest, FirstPushGetsSequenceOneAndFrozenFields) {
    LogRing ring;
    ring.Push(3, 1720000000000000LL, "Physics", "Body.cpp:7", "hello");

    EngineLogRecord out[kBatch];
    LogRing::PollStats stats;
    const size_t n = ring.Poll(0, out, kBatch, stats);

    ASSERT_EQ(n, 1u);
    EXPECT_EQ(out[0].sequence, 1u);
    EXPECT_EQ(out[0].timestampUtcUs, 1720000000000000LL);
    EXPECT_EQ(out[0].level, 3);
    EXPECT_EQ(out[0].flags, 0);
    EXPECT_EQ(out[0].categoryLength, 7u);
    EXPECT_EQ(std::string(out[0].category, out[0].categoryLength), "Physics");
    EXPECT_EQ(out[0].sourceLength, 10u);
    EXPECT_EQ(std::string(out[0].source, out[0].sourceLength), "Body.cpp:7");
    EXPECT_EQ(out[0].messageLength, 5u);
    EXPECT_EQ(std::string(out[0].message, out[0].messageLength), "hello");
    EXPECT_EQ(stats.nextSequence, 1u);
    EXPECT_EQ(stats.oldestSequence, 1u);
    EXPECT_EQ(stats.newestSequence, 1u);
    EXPECT_EQ(stats.droppedTotal, 0u);
}

TEST(LogRingTest, PollAfterSequenceReturnsOnlyNewerRecords) {
    LogRing ring;
    ring.Push(2, 100, "c", "", "one");
    ring.Push(2, 200, "c", "", "two");
    ring.Push(2, 300, "c", "", "three");

    EngineLogRecord out[kBatch];
    LogRing::PollStats stats;
    const size_t n = ring.Poll(1, out, kBatch, stats);

    ASSERT_EQ(n, 2u);
    EXPECT_EQ(out[0].sequence, 2u);
    EXPECT_EQ(out[1].sequence, 3u);
    EXPECT_EQ(stats.nextSequence, 3u);
    EXPECT_EQ(stats.newestSequence, 3u);
}

TEST(LogRingTest, PollEmptyRingReturnsSuccessZero) {
    LogRing ring;
    EngineLogRecord out[kBatch];
    LogRing::PollStats stats;
    const size_t n = ring.Poll(0, out, kBatch, stats);

    EXPECT_EQ(n, 0u);
    EXPECT_EQ(stats.nextSequence, 0u);
    EXPECT_EQ(stats.oldestSequence, 0u);
    EXPECT_EQ(stats.newestSequence, 0u);
    EXPECT_EQ(stats.droppedTotal, 0u);
}

TEST(LogRingTest, PollAtOrPastNewestEchoesCursor) {
    LogRing ring;
    ring.Push(2, 1, "c", "", "one");
    ring.Push(2, 2, "c", "", "two");

    EngineLogRecord out[kBatch];
    LogRing::PollStats stats;
    EXPECT_EQ(ring.Poll(2, out, kBatch, stats), 0u);
    EXPECT_EQ(stats.nextSequence, 2u);

    EXPECT_EQ(ring.Poll(99, out, kBatch, stats), 0u);
    EXPECT_EQ(stats.nextSequence, 99u);
    EXPECT_EQ(stats.newestSequence, 2u);
}

TEST(LogRingTest, PollBatchIsCappedAt32) {
    LogRing ring;
    for (int i = 0; i < 40; ++i) {
        ring.Push(2, i, "c", "", "m" + std::to_string(i));
    }

    EngineLogRecord out[64];
    LogRing::PollStats stats;
    const size_t first = ring.Poll(0, out, 64, stats);
    EXPECT_EQ(first, 32u);
    EXPECT_EQ(stats.nextSequence, 32u);
    EXPECT_EQ(out[0].sequence, 1u);
    EXPECT_EQ(out[31].sequence, 32u);

    const size_t second = ring.Poll(stats.nextSequence, out, 64, stats);
    EXPECT_EQ(second, 8u);
    EXPECT_EQ(stats.nextSequence, 40u);
}

TEST(LogRingTest, PollHonorsMaxRecordsSmallerThanBatch) {
    LogRing ring;
    ring.Push(2, 1, "c", "", "one");
    ring.Push(2, 2, "c", "", "two");

    EngineLogRecord out[kBatch];
    LogRing::PollStats stats;
    EXPECT_EQ(ring.Poll(0, out, 1, stats), 1u);
    EXPECT_EQ(stats.nextSequence, 1u);
}

TEST(LogRingTest, PollProbeWithoutBufferReportsRangeOnly) {
    LogRing ring(4);
    ring.Push(2, 1, "c", "", "one");
    ring.Push(2, 2, "c", "", "two");

    LogRing::PollStats stats;
    const size_t n = ring.Poll(0, nullptr, 0, stats);
    EXPECT_EQ(n, 0u);
    EXPECT_EQ(stats.oldestSequence, 1u);
    EXPECT_EQ(stats.newestSequence, 2u);
    EXPECT_EQ(stats.nextSequence, 0u);
    EXPECT_EQ(stats.droppedTotal, 0u);
}

TEST(LogRingTest, OverflowRetainsNewestAndCountsDropped) {
    LogRing ring(8);
    for (int i = 1; i <= 10; ++i) {
        ring.Push(2, i, "c", "", "m" + std::to_string(i));
    }

    EXPECT_EQ(ring.DroppedTotal(), 2u);

    EngineLogRecord out[16];
    LogRing::PollStats stats;
    const size_t n = ring.Poll(0, out, 16, stats);
    ASSERT_EQ(n, 8u);
    EXPECT_EQ(out[0].sequence, 3u);
    EXPECT_EQ(out[7].sequence, 10u);
    EXPECT_EQ(stats.oldestSequence, 3u);
    EXPECT_EQ(stats.newestSequence, 10u);
    EXPECT_EQ(stats.nextSequence, 10u);
    EXPECT_EQ(stats.droppedTotal, 2u);
}

TEST(LogRingTest, DefaultCapacityOverflowKeepsNewest4096) {
    LogRing ring;
    EXPECT_EQ(ring.Capacity(), Engine::kLogRingCapacity);
    EXPECT_EQ(ring.Capacity(), 4096u);

    const int total = 4100;
    for (int i = 1; i <= total; ++i) {
        ring.Push(2, i, "c", "", "m");
    }

    EXPECT_EQ(ring.DroppedTotal(), 4u);

    EngineLogRecord out[kBatch];
    LogRing::PollStats stats;
    EXPECT_EQ(ring.Poll(0, out, kBatch, stats), 32u);
    EXPECT_EQ(out[0].sequence, 5u);
    EXPECT_EQ(stats.oldestSequence, 5u);
    EXPECT_EQ(stats.newestSequence, static_cast<uint64_t>(total));

    uint64_t cursor = 0;
    size_t seen = 0;
    for (;;) {
        const size_t n = ring.Poll(cursor, out, kBatch, stats);
        if (n == 0) break;
        seen += n;
        cursor = stats.nextSequence;
    }
    EXPECT_EQ(seen, 4096u);
    EXPECT_EQ(cursor, static_cast<uint64_t>(total));
}

TEST(LogRingTest, PollAfterEvictedSequenceStartsAtOldestAndExposesGap) {
    LogRing ring(4);
    for (int i = 1; i <= 6; ++i) {
        ring.Push(2, i, "c", "", "m" + std::to_string(i));
    }

    EngineLogRecord out[8];
    LogRing::PollStats stats;
    const size_t n = ring.Poll(1, out, 8, stats);
    ASSERT_EQ(n, 4u);
    EXPECT_EQ(out[0].sequence, 3u);
    EXPECT_EQ(stats.oldestSequence, 3u);
    EXPECT_EQ(stats.droppedTotal, 2u);
}

TEST(LogRingTest, MessageTruncationStopsAtUtf8Boundary) {
    LogRing ring;
    const std::string message = std::string(2047, 'a') + "\xF0\x9F\x98\x80";
    ASSERT_EQ(message.size(), 2051u);
    ring.Push(2, 0, "c", "", message);

    const EngineLogRecord rec = PollOne(ring, 0);
    EXPECT_EQ(rec.messageLength, 2047u);
    EXPECT_EQ(rec.flags & Engine::kLogFlagMessageTruncated,
              Engine::kLogFlagMessageTruncated);
    EXPECT_EQ(std::string(rec.message, rec.messageLength), std::string(2047, 'a'));
}

TEST(LogRingTest, CategoryTruncationStopsAtUtf8Boundary) {
    LogRing ring;
    const std::string euro = "\xE2\x82\xAC";
    std::string category;
    for (int i = 0; i < 22; ++i) category += euro; // 66 bytes
    ASSERT_EQ(category.size(), 66u);

    ring.Push(2, 0, category, "", "m");
    const EngineLogRecord rec = PollOne(ring, 0);

    EXPECT_EQ(rec.categoryLength, 63u);
    EXPECT_EQ(rec.flags & Engine::kLogFlagCategoryTruncated,
              Engine::kLogFlagCategoryTruncated);
    std::string expected;
    for (int i = 0; i < 21; ++i) expected += euro; // 63 bytes, 完整 codepoint
    EXPECT_EQ(std::string(rec.category, rec.categoryLength), expected);
}

TEST(LogRingTest, CategoryExactCapacityIsNotTruncated) {
    LogRing ring;
    const std::string category(Engine::kLogCategoryBytes, 'c');
    ring.Push(2, 0, category, "", "m");
    const EngineLogRecord rec = PollOne(ring, 0);

    EXPECT_EQ(rec.categoryLength, Engine::kLogCategoryBytes);
    EXPECT_EQ(rec.flags & Engine::kLogFlagCategoryTruncated, 0);
    EXPECT_EQ(std::string(rec.category, rec.categoryLength), category);
}

TEST(LogRingTest, SourceTruncationSetsFlag) {
    LogRing ring;
    const std::string source(300, 's');
    ring.Push(2, 0, "c", source, "m");
    const EngineLogRecord rec = PollOne(ring, 0);

    EXPECT_EQ(rec.sourceLength, Engine::kLogSourceBytes);
    EXPECT_EQ(rec.flags & Engine::kLogFlagSourceTruncated,
              Engine::kLogFlagSourceTruncated);
    EXPECT_EQ(std::string(rec.source, rec.sourceLength),
              std::string(Engine::kLogSourceBytes, 's'));
}

TEST(LogRingTest, CombinedTruncationFlagsAreAllSet) {
    LogRing ring;
    ring.Push(2, 0, std::string(100, 'c'), std::string(300, 's'),
              std::string(3000, 'm'));
    const EngineLogRecord rec = PollOne(ring, 0);

    EXPECT_EQ(rec.flags, static_cast<uint8_t>(
                             Engine::kLogFlagCategoryTruncated |
                             Engine::kLogFlagSourceTruncated |
                             Engine::kLogFlagMessageTruncated));
}

TEST(LogRingTest, EmptyMessageAndFieldsAreAllowed) {
    LogRing ring;
    ring.Push(2, 0, "", "", "");
    const EngineLogRecord rec = PollOne(ring, 0);

    EXPECT_EQ(rec.sequence, 1u);
    EXPECT_EQ(rec.categoryLength, 0u);
    EXPECT_EQ(rec.sourceLength, 0u);
    EXPECT_EQ(rec.messageLength, 0u);
    EXPECT_EQ(rec.flags, 0);
}

TEST(LogRingTest, ReusedSlotUnusedSpaceIsZeroed) {
    LogRing ring(1);
    ring.Push(2, 0, std::string(50, 'c'), std::string(50, 's'),
              std::string(100, 'A'));
    ASSERT_EQ(PollOne(ring, 0).messageLength, 100u);

    ring.Push(2, 0, "c", "s", "b");
    const EngineLogRecord rec = PollOne(ring, 0);

    ASSERT_EQ(rec.sequence, 2u);
    EXPECT_EQ(rec.messageLength, 1u);
    for (size_t i = 1; i < Engine::kLogMessageBytes; ++i) {
        EXPECT_EQ(rec.message[i], '\0') << "message byte " << i;
    }
    for (size_t i = 1; i < Engine::kLogCategoryBytes; ++i) {
        EXPECT_EQ(rec.category[i], '\0') << "category byte " << i;
    }
    for (size_t i = 1; i < Engine::kLogSourceBytes; ++i) {
        EXPECT_EQ(rec.source[i], '\0') << "source byte " << i;
    }
}

TEST(LogRingTest, NewlinesAreNormalized) {
    LogRing ring;
    ring.Push(2, 0, "", "", "line1\r\nline2\r\n");
    ring.Push(2, 0, "", "", "\r\n\r\n");
    ring.Push(2, 0, "", "", "no-newline");

    std::vector<EngineLogRecord> all;
    EngineLogRecord out[kBatch];
    LogRing::PollStats stats;
    uint64_t cursor = 0;
    for (;;) {
        const size_t n = ring.Poll(cursor, out, kBatch, stats);
        if (n == 0) break;
        all.insert(all.end(), out, out + n);
        cursor = stats.nextSequence;
    }

    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(std::string(all[0].message, all[0].messageLength), "line1  line2");
    EXPECT_EQ(all[0].flags, 0);
    EXPECT_EQ(all[1].messageLength, 0u);
    EXPECT_EQ(all[1].flags, 0);
    EXPECT_EQ(std::string(all[2].message, all[2].messageLength), "no-newline");
}

TEST(LogRingTest, LevelIsClampedToCritical) {
    LogRing ring;
    ring.Push(200, 0, "c", "", "m");
    EXPECT_EQ(PollOne(ring, 0).level, 5);
}

TEST(LogRingTest, ConcurrentWritersKeepSequenceUniqueAndContiguous) {
    LogRing ring;
    constexpr int kThreads = 4;
    constexpr int kPerThread = 250;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&ring, t]() {
            for (int i = 0; i < kPerThread; ++i) {
                ring.Push(2, 1000 + t * 1000 + i, "MT", "",
                          "record-" + std::to_string(t) + "-" +
                              std::to_string(i));
            }
        });
    }
    for (auto& th : threads) th.join();

    EXPECT_EQ(ring.DroppedTotal(), 0u);

    std::vector<EngineLogRecord> all;
    EngineLogRecord out[kBatch];
    LogRing::PollStats stats;
    uint64_t cursor = 0;
    for (;;) {
        const size_t n = ring.Poll(cursor, out, kBatch, stats);
        if (n == 0) break;
        all.insert(all.end(), out, out + n);
        cursor = stats.nextSequence;
    }
    ASSERT_EQ(all.size(), static_cast<size_t>(kThreads * kPerThread));
    for (size_t i = 0; i < all.size(); ++i) {
        EXPECT_EQ(all[i].sequence, static_cast<uint64_t>(i + 1));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Log::Init sink 安装 / 覆盖集成测试（进程级 ring）
//
// SetUpTestSuite 在 Log::Init 之前创建一个已注册 logger，用于验证
// apply_all 补齐；bootstrap 日志断言在 Init 后由第一个用例完成。
// ══════════════════════════════════════════════════════════════════════

class LogRingIntegrationTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        s_ScratchDir = std::filesystem::temp_directory_path() /
                       ("t6b_logring_" + std::to_string(stamp));
        std::error_code ec;
        std::filesystem::create_directories(s_ScratchDir, ec);
        s_LogFile = (s_ScratchDir / "engine_test.log").string();

        // 模拟 LogRing 安装前已创建的 logger
        s_PreExisting =
            spdlog::create<spdlog::sinks::null_sink_mt>("T6BPreExisting");

        Engine::Log::Init(s_LogFile);
        s_Ring = Engine::Log::GetLogRing();
    }

    static void TearDownTestSuite() {
        if (Engine::Log::IsInitialized()) {
            Engine::Log::Shutdown();
        }
        std::error_code ec;
        std::filesystem::remove_all(s_ScratchDir, ec);
    }

    static std::filesystem::path s_ScratchDir;
    static std::string s_LogFile;
    static std::shared_ptr<spdlog::logger> s_PreExisting;
    static std::shared_ptr<LogRing> s_Ring;
};

std::filesystem::path LogRingIntegrationTest::s_ScratchDir;
std::string LogRingIntegrationTest::s_LogFile;
std::shared_ptr<spdlog::logger> LogRingIntegrationTest::s_PreExisting;
std::shared_ptr<LogRing> LogRingIntegrationTest::s_Ring;

TEST_F(LogRingIntegrationTest, BootstrapLogIsCapturedFromInit) {
    ASSERT_NE(s_Ring, nullptr);
    ASSERT_TRUE(WaitForMessageContaining(s_Ring, "Log system initialized"));

    const auto all = DrainAll(s_Ring);
    ASSERT_FALSE(all.empty());
    EXPECT_EQ(all.front().sequence, 1u);
    EXPECT_EQ(std::string(all.front().category, all.front().categoryLength),
              "Engine");
    EXPECT_EQ(all.front().level, 2);
    EXPECT_NE(std::string(all.front().message, all.front().messageLength).find(
                  "Log system initialized"),
              std::string::npos);
}

TEST_F(LogRingIntegrationTest, PreExistingLoggerIsPatchedByApplyAll) {
    ASSERT_NE(s_PreExisting, nullptr);
    s_PreExisting->info("pre-existing-logger-marker");
    ASSERT_TRUE(WaitForMessageContaining(s_Ring, "pre-existing-logger-marker"));

    EXPECT_EQ(CountMessageContaining(s_Ring, "pre-existing-logger-marker"), 1u);
    const auto rec = FindMessageContaining(s_Ring, "pre-existing-logger-marker");
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(std::string(rec->category, rec->categoryLength), "T6BPreExisting");
    EXPECT_EQ(rec->level, 2);
}

TEST_F(LogRingIntegrationTest, DefaultLoggerLogCapturedExactlyOnce) {
    Engine::Log::Info("default-marker-11");
    ASSERT_TRUE(WaitForMessageContaining(s_Ring, "default-marker-11"));

    EXPECT_EQ(CountMessageContaining(s_Ring, "default-marker-11"), 1u);
    const auto rec = FindMessageContaining(s_Ring, "default-marker-11");
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(std::string(rec->category, rec->categoryLength), "Engine");
    EXPECT_EQ(rec->level, 2);
    EXPECT_EQ(rec->sourceLength, 0u);
}

TEST_F(LogRingIntegrationTest, MacroLogCapturesSourceLocation) {
    ENGINE_LOG_WARN("T6BMacro", "macro-marker-13");
    ASSERT_TRUE(WaitForMessageContaining(s_Ring, "macro-marker-13"));

    const auto rec = FindMessageContaining(s_Ring, "macro-marker-13");
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(std::string(rec->category, rec->categoryLength), "T6BMacro");
    EXPECT_EQ(rec->level, 3);
    ASSERT_GT(rec->sourceLength, 0u);
    const std::string source(rec->source, rec->sourceLength);
    EXPECT_NE(source.find("LogRingTest.cpp"), std::string::npos);
    EXPECT_NE(source.find(':'), std::string::npos);
}

TEST_F(LogRingIntegrationTest, CategoryLoggerInheritsRingSinkExactlyOnce) {
    auto logger = Engine::Log::Get("T6BCategory");
    ASSERT_NE(logger, nullptr);
    logger->info("category-marker-17");
    ASSERT_TRUE(WaitForMessageContaining(s_Ring, "category-marker-17"));

    EXPECT_EQ(CountMessageContaining(s_Ring, "category-marker-17"), 1u);
    const auto rec = FindMessageContaining(s_Ring, "category-marker-17");
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(std::string(rec->category, rec->categoryLength), "T6BCategory");
}

TEST_F(LogRingIntegrationTest, DuplicateMessagesAreNotDeduplicated) {
    auto logger = Engine::Log::Get("T6BDuplicate");
    ASSERT_NE(logger, nullptr);
    logger->info("duplicate-marker-19");
    logger->info("duplicate-marker-19");
    ASSERT_TRUE(WaitForMessageContaining(s_Ring, "duplicate-marker-19"));

    EXPECT_EQ(CountMessageContaining(s_Ring, "duplicate-marker-19"), 2u);
}

TEST_F(LogRingIntegrationTest, PollCursorCoversEverySequenceExactlyOnce) {
    Engine::Log::Info("drain-sentinel-23");
    ASSERT_TRUE(WaitForMessageContaining(s_Ring, "drain-sentinel-23"));

    const auto all = DrainAll(s_Ring);
    ASSERT_FALSE(all.empty());
    for (size_t i = 0; i < all.size(); ++i) {
        EXPECT_EQ(all[i].sequence, static_cast<uint64_t>(i + 1));
    }
    EXPECT_EQ(s_Ring->DroppedTotal(), 0u);
}

TEST_F(LogRingIntegrationTest, GetLogRingIsStableAcrossCalls) {
    EXPECT_EQ(Engine::Log::GetLogRing(), s_Ring);
}
