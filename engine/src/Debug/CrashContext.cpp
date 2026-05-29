#include "Engine/Debug/CrashContext.h"
#include <sstream>
#include <iomanip>
#include <ctime>

namespace Engine {

// ============================================================
// 静态辅助
// ============================================================

std::mutex& CrashContext::Mutex() {
    static std::mutex m;
    return m;
}

CrashContext::Context& CrashContext::State() {
    static Context ctx;
    return ctx;
}

std::unordered_map<std::string, CrashContext::DataProvider>& CrashContext::Providers() {
    static std::unordered_map<std::string, DataProvider> provs;
    return provs;
}

// ============================================================
// 快速设值
// ============================================================

void CrashContext::SetLevel(const std::string& level) {
    std::lock_guard lock(Mutex());
    State().level = level;
}

void CrashContext::SetPosition(float x, float y, float z) {
    std::lock_guard lock(Mutex());
    State().posX = x; State().posY = y; State().posZ = z;
}

void CrashContext::SetPlayerState(const std::string& state) {
    std::lock_guard lock(Mutex());
    State().playerState = state;
}

void CrashContext::SetScriptStack(const std::vector<std::string>& scripts) {
    std::lock_guard lock(Mutex());
    State().scriptStack = scripts;
}

void CrashContext::SetCustom(const std::string& key, const std::string& value) {
    std::lock_guard lock(Mutex());
    State().custom[key] = value;
}

// ============================================================
// 回调管理
// ============================================================

void CrashContext::Register(const std::string& category, DataProvider provider) {
    std::lock_guard lock(Mutex());
    Providers()[category] = std::move(provider);
}

void CrashContext::Unregister(const std::string& category) {
    std::lock_guard lock(Mutex());
    Providers().erase(category);
}

// ============================================================
// 收集 — JSON 格式
// ============================================================

std::string CrashContext::CollectAll() {
    std::lock_guard lock(Mutex());
    auto& ctx = State();
    std::ostringstream oss;
    oss << "{\n";

    // 时间戳
    auto now = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::gmtime(&now));
    oss << "  \"timestamp\": \"" << buf << "\",\n";

    // 关卡
    oss << "  \"level\": "
        << (ctx.level.empty() ? "null" : "\"" + ctx.level + "\"") << ",\n";

    // 位置
    oss << "  \"position\": ["
        << ctx.posX << ", " << ctx.posY << ", " << ctx.posZ << "],\n";

    // 玩家状态
    oss << "  \"playerState\": "
        << (ctx.playerState.empty() ? "null" : "\"" + ctx.playerState + "\"") << ",\n";

    // 脚本栈
    oss << "  \"scriptStack\": [";
    for (size_t i = 0; i < ctx.scriptStack.size(); ++i) {
        if (i) oss << ", ";
        oss << "\"" << ctx.scriptStack[i] << "\"";
    }
    oss << "],\n";

    // 自定义字段
    oss << "  \"custom\": {\n";
    for (auto it = ctx.custom.begin(); it != ctx.custom.end(); ++it) {
        if (it != ctx.custom.begin()) oss << ",\n";
        oss << "    \"" << it->first << "\": \"" << it->second << "\"";
    }
    oss << "\n  },\n";

    // 回调提供者
    oss << "  \"providers\": {\n";
    for (auto it = Providers().begin(); it != Providers().end(); ++it) {
        if (it != Providers().begin()) oss << ",\n";
        oss << "    \"" << it->first << "\": ";
        try {
            std::string val = it->second();
            // JSON 转义双引号
            for (size_t i = 0; i < val.size(); ++i) {
                if (val[i] == '"') { val.insert(i, 1, '\\'); ++i; }
            }
            oss << "\"" << val << "\"";
        } catch (...) {
            oss << "\"<exception during data collection>\"";
        }
    }
    oss << "\n  }\n}\n";

    return oss.str();
}

void CrashContext::Clear() {
    std::lock_guard lock(Mutex());
    State() = Context{};
    Providers().clear();
}

} // namespace Engine
