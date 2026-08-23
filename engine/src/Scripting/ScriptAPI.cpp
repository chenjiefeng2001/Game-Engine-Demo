/**
 * @file ScriptAPI.cpp
 * @brief 脚本 → 引擎 稳定 API 边界实现（API Contract v2 · 分域）
 *
 * v2 分域布局（ownership 在无债期建立，防止 Engine God Object）：
 *   Engine.api_version      = 2.0
 *   Engine.log.{info,warn,error}(msg)
 *   Engine.time.now()       -> seconds
 *   Engine.random(lo,hi)    -> number
 *
 *   （input/entity/transform 三域由 GameplayAPI::RegisterDomains 注册，
 *     见 GameplayAPI.cpp —— 本文件保持基础域）
 *
 * 边界规则：
 *   - 每个注册函数体内部完成 C++ ↔ Lua 栈的全部转换
 *   - 函数不得抛出 C++ 异常穿越 Lua 边界（统一走 lua_error 语义）
 *   - 新增接口 = 对应域表加一行 + 静态函数；破坏性变更须递增 api_version
 */

#include "Engine/Scripting/ScriptAPI.h"
#include "Engine/Scripting/LuaEngine.h"
#include "Engine/Core/Log.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <chrono>
#include <random>
#include <string>

namespace Engine {
namespace Scripting {
namespace ScriptAPI {

namespace {

    Logger s_Log("ScriptAPI");

    std::string s_HudMsg;
    bool s_HudDirty = false;

    std::chrono::steady_clock::time_point ClockOrigin() {
        static const std::chrono::steady_clock::time_point origin =
            std::chrono::steady_clock::now();
        return origin;
    }

    int L_LogInfo(lua_State* L) {
        if (lua_gettop(L) < 1 || !lua_isstring(L, 1)) return 0;
        s_Log.Info("[script] {}", lua_tostring(L, 1));
        return 0;
    }

    int L_LogWarn(lua_State* L) {
        if (lua_gettop(L) < 1 || !lua_isstring(L, 1)) return 0;
        s_Log.Warn("[script] {}", lua_tostring(L, 1));
        return 0;
    }

    int L_LogError(lua_State* L) {
        if (lua_gettop(L) < 1 || !lua_isstring(L, 1)) return 0;
        s_Log.Error("[script] {}", lua_tostring(L, 1));
        return 0;
    }

    int L_TimeNow(lua_State* L) {
        const auto& origin = ClockOrigin();              // 先确保基点已初始化
        const auto now = std::chrono::steady_clock::now();
        const double sec =
            std::chrono::duration<double>(now - origin).count();
        lua_pushnumber(L, sec);
        return 1;
    }

    int L_Random(lua_State* L) {
        thread_local std::mt19937 rng{ std::random_device{}() };
        const double lo = lua_gettop(L) >= 2 ? luaL_checknumber(L, 1) : 0.0;
        const double hi = lua_gettop(L) >= 2 ? luaL_checknumber(L, 2) : 1.0;
        std::uniform_real_distribution<double> dist(
            lo <= hi ? lo : hi, hi >= lo ? hi : lo);
        lua_pushnumber(L, dist(rng));
        return 1;
    }

    int L_UiText(lua_State* L) {
        if (lua_gettop(L) >= 1 && lua_isstring(L, 1)) {
            s_HudMsg = lua_tostring(L, 1);
            s_HudDirty = true;
            s_Log.Info("[ui] {}", s_HudMsg);
        }
        return 0;
    }

} // namespace

void ResetClockOrigin() {
    // MVP 接受进程级单调时钟；重置需求出现时再演进
}

double GetApiVersion() { return 2.1; }

void SetHudText(const std::string& msg) { s_HudMsg = msg; s_HudDirty = true; }
const char* GetHudText() { return s_HudDirty ? s_HudMsg.c_str() : nullptr; }
void ClearHudText() { s_HudDirty = false; }

void RegisterAll(LuaEngine& engine) {
    lua_State* L = engine.GetState();
    if (!L) return;

    // Engine 根表（幂等重建）
    lua_newtable(L);

    // ── Engine.log 子域 ──
    lua_newtable(L);
    lua_pushlightuserdata(L, &engine);
    lua_pushcclosure(L, L_LogInfo, 1);
    lua_setfield(L, -2, "info");
    lua_pushcclosure(L, L_LogWarn, 0);
    lua_setfield(L, -2, "warn");
    lua_pushcclosure(L, L_LogError, 0);
    lua_setfield(L, -2, "error");
    lua_setfield(L, -2, "log");

    // ── Engine.time 子域 ──
    lua_newtable(L);
    lua_pushcclosure(L, L_TimeNow, 0);
    lua_setfield(L, -2, "now");
    lua_setfield(L, -2, "time");

    // ── 基础函数 ──
    lua_pushcclosure(L, L_Random, 0);
    lua_setfield(L, -2, "random");

    // ── Engine.ui 子域 ──
    lua_newtable(L);
    lua_pushcclosure(L, L_UiText, 0);
    lua_setfield(L, -2, "text");
    lua_setfield(L, -2, "ui");

    lua_pushnumber(L, GetApiVersion());   // API Contract v2.1：M005 ui.text + M001 entity.find
    lua_setfield(L, -2, "api_version");

    lua_setglobal(L, "Engine");
}

}} // namespace ScriptAPI
} // namespace Engine
