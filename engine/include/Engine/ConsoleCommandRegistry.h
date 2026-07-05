#pragma once

/**
 * @file ConsoleCommandRegistry.h
 * @brief 命令注册系统 — 注册/查找/执行控制台命令
 *
 * 设计原则：
 *   - 全局单例，与 ConsoleLog / ConsolePanel 协作
 *   - 支持带参数的字符串命令解析：`cmd arg1 arg2`
 *   - 支持 Tab 自动补全
 *   - 内置 help / clear / echo 等基础命令
 *
 * 使用方式：
 * @code
 *   // 在子系统初始化时注册
 *   ConsoleCommandRegistry::Instance().Register({
 *       "god", "Toggle god mode", "",
 *       [](const auto& args, auto& out) {
 *           g_GodMode = !g_GodMode;
 *           out = g_GodMode ? "God mode ON" : "God mode OFF";
 *       }
 *   });
 *
 *   // 执行命令（由 ConsolePanel 调用）
 *   std::string output;
 *   ConsoleCommandRegistry::Instance().Execute("god", output);
 * @endcode
 */

#include "Engine/Types.h"
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>

namespace Engine {

    /// 命令回调签名：参数列表 + 输出字符串引用
    using ConsoleCommandFn = std::function<void(const std::vector<std::string>& args, std::string& output)>;

    /// 控制台命令描述
    struct ConsoleCommand {
        std::string name;          ///< 命令名（唯一标识）
        std::string description;   ///< 简要描述
        std::string usage;         ///< 用法提示
        std::string category;      ///< 分类标签（如 "System", "Render", "Editor" 等）
        ConsoleCommandFn callback; ///< 执行回调

        /// 默认构造函数（std::unordered_map 需要）
        ConsoleCommand() = default;

        /// 4 参数构造函数（兼容旧代码的 initializer list），category 留空由 Register 自动推断
        ConsoleCommand(std::string n, std::string d, std::string u, ConsoleCommandFn cb)
            : name(std::move(n)), description(std::move(d)), usage(std::move(u)), callback(std::move(cb)) {}

        /// 5 参数完整构造函数
        ConsoleCommand(std::string n, std::string d, std::string u, std::string c, ConsoleCommandFn cb)
            : name(std::move(n)), description(std::move(d)), usage(std::move(u)), category(std::move(c)), callback(std::move(cb)) {}
    };

    // ============================================================
    // 命令注册表（Meyer's 单例）
    // ============================================================
    class ConsoleCommandRegistry {
    public:
        /// 获取单例
        static ConsoleCommandRegistry& Instance();

        /// 注册一条命令（若已存在则覆盖）
        void Register(const ConsoleCommand& cmd);

        /// 注销命令
        void Unregister(const std::string& name);

        /**
         * @brief 执行命令字符串
         * @param input  原始输入（如 "god" 或 "spawn 3"）
         * @param output 输出文本
         * @return true 命令找到并执行，false 未找到
         */
        bool Execute(const std::string& input, std::string& output);

        /// 获取所有匹配前缀的命令名称（用于 Tab 补全）
        std::vector<std::string> GetCompletions(const std::string& prefix) const;

        /// 获取所有已注册命令
        const std::unordered_map<std::string, ConsoleCommand>& GetAll() const {
            return m_Commands;
        }

        /// 查找指定命令
        const ConsoleCommand* Find(const std::string& name) const;

        /// 清空所有命令
        void Clear();

    private:
        ConsoleCommandRegistry();
        ~ConsoleCommandRegistry() = default;
        ConsoleCommandRegistry(const ConsoleCommandRegistry&) = delete;
        ConsoleCommandRegistry& operator=(const ConsoleCommandRegistry&) = delete;

        /// 注册内置命令
        void RegisterBuiltins();

        std::unordered_map<std::string, ConsoleCommand> m_Commands;
        bool m_BuiltinsRegistered = false;
    };

} // namespace Engine

/// 便捷宏：在任意初始化代码中快速注册命令（带分类标签）
#define CONSOLE_CMD(name, cat, desc, usage, body)                            \
    do {                                                                     \
        ::Engine::ConsoleCommandRegistry::Instance().Register({               \
            (name), (desc), (usage), (cat),                                   \
            [](const std::vector<std::string>& /*args*/, std::string& out) body \
        });                                                                   \
    } while(false)
