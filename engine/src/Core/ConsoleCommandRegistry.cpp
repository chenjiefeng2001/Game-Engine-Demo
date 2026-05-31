#include "Engine/ConsoleCommandRegistry.h"
#include "Engine/ConsoleLog.h"
#include "Engine/ConsoleVariable.h"
#include "Engine/MemoryTracker.h"
#include "Engine/Profiler.h"
#include "Engine/Platform/PlatformUtils.h"
#include <algorithm>
#include <sstream>

namespace Engine {

// ============================================================
// 单例
// ============================================================

ConsoleCommandRegistry& ConsoleCommandRegistry::Instance() {
    static ConsoleCommandRegistry instance;
    return instance;
}

// ============================================================
// 构造 — 注册内置命令
// ============================================================

ConsoleCommandRegistry::ConsoleCommandRegistry() {
    // 内置命令在首次访问时延迟注册（确保静态初始化顺序安全）
}

// ============================================================
// 辅助函数 — 描述当前时间速率
// ============================================================

namespace {

    const char* DescribeSpeed(float scale) {
        if (scale <= 0.0f)  return "|| PAUSED";
        if (scale < 0.4f)    return "<< Very slow";
        if (scale < 0.8f)    return "< Slow-mo";
        if (scale < 1.2f)    return "> Normal";
        if (scale < 2.5f)    return ">> Fast forward";
        if (scale < 5.0f)    return ">>> Super fast";
        return ">>> LUDICROUS SPEED!";
    }

} // anonymous namespace

void ConsoleCommandRegistry::RegisterBuiltins() {
    if (m_BuiltinsRegistered) return;
    m_BuiltinsRegistered = true;

    // ── help — 列出所有命令或显示指定命令的帮助 ──
    Register({
        "help",
        "显示命令列表或指定命令的帮助",
        "help [command]",
        [this](const std::vector<std::string>& args, std::string& out) {
            if (args.size() > 1) {
                // help <cmd>
                const auto* cmd = Find(args[1]);
                if (!cmd) {
                    out = "^1Unknown command:^7 " + args[1];
                    return;
                }
                out = std::string("^2") + cmd->name + "^7 — " + cmd->description;
                if (!cmd->usage.empty()) {
                    out += "\n  ^5Usage:^7 " + cmd->usage;
                }
            } else {
                // help — 列出所有命令
                out = "^5Available commands:^7\n";
                // 按名称排序
                std::vector<std::string> names;
                for (const auto& [n, _] : m_Commands)
                    names.push_back(n);
                std::sort(names.begin(), names.end());

                for (const auto& n : names) {
                    const auto& cmd = m_Commands.at(n);
                    out += "  ^2" + cmd.name;
                    // 对齐
                    if (cmd.name.size() < 16)
                        out.append(16 - cmd.name.size(), ' ');
                    out += "^7 — " + cmd.description + "\n";
                }
                out += "\n^3Tip:^7 use 'help <command>' for details on a specific command.";
            }
        }
    });

    // ── clear — 清空控制台 ──
    Register({
        "clear",
        "清空控制台日志",
        "clear",
        [](const std::vector<std::string>&, std::string& out) {
            ConsoleLog::Instance().Clear();
            out = "Console cleared.";
        }
    });

    // ── echo — 输出文本 ──
    Register({
        "echo",
        "输出文本到控制台",
        "echo <text...>",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 2) {
                out = "";
                return;
            }
            // 拼接剩余参数
            for (size_t i = 1; i < args.size(); ++i) {
                if (i > 1) out += " ";
                out += args[i];
            }
        }
    });

    // ── set — 修改 CVar ──
    Register({
        "set",
        "修改控制台变量的值",
        "set <name> <value>",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 3) {
                out = "Usage: set <name> <value>";
                return;
            }
            auto* cvar = CVarRegistry::Instance().Find(args[1]);
            if (!cvar) {
                out = "Unknown variable: '" + args[1] + "'";
                return;
            }
            // 拼接值（支持带空格的字符串）
            std::string valueStr = args[2];
            for (size_t i = 3; i < args.size(); ++i)
                valueStr += " " + args[i];

            if (cvar->FromString(valueStr)) {
                out = cvar->GetName() + " = " + cvar->ToString();
            } else {
                out = "Failed to set '" + args[1] + "' to '" + valueStr + "'";
            }
        }
    });

    // ── get — 查看 CVar ──
    Register({
        "get",
        "查看控制台变量的值",
        "get [name]",
        [](const std::vector<std::string>& args, std::string& out) {
            auto& registry = CVarRegistry::Instance();
            if (args.size() < 2) {
                // 列出所有变量
                out = "Registered CVars:\n";
                for (const auto& [name, cvar] : registry.GetAll()) {
                    out += "  " + name;
                    if (name.size() < 24)
                        out.append(24 - name.size(), ' ');
                    out += " (" + std::string(CVarTypeName(cvar->GetType())) + ") = "
                         + cvar->ToString() + "\n";
                }
                return;
            }
            auto* cvar = registry.Find(args[1]);
            if (!cvar) {
                out = "Unknown variable: '" + args[1] + "'";
                return;
            }
            out = cvar->GetName() + " (" + CVarTypeName(cvar->GetType()) + ")"
                + " = " + cvar->ToString()
                + "  — " + cvar->GetDescription();
        }
    });

    // ── timescale — 设置游戏速率 ──
    Register({
        "timescale",
        "设置游戏速率（0=暂停, 0.5=半速, 1=正常, 2=双倍）",
        "timescale <value>",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 2) {
                out = "Current timescale: " + std::to_string(Time::GetTimeScale());
                return;
            }
            try {
                float scale = std::stof(args[1]);
                scale = std::max(0.0f, std::min(100.0f, scale)); // 0 ~ 100
                Time::SetTimeScale(scale);
                out = "^2Timescale set to^7 " + std::to_string(scale)
                    + "  ^5" + DescribeSpeed(scale) + "^7";
            } catch (...) {
                out = "^1Invalid value:^7 " + args[1] + ". Use a number like 0.5, 1.0, 2.0";
            }
        }
    });

    // ── pause — 暂停游戏 ──
    Register({
        "pause",
        "暂停游戏（TimeScale = 0）",
        "pause",
        [](const std::vector<std::string>&, std::string& out) {
            Time::Pause();
            out = "|| Game paused (timescale = 0). Use 'resume' to continue.";
        }
    });

    // ── resume — 恢复游戏 ──
    Register({
        "resume",
        "恢复游戏（TimeScale = 1）",
        "resume",
        [](const std::vector<std::string>&, std::string& out) {
            Time::Resume();
            out = "> Game resumed (timescale = 1).";
        }
    });

    // ── unpause — resume 的别名 ──
    Register({
        "unpause",
        "恢复游戏（TimeScale = 1，resume 的别名）",
        "unpause",
        [](const std::vector<std::string>&, std::string& out) {
            Time::Resume();
            out = "> Game resumed (timescale = 1).";
        }
    });

    // ── slowmo — 快速切换半速 ──
    Register({
        "slowmo",
        "切换慢放模式（半速 0.5x）",
        "slowmo",
        [](const std::vector<std::string>&, std::string& out) {
            float current = Time::GetTimeScale();
            if (std::abs(current - 0.5f) < 0.01f) {
                Time::Resume();
                out = "Slow-mo disabled, returned to normal speed.";
            } else {
                Time::SetTimeScale(0.5f);
                out = "< Slow-mo enabled (0.5x). Use 'resume' for normal speed.";
            }
        }
    });

    // ── speed — 查看当前速率 ──
    Register({
        "speed",
        "显示当前游戏速率",
        "speed",
        [](const std::vector<std::string>&, std::string& out) {
            float scale = Time::GetTimeScale();
            out = "^5Current timescale:^7 " + std::to_string(scale)
                + "  ^2" + DescribeSpeed(scale) + "^7";
        }
    });

    // ── profiler — 查看/切换 Profiler 状态 ──
    Register({
        "profiler",
        "显示 Profiler (Tracy) 连接状态与使用指南",
        "profiler [status|help|server]",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() > 1) {
                std::string sub = args[1];
                if (sub == "status") {
                    out = Engine::Profiler::GetDetailedStatus();
                } else if (sub == "help") {
                    out = Engine::Profiler::GetHelp();
                } else if (sub == "server") {
                    if (Engine::Profiler::LaunchServer()) {
                        out = "^2Launching Tracy Server...^7\n"
                              "Server should appear shortly. Once running,\n"
                              "it will auto-discover this game.";
                    } else {
                        out = "^3Could not find Tracy Server executable.^7\n"
                              "Please download from GitHub Releases:\n"
                              "  https://github.com/wolfpld/tracy/releases\n"
                              "(ensure version matches submodule ^5v0.13.1^7)";
                    }
                } else {
                    out = "^1Unknown subcommand:^7 " + sub + ". Try: status, help, server";
                }
            } else {
                // 无参数 → 显示详细状态
                out = Engine::Profiler::GetDetailedStatus();
            }
        }
    });

    // ── memory — 内存使用统计 ──
    Register({
        "memory",
        "显示内存分配统计",
        "memory",
        [](const std::vector<std::string>&, std::string& out) {
            out = Engine::MemoryTracker::GetStatsString();
        }
    });

    // ── memory_panel — 打开/关闭内存监控面板 ──
    Register({
        "memory_panel",
        "打开内存监控面板",
        "memory_panel",
        [](const std::vector<std::string>&, std::string& out) {
            // 通过 Application 获取 ConsolePanel？不，MemoryPanel 在 SystemTestApp 里。
            // 这里仅提供提示，实际面板由 SystemTestApp Dashboard 按钮打开。
            out = "^5Memory Panel:^7 Click 'Memory Panel' in SystemTest Dashboard\n"
                  "to open the real-time memory monitor with usage graph.";
        }
    });
}

