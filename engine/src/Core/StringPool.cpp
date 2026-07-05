/**
 * @file StringPool.cpp
 * @brief 字符串驻留池静态成员定义
 */

#include "Engine/Core/StringPool.h"
#include "Engine/Core/Log.h"
#include <sstream>
#include <cstring>

namespace Engine {

    // ============================================================
    // 静态成员定义
    // ============================================================

    std::shared_mutex StringPool::s_Mutex;
    std::unordered_map<uint64_t, const char*> StringPool::s_Map;
    std::vector<std::unique_ptr<char[]>> StringPool::s_Blocks;
    size_t StringPool::s_CurrentOffset = 0;

    namespace {
        Logger s_Log("StringPool");
    }

    // ============================================================
    // Intern — 读写锁 + Double-Check
    // ============================================================

    StringID StringPool::Intern(std::string_view str) {
        uint64_t hash = HashString64(str);

        // ── 阶段 1：并发读（shared_lock，99% 的调用在此返回） ──
        {
            std::shared_lock<std::shared_mutex> readLock(s_Mutex);
            auto it = s_Map.find(hash);
            if (it != s_Map.end()) {
                return StringID(hash);
            }
        }  // 读锁在此释放

        // ── 阶段 2：独占写（unique_lock，仅首次注册时进入） ──
        {
            std::unique_lock<std::shared_mutex> writeLock(s_Mutex);

            // Double-Check：在获取写锁期间，可能有其他线程抢先注册
            auto it = s_Map.find(hash);
            if (it != s_Map.end()) {
                return StringID(hash);
            }

            const char* ptr = StoreInArena(hash, str);
            return StringID(hash);
        }
    }

    // ============================================================
    // 反向查找
    // ============================================================

    const char* StringPool::GetString(StringID id) {
        std::shared_lock<std::shared_mutex> readLock(s_Mutex);
        auto it = s_Map.find(id.Value());
        if (it != s_Map.end()) {
            return it->second;
        }
        return "<Unknown>";
    }

    std::string_view StringPool::GetStringView(StringID id) {
        const char* ptr = GetString(id);
        return std::string_view(ptr);
    }

    // ============================================================
    // Arena Bump 分配
    // ============================================================

    const char* StringPool::StoreInArena(uint64_t hash, std::string_view str) {
        size_t len = str.length();

        // 如果剩余空间不足，分配新块
        if (s_CurrentOffset + len + 1 > s_BlockSize) {
            s_Blocks.push_back(std::make_unique<char[]>(s_BlockSize));
            s_CurrentOffset = 0;
            s_Log.Info("Arena block {} allocated ({} total blocks)",
                       s_Blocks.size(), s_Blocks.size());
        }

        // 特殊情况：字符串长度超过单块大小的保护
        // (2MB 块对于任何合理的字符串标识符来说都是足够的)
        if (len + 1 > s_BlockSize) {
            s_Log.Error("String too long for Arena block: {} bytes (max {})",
                        len, s_BlockSize);
            return nullptr;
        }

        // Bump 分配：从当前偏移处写入
        char* dest = s_Blocks.back().get() + s_CurrentOffset;
        std::memcpy(dest, str.data(), len);
        dest[len] = '\0';

        s_CurrentOffset += len + 1;
        s_Map[hash] = dest;

        return dest;
    }

    // ============================================================
    // 统计
    // ============================================================

    size_t StringPool::GetCount() {
        std::shared_lock<std::shared_mutex> readLock(s_Mutex);
        return s_Map.size();
    }

    size_t StringPool::GetBlockCount() {
        std::shared_lock<std::shared_mutex> readLock(s_Mutex);
        return s_Blocks.size();
    }

    size_t StringPool::GetUsedBytes() {
        std::shared_lock<std::shared_mutex> readLock(s_Mutex);
        // 所有完整块的大小 + 当前块的偏移
        if (s_Blocks.empty()) return 0;
        return (s_Blocks.size() - 1) * s_BlockSize + s_CurrentOffset;
    }

    void StringPool::LogStats() {
        size_t count = GetCount();
        size_t blocks = GetBlockCount();
        size_t bytes = GetUsedBytes();
        s_Log.Info("StringPool: {} strings, {} blocks, {:.1f} MB used",
                   count, blocks, static_cast<double>(bytes) / (1024.0 * 1024.0));
    }

    std::string StringPool::DumpStats() {
        size_t count = GetCount();
        size_t blocks = GetBlockCount();
        size_t bytes = GetUsedBytes();

        std::ostringstream oss;
        oss << "{\"count\":" << count
            << ",\"blocks\":" << blocks
            << ",\"usedMB\":" << static_cast<double>(bytes) / (1024.0 * 1024.0)
            << ",\"blockSizeKB\":" << (s_BlockSize / 1024)
            << "}";
        return oss.str();
    }

} // namespace Engine