#pragma once

/**
 * @file GUID.h
 * @brief 128-bit 全局唯一标识符 — 场景资源的“身份证”
 *
 * 设计原则：
 *   - 资源永不通过文件路径引用，只通过 GUID
 *   - AssetDatabase 维护 GUID ↔ 当前文件路径的映射
 *   - 支持从字符串/二进制反序列化
 */

#include "Engine/Types.h"
#include <string>
#include <cstdint>
#include <cstring>
#include <random>
#include <sstream>
#include <iomanip>

namespace Engine {

    // ============================================================
    // GUID — 128-bit 唯一标识
    // ============================================================
    struct GUID {
        uint64_t high = 0;  // 高 64 位
        uint64_t low  = 0;  // 低 64 位

        bool operator==(const GUID& o) const noexcept {
            return high == o.high && low == o.low;
        }
        bool operator!=(const GUID& o) const noexcept {
            return high != o.high || low != o.low;
        }
        bool operator<(const GUID& o) const noexcept {
            return high < o.high || (high == o.high && low < o.low);
        }

        bool IsValid() const noexcept { return high != 0 || low != 0; }

        /** @brief 生成一个新的随机 GUID */
        static GUID Generate() noexcept {
            static thread_local std::mt19937_64 rng(
                std::random_device{}() ^
                (reinterpret_cast<uint64_t>(&rng) << 1) ^
                __rdtsc());

            GUID g;
            g.high = rng();
            g.low  = rng();
            return g;
        }

        /** @brief 从格式化的 UUID 字符串解析 ("xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx") */
        static GUID FromString(const std::string& str) noexcept {
            GUID g;
            uint64_t parts[2];
            // 简单的 hex 解析，跳过连字符
            std::string hex;
            for (char c : str) {
                if (c != '-') hex += c;
            }
            if (hex.size() >= 16) {
                g.high = std::stoull(hex.substr(0, 16), nullptr, 16);
            }
            if (hex.size() >= 32) {
                g.low  = std::stoull(hex.substr(16, 16), nullptr, 16);
            }
            return g;
        }

        /** @brief 转为标准 UUID 字符串 */
        std::string ToString() const noexcept {
            std::ostringstream oss;
            oss << std::hex << std::setfill('0')
                << std::setw(16) << high
                << std::setw(16) << low;
            std::string raw = oss.str();
            // 插入连字符
            return raw.substr(0, 8) + "-" + raw.substr(8, 4) + "-"
                 + raw.substr(12, 4) + "-" + raw.substr(16, 4) + "-"
                 + raw.substr(20);
        }

        /** @brief 空 GUID */
        static const GUID& Null() noexcept {
            static GUID g{0, 0};
            return g;
        }
    };

    // ============================================================
    // GUID 的哈希支持（用于 unordered_map）
    // ============================================================
    struct GUIDHasher {
        size_t operator()(const GUID& g) const noexcept {
            return static_cast<size_t>(g.high ^ (g.low * 0x9e3779b97f4a7c15ULL));
        }
    };

} // namespace Engine