// ============================================================
// 注册 / 注销
// ============================================================

void ConsoleCommandRegistry::Register(const ConsoleCommand& cmd) {
    // 确保内置命令已注册
    RegisterBuiltins();
    m_Commands[cmd.name] = cmd;
}

void ConsoleCommandRegistry::Unregister(const std::string& name) {
    m_Commands.erase(name);
}

// ============================================================
// 执行
// ============================================================

bool ConsoleCommandRegistry::Execute(const std::string& input, std::string& output) {
    // 确保内置命令已注册
    RegisterBuiltins();

    if (input.empty()) {
        output = "";
        return true;
    }

    // 按空白字符分割参数（支持引号？暂不实现，保持简单）
    std::vector<std::string> args;
    std::istringstream iss(input);
    std::string token;
    while (iss >> token) {
        args.push_back(token);
    }

    if (args.empty()) {
        output = "";
        return true;
    }

    const std::string& cmdName = args[0];

    auto it = m_Commands.find(cmdName);
    if (it == m_Commands.end()) {
        output = "Unknown command: '" + cmdName + "'. Type 'help' for available commands.";
        return false;
    }

    try {
        it->second.callback(args, output);
    } catch (const std::exception& e) {
        output = "Error executing '" + cmdName + "': " + e.what();
        return false;
    }

    return true;
}

// ============================================================
// 查询
// ============================================================

const ConsoleCommand* ConsoleCommandRegistry::Find(const std::string& name) const {
    auto it = m_Commands.find(name);
    if (it != m_Commands.end())
        return &it->second;
    return nullptr;
}

std::vector<std::string> ConsoleCommandRegistry::GetCompletions(const std::string& prefix) const {
    std::vector<std::string> results;
    for (const auto& [name, _] : m_Commands) {
        if (name.size() >= prefix.size() &&
            name.compare(0, prefix.size(), prefix) == 0) {
            results.push_back(name);
        }
    }
    std::sort(results.begin(), results.end());
    return results;
}

void ConsoleCommandRegistry::Clear() {
    m_Commands.clear();
    m_BuiltinsRegistered = false;  // 下次访问时重建内置命令
}

} // namespace Engine
