#pragma once

/**
 * @file MemoryTracker.h
 * @brief 分类内存追踪 — 支持 Retail/Debug/Stack/GPU 四类 + 实时面板
 *
 * 内存分类：
 *   Retail  — 游戏逻辑分配（new/delete 默认归入此类）
 *   Debug   — 仅在 Debug 模式产生的额外分配（可通过 MemCategory 切换）
 *   Stack   — StackAllocator 内存池（使用量而非分配次数）
 *   GPU     — 显存/Host 端 buffer（预留）
 */

#include "Engine/Types.h"
#include <cstddef>
#include <string>
#include <array>

namespace Engine {

    // ============================================================
    // 内存类别
    // ============================================================
    enum class MemCategory : uint8 {
        Retail  = 0,    // 游戏逻辑堆
        Debug   = 1,    // Debug 工具开销
        Stack   = 2,    // StackAllocator 内存池
        GPU     = 3,    // GPU 资源
        COUNT
    };

    inline const char* MemCategoryName(MemCategory cat) {
        switch (cat) {
            case MemCategory::Retail: return "Retail Heap";
            case MemCategory::Debug:  return "Debug  Heap";
            case MemCategory::Stack:  return "Stack  Pool";
            case MemCategory::GPU:    return "GPU Mem";
            default:                  return "Unknown";
        }
    }

    inline uint32 MemCategoryColor(MemCategory cat) {
        // ABGR
        switch (cat) {
            case MemCategory::Retail: return 0xFF66CCFF;  // 橙色
            case MemCategory::Debug:  return 0xFF6666FF;  // 红色
            case MemCategory::Stack:  return 0xFF88CC00;  // 绿色
            case MemCategory::GPU:    return 0xFFFF8800;  // 蓝色
            default:                  return 0xFFAAAAAA;
        }
    }

    // ============================================================
    // 分类统计快照
    // ============================================================
    struct MemCategoryStat {
        size_t current = 0;
        size_t peak    = 0;
        size_t total   = 0;
        size_t count   = 0;
        size_t frameAlloc = 0;
        size_t frameFree  = 0;
        int    frameAllocCnt = 0;
        int    frameFreeCnt  = 0;
    };

    // ============================================================
    // 分配记录（供详细面板显示）
    // ============================================================
    static constexpr int kRecentMax = 500;

    struct MemAllocRecord {
        MemCategory cat;
        size_t      size;
        double      timestamp;
        char        tag[32];         // 分配站点标签
        bool        isFree;          // true=释放, false=分配
    };

    // 分配站点标签统计
    struct MemTagStat {
        std::string tag;
        size_t bytes = 0;
        size_t count = 0;
    };

    // ============================================================
    // MemoryTracker
    // ============================================================
    class MemoryTracker {
    public:
        static constexpr int kHistoryFrames = 120;

        // ── 帧生命周期 ──
        static void FrameStart();
        static void FrameEnd();

        // ── 分类追踪接口 ──
        static void OnAlloc(MemCategory cat, size_t size, const char* tag = nullptr);
        static void OnFree(MemCategory cat, size_t size = 0, const char* tag = nullptr);

        // ── 快捷方法（默认 Retail） ──
        static void OnAlloc(size_t size, const char* tag = nullptr)  { OnAlloc(MemCategory::Retail, size, tag); }
        static void OnFree(size_t size = 0)                          { OnFree(MemCategory::Retail, size); }

        // ── 聚合统计 ──
        static size_t GetCurrentUsage();
        static size_t GetPeakUsage();
        static size_t GetTotalAllocated();
        static size_t GetTotalFreed();
        static size_t GetAllocCount();
        static size_t GetFreeCount();

        // ── 分帧快照 ──
        static size_t GetFrameAllocBytes();
        static size_t GetFrameFreeBytes();
        static int    GetFrameAllocCount();
        static int    GetFrameFreeCount();

        // ── 分类查询 ──
        static MemCategoryStat GetCategoryStat(MemCategory cat);
        static int GetCategoryCount() { return static_cast<int>(MemCategory::COUNT); }

        // ── 历史数据 ──
        static const std::array<size_t, kHistoryFrames>& GetUsageHistory();
        static int GetHistoryIndex();

        // ── 详细追踪 ──
        static const MemAllocRecord* GetRecentRecords(int& outCount);
        static std::vector<MemTagStat> GetTagStats(MemCategory cat);
        static std::vector<size_t> GetSizeHistogram(MemCategory cat);  // 8 个桶

        // ── 格式化 ──
        static std::string GetStatsString();
        static std::string FormatBytes(size_t bytes);
    };

} // namespace Engine
