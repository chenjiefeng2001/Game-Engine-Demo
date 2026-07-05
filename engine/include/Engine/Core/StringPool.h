#pragma once

/**
 * @file StringPool.h
 * @brief 高性能字符串驻留池 — 类似 Unreal FName 系统
 *
 * 设计目标：
 *   1. 在所有线程间共享的读写优化字符串注册表
 *   2. 每个字符串在引擎生命周期内只存储一份拷贝（零冗余）
 *   3. 反向查找：给定 StringID，可找回原始 C 字符串（供编辑器/日志/崩溃报告使用）
 *   4. Arena Bump 分配：2MB 大页连续存储，消除碎片和频繁小堆分配
 *   5. 读写锁分离：99% 的操作是 queries（读），仅首次注册需要写锁
 *
 * 架构说明：
 *   - StringID.h（纯哈希）与 StringPool（驻留+反向查找）完全解耦
 *   - StringID 构造不调用 StringPool（避免全局锁依赖）
 *   - 仅在 Intern() 时与全局池交互
 *
 * 使用示例：
 * @code
 *   // ── 注册新字符串或获取已有 ID ──
 *   StringID id = StringPool::Intern("Ogre_Boss_01");
 *
 *   // ── 编辑器 / 日志中反查 ──
 *   std::string_view name = StringPool::GetString(id);  // "Ogre_Boss_01"
 *   ImGui::Text("Name: %s", name.data());
 *
 *   // ── 统计 ──
 *   StringPool::LogStats();  // "Pool: 1234 strings, 2 blocks (3.1 MB)"
 * @endcode
 *
 * 并发安全：
 *   - Intern() 和 GetString() 互相阻塞：读锁与写锁互斥
 *   - 多个 Intern() 对同一条字符串是安全的（第二个 Intern 在读锁阶段找到，直接返回）
 *   - Arena 分配在写锁内完成
 */

#include "Engine/Core/StringID.h"
#include <shared_mutex>
#include <vector>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <cstring>

namespace Engine {

    class StringPool {
    public:
        // ══════════════════════════════════════════════════════
        // 注册字符串（返回对应的 StringID）
        // ══════════════════════════════════════════════════════

        /**
         * @brief 将字符串注册到驻留池，返回对应的 StringID
         *
         * @param str 待注册的字符串
         * @return StringID 编译期或运行期的哈希值
         *
         * 并发策略（读写锁 + Double-Check）：
         *   1. 计算哈希值（无锁）
         *   2. 获取 shared_lock（读锁）：99% 的调用在此返回
         *   3. 若未找到，升级为 unique_lock（写锁）：再次检查是否有其他线程抢先注册
         *   4. 写入 Arena 缓冲区并更新 map
         */
        static StringID Intern(std::string_view str);

        // ══════════════════════════════════════════════════════
        // 反向查找（仅用于调试/编辑器/日志）
        // ══════════════════════════════════════════════════════

        /**
         * @brief 给定 StringID，获取原始 C 字符串指针
         *
         * @param id 字符串哈希值
         * @return 指向 Arena 内存储的 null-terminated 字符串的指针。
         *         未注册时返回 "<Unknown>"。
         *
         * 线程安全：shared_lock。
         */
        static const char* GetString(StringID id);

        /**
         * @brief 给定 StringID，获取 string_view 包装
         *
         * 比 GetString() 多一次 strlen 调用，返回值语义。
         */
        static std::string_view GetStringView(StringID id);

        // ══════════════════════════════════════════════════════
        // 统计与调试
        // ══════════════════════════════════════════════════════

        /** 注册表中的字符串数量 */
        static size_t GetCount();

        /** Arena 块的数量 */
        static size_t GetBlockCount();

        /** Arena 当前已使用的字节数（含块内偏移） */
        static size_t GetUsedBytes();

        /** 格式化输出统计信息到日志 */
        static void LogStats();

        /** 调试用：以 JSON 格式转储统计 */
        static std::string DumpStats();

    private:
        // ══════════════════════════════════════════════════════
        // 内部实现
        // ══════════════════════════════════════════════════════

        /**
         * @brief 将字符串写入 Arena 缓冲区
         *
         * 写入策略（Bump 分配）：
         *   - 如果当前 2MB 块剩余空间 ≥ len+1，直接在当前偏移写入
         *   - 如果剩余空间不足，分配新的 2MB 块，从偏移 0 开始
         *
         * @param hash 字符串哈希值（仅用于更新 map）
         * @param str  待写入的字符串
         * @return 指向 Arena 内 null-terminated 副本的指针
         */
        static const char* StoreInArena(uint64_t hash, std::string_view str);

        // ══════════════════════════════════════════════════════
        // 静态数据成员
        // ══════════════════════════════════════════════════════

        /** 读写锁（shared_mutex）—— 支持并发读，独占写 */
        static std::shared_mutex s_Mutex;

        /** 映射：uint64_t hash → Arena 中的 char* */
        static std::unordered_map<uint64_t, const char*> s_Map;

        /** Arena 大页块：每个块 2MB 连续内存 */
        static constexpr size_t s_BlockSize = 2 * 1024 * 1024;  // 2MB

        /** 所有 Arena 块（每个块是 char[s_BlockSize]） */
        static std::vector<std::unique_ptr<char[]>> s_Blocks;

        /** 当前 Arena 块内的写入偏移（字节） */
        static size_t s_CurrentOffset;
    };

} // namespace Engine