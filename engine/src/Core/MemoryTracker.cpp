/**
 * @file MemoryTracker.cpp
 * @brief 内存分配统计 — 帧级快照 + 历史追踪
 *
 * 全局 new/delete 钩子在 Debug 模式下自动追踪所有堆分配，
 * 无需手动插桩即可在 MemoryPanel 中看到实时数据。
 */

#include "Engine/MemoryTracker.h"
#include "Engine/Profiler.h"
#include <atomic>
#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <new>
#include <cstdio>
#include <cstring>
#include <cstdio>

// ============================================================
// 全局 operator new/delete 钩子（仅 Debug，自动调用 MemoryTracker）
// ============================================================
#if defined(_DEBUG) && defined(ENGINE_ENABLE_PROFILING)

// Windows 更名：msize → _msize
#ifdef _WIN32
#define TRACKED_FREE_SZ(p) _msize(p)
#else
#define TRACKED_FREE_SZ(p) 0
#endif

void* operator new(size_t size) {
    void* ptr = std::malloc(size);
    if (ptr) {
        Engine::MemoryTracker::OnAlloc(Engine::MemCategory::Retail, size);
        PROFILE_MALLOC(ptr, size);
    }
    return ptr;
}

void* operator new[](size_t size) {
    void* ptr = std::malloc(size);
    if (ptr) {
        Engine::MemoryTracker::OnAlloc(Engine::MemCategory::Retail, size);
        PROFILE_MALLOC(ptr, size);
    }
    return ptr;
}

void operator delete(void* ptr) noexcept {
    if (ptr) {
        PROFILE_FREE(ptr);
        Engine::MemoryTracker::OnFree(Engine::MemCategory::Retail, TRACKED_FREE_SZ(ptr));
    }
    std::free(ptr);
}

void operator delete[](void* ptr) noexcept {
    if (ptr) {
        PROFILE_FREE(ptr);
        Engine::MemoryTracker::OnFree(Engine::MemCategory::Retail, TRACKED_FREE_SZ(ptr));
    }
    std::free(ptr);
}

void operator delete(void* ptr, size_t size) noexcept {
    if (ptr) {
        PROFILE_FREE(ptr);
        Engine::MemoryTracker::OnFree(Engine::MemCategory::Retail, size);
    }
    std::free(ptr);
}

void operator delete[](void* ptr, size_t size) noexcept {
    if (ptr) {
        PROFILE_FREE(ptr);
        Engine::MemoryTracker::OnFree(Engine::MemCategory::Retail, size);
    }
    std::free(ptr);
}

#undef TRACKED_FREE_SZ

#endif // _DEBUG && ENGINE_ENABLE_PROFILING

namespace Engine {

    struct Cat {
        std::atomic<size_t> current{0}, peak{0}, total{0}, count{0}, fAlloc{0}, fFree{0};
        std::atomic<int>    fAllocCnt{0}, fFreeCnt{0};
    };
    static Cat s_Cat[(int)MemCategory::COUNT];
    static std::array<size_t, MemoryTracker::kHistoryFrames> s_Hist{};
    static int s_HistIdx = 0;

    // 详细追踪
    struct TagInfo { size_t bytes=0, count=0; };
    static MemAllocRecord s_Recent[kRecentMax]{};
    static int s_RecentIdx = 0;
    static std::mutex s_TagMutex;
    static std::unordered_map<std::string, TagInfo> s_Tags[(int)MemCategory::COUNT];

    void MemoryTracker::FrameStart() {
        for (auto& c : s_Cat) { c.fAlloc = 0; c.fFree = 0; c.fAllocCnt = 0; c.fFreeCnt = 0; }
    }
    void MemoryTracker::FrameEnd() {
        size_t t = 0; for (auto& c : s_Cat) t += c.current;
        // 记录实际使用量（0 也是合法值，折线图需要能下降）
        s_Hist[s_HistIdx] = t;
        s_HistIdx = (s_HistIdx + 1) % kHistoryFrames;
    }

    void MemoryTracker::OnAlloc(MemCategory cat, size_t s, const char* tag) {
        auto& c = s_Cat[(int)cat];
        c.total += s; c.current += s; c.count++; c.fAlloc += s; c.fAllocCnt++;
        size_t cur = c.current, pk = c.peak;
        while (cur > pk && !c.peak.compare_exchange_weak(pk, cur)) {}

        // 最近分配记录
        auto& rec = s_Recent[s_RecentIdx % kRecentMax];
        rec.cat = cat; rec.size = s; rec.isFree = false;
        if (tag) {
            std::snprintf(rec.tag, sizeof(rec.tag), "%s", tag);
        } else {
            rec.tag[0] = '\0';
        }
        s_RecentIdx++;

        if (tag && tag[0]) { std::lock_guard g(s_TagMutex); auto& ti = s_Tags[(int)cat][tag]; ti.bytes += s; ti.count++; }
    }
    void MemoryTracker::OnFree(MemCategory cat, size_t s, const char* tag) {
        auto& c = s_Cat[(int)cat];
        if (s > 0) c.current -= s;
        c.fFree += s; c.fFreeCnt++;

        auto& rec = s_Recent[s_RecentIdx % kRecentMax];
        rec.cat = cat; rec.size = s; rec.isFree = true;
        if (tag) {
            std::snprintf(rec.tag, sizeof(rec.tag), "%s", tag);
        } else {
            rec.tag[0] = '\0';
        }
        s_RecentIdx++;

        if (tag && tag[0] && s > 0) { std::lock_guard g(s_TagMutex); auto& ti = s_Tags[(int)cat][tag]; if(ti.bytes>=s)ti.bytes-=s; ti.count++; }
    }

