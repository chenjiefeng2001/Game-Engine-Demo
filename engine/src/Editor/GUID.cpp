#include "Engine/Editor/AssetTypes.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cstdio>

namespace Engine {

    std::string GUID::ToString() const {
        // UUID v4 format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
        auto toHex = [](uint64_t val, int digits) -> std::string {
            std::string result(digits, '0');
            for (int i = digits - 1; i >= 0; --i) {
                int nibble = static_cast<int>(val & 0xF);
                result[i] = (nibble < 10) ? ('0' + nibble) : ('a' + nibble - 10);
                val >>= 4;
            }
            return result;
        };

        // 低 64 位: 前 8 字节
        // 高 64 位: 后 8 字节
        uint32_t time_low = static_cast<uint32_t>(low & 0xFFFFFFFF);
        uint16_t time_mid = static_cast<uint16_t>((low >> 32) & 0xFFFF);
        uint16_t time_hi_and_version = static_cast<uint16_t>((low >> 48) & 0xFFFF);
        uint8_t clock_seq_hi = static_cast<uint8_t>((high >> 56) & 0xFF);
        uint64_t node = high & 0xFFFFFFFFFFFFULL;

        char buf[40];
        std::snprintf(buf, sizeof(buf),
            "%08x-%04x-%04x-%02x%02x-%012llx",
            time_low, time_mid, time_hi_and_version,
            clock_seq_hi,
            static_cast<unsigned char>((high >> 48) & 0xFF),
            static_cast<unsigned long long>(node));
        return std::string(buf);
    }

    GUID GUID::FromString(const std::string& str) {
        GUID g;
        unsigned long long node;
        unsigned int time_low, time_mid, time_hi_and_version;
        unsigned int clock_seq_hi, clock_seq_low;

        if (std::sscanf(str.c_str(), "%08x-%04x-%04x-%02x%02x-%012llx",
                        &time_low, &time_mid, &time_hi_and_version,
                        &clock_seq_hi, &clock_seq_low, &node) == 6) {
            g.low = static_cast<uint64_t>(time_low) |
                    (static_cast<uint64_t>(time_mid) << 32) |
                    (static_cast<uint64_t>(time_hi_and_version) << 48);
            g.high = (static_cast<uint64_t>(clock_seq_hi) << 56) |
                     (static_cast<uint64_t>(clock_seq_low) << 48) |
                     node;
        }
        return g;
    }

    GUID GUID::Generate() {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint64_t> dist;

        GUID g;
        g.low = dist(gen);
        g.high = dist(gen);

        // UUID v4: 设置版本位 (4xxx)
        g.low = (g.low & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
        // 设置变体位 (10xx)
        g.high = (g.high & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

        return g;
    }

} // namespace Engine