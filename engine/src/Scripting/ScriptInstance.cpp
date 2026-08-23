/**
 * @file ScriptInstance.cpp
 * @brief 脚本实例实现 — 生命周期 / 持久状态 / 热重载（Scripting MVP · S3/S4）
 *
 * Reload 的 _PERSIST 保留协议（S4）：
 *   1. 把当前全局 _PERSIST 存入注册表（ref）
 *   2. 重新执行脚本文件
 *   3. 若新代码未定义 _PERSIST -> 从 ref 恢复旧表；若新代码显式定义了
 *      新表 -> 旧表让位（数据迁移由脚本自行负责，MVP 不做深合并）
 *
 * 脚本首载建议写法：`_PERSIST = _PERSIST or {}`
 */

#include "Engine/Scripting/ScriptInstance.h"
#include "Engine/Scripting/LuaEngine.h"
#include "Engine/Scripting/ScriptAPI.h"
#include "Engine/Scripting/GameplayAPI.h"
#include "Engine/Core/Log.h"

extern "C" {
#include <cstdio>
#include <lua.h>
#include <lauxlib.h>
}

namespace Engine {
namespace Scripting {

namespace {
    Logger s_Log("ScriptInstance");

    constexpr const char* kPersistKey = "_PERSIST";
}

ScriptInstance::~ScriptInstance() { Shutdown(); }

bool ScriptInstance::IsValid() const {
    return m_Loaded && m_Lua && m_Lua->IsInitialized();
}

bool ScriptInstance::Initialize(const std::string& scriptPath, const Config& config) {
    Shutdown();

    m_Lua = std::make_unique<LuaEngine>();
    m_Lua->EnableSandbox(config.sandbox);
    if (!m_Lua->Init()) return false;

    // S2：注册稳定 API 边界（在脚本执行前就位）—— 基础域 + gameplay 三域
    ScriptAPI::RegisterAll(*m_Lua);
    GameplayAPI::RegisterDomains(*m_Lua);

    // S5：指令预算 —— 死循环由 debug hook 中止，pcall 捕获
    m_Lua->SetInstructionBudget(config.instructionBudget);

    m_Path = scriptPath;
    if (!m_Lua->RunFile(m_Path)) {
        s_Log.Error("Load failed: {} ({})", m_Path, m_Lua->GetLastError().message);
        m_Loaded = false;
        return false;
    }

    m_Loaded = true;

    // 回调存在性缓存（Reload 后刷新）
    m_HasOnCreate  = m_Lua->HasFunction("OnCreate");
    m_HasOnUpdate  = m_Lua->HasFunction("OnUpdate");
    m_HasOnFixed   = m_Lua->HasFunction("OnFixedUpdate");
    m_HasOnDestroy = m_Lua->HasFunction("OnDestroy");

    s_Log.Info("Script loaded: {}", scriptPath);
    return true;
}

void ScriptInstance::Shutdown() {
    if (m_Loaded) OnDestroy();
    m_Loaded = false;
    if (m_Lua) m_Lua->Shutdown();
    m_Lua.reset();
}

void ScriptInstance::OnCreate() {
    if (!IsValid()) return;
    if (!m_Lua->CallFunctionVoid("OnCreate")) {
        s_Log.Error("OnCreate: {}", m_Lua->GetLastError().message);
    }
}

void ScriptInstance::OnUpdate(float dt) {
    if (!IsValid()) return;
    if (!m_Lua->CallFunctionVoidWithArg("OnUpdate", dt)) {
        s_Log.Error("OnUpdate: {}", m_Lua->GetLastError().message);
    }
}

void ScriptInstance::OnFixedUpdate(float fixedDt) {
    if (!IsValid()) return;
    if (!m_Lua->CallFunctionVoidWithArg("OnFixedUpdate", fixedDt)) {
        s_Log.Error("OnFixedUpdate: {}", m_Lua->GetLastError().message);
    }
}

void ScriptInstance::OnDestroy() {
    if (!m_Lua || !m_Lua->IsInitialized()) return;
    if (!m_Lua->CallFunctionVoid("OnDestroy")) {
        s_Log.Debug("OnDestroy: {}", m_Lua->GetLastError().message);
    }
}

bool ScriptInstance::Execute(const std::string& luaCode) {
    if (!IsValid()) return false;
    return m_Lua->RunString(luaCode);
}

bool ScriptInstance::Reload() {
    if (!IsValid() || m_Path.empty()) return false;    lua_State* L = m_Lua->GetState();

    // ── S4 step1: 暂存旧 _PERSIST 到注册表 ──
    lua_getglobal(L, kPersistKey);
    const int persistRef = luaL_ref(L, LUA_REGISTRYINDEX);   // 弹出并存引用

    // ── S4 step2: 清空瞬态全局（干净重建语义）并恢复基础环境 + API 边界 ──
    m_Lua->ResetGlobalState();
    ScriptAPI::RegisterAll(*m_Lua);
    GameplayAPI::RegisterDomains(*m_Lua);

    // ── S4 step3: 在执行新代码【前】恢复持久表 ──
    //     使脚本文档化的 `_PERSIST = _PERSIST or {}` 首载/热载兼容模式自然成立：
    //     新代码读到旧数据继续累积；首载时为空表从零开始。
    lua_rawgeti(L, LUA_REGISTRYINDEX, persistRef);
    lua_setglobal(L, kPersistKey);

    // ── S4 step4: 执行新代码 ──
    const bool ok = m_Lua->RunFile(m_Path);
    if (!ok) {
        s_Log.Error("Reload failed: {} ({})", m_Path, m_Lua->GetLastError().message);
        return false;
    }

    m_HasOnCreate  = m_Lua->HasFunction("OnCreate");
    m_HasOnUpdate  = m_Lua->HasFunction("OnUpdate");
    m_HasOnFixed   = m_Lua->HasFunction("OnFixedUpdate");
    m_HasOnDestroy = m_Lua->HasFunction("OnDestroy");

    s_Log.Info("Script reloaded (persist preserved): {}", m_Path);
    return true;
}

}} // namespace Scripting
