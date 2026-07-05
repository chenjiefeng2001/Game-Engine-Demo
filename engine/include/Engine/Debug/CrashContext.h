#pragma once

/**
 * @file CrashContext.h
 * @brief 崩溃上下文 — 集中式状态注册表。
 *        各系统在正常运行中写入状态，崩溃时由 CrashHandler 收集。
 *
 * 使用方式：
 * @code
 *   CrashContext::SetLevel("World1-3");
 *   CrashContext::SetPosition(transform->GetPosition());
 *   CrashContext::SetPlayerState("running|jumping");
 *   CrashContext::SetScriptStack({"player.lua", "physics.lua"});
 *
 *   // 注册持久回调（CollectAll() 时自动调用）
 *   CrashContext::Register("AI/Behavior", []() { return aiTree->Dump(); });
 *
 *   // 崩溃时收集（由 CrashHandler 调用）
 *   std::string report = CrashContext::CollectAll();
 * @endcode
 */

#include "Engine/Types.h"
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <mutex>

namespace Engine {

    class CrashContext {
    public:
        /// 数据提供者签名：返回格式化的状态文本
        using DataProvider = std::function<std::string()>;

        // ── 写入快速状态 ──

        static void SetLevel(const std::string& level);
        static void SetPosition(float x, float y, float z);
        static void SetPlayerState(const std::string& state);
        static void SetScriptStack(const std::vector<std::string>& scripts);
        static void SetCustom(const std::string& key, const std::string& value);

        // ── 注册持久回调 ──

        /** @brief 注册一个会在 CollectAll() 时自动调用的数据提供者 */
        static void Register(const std::string& category, DataProvider provider);

        /** @brief 注销回调 */
        static void Unregister(const std::string& category);

        // ── 收集 ──

        /** @brief 收集所有上下文，返回 JSON 格式字符串 */
        static std::string CollectAll();

        /** @brief 清空所有状态 */
        static void Clear();

    private:
        static std::mutex& Mutex();

        struct Context {
            std::string level;
            float posX = 0, posY = 0, posZ = 0;
            std::string playerState;
            std::vector<std::string> scriptStack;
            std::unordered_map<std::string, std::string> custom;
        };

        static Context& State();
        static std::unordered_map<std::string, DataProvider>& Providers();
    };

} // namespace Engine
