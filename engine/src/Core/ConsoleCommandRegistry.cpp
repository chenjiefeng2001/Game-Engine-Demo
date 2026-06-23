#include "Engine/ConsoleCommandRegistry.h"
#include "Engine/ConsoleLog.h"
#include "Engine/ConsoleVariable.h"
#include "Engine/MemoryTracker.h"
#include "Engine/Profiler.h"
#include "Engine/Platform/PlatformUtils.h"
#include <algorithm>
#include <sstream>
#include <ctime>
#include <imgui.h>

namespace Engine {

// ============================================================
// 单例
// ============================================================

ConsoleCommandRegistry& ConsoleCommandRegistry::Instance() {
    static ConsoleCommandRegistry instance;
    return instance;
}

// ============================================================
// 构造 — 内置命令在首次访问时延迟注册
// ============================================================

ConsoleCommandRegistry::ConsoleCommandRegistry() {
}

// ============================================================
// 辅助函数
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

    /// 将字符串转为小写
    std::string ToLower(const std::string& s) {
        std::string result = s;
        for (auto& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return result;
    }

    /// 安全的 stof，返回 true 成功
    bool SafeStof(const std::string& s, float& out) {
        try {
            out = std::stof(s);
            return true;
        } catch (...) { return false; }
    }

    /// 安全的 stoi，返回 true 成功
    bool SafeStoi(const std::string& s, int32& out) {
        try {
            out = std::stoi(s);
            return true;
        } catch (...) { return false; }
    }

} // anonymous namespace

void ConsoleCommandRegistry::RegisterBuiltins() {
    if (m_BuiltinsRegistered) return;
    m_BuiltinsRegistered = true;

    // ================================================================
    // 1. 帮助 / 信息类
    // ================================================================

    Register({
        "help",
        "显示命令列表或指定命令的详细信息",
        "help [command]",
        [this](const std::vector<std::string>& args, std::string& out) {
            if (args.size() > 1) {
                const auto* cmd = Find(args[1]);
                if (!cmd) {
                    out = "^1Unknown command:^7 " + args[1];
                    return;
                }
                out = "^2" + cmd->name + "^7 — " + cmd->description + "\n"
                    + "  ^5Usage:^7 " + (cmd->usage.empty() ? cmd->name : cmd->usage);
            } else {
                out = "^5=== Available Commands (" + std::to_string(m_Commands.size()) + ") ===^7\n";
                std::vector<std::string> names;
                for (const auto& [n, _] : m_Commands)
                    names.push_back(n);
                std::sort(names.begin(), names.end());

                for (const auto& n : names) {
                    const auto& cmd = m_Commands.at(n);
                    out += "  ^2" + cmd.name;
                    if (cmd.name.size() < 20)
                        out.append(20 - cmd.name.size(), ' ');
                    out += "^7 " + cmd.description + "\n";
                }
                out += "\n^3Tip:^7 'help <command>' for details  |  'cmdlist' for grouped list";
            }
        }
    });

    Register({
        "cmdlist",
        "按分类显示所有控制台命令",
        "cmdlist",
        [this](const std::vector<std::string>&, std::string& out) {
            // 用前缀做简单分组
            struct Group {
                std::string name;
                std::vector<std::string> cmds;
            };
            std::vector<Group> groups = {
                {"Help",     {"help","cmdlist","echo","version","credits"}},
                {"Time",     {"timescale","pause","resume","unpause","slowmo","speed"}},
                {"Render",   {"r_stats","r_wireframe","r_vsync","r_fps","r_fullscreen","r_resolution","r_aa","r_drawcalls","r_debug"}},
                {"Physics",  {"phys_gravity","phys_debug","phys_speed","phys_pause"}},
                {"Audio",    {"s_volume","s_mute","s_debug","s_restart"}},
                {"Debug",    {"god","noclip","fly","kill","tp","spawn","give","ammo","heal"}},
                {"Editor",   {"play","stop","pause_game","save","load","undo","redo"}},
                {"System",   {"quit","exit","exec","config","version","log","crash","memory","profiler","screenshot"}},
                {"Console",  {"clear","set","get","bind","unbind","con_log","con_height"}},
                {"Input",    {"cl_sensitivity","cl_invert","cl_showmouse","cl_crosshair"}},
                {"UI",       {"ui_scale","ui_theme"}},
            };

            out = "^5=== Command Categories ===^7\n";
            for (const auto& g : groups) {
                out += "\n^3[" + g.name + "]^7\n";
                for (const auto& cname : g.cmds) {
                    const auto* cmd = Find(cname);
                    if (cmd) {
                        out += "  ^2" + cmd->name;
                        if (cmd->name.size() < 18)
                            out.append(18 - cmd->name.size(), ' ');
                        out += "^7 " + cmd->description + "\n";
                    }
                }
            }
        }
    });

    Register({
        "echo",
        "输出指定文本到控制台",
        "echo <text...>",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 2) return;
            for (size_t i = 1; i < args.size(); ++i) {
                if (i > 1) out += " ";
                out += args[i];
            }
        }
    });

    Register({
        "version",
        "显示引擎版本信息",
        "version",
        [](const std::vector<std::string>&, std::string& out) {
            out = "^5=== Game Engine Demo ===^7\n"
                  "  Version:   0.1.0-dev\n"
                  "  Build:     " __DATE__ " " __TIME__ "\n"
                  "  Render:    OpenGL 4.6\n"
                  "  UI:        Dear ImGui 1.91\n"
                  "  Physics:   Box2D\n"
                  "  Audio:     OpenAL Soft\n"
                  "  Profiler:  Tracy 0.13";
        }
    });

    Register({
        "credits",
        "显示引擎技术栈致谢",
        "credits",
        [](const std::vector<std::string>&, std::string& out) {
            out = "^5=== Credits ===^7\n"
                  "  Game Engine Demo\n"
                  "  Built with:\n"
                  "    • GLFW         — Window & Input\n"
                  "    • GLAD         — OpenGL Loader\n"
                  "    • glm          — Mathematics\n"
                  "    • Box2D        — 2D Physics\n"
                  "    • OpenAL Soft  — 3D Audio\n"
                  "    • spdlog       — Logging\n"
                  "    • stb          — Image Loading\n"
                  "    • Dear ImGui   — Editor UI\n"
                  "    • ImGuizmo     — Transform Gizmo\n"
                  "    • nlohmann/json — Serialization\n"
                  "    • Tracy        — Profiling\n"
                  "    • Freetype     — Font Rendering";
        }
    });

    // ================================================================
    // 2. 控制台自身
    // ================================================================

    Register({
        "clear",
        "清空控制台历史日志",
        "clear",
        [](const std::vector<std::string>&, std::string& out) {
            ConsoleLog::Instance().Clear();
            out = "Console cleared.";
        }
    });

    Register({
        "con_log",
        "设置/查看控制台日志文件路径",
        "con_log [path|off]",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 2) {
                auto& log = ConsoleLog::Instance();
                out = "Console log: " + (log.GetLogPath().empty() ? "^3(not set)^7" : log.GetLogPath());
                return;
            }
            if (args[1] == "off") {
                ConsoleLog::Instance().SetLogPath("");
                out = "Console log disabled.";
            } else {
                ConsoleLog::Instance().SetLogPath(args[1]);
                out = "Console log set to: " + args[1];
            }
        }
    });

    Register({
        "con_height",
        "设置控制台面板高度（0.0 ~ 1.0 比例）",
        "con_height <ratio>",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 2) {
                out = "Current console height ratio: 0.5";
                return;
            }
            out = "Console height set (NYI). Use slider in Console panel.";
        }
    });

    // ================================================================
    // 3. CVar 系统
    // ================================================================

    Register({
        "set",
        "设置控制台变量值",
        "set <name> <value>",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 3) {
                out = "Usage: set <name> <value> (e.g. 'set player_speed 2.0')";
                return;
            }
            auto* cvar = CVarRegistry::Instance().Find(args[1]);
            if (!cvar) {
                out = "^1Unknown variable:^7 '" + args[1] + "'. Use 'get' to list all.";
                return;
            }
            std::string valueStr = args[2];
            for (size_t i = 3; i < args.size(); ++i)
                valueStr += " " + args[i];

            if (cvar->FromString(valueStr)) {
                out = "^2" + cvar->GetName() + "^7 = " + cvar->ToString();
            } else {
                out = "^1Failed to set^7 '" + args[1] + "' to '" + valueStr + "'\n"
                      "  Expected type: " + std::string(CVarTypeName(cvar->GetType()));
            }
        }
    });

    Register({
        "get",
        "查看控制台变量值（无参数时列出全部）",
        "get [name]",
        [](const std::vector<std::string>& args, std::string& out) {
            auto& registry = CVarRegistry::Instance();
            if (args.size() < 2) {
                out = "^5Registered CVars (" + std::to_string(registry.GetAll().size()) + "):^7\n";
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
                out = "^1Unknown variable:^7 '" + args[1] + "'";
                return;
            }
            out = "^2" + cvar->GetName() + "^7 (" + CVarTypeName(cvar->GetType()) + ")"
                + " = ^5" + cvar->ToString() + "^7\n"
                + "  " + cvar->GetDescription();
        }
    });

    Register({
        "bind",
        "将按键绑定到控制台命令（预留）",
        "bind <key> <command>",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 3) {
                out = "Usage: bind <key> <command>\n"
                      "  e.g. 'bind F1 god' — press F1 to toggle god mode\n"
                      "^3(Key binding system not yet implemented)^7";
                return;
            }
            out = "^2Bound^7 '" + args[1] + "' -> '" + args[2] + "'\n"
                  "^3(Key binding storage pending implementation)^7";
        }
    });

    Register({
        "unbind",
        "解除按键绑定（预留）",
        "unbind <key>",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 2) {
                out = "Usage: unbind <key>";
                return;
            }
            out = "^2Unbound^7 key: " + args[1];
        }
    });

    // ================================================================
    // 4. 时间控制
    // ================================================================

    Register({
        "timescale",
        "设置游戏时间速率（0=暂停, 0.5=半速, 1=正常, 2=双倍, 5=极速）",
        "timescale <value>",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 2) {
                out = "Current timescale: ^5" + std::to_string(Time::GetTimeScale()) + "^7\n"
                      "  " + DescribeSpeed(Time::GetTimeScale());
                return;
            }
            float scale;
            if (!SafeStof(args[1], scale)) {
                out = "^1Invalid value:^7 '" + args[1] + "'. Use a number (0.0 ~ 100.0)";
                return;
            }
            scale = std::max(0.0f, std::min(100.0f, scale));
            Time::SetTimeScale(scale);
            out = "^2Timescale set to^7 " + std::to_string(scale) + "  ^5[ " + DescribeSpeed(scale) + " ]^7";
        }
    });

    Register({
        "pause",
        "暂停游戏（设置 TimeScale = 0）",
        "pause",
        [](const std::vector<std::string>&, std::string& out) {
            Time::Pause();
            out = "^5|| Game PAUSED^7\n  Use 'resume' or 'unpause' to continue.";
        }
    });

    Register({
        "resume",
        "恢复游戏至正常速度（TimeScale = 1）",
        "resume",
        [](const std::vector<std::string>&, std::string& out) {
            Time::Resume();
            out = "^2> Game resumed^7 (timescale = 1.0)";
        }
    });

    Register({
        "unpause",
        "resume 的别名 — 恢复游戏",
        "unpause",
        [](const std::vector<std::string>&, std::string& out) {
            Time::Resume();
            out = "^2> Game resumed^7 (timescale = 1.0)";
        }
    });

    Register({
        "slowmo",
        "切换慢动作模式（0.5x 半速）",
        "slowmo",
        [](const std::vector<std::string>&, std::string& out) {
            float current = Time::GetTimeScale();
            if (std::abs(current - 0.5f) < 0.01f) {
                Time::Resume();
                out = "^2Slow-mo disabled^7, returned to normal speed.";
            } else {
                Time::SetTimeScale(0.5f);
                out = "^3< Slow-mo enabled^7 (0.5x). Use 'resume' for normal speed.";
            }
        }
    });

    Register({
        "speed",
        "显示当前游戏速率详细状态",
        "speed",
        [](const std::vector<std::string>&, std::string& out) {
            float scale = Time::GetTimeScale();
            float gameDt = Time::GetGameDeltaTime();
            float rawDt  = Time::GetDeltaTime();
            out = "^5=== Time State ===^7\n"
                  "  Timescale:  ^2" + std::to_string(scale) + "^7\n"
                  "  Game dt:    " + std::to_string(gameDt) + "s\n"
                  "  Raw dt:     " + std::to_string(rawDt) + "s\n"
                  "  Status:     ^5[ " + std::string(DescribeSpeed(scale)) + " ]^7";
        }
    });

    // ================================================================
    // 5. 渲染 / 图形
    // ================================================================

    Register({
        "r_stats",
        "显示当前帧渲染统计信息",
        "r_stats",
        [](const std::vector<std::string>&, std::string& out) {
            // 通过 Application 获取 — 这里只做占位
            out = "^5=== Render Stats ===^7\n"
                  "  FPS:         (check Performance window)\n"
                  "  Draw calls:  (check Performance window)\n"
                  "  Triangles:   (check Performance window)\n"
                  "  VRAM:        (check Performance window)\n"
                  "^3Detailed stats available in Performance window^7";
        }
    });

    Register({
        "r_wireframe",
        "切换线框模式（预览功能）",
        "r_wireframe [on|off]",
        [](const std::vector<std::string>& args, std::string& out) {
            out = "^3Wireframe mode toggle — pending renderer integration^7\n"
                  "  (Use Viewport toolbar's ViewMode dropdown for now)";
        }
    });

    Register({
        "r_vsync",
        "启用/关闭垂直同步",
        "r_vsync [on|off]",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 2) {
                out = "Usage: r_vsync on|off\n"
                      "^3(VSync toggle pending window backend integration)^7";
                return;
            }
            bool enable = (ToLower(args[1]) == "on" || args[1] == "1");
            out = "^2VSync^7 " + std::string(enable ? "ON" : "OFF")
                + " ^3(implementation pending)^7";
        }
    });

    Register({
        "r_fps",
        "显示/限制最大帧率",
        "r_fps [max_fps]",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 2) {
                out = "Current FPS limit: unlimited (check Performance window for actual FPS)";
                return;
            }
            int32 fps;
            if (SafeStoi(args[1], fps) && fps > 0) {
                out = "^2FPS limit set to^7 " + std::to_string(fps) + " ^3(NYI: frame pacing)^7";
            } else {
                out = "^1Invalid FPS:^7 '" + args[1] + "'. Use a positive number.";
            }
        }
    });

    Register({
        "r_fullscreen",
        "切换全屏/窗口模式",
        "r_fullscreen",
        [](const std::vector<std::string>&, std::string& out) {
            out = "^3Fullscreen toggle — pending window backend integration^7";
        }
    });

    Register({
        "r_resolution",
        "设置窗口分辨率",
        "r_resolution <width> <height>",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 3) {
                out = "Usage: r_resolution <width> <height>\n"
                      "  e.g. 'r_resolution 1920 1080'\n"
                      "^3(Resolution change pending window backend)^7";
                return;
            }
            out = "Resolution: " + args[1] + "x" + args[2]
                + " ^3(NYI: window resize)^7";
        }
    });

    Register({
        "r_aa",
        "设置抗锯齿模式",
        "r_aa [0|2|4|8|off]",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 2) {
                out = "Current AA: MSAA x4\n"
                      "  Options: off, 2, 4, 8\n"
                      "^3(AA toggle pending GPU backend)^7";
                return;
            }
            out = "^2Anti-aliasing set to^7 " + args[1]
                + " ^3(NYI: renderer re-initialization)^7";
        }
    });

    Register({
        "r_drawcalls",
        "查看/控制每帧最大绘制调用数",
        "r_drawcalls [limit]",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 2) {
                out = "Draw call limit: unlimited\n"
                      "  (Check Performance window for actual count)";
                return;
            }
            out = "^2Draw call limit set to^7 " + args[1];
        }
    });

    Register({
        "r_debug",
        "切换渲染调试可视化（法线/UV/光照等）",
        "r_debug [none|normals|uv|lighting]",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 2) {
                out = "Usage: r_debug none|normals|uv|lighting\n"
                      "  Current: none";
                return;
            }
            out = "^2Render debug mode:^7 " + args[1];
        }
    });

    // ================================================================
    // 6. 物理
    // ================================================================

    Register({
        "phys_gravity",
        "设置/查看物理世界重力",
        "phys_gravity <x> <y>",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 3) {
                out = "Current gravity: (0.0, -9.81)\n"
                      "  Usage: phys_gravity <x> <y>\n"
                      "  e.g. 'phys_gravity 0 -20' for heavier gravity\n"
                      "       'phys_gravity 0 0' for zero-G\n"
                      "^3(Gravity change pending physics world access)^7";
                return;
            }
            out = "^2Gravity set to^7 (" + args[1] + ", " + args[2] + ")"
                + " ^3(NYI: apply to physics world)^7";
        }
    });

    Register({
        "phys_debug",
        "切换物理调试绘制（碰撞体/关节等）",
        "phys_debug [on|off]",
        [](const std::vector<std::string>& args, std::string& out) {
            bool enable = (args.size() < 2) || (ToLower(args[1]) == "on" || args[1] == "1");
            if (args.size() >= 2 && ToLower(args[1]) == "off") enable = false;
            out = "^2Physics debug draw^7 " + std::string(enable ? "ON" : "OFF")
                + " ^3(NYI: wire up to Box2DDebugDraw)^7";
        }
    });

    Register({
        "phys_speed",
        "设置物理模拟速率倍率",
        "phys_speed <multiplier>",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 2) {
                out = "Physics speed multiplier: 1.0x";
                return;
            }
            float mult;
            if (SafeStof(args[1], mult) && mult > 0.0f) {
                out = "^2Physics speed set to^7 " + std::to_string(mult) + "x"
                    + " ^3(NYI: per-system timescale)^7";
            } else {
                out = "^1Invalid multiplier:^7 '" + args[1] + "'";
            }
        }
    });

    Register({
        "phys_pause",
        "暂停物理模拟",
        "phys_pause",
        [](const std::vector<std::string>&, std::string& out) {
            out = "^3Physics pause — pending subsystem pause API^7";
        }
    });

    // ================================================================
    // 7. 音频
    // ================================================================

    Register({
        "s_volume",
        "设置主音量（0.0 ~ 1.0）",
        "s_volume <level>",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 2) {
                out = "Master volume: 1.0 (100%%)";
                return;
            }
            float vol;
            if (SafeStof(args[1], vol)) {
                vol = std::max(0.0f, std::min(1.0f, vol));
                out = "^2Master volume set to^7 " + std::to_string(static_cast<int>(vol * 100.0f)) + "%"
                    + " ^3(NYI: apply to AudioEngine)^7";
            } else {
                out = "^1Invalid volume:^7 '" + args[1] + "'. Use 0.0 ~ 1.0";
            }
        }
    });

    Register({
        "s_mute",
        "静音/取消静音",
        "s_mute",
        [](const std::vector<std::string>&, std::string& out) {
            out = "^3Mute toggle — pending AudioEngine integration^7";
        }
    });

    Register({
        "s_debug",
        "显示音频调试信息（活动音源/缓冲等）",
        "s_debug",
        [](const std::vector<std::string>&, std::string& out) {
            out = "^3Audio debug — pending AudioEngine query API^7";
        }
    });

    Register({
        "s_restart",
        "重启音频引擎",
        "s_restart",
        [](const std::vector<std::string>&, std::string& out) {
            out = "^3Audio restart — pending AudioEngine lifecycle^7";
        }
    });

    // ================================================================
    // 8. 调试 / 作弊
    // ================================================================

    Register({
        "god",
        "切换无敌模式",
        "god",
        [](const std::vector<std::string>&, std::string& out) {
            // 实际由 SystemTestApp 的 CVar 处理
            out = "^2God mode toggled^7 (actual implementation in your game code)\n"
                  "  Tip: Use 'set god_mode 1' in your app";
        }
    });

    Register({
        "noclip",
        "切换穿墙/自由飞行模式",
        "noclip",
        [](const std::vector<std::string>&, std::string& out) {
            out = "^2NoClip mode toggled^7 (actual implementation in your game code)\n"
                  "  Tip: Use 'set noclip 1' in your app";
        }
    });

    Register({
        "fly",
        "切换飞行模式",
        "fly",
        [](const std::vector<std::string>&, std::string& out) {
            out = "^2Fly mode toggled^7 (implementation in your game code)";
        }
    });

    Register({
        "kill",
        "杀死玩家角色",
        "kill",
        [](const std::vector<std::string>&, std::string& out) {
            out = "^1Player killed^7 (implementation in your game code)";
        }
    });

    Register({
        "heal",
        "恢复玩家生命值到满",
        "heal [amount]",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 2) {
                out = "^2Player fully healed^7 (implementation in your game code)";
            } else {
                out = "^2Player healed for^7 " + args[1] + " HP (implementation in your game code)";
            }
        }
    });

    Register({
        "ammo",
        "补充弹药",
        "ammo",
        [](const std::vector<std::string>&, std::string& out) {
            out = "^2Ammo refilled^7 (implementation in your game code)";
        }
    });

    Register({
        "give",
        "给予指定物品/武器",
        "give <item_name> [count]",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 2) {
                out = "Usage: give <item_name> [count]\n"
                      "  e.g. 'give health' or 'give weapon_rifle'";
                return;
            }
            out = "^2Given^7 " + args[1]
                + (args.size() > 2 ? " x" + args[2] : "")
                + " ^3(implementation in your game code)^7";
        }
    });

    Register({
        "tp",
        "传送玩家到指定坐标",
        "tp <x> <y> [z]",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 3) {
                out = "Usage: tp <x> <y> [z]\n"
                      "  e.g. 'tp 400 300' — teleport to center";
                return;
            }
            out = "^2Teleported to^7 (" + args[1];
            out += ", " + args[2];
            if (args.size() > 3) out += ", " + args[3];
            out += ") ^3(implementation in your game code)^7";
        }
    });

    Register({
        "spawn",
        "生成实体",
        "spawn <entity_type> [count]",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 2) {
                out = "Usage: spawn <entity_type> [count]\n"
                      "  e.g. 'spawn enemy' or 'spawn pickup 5'";
                return;
            }
            int32 count = 1;
            if (args.size() > 2) SafeStoi(args[2], count);
            out = "^2Spawned^7 " + std::to_string(count) + " x '" + args[1] + "'"
                + " ^3(implementation in your game code)^7";
        }
    });

    Register({
        "sv_cheats",
        "启用/禁用作弊模式",
        "sv_cheats [on|off]",
        [](const std::vector<std::string>& args, std::string& out) {
            bool enable = (args.size() < 2) || (ToLower(args[1]) != "off");
            out = "^5Cheats^7 " + std::string(enable ? "ENABLED" : "DISABLED");
        }
    });

    // ================================================================
    // 9. 编辑器
    // ================================================================

    Register({
        "play",
        "进入游戏运行模式（编辑器内点击 Play）",
        "play",
        [](const std::vector<std::string>&, std::string& out) {
            out = "^2> Play mode activated^7\n"
                  "  Click the Play button in Editor toolbar, or Scene → Play";
        }
    });

    Register({
        "stop",
        "停止游戏运行模式，返回编辑器",
        "stop",
        [](const std::vector<std::string>&, std::string& out) {
            out = "^2< Play mode stopped^7, returned to Editor";
        }
    });

    Register({
        "save",
        "保存当前场景",
        "save [file_path]",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 2) {
                out = "^5Scene saved^7 (default path) ^3(NYI: serialization)^7";
            } else {
                out = "^5Scene saved to^7 " + args[1] + " ^3(NYI: serialization)^7";
            }
        }
    });

    Register({
        "load",
        "加载场景文件",
        "load <file_path>",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 2) {
                out = "Usage: load <file_path>\n"
                      "  e.g. 'load scenes/test_scene.json'";
                return;
            }
            out = "^5Loading scene:^7 " + args[1] + " ^3(NYI: deserialization)^7";
        }
    });

    Register({
        "undo",
        "撤销上一步操作",
        "undo",
        [](const std::vector<std::string>&, std::string& out) {
            out = "^5Undo^7 ^3(NYI: editor action history)^7";
        }
    });

    Register({
        "redo",
        "重做已撤销的操作",
        "redo",
        [](const std::vector<std::string>&, std::string& out) {
            out = "^5Redo^7 ^3(NYI: editor action history)^7";
        }
    });

    // ================================================================
    // 10. 系统
    // ================================================================

    Register({
        "quit",
        "退出游戏/编辑器",
        "quit",
        [](const std::vector<std::string>&, std::string& out) {
            // 这里不能直接关闭窗口，需要 Application 接收信号
            out = "^1Quit requested^7 — closing application...";
            // 注意：Application 需要在主循环中检查这个状态
            // 可以通过标记 CVar 或设置窗口关闭标志
        }
    });

    Register({
        "exit",
        "quit 的别名 — 退出程序",
        "exit",
        [](const std::vector<std::string>&, std::string& out) {
            out = "^1Exit requested^7 — closing application...";
        }
    });

    Register({
        "exec",
        "执行一个脚本文件中的命令序列",
        "exec <filename>",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 2) {
                out = "Usage: exec <filename>\n"
                      "  e.g. 'exec autoexec.cfg' — runs on startup";
                return;
            }
            out = "^5Executing script:^7 " + args[1] + " ^3(NYI: script parser)^7";
        }
    });

    Register({
        "config",
        "查看/管理引擎配置",
        "config [save|load|reset]",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 2) {
                out = "Usage: config save|load|reset\n"
                      "  save  — Save current settings to config file\n"
                      "  load  — Load settings from config file\n"
                      "  reset — Reset to defaults";
                return;
            }
            if (args[1] == "save") {
                out = "^2Configuration saved^7 (NYI: serialize to file)";
            } else if (args[1] == "load") {
                out = "^2Configuration loaded^7 (NYI: deserialize from file)";
            } else if (args[1] == "reset") {
                out = "^3Configuration reset to defaults^7 (NYI)";
            } else {
                out = "^1Unknown subcommand:^7 " + args[1];
            }
        }
    });

    Register({
        "log",
        "控制日志系统（级别/过滤）",
        "log [level|filter] [param]",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 2) {
                out = "Usage:\n"
                      "  log level trace|debug|info|warn|error\n"
                      "  log filter <keyword>\n"
                      "  log status  — show current log levels";
                return;
            }
            if (args[1] == "status") {
                out = "^5Log System Status^7\n"
                      "  Levels: trace, debug, info, warn, error, critical\n"
                      "  Default level: info\n"
                      "  (Per-module log levels can be set in code via Log::Get())";
            } else if (args[1] == "level" && args.size() > 2) {
                out = "^2Log level set to^7 " + args[2];
            } else {
                out = "^3Log command^7 — see 'log status'";
            }
        }
    });

    Register({
        "crash",
        "触发一次崩溃（用于测试崩溃报告系统）",
        "crash [nullptr|assert|stack]",
        [](const std::vector<std::string>& args, std::string& out) {
            out = "^1Crash test trigger^7\n"
                  "  Usage: crash nullptr|assert|stack\n"
                  "  ^3(Use the 'Trigger Nullptr Crash' button in SystemTest)^7";
        }
    });

    Register({
        "screenshot",
        "截取当前帧到文件",
        "screenshot [filename]",
        [](const std::vector<std::string>& args, std::string& out) {
            std::string name = (args.size() > 1) ? args[1] : "screenshot_" + std::to_string(std::time(nullptr)) + ".png";
            out = "^2Screenshot saved to^7 " + name + " ^3(NYI: capture and encode)^7";
        }
    });

    // ================================================================
    // 11. Profiler & 内存
    // ================================================================

    Register({
        "profiler",
        "Profiler (Tracy) 调试工具",
        "profiler [status|help|server]",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() > 1) {
                std::string sub = args[1];
                if (sub == "status") {
                    out = Profiler::GetDetailedStatus();
                } else if (sub == "help") {
                    out = Profiler::GetHelp();
                } else if (sub == "server") {
                    if (Profiler::LaunchServer()) {
                        out = "^2Launching Tracy Server...^7\n"
                              "Server should appear shortly. It will auto-discover this app.";
                    } else {
                        out = "^3Could not find Tracy Server executable.^7\n"
                              "Download from: https://github.com/wolfpld/tracy/releases";
                    }
                } else {
                    out = "^1Unknown subcommand:^7 " + sub + ". Try: status, help, server";
                }
            } else {
                out = Profiler::GetDetailedStatus();
            }
        }
    });

    Register({
        "memory",
        "显示详细内存使用统计",
        "memory",
        [](const std::vector<std::string>&, std::string& out) {
            out = MemoryTracker::GetStatsString();
        }
    });

    Register({
        "memory_panel",
        "打开/关闭内存监控面板",
        "memory_panel",
        [](const std::vector<std::string>&, std::string& out) {
            out = "^5Memory Panel:^7 Use 'Memory Panel' button in SystemTest Dashboard\n"
                  "  or add m_MemoryPanel.OnImGui() to your OnImGui() override.";
        }
    });

    // ================================================================
    // 12. 输入 / UI
    // ================================================================

    Register({
        "cl_sensitivity",
        "设置鼠标灵敏度",
        "cl_sensitivity <value>",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 2) {
                out = "Mouse sensitivity: 1.0";
                return;
            }
            float sens;
            if (SafeStof(args[1], sens) && sens > 0.0f) {
                out = "^2Mouse sensitivity set to^7 " + std::to_string(sens)
                    + " ^3(NYI: apply to camera)^7";
            } else {
                out = "^1Invalid sensitivity:^7 '" + args[1] + "'";
            }
        }
    });

    Register({
        "cl_invert",
        "切换鼠标 Y 轴反转",
        "cl_invert [on|off]",
        [](const std::vector<std::string>& args, std::string& out) {
            bool invert = (args.size() < 2) || (ToLower(args[1]) != "off");
            out = "^2Mouse Y-axis invert^7: " + std::string(invert ? "ON" : "OFF")
                + " ^3(NYI)^7";
        }
    });

    Register({
        "cl_showmouse",
        "切换鼠标光标可见性",
        "cl_showmouse [on|off]",
        [](const std::vector<std::string>& args, std::string& out) {
            bool show = (args.size() < 2) || (ToLower(args[1]) != "off");
            out = "^2Mouse cursor^7: " + std::string(show ? "visible" : "hidden")
                + " ^3(NYI: cursor mode toggle)^7";
        }
    });

    Register({
        "cl_crosshair",
        "切换/自定义准星",
        "cl_crosshair [on|off|style]",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 2) {
                out = "Crosshair: ON (default) ^3(NYI: customization)^7";
                return;
            }
            out = "^2Crosshair^7: " + args[1] + " ^3(NYI)^7";
        }
    });

    Register({
        "ui_scale",
        "设置 UI 缩放比例",
        "ui_scale <scale>",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 2) {
                out = "UI scale: 1.0 (use Ctrl+mouse wheel in ImGui)";
                return;
            }
            float scale;
            if (SafeStof(args[1], scale) && scale > 0.25f && scale <= 4.0f) {
                out = "^2UI scale set to^7 " + std::to_string(scale)
                    + " ^3(NYI: ImGui global scale)^7";
            } else {
                out = "^1Invalid scale:^7 '" + args[1] + "'. Use 0.25 ~ 4.0";
            }
        }
    });

    Register({
        "ui_theme",
        "切换 UI 主题",
        "ui_theme [dark|light|classic]",
        [](const std::vector<std::string>& args, std::string& out) {
            if (args.size() < 2) {
                out = "Current theme: Dark\n"
                      "  Options: dark, light, classic\n"
                      "  (theme changes take effect immediately via ImGui::StyleColors*)";
                return;
            }
            std::string theme = ToLower(args[1]);
            if (theme == "light") {
                ImGui::StyleColorsLight();
                out = "^2Theme changed to^7 'Light'";
            } else if (theme == "classic") {
                ImGui::StyleColorsClassic();
                out = "^2Theme changed to^7 'Classic'";
            } else {
                ImGui::StyleColorsDark();
                out = "^2Theme changed to^7 'Dark'";
            }
        }
    });
}

// ============================================================
// 注册 / 注销
// ============================================================

void ConsoleCommandRegistry::Register(const ConsoleCommand& cmd) {
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
    RegisterBuiltins();

    if (input.empty()) {
        output = "";
        return true;
    }

    // 按空白字符分割参数
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
        output = "^1Unknown command:^7 '" + cmdName + "'.\n"
                 "  Type 'help' for available commands or 'cmdlist' for categorized list.";
        return false;
    }

    try {
        it->second.callback(args, output);
    } catch (const std::exception& e) {
        output = "^1Error executing '" + cmdName + "':^7 " + e.what();
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
    m_BuiltinsRegistered = false;
}

} // namespace Engine