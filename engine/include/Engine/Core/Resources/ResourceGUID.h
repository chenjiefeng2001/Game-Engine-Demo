#pragma once

/**
 * @file ResourceGUID.h
 * @brief 资源全局唯一标识符 — GUID 生成、序列化、比较
 *
 * 使用与 AssetDatabase 相同的 UUID v4 格式，确保运行时
 * 资源 GUID 与资产数据库 GUID 兼容。
 *
 * 设计要点：
 *   - 128 位随机 UUID v4（字符串形式 "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx"）
 *   - 支持空值（Null GUID）作为"无引用"
 *   - 轻量值类型（16 字节）
 *   - 可哈希（用于 std::unordered_map）
 */

#include "Engine/Types.h"
#include <string>
#include <cstring>
#include <functional>
#include <ostream>

namespace Engine {

    // ============================================================
    // ResourceGUID — 128 位 UUID
    // ============================================================
    struct ResourceGUID {
        uint64 high = 0;    // 高 64 位
        uint64 low  = 0;    // 低 64 位

        // ── 常量 ──
        static const ResourceGUID Null;  // 全零 GUID

        // ── 构造 ──
        constexpr ResourceGUID() = default;
        constexpr ResourceGUID(uint64 h, uint64 l) : high(h), low(l) {}

        // ── 工厂 ──

        /** 生成一个随机 UUID v4 */
        static ResourceGUID Create();

        /** 从标准 UUID 字符串解析（"xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx"） */
        static ResourceGUID FromString(const std::string& str);

        /** 从 32 字符十六进制字符串解析（无连字符） */
        static ResourceGUID FromHex(const std::string& hex);

        // ── 序列化 ──

        /** 转为标准 UUID 字符串 */
        std::string ToString() const;

        /** 转为 32 字符十六进制字符串（无连字符） */
        std::string ToHex() const;

        // ── 查询 ──

        /** 是否为空 GUID */
        constexpr bool IsNull() const { return high == 0 && low == 0; }

        /** 是否非空 */
        constexpr explicit operator bool() const { return !IsNull(); }

        // ── 比较 ──
        constexpr bool operator==(const ResourceGUID& other) const {
            return high == other.high && low == other.low;
        }
        constexpr bool operator!=(const ResourceGUID& other) const {
            return !(*this == other);
        }
        constexpr bool operator<(const ResourceGUID& other) const {
            if (high != other.high) return high < other.high;
            return low < other.low;
        }
    };

    // 空 GUID 常量
    inline constexpr ResourceGUID ResourceGUID::Null{0, 0};

    // ── 打印支持 ──
    inline std::ostream& operator<<(std::ostream& os, const ResourceGUID& guid) {
        os << guid.ToString();
        return os;
    }

} // namespace Engine

// ── std::hash 特化 ──
namespace std {
    template<>
    struct hash<Engine::ResourceGUID> {
        size_t operator()(const Engine::ResourceGUID& guid) const noexcept {
            // 混合高 64 位和低 64 位
            return static_cast<size_t>(guid.high ^
                (guid.low + 0x9e3779b97f4a7c15ULL +
                 (guid.high << 6) + (guid.high >> 2)));
        }
    };
} // namespace std
