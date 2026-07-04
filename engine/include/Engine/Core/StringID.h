#pragma once

/**
 * @file StringID.h
 * @brief 编译期字符串哈希系统 — 零运行时分配、0 次字符串比较
 *
 * 设计目标：
 *   - 100% 编译期求值的 FNV-1a 64 位哈希
 *   - 严格禁止隐式运行时 C 字符串构造（防止热点循环静默降级）
 *   - uint64_t 值语义，8 字节传递，O(1) 整数比较
 *   - 线程安全：纯无状态，不持有锁、不访问全局状态、不存储字符串
 *
 * 使用方式：
 * @code
 *   // ── 编译期（主推方式） ──
 *   StringID id = SID("PlayerHealth");              // C++14/17 宏，保证编译期求值
 *   std::unordered_map<StringID, float> stats;       // 8 字节键，无字符串开销
 *   stats[SID("Strength")] = 100.0f;
 *
 *   // ── 运行时哈希（仅在加载外部文件/网络包时使用） ──
 *   StringID id = StringID::Runtime(json["key"].get<std::string>());
 * @endcode
 *
 * 架构说明：
 *   此文件绝不调用 StringPool。调试用反向查找由 StringPool::GetString() 提供。
 */

#include <cstdint>
#include <string_view>
#include <functional>
#include <type_traits>

namespace Engine {

    // ============================================================
    // FNV-1a 编译期哈希核心
    // ============================================================
    /**
     * @brief 64 位 FNV-1a 哈希（constexpr，支持编译期和运行期求值）
     *
     * 算法特性：
     *   - 极低碰撞率（随机键碰撞概率约 1/2^32）
     *   - 计算快速（每字节 1 次 XOR + 1 次乘法 + 1 次分支）
     *   - 分布均匀（适合作为 unordered_map 的哈希函数）
     */
    constexpr uint64_t HashString64(std::string_view str) noexcept {
        uint64_t hash = 0xcbf29ce484222325ULL;   // FNV offset basis (64-bit)
        for (char c : str) {
            hash ^= static_cast<uint64_t>(static_cast<uint8_t>(c));
            hash *= 0x100000001b3ULL;             // FNV prime (64-bit)
        }
        return hash;
    }

    // ============================================================
    // StringID — 纯整数包装
    // ============================================================
    class StringID {
    public:
        // ── 构造 ──

        /** 默认构造：空 ID (0) */
        constexpr StringID() noexcept : m_ID(0) {}

        /** 从原始哈希值显式构造 */
        explicit constexpr StringID(uint64_t id) noexcept : m_ID(id) {}

        // ══════════════════════════════════════════════════════
        // 「禁止隐式运行时 C 字符串构造」是核心设计决策：
        // 如果允许 StringID("hello")，开发者在热点循环中
        // 每帧会触发数十万次运行期哈希计算。
        // 使用 SID("hello") 宏强制编译期求值。
        // 运行时如果确实需要，使用显式 StringID::Runtime(str)。
        // ══════════════════════════════════════════════════════

        /** 从 C 字符串构造 — 已删除，防止静默运行时哈希 */
        explicit StringID(const char*) = delete;

        /** 从 string_view 构造 — 已删除，防止静默运行时哈希 */
        explicit StringID(std::string_view) = delete;

        // ── 显式运行时构造（仅在加载外部文件/网络包等场景使用） ──

        /** @brief 显式运行时字符串哈希
         *  @warning 此方法会在运行时 O(N) 遍历字符串。不可用于热点循环。
         */
        static StringID Runtime(std::string_view str) noexcept {
            return StringID(HashString64(str));
        }

        // ── 访问器 ──

        constexpr uint64_t Value() const noexcept { return m_ID; }
        constexpr explicit operator bool() const noexcept { return m_ID != 0; }

        // ── 比较 ──

        constexpr bool operator==(StringID rhs) const noexcept { return m_ID == rhs.m_ID; }
        constexpr bool operator!=(StringID rhs) const noexcept { return m_ID != rhs.m_ID; }
        constexpr bool operator<(StringID rhs)  const noexcept { return m_ID < rhs.m_ID; }

        // ── 哈希支持 ──
        struct Hasher {
            size_t operator()(StringID id) const noexcept {
                return static_cast<size_t>(id.Value());
            }
        };

    private:
        uint64_t m_ID;
    };

} // namespace Engine

// ============================================================
// 强制编译期求值宏 — SID(str)
// ============================================================
/**
 * @brief 将字符串文本在编译期强制转换为 StringID
 *
 * 原理：通过 std::integral_constant 包裹哈希值，
 *       强迫编译器在编译期完成 FNV-1a 计算，
 *       然后通过 StringID(uint64_t) 构造函数包装。
 *
 * 使用示例：
 * @code
 *   StringID id = SID("PlayerHealth");  // 编译期 → 单条 MOV 指令
 *   std::unordered_map<StringID, float, StringID::Hasher> stats;
 *   stats[SID("Strength")] = 100.0f;    // 无内存分配，无字符串遍历
 * @endcode
 */
#define SID(str) \
    ::Engine::StringID(::std::integral_constant<uint64_t, ::Engine::HashString64(str)>::value)

// ============================================================
// std::hash 特化（支持 std::unordered_map<StringID> 简洁写法）
// ============================================================
namespace std {
    template <>
    struct hash<Engine::StringID> {
        size_t operator()(const Engine::StringID& sid) const noexcept {
            return static_cast<size_t>(sid.Value());
        }
    };
}