    // 新增查询
    const MemAllocRecord* MemoryTracker::GetRecentRecords(int& outCount) {
        outCount = s_RecentIdx < kRecentMax ? s_RecentIdx : kRecentMax;
        return s_Recent;
    }

    std::vector<MemTagStat> MemoryTracker::GetTagStats(MemCategory cat) {
        std::lock_guard g(s_TagMutex);
        std::vector<MemTagStat> result;
        for (auto& [tag, info] : s_Tags[(int)cat])
            result.push_back({tag, info.bytes, info.count});
        std::sort(result.begin(), result.end(), [](auto& a, auto& b) { return a.bytes > b.bytes; });
        return result;
    }

    std::vector<size_t> MemoryTracker::GetSizeHistogram(MemCategory cat) {
        std::array<size_t, 8> buckets{};
        std::lock_guard g(s_TagMutex);
        for (auto& [tag, info] : s_Tags[(int)cat]) {
            size_t avg = info.count ? info.bytes / info.count : 0;
            if (avg <= 64) buckets[0]++; else if (avg <= 256) buckets[1]++;
            else if (avg <= 1024) buckets[2]++; else if (avg <= 4096) buckets[3]++;
            else if (avg <= 16384) buckets[4]++; else if (avg <= 65536) buckets[5]++;
            else if (avg <= 262144) buckets[6]++; else buckets[7]++;
        }
        return std::vector<size_t>(buckets.begin(), buckets.end());
    }

    static size_t S(std::atomic<size_t> Cat::* f) { size_t v=0; for(auto&c:s_Cat)v+=(c.*f).load(); return v; }
    static int Si(std::atomic<int> Cat::* f) { int v=0; for(auto&c:s_Cat)v+=(c.*f).load(); return v; }
    size_t MemoryTracker::GetCurrentUsage()  { return S(&Cat::current); }
    size_t MemoryTracker::GetTotalAllocated(){ return S(&Cat::total); }
    size_t MemoryTracker::GetPeakUsage()     { return S(&Cat::peak); }
    size_t MemoryTracker::GetAllocCount()    { return S(&Cat::count); }
    size_t MemoryTracker::GetFreeCount()     { return 0; }
    size_t MemoryTracker::GetFrameAllocBytes(){ return S(&Cat::fAlloc); }
    size_t MemoryTracker::GetFrameFreeBytes() { return S(&Cat::fFree); }
    int    MemoryTracker::GetFrameAllocCount() { return Si(&Cat::fAllocCnt); }
    int    MemoryTracker::GetFrameFreeCount()  { return Si(&Cat::fFreeCnt); }

    MemCategoryStat MemoryTracker::GetCategoryStat(MemCategory cat) {
        auto& c = s_Cat[(int)cat];
        MemCategoryStat st;
        st.current=c.current; st.peak=c.peak; st.total=c.total; st.count=c.count;
        st.frameAlloc=c.fAlloc; st.frameFree=c.fFree;
        st.frameAllocCnt=c.fAllocCnt; st.frameFreeCnt=c.fFreeCnt;
        return st;
    }

    const std::array<size_t, MemoryTracker::kHistoryFrames>& MemoryTracker::GetUsageHistory() { return s_Hist; }
    int MemoryTracker::GetHistoryIndex() { return s_HistIdx; }

    std::string MemoryTracker::FormatBytes(size_t bytes) {
        char buf[32];
        if (bytes < 1024)       snprintf(buf, 32, "%zu B", bytes);
        else if (bytes < 1048576) snprintf(buf, 32, "%.1f KB", bytes/1024.0);
        else                    snprintf(buf, 32, "%.1f MB", bytes/1048576.0);
        return buf;
    }

    std::string MemoryTracker::GetStatsString() {
        std::string out = "^5=== Memory Stats ===^7\n";
        out += "  ^5Total:^7 ^2" + FormatBytes(GetCurrentUsage()) + "^7";
        for (int i = 0; i < (int)MemCategory::COUNT; ++i) {
            auto st = GetCategoryStat((MemCategory)i);
            if (st.current > 0 || st.total > 0)
                out += "\n  " + std::string(MemCategoryName((MemCategory)i)) + ": " + FormatBytes(st.current);
        }
        return out;
    }

} // namespace Engine
