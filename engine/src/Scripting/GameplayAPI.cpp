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
#include "Engine/Core/GameObject/Component.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <unordered_map>
#include <cstring>

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
        // 组件句柄 → (实体句柄, 稳定类型名)。实体销毁时对应组件句柄随之失效
        // （查找时以实体仍存活为前提）。
        std::unordered_map<uint32_t, std::pair<uint32_t, std::string>> components;
        uint32_t nextComponentHandle = 1;
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

    // ── Component Contract（F1-E）：Engine.component.* 通用组件 API ──
    //   不堆 Engine.camera.* 特化；组件经稳定类型名 + 反射属性读写。

    /// 解析组件句柄：返回实体 GameObject*（仍存活）与组件指针分别写回。
    static bool ResolveComponent(uint32_t compH, GameObject** outObj,
                                 Component** outComp) {
        auto& c = Ctx();
        auto it = c.components.find(compH);
        if (it == c.components.end()) return false;
        auto eIt = c.entities.find(it->second.first);
        if (eIt == c.entities.end()) return false;
        GameObject* obj = eIt->second.get();
        Component* comp = obj->GetComponentByName(it->second.second);
        if (!comp) return false;
        *outObj = obj;
        *outComp = comp;
        return true;
    }

    /// 登记（或复用）实体上某类型组件的句柄
    static uint32_t EnsureComponentHandle(GameObject* obj, const char* type) {
        auto& c = Ctx();
        uint32_t entityH = 0;
        for (auto& [eh, o] : c.entities)
            if (o.get() == obj) { entityH = eh; break; }
        if (entityH == 0) return 0;
        for (auto& [ch, kv] : c.components) {
            if (kv.first == entityH && kv.second == type) return ch;
        }
        const uint32_t ch = c.nextComponentHandle++;
        c.components[ch] = { entityH, type };
        return ch;
    }

    /// 清除该实体的所有组件句柄（Destroy 实体内调用，避免悬垂）
    static void EvictComponentHandles(uint32_t entityH) {
        auto& c = Ctx();
        for (auto it = c.components.begin(); it != c.components.end();) {
            if (it->second.first == entityH) it = c.components.erase(it);
            else ++it;
        }
    }

    /// 反射属性索引按名查找
    static int FindPropIndex(Component* comp, const char* name) {
        const size_t n = comp->GetPropertyCount();
        for (size_t i = 0; i < n; ++i) {
            ComponentPropertyDesc d; comp->GetPropertyDesc(i, &d);
            if (d.name && std::strcmp(d.name, name) == 0)
                return static_cast<int>(i);
        }
        return -1;
    }

    // ── Engine.component.add(entity, type) -> bool ──
    int L_ComponentAdd(lua_State* L) {
        const auto        eh = static_cast<uint32_t>(luaL_checkinteger(L, 1));
        const char*        type = luaL_checkstring(L, 2);
        auto& c = Ctx();
        auto it = c.entities.find(eh);
        if (it == c.entities.end() || !type) { lua_pushboolean(L, 0); return 1; }
        GameObject* obj = it->second.get();
        Component* comp = obj->AddComponentByName(type);
        lua_pushboolean(L, comp ? 1 : 0);
        return 1;
    }

    // ── Engine.component.has(entity, type) -> bool ──
    int L_ComponentHas(lua_State* L) {
        const auto eh = static_cast<uint32_t>(luaL_checkinteger(L, 1));
        const char* type = luaL_checkstring(L, 2);
        auto it = Ctx().entities.find(eh);
        bool has = (it != Ctx().entities.end() && type
                    && it->second->HasComponentByName(type));
        lua_pushboolean(L, has ? 1 : 0);
        return 1;
    }

    // ── Engine.component.get(entity, type) -> compHandle|nil ──
    int L_ComponentGet(lua_State* L) {
        const auto eh = static_cast<uint32_t>(luaL_checkinteger(L, 1));
        const char* type = luaL_checkstring(L, 2);
        auto it = Ctx().entities.find(eh);
        if (it == Ctx().entities.end() || !type
            || !it->second->HasComponentByName(type)) { lua_pushnil(L); return 1; }
        lua_pushinteger(L, static_cast<lua_Integer>(
            EnsureComponentHandle(it->second.get(), type)));
        return 1;
    }

    // ── Engine.component.remove(entity, type) -> bool ──
    int L_ComponentRemove(lua_State* L) {
        const auto       eh = static_cast<uint32_t>(luaL_checkinteger(L, 1));
        const char*       type = luaL_checkstring(L, 2);
        auto& c = Ctx();
        auto it = c.entities.find(eh);
        if (it == c.entities.end() || !type) { lua_pushboolean(L, 0); return 1; }
        const bool removed = it->second->RemoveComponentByName(type);
        if (removed) {
            // 失效本实体对应组件句柄（即便非本句柄）
            for (auto jt = c.components.begin(); jt != c.components.end();) {
                if (jt->second.first == eh && jt->second.second == type) {
                    jt = c.components.erase(jt);
                } else ++jt;
            }
            // 实体已无任何组件句柄也可能残留空映射 —— 统一 Evict 由调用方决定，这里
            // 再扫一次实体销毁场景（EntityDestroy 见下）。
        }
        lua_pushboolean(L, removed ? 1 : 0);
        return 1;
    }

    // ── Engine.component.get_number(comp, prop) -> number|nil ──
    int L_ComponentGetNumber(lua_State* L) {
        const auto   ch  = static_cast<uint32_t>(luaL_checkinteger(L, 1));
        const char*  prop = luaL_checkstring(L, 2);
        GameObject* obj; Component* comp;
        if (!ResolveComponent(ch, &obj, &comp) || !prop) { lua_pushnil(L); return 1; }
        const int i = FindPropIndex(comp, prop);
        if (i < 0) { lua_pushnil(L); return 1; }
        ComponentPropertyValue v; if (!comp->GetPropertyValue(static_cast<size_t>(i), &v)) { lua_pushnil(L); return 1; }
        if (v.type == ComponentValueType::Float) lua_pushnumber(L, v.floatValue);
        else if (v.type == ComponentValueType::Int) lua_pushinteger(L, v.intValue);
        else { lua_pushnil(L); }
        return 1;
    }

    // ── Engine.component.set_number(comp, prop, value) -> bool ──
    int L_ComponentSetNumber(lua_State* L) {
        const auto ch  = static_cast<uint32_t>(luaL_checkinteger(L, 1));
        const char* prop = luaL_checkstring(L, 2);
        const double val = luaL_checknumber(L, 3);
        GameObject* obj; Component* comp;
        if (!ResolveComponent(ch, &obj, &comp) || !prop) { lua_pushboolean(L, 0); return 1; }
        const int i = FindPropIndex(comp, prop);
        if (i < 0) { lua_pushboolean(L, 0); return 1; }
        ComponentPropertyDesc d; comp->GetPropertyDesc(static_cast<size_t>(i), &d);
        ComponentPropertyValue v;
        if (d.type == ComponentValueType::Float) { v.type = ComponentValueType::Float; v.floatValue = static_cast<float>(val); }
        else if (d.type == ComponentValueType::Int) { v.type = ComponentValueType::Int; v.intValue = static_cast<int32>(val); }
        else { lua_pushboolean(L, 0); return 1; }
        lua_pushboolean(L, comp->SetPropertyValue(static_cast<size_t>(i), v) ? 1 : 0);
        return 1;
    }

    // ── Engine.component.get_bool(comp, prop) -> bool|nil ──
    int L_ComponentGetBool(lua_State* L) {
        const auto ch  = static_cast<uint32_t>(luaL_checkinteger(L, 1));
        const char* prop = luaL_checkstring(L, 2);
        GameObject* obj; Component* comp;
        if (!ResolveComponent(ch, &obj, &comp) || !prop) { lua_pushnil(L); return 1; }
        const int i = FindPropIndex(comp, prop);
        if (i < 0) { lua_pushnil(L); return 1; }
        ComponentPropertyValue v;
        if (!comp->GetPropertyValue(static_cast<size_t>(i), &v)
            || v.type != ComponentValueType::Bool) { lua_pushnil(L); return 1; }
        lua_pushboolean(L, v.boolValue ? 1 : 0);
        return 1;
    }

    // ── Engine.component.set_bool(comp, prop, bool) -> bool ──
    int L_ComponentSetBool(lua_State* L) {
        const auto ch  = static_cast<uint32_t>(luaL_checkinteger(L, 1));
        const char* prop = luaL_checkstring(L, 2);
        const int b = lua_toboolean(L, 3);
        GameObject* obj; Component* comp;
        if (!ResolveComponent(ch, &obj, &comp) || !prop) { lua_pushboolean(L, 0); return 1; }
        const int i = FindPropIndex(comp, prop);
        if (i < 0) { lua_pushboolean(L, 0); return 1; }
        ComponentPropertyValue v; v.type = ComponentValueType::Bool; v.boolValue = (b != 0);
        lua_pushboolean(L, comp->SetPropertyValue(static_cast<size_t>(i), v) ? 1 : 0);
        return 1;
    }

    // ── Engine.component.get_string(comp, prop) -> string|nil ──
    int L_ComponentGetString(lua_State* L) {
        const auto ch  = static_cast<uint32_t>(luaL_checkinteger(L, 1));
        const char* prop = luaL_checkstring(L, 2);
        GameObject* obj; Component* comp;
        if (!ResolveComponent(ch, &obj, &comp) || !prop) { lua_pushnil(L); return 1; }
        const int i = FindPropIndex(comp, prop);
        if (i < 0) { lua_pushnil(L); return 1; }
        ComponentPropertyValue v;
        if (!comp->GetPropertyValue(static_cast<size_t>(i), &v)
            || v.type != ComponentValueType::String) { lua_pushnil(L); return 1; }
        lua_pushstring(L, v.stringValue.c_str());
        return 1;
    }

    // ── Engine.component.set_string(comp, prop, str) -> bool ──
    int L_ComponentSetString(lua_State* L) {
        const auto ch  = static_cast<uint32_t>(luaL_checkinteger(L, 1));
        const char* prop = luaL_checkstring(L, 2);
        const char* str = luaL_checkstring(L, 3);
        GameObject* obj; Component* comp;
        if (!ResolveComponent(ch, &obj, &comp) || !prop) { lua_pushboolean(L, 0); return 1; }
        const int i = FindPropIndex(comp, prop);
        if (i < 0) { lua_pushboolean(L, 0); return 1; }
        ComponentPropertyValue v; v.type = ComponentValueType::String; v.stringValue = str ? str : "";
        lua_pushboolean(L, comp->SetPropertyValue(static_cast<size_t>(i), v) ? 1 : 0);
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
    Ctx().components.clear();
    Ctx().nextHandle = 1;
    Ctx().nextComponentHandle = 1;
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
    EvictComponentHandles(handle);
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
    static const luaL_Reg componentFuncs[] = {
        { "add",          L_ComponentAdd },
        { "has",          L_ComponentHas },
        { "get",          L_ComponentGet },
        { "remove",       L_ComponentRemove },
        { "get_number",   L_ComponentGetNumber },
        { "set_number",   L_ComponentSetNumber },
        { "get_bool",     L_ComponentGetBool },
        { "set_bool",     L_ComponentSetBool },
        { "get_string",   L_ComponentGetString },
        { "set_string",   L_ComponentSetString },
        { nullptr, nullptr },
    };

    SetSubtable(L, "input",     inputFuncs);
    SetSubtable(L, "entity",    entityFuncs);
    SetSubtable(L, "transform", transformFuncs);
    SetSubtable(L, "component", componentFuncs);

    s_Log.Info("Gameplay domains registered: input/entity/transform/component");
}

} // namespace GameplayAPI
} // namespace Scripting
} // namespace Engine
