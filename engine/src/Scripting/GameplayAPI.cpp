/**
 * @file GameplayAPI.cpp
 * @brief Gameplay Vertical Slice 绑定实现（API Contract v2）
 *
 * 链路验证目标：
 *   OS/Input → IScriptInputProvider → Lua(Engine.input.*)
 *   → handle → ScriptSceneBridge → GameObject/TransformComponent
 *
 * 手写 binding（v2 决策：vertical slice 阶段不引入反射/生成器）。
 */

#include "Engine/Scripting/GameplayAPI.h"
#include "Engine/Scripting/LuaEngine.h"
#include "Engine/Core/Log.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/GameObject/GameObject.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <unordered_map>

namespace Engine {
namespace Scripting {
namespace GameplayAPI {

namespace {

    Logger s_Log("GameplayAPI");

    struct BridgeState {
        Scene* scene = nullptr;
        IScriptInputProvider* input = nullptr;
        std::unordered_map<uint32_t, std::shared_ptr<GameObject>> entities;
        uint32_t nextHandle = 1;
    };

    BridgeState& Ctx() {
        static BridgeState ctx;
        return ctx;
    }

    // ── Engine.input.is_down(key) -> bool ──
    int L_InputIsDown(lua_State* L) {
        const char* key = luaL_checkstring(L, 1);
        bool down = false;
        if (Ctx().input) down = Ctx().input->IsKeyDown(key);
        lua_pushboolean(L, down ? 1 : 0);
        return 1;
    }

    // ── Engine.entity.find(name) -> handle|nil ──
    int L_EntityFind(lua_State* L) {
        const char* name = luaL_checkstring(L, 1);
        const uint32_t h = HandleFindByName(name);
        if (h == 0) { lua_pushnil(L); return 1; }
        lua_pushinteger(L, static_cast<lua_Integer>(h));
        return 1;
    }

    // ── Engine.entity.spawn(name) -> handle|nil ──
    int L_EntitySpawn(lua_State* L) {
        const char* name = luaL_checkstring(L, 1);
        const uint32_t h = HandleSpawn(name);
        if (h == 0) { lua_pushnil(L); return 1; }
        lua_pushinteger(L, static_cast<lua_Integer>(h));
        return 1;
    }

    // ── Engine.entity.destroy(handle) -> bool ──
    int L_EntityDestroy(lua_State* L) {
        const auto h = static_cast<uint32_t>(luaL_checkinteger(L, 1));
        lua_pushboolean(L, HandleDestroy(h) ? 1 : 0);
        return 1;
    }

    // ── Engine.transform.get_position(h) -> x,y,z | nil ──
    int L_TransformGetPosition(lua_State* L) {
        const auto h = static_cast<uint32_t>(luaL_checkinteger(L, 1));
        float x, y, z;
        if (!HandleGetPosition(h, x, y, z)) { lua_pushnil(L); return 1; }
        lua_pushnumber(L, x); lua_pushnumber(L, y); lua_pushnumber(L, z);
        return 3;
    }

    // ── Engine.transform.set_position(h,x,y,z) -> bool ──
    int L_TransformSetPosition(lua_State* L) {
        const auto h  = static_cast<uint32_t>(luaL_checkinteger(L, 1));
        const float x = static_cast<float>(luaL_checknumber(L, 2));
        const float y = static_cast<float>(luaL_checknumber(L, 3));
        const float z = static_cast<float>(luaL_checknumber(L, 4));
        lua_pushboolean(L, HandleSetPosition(h, x, y, z) ? 1 : 0);
        return 1;
    }

    // ── Engine.transform.translate(h,dx,dy,dz) -> bool ──
    int L_TransformTranslate(lua_State* L) {
        const auto h  = static_cast<uint32_t>(luaL_checkinteger(L, 1));
        const float dx = static_cast<float>(luaL_checknumber(L, 2));
        const float dy = static_cast<float>(luaL_checknumber(L, 3));
        const float dz = static_cast<float>(luaL_checknumber(L, 4));
        lua_pushboolean(L, HandleTranslate(h, dx, dy, dz) ? 1 : 0);
        return 1;
    }

