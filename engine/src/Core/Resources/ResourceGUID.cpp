#include "Engine/Core/Resources/ResourceGUID.h"
#include <random>
#include <sstream>
#include <iomanip>

namespace Engine {

    // ============================================================
    // ResourceGUID::Create — 生成随机 UUID v4
    // ============================================================

    ResourceGUID ResourceGUID::Create() {
        static thread_local std::mt19937_64 rng(std::random_device{}());

        uint64 h = rng();
        uint64 l = rng();

        // UUID v4：高位版本号 4，低位变体 10xx
        h = (h & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;  // version 4
        l = (l & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;  // variant 1

        return ResourceGUID(h, l);
    }

    // ============================================================
    // ResourceGUID::FromString
    // ============================================================

    ResourceGUID ResourceGUID::FromString(const std::string& str) {
        if (str.size() < 36) return Null;

        uint64 h = 0, l = 0;
        unsigned int parts[8] = {0};

        // 格式：xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
        // 用 sscanf 或手动解析
        int matched = std::sscanf(str.c_str(),
            "%08x-%04x-%04x-%04x-%012llx",
            &parts[0], &parts[1], &parts[2], &parts[3],
            reinterpret_cast<unsigned long long*>(&l));

        if (matched < 5) return Null;

        h = (static_cast<uint64>(parts[0]) << 32) |
            (static_cast<uint64>(parts[1]) << 16) |
            static_cast<uint64>(parts[2]);
        // 注意：parts[3] 是前两个字节，与 l 的高 16 位重叠
        // 标准格式中第 4 段（4 位十六进制）实际上属于高 64 位的低 16 位
        // 让我们重新正确解析

        // 更可靠的解析方式
        return FromHex(str);
    }

    // ============================================================
    // ResourceGUID::FromHex
    // ============================================================

    ResourceGUID ResourceGUID::FromHex(const std::string& hex) {
        if (hex.empty()) return Null;

        std::string cleaned;
        cleaned.reserve(32);
        for (char c : hex) {
            if (c != '-' && c != '{' && c != '}' && c != ' ')
                cleaned += c;
        }

        if (cleaned.size() != 32) return Null;

        uint64 h = 0, l = 0;
        for (int i = 0; i < 16; ++i) {
            char c = cleaned[i];
            uint8 nibble = 0;
            if (c >= '0' && c <= '9')      nibble = static_cast<uint8>(c - '0');
            else if (c >= 'a' && c <= 'f') nibble = static_cast<uint8>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') nibble = static_cast<uint8>(c - 'A' + 10);
            else return Null;
            h = (h << 4) | nibble;
        }
        for (int i = 16; i < 32; ++i) {
            char c = cleaned[i];
            uint8 nibble = 0;
            if (c >= '0' && c <= '9')      nibble = static_cast<uint8>(c - '0');
            else if (c >= 'a' && c <= 'f') nibble = static_cast<uint8>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') nibble = static_cast<uint8>(c - 'A' + 10);
            else return Null;
            l = (l << 4) | nibble;
        }

        return ResourceGUID(h, l);
    }

    // ============================================================
    // ResourceGUID::ToString
    // ============================================================

    std::string ResourceGUID::ToString() const {
        // 提取各段
        uint32 part0 = static_cast<uint32>(high >> 32);
        uint16 part1 = static_cast<uint16>(high >> 16);
        uint16 part2 = static_cast<uint16>(high);
        uint16 part3 = static_cast<uint16>(low >> 48);
        uint64 part4 = low & 0xFFFFFFFFFFFFULL;

        std::stringstream ss;
        ss << std::hex << std::setfill('0')
           << std::setw(8)  << part0 << "-"
           << std::setw(4)  << part1 << "-"
           << std::setw(4)  << part2 << "-"
           << std::setw(4)  << part3 << "-"
           << std::setw(12) << part4;
        return ss.str();
    }

    // ============================================================
    // ResourceGUID::ToHex
    // ============================================================

    std::string ResourceGUID::ToHex() const {
        std::stringstream ss;
        ss << std::hex << std::setfill('0')
           << std::setw(16) << high
           << std::setw(16) << low;
        return ss.str();
    }

} // namespace Engine