    void SetSubtable(lua_State* L, const char* domain, const luaL_Reg* funcs) {
        lua_getglobal(L, "Engine");
        if (!lua_istable(L, -1)) {           // RegisterDomains 先于 ScriptAPI::RegisterAll 时兜底
            lua_pop(L, 1);
            lua_newtable(L);
            lua_setglobal(L, "Engine");
            lua_getglobal(L, "Engine");
        }
        lua_newtable(L);
        luaL_setfuncs(L, funcs, 0);
        lua_setfield(L, -2, domain);
        lua_pop(L, 1);                       // 弹 Engine 表
    }

} // namespace

// ── 上下文注入 ─────────────────────────────────────────

void SetScene(Scene* scene) {
    Ctx().scene = scene;
    if (!scene) Ctx().entities.clear();
}

void SetInputProvider(IScriptInputProvider* provider) {
    Ctx().input = provider;
}

void Reset() {
    Ctx().scene = nullptr;
    Ctx().input = nullptr;
    Ctx().entities.clear();
    Ctx().nextHandle = 1;
}

// ── 句柄操作 ───────────────────────────────────────────

uint32_t HandleFindByName(const std::string& name) {
    auto& c = Ctx();
    for (auto& [h, obj] : c.entities)
        if (obj->GetName() == name) return h;
    return 0;
}

uint32_t HandleSpawn(const std::string& name) {
    auto& c = Ctx();
    if (!c.scene) return 0;
    auto obj = std::make_shared<GameObject>(name.empty() ? "entity" : name);
    c.scene->AddObject(obj);
    const uint32_t h = c.nextHandle++;
    c.entities[h] = std::move(obj);
    return h;
}

uint32_t HandleAdopt(std::shared_ptr<GameObject> obj) {
    auto& c = Ctx();
    if (!c.scene || !obj) return 0;
    // 已在场景中的对象直接登记句柄；否则加入场景
    bool inScene = false;
    for (auto& o : c.scene->GetObjects())
        if (o.get() == obj.get()) { inScene = true; break; }
    if (!inScene) c.scene->AddObject(obj);
    const uint32_t h = c.nextHandle++;
    c.entities[h] = std::move(obj);
    return h;
}

std::string HandleGetName(uint32_t h) {
    auto& c = Ctx();
    auto it = c.entities.find(h);
    return it != c.entities.end() ? it->second->GetName() : std::string();
}

bool HandleDestroy(uint32_t handle) {
    auto& c = Ctx();
    auto it = c.entities.find(handle);
    if (it == c.entities.end()) return false;
    if (c.scene) c.scene->RemoveObject(it->second.get());
    c.entities.erase(it);
    return true;
}

bool HandleGetPosition(uint32_t h, float& x, float& y, float& z) {
    auto& c = Ctx();
    auto it = c.entities.find(h);
    if (it == c.entities.end()) return false;
    const Vec3 p = it->second->GetTransform().GetPosition();
    x = p.x; y = p.y; z = p.z;
    return true;
}

bool HandleSetPosition(uint32_t h, float x, float y, float z) {
    auto& c = Ctx();
    auto it = c.entities.find(h);
    if (it == c.entities.end()) return false;
    it->second->GetTransform().SetPosition(x, y, z);
    return true;
}

bool HandleTranslate(uint32_t h, float dx, float dy, float dz) {
    auto& c = Ctx();
    auto it = c.entities.find(h);
    if (it == c.entities.end()) return false;
    const Vec3 p = it->second->GetTransform().GetPosition();
    it->second->GetTransform().SetPosition(p.x + dx, p.y + dy, p.z + dz);
    return true;
}

uint32_t HandleCount() { return static_cast<uint32_t>(Ctx().entities.size()); }

// ── 分域注册 ───────────────────────────────────────────

void RegisterDomains(LuaEngine& engine) {
    lua_State* L = engine.GetState();
    if (!L) return;

    static const luaL_Reg inputFuncs[] = {
        { "is_down", L_InputIsDown },
        { nullptr, nullptr },
    };
    static const luaL_Reg entityFuncs[] = {
        { "spawn",  L_EntitySpawn },
        { "find",   L_EntityFind },
        { "destroy",L_EntityDestroy },
        { nullptr, nullptr },
    };
    static const luaL_Reg transformFuncs[] = {
        { "get_position", L_TransformGetPosition },
        { "set_position", L_TransformSetPosition },
        { "translate",    L_TransformTranslate },
        { nullptr, nullptr },
    };

    SetSubtable(L, "input",     inputFuncs);
    SetSubtable(L, "entity",    entityFuncs);
    SetSubtable(L, "transform", transformFuncs);

    s_Log.Info("Gameplay domains registered: input/entity/transform");
}

} // namespace GameplayAPI
} // namespace Scripting
} // namespace Engine
