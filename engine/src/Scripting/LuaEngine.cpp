#include "Engine/Scripting/LuaEngine.h"
#include "Engine/Core/Log.h"

// ============================================================
// Lua 脚本引擎实现
//
// 要启用 Lua 支持：
//   1. 将 Lua 源码放入 third_party/lua/
//   2. 取消 engine/CMakeLists.txt 中 Lua 相关注释
//   3. 重新配置 CMake
//
// 启用后会自动定义 ENGINE_ENABLE_LUA 宏
// ============================================================

#ifdef ENGINE_ENABLE_LUA

extern "C" {
    #include <lua.h>
    #include <lauxlib.h>
    #include <lualib.h>
}

#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>

namespace Engine { namespace Scripting {

    namespace {
        Logger s_Log("LuaEngine");

        int LuaPrint(lua_State* L) {
            int nargs = lua_gettop(L);
            std::string msg;
            for (int i = 1; i <= nargs; ++i) {
                if (i > 1) msg += "\t";
                msg += lua_tostring(L, i);
            }
            s_Log.Info("[Lua] {}", msg);
            return 0;
        }

        int LuaError(lua_State* L) {
            std::string msg = lua_tostring(L, 1);
            s_Log.Error("[Lua] {}", msg);
            return 0;
        }

        int64_t GetFileModifiedTime(const std::string& path) {
            std::error_code ec;
            auto ftime = std::filesystem::last_write_time(path, ec);
            if (ec) return 0;
            return std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::time_point_cast<std::chrono::seconds>(
                    std::chrono::file_clock::to_utc(ftime)).time_since_epoch()).count();
        }
    }

    LuaEngine::LuaEngine() = default;
    LuaEngine::~LuaEngine() { Shutdown(); }

    bool LuaEngine::Init() {
        if (m_State) { s_Log.Warn("Lua engine already initialized"); return true; }
        m_State = luaL_newstate();
        if (!m_State) { s_Log.Error("Failed to create Lua state"); return false; }
        lua_atpanic(m_State, PanicHandler);
        luaL_openlibs(m_State);
        SetupBaseAPI();
        s_Log.Info("Lua engine initialized");
        return true;
    }

    void LuaEngine::Shutdown() {
        if (m_State) { lua_close(m_State); m_State = nullptr; }
        m_Functions.clear();
        m_WatchedScripts.clear();
    }

    void LuaEngine::Update(float dt) {
        if (!m_State) return;
        m_WatchTimer += dt;
        if (m_WatchTimer >= m_WatchInterval) {
            m_WatchTimer = 0.0f;
            for (auto& watch : m_WatchedScripts) {
                if (!watch.autoReload) continue;
                int64_t newTime = GetFileModifiedTime(watch.path);
                if (newTime > watch.lastModified) {
                    watch.lastModified = newTime;
                    s_Log.Info("Script changed, reloading: {}", watch.path);
                    if (RunFile(watch.path)) {
                        if (watch.onReload) watch.onReload();
                    } else {
                        s_Log.Error("Failed to reload script: {}", watch.path);
                    }
                }
            }
        }
    }

    bool LuaEngine::RunFile(const std::string& filePath) {
        if (!m_State) { m_LastError.message = "Lua engine not initialized"; return false; }
        if (!std::filesystem::exists(filePath)) {
            m_LastError.message = "File not found: " + filePath;
            s_Log.Error(m_LastError.message); return false;
        }
        int result = luaL_loadfile(m_State, filePath.c_str());
        if (result != LUA_OK) {
            m_LastError.message = lua_tostring(m_State, -1);
            lua_pop(m_State, 1);
            if (m_ErrorCallback) m_ErrorCallback(m_LastError);
            return false;
        }
        result = lua_pcall(m_State, 0, LUA_MULTRET, 0);
        if (result != LUA_OK) {
            m_LastError.message = lua_tostring(m_State, -1);
            lua_pop(m_State, 1);
            if (m_ErrorCallback) m_ErrorCallback(m_LastError);
            return false;
        }
        return true;
    }

    bool LuaEngine::RunString(const std::string& luaCode) {
        return RunBuffer("(string)", luaCode.c_str(), luaCode.size());
    }

    bool LuaEngine::RunBuffer(const std::string& name, const char* buffer, size_t size) {
        if (!m_State) return false;
        int result = luaL_loadbuffer(m_State, buffer, size, name.c_str());
        if (result != LUA_OK) { m_LastError.message = lua_tostring(m_State, -1); lua_pop(m_State, 1); return false; }
        result = lua_pcall(m_State, 0, LUA_MULTRET, 0);
        if (result != LUA_OK) { m_LastError.message = lua_tostring(m_State, -1); lua_pop(m_State, 1); return false; }
        return true;
    }

    bool LuaEngine::CallFunctionVoid(const std::string& name) {
        lua_getglobal(m_State, name.c_str());
        if (!lua_isfunction(m_State, -1)) { lua_pop(m_State, 1); return false; }
        if (lua_pcall(m_State, 0, 0, 0) != LUA_OK) { lua_pop(m_State, 1); return false; }
        return true;
    }

    int LuaEngine::CallFunctionInt(const std::string& name, int arg1) {
        lua_getglobal(m_State, name.c_str());
        if (!lua_isfunction(m_State, -1)) { lua_pop(m_State, 1); return 0; }
        lua_pushinteger(m_State, arg1);
        if (lua_pcall(m_State, 1, 1, 0) != LUA_OK) { lua_pop(m_State, 1); return 0; }
        int result = (int)lua_tointeger(m_State, -1); lua_pop(m_State, 1); return result;
    }

    double LuaEngine::CallFunctionDouble(const std::string& name, double arg1) {
        lua_getglobal(m_State, name.c_str());
        if (!lua_isfunction(m_State, -1)) { lua_pop(m_State, 1); return 0.0; }
        lua_pushnumber(m_State, arg1);
        if (lua_pcall(m_State, 1, 1, 0) != LUA_OK) { lua_pop(m_State, 1); return 0.0; }
        double result = lua_tonumber(m_State, -1); lua_pop(m_State, 1); return result;
    }

    std::string LuaEngine::CallFunctionString(const std::string& name, const std::string& arg1) {
        lua_getglobal(m_State, name.c_str());
        if (!lua_isfunction(m_State, -1)) { lua_pop(m_State, 1); return ""; }
        lua_pushstring(m_State, arg1.c_str());
        if (lua_pcall(m_State, 1, 1, 0) != LUA_OK) { lua_pop(m_State, 1); return ""; }
        const char* r = lua_tostring(m_State, -1);
        std::string s(r ? r : ""); lua_pop(m_State, 1); return s;
    }

    void LuaEngine::RegisterFunction(const std::string& name, std::function<int(lua_State*)> func) {
        if (!m_State) return;
        auto storedFunc = std::make_shared<std::function<int(lua_State*)>>(std::move(func));
        m_Functions[name] = *storedFunc;
        lua_pushlightuserdata(m_State, storedFunc.get());
        lua_pushcclosure(m_State, [](lua_State* L) -> int {
            auto* fp = static_cast<std::function<int(lua_State*)>*>(lua_touserdata(L, lua_upvalueindex(1)));
            return fp ? (*fp)(L) : 0;
        }, 1);
        lua_setglobal(m_State, name.c_str());
    }

    void LuaEngine::RegisterSimpleFunction(const std::string& name, std::function<void()> func) {
        RegisterFunction(name, [f = std::move(func)](lua_State*) -> int { f(); return 0; });
    }

    void LuaEngine::SetGlobal(const std::string& name, int v) { lua_pushinteger(m_State, v); lua_setglobal(m_State, name.c_str()); }
    void LuaEngine::SetGlobal(const std::string& name, double v) { lua_pushnumber(m_State, v); lua_setglobal(m_State, name.c_str()); }
    void LuaEngine::SetGlobal(const std::string& name, const std::string& v) { lua_pushstring(m_State, v.c_str()); lua_setglobal(m_State, name.c_str()); }
    void LuaEngine::SetGlobal(const std::string& name, bool v) { lua_pushboolean(m_State, v); lua_setglobal(m_State, name.c_str()); }

    int LuaEngine::GetGlobalInt(const std::string& name, int d) const {
        lua_getglobal(m_State, name.c_str());
        bool nil = lua_isnil(m_State, -1); int v = (int)lua_tointeger(m_State, -1); lua_pop(m_State, 1); return nil ? d : v;
    }
    double LuaEngine::GetGlobalDouble(const std::string& name, double d) const {
        lua_getglobal(m_State, name.c_str());
        bool nil = lua_isnil(m_State, -1); double v = lua_tonumber(m_State, -1); lua_pop(m_State, 1); return nil ? d : v;
    }
    std::string LuaEngine::GetGlobalString(const std::string& name, const std::string& d) const {
        lua_getglobal(m_State, name.c_str());
        const char* s = lua_tostring(m_State, -1); lua_pop(m_State, 1); return s ? std::string(s) : d;
    }
    bool LuaEngine::GetGlobalBool(const std::string& name, bool d) const {
        lua_getglobal(m_State, name.c_str());
        bool nil = lua_isnil(m_State, -1); bool v = lua_toboolean(m_State, -1); lua_pop(m_State, 1); return nil ? d : v;
    }

    void LuaEngine::WatchScript(const std::string& filePath, bool autoReload) {
        ScriptWatchEntry e;
        e.path = filePath;
        e.name = std::filesystem::path(filePath).filename().string();
        e.lastModified = GetFileModifiedTime(filePath);
        e.autoReload = autoReload;
        for (auto& w : m_WatchedScripts) { if (w.path == filePath) { w.autoReload = autoReload; return; } }
        m_WatchedScripts.push_back(e);
        s_Log.Info("Watching script: {}", filePath);
    }

    bool LuaEngine::ReloadScript(const std::string& filePath) { return RunFile(filePath); }
    void LuaEngine::SetAllowedAPIs(const std::vector<std::string>& apis) { (void)apis; }

    int LuaEngine::PanicHandler(lua_State* L) {
        s_Log.Error("Lua panic: {}", lua_tostring(L, -1) ? lua_tostring(L, -1) : "(unknown)");
        return 0;
    }

    void LuaEngine::SetupBaseAPI() {
        lua_register(m_State, "print", LuaPrint);
        lua_register(m_State, "error", LuaError);
        SetGlobal("ENGINE_VERSION", 1.0);
        RunString(R"(
            function clamp(v, mn, mx)
                if v < mn then return mn end
                if v > mx then return mx end
                return v
            end
            function lerp(a, b, t) return a + (b - a) * t end
        )");
    }

    void LuaEngine::CheckStack(int slots) const { if (m_State) lua_checkstack(m_State, slots); }
    bool LuaEngine::LoadLuaLibrary(const std::string& p, const std::string& n) { return RunFile(p + "/" + n + ".lua"); }

}} // namespace Engine::Scripting

// ============================================================
// 当 Lua 未启用时的桩实现
// ============================================================
#else

namespace Engine { namespace Scripting {

    namespace { Logger s_Log("LuaEngine"); }

    LuaEngine::LuaEngine() = default;
    LuaEngine::~LuaEngine() { Shutdown(); }

    bool LuaEngine::Init() {
        s_Log.Warn("Lua support not enabled. Set ENGINE_ENABLE_LUA to enable.");
        return false;
    }
    void LuaEngine::Shutdown() {}
    void LuaEngine::Update(float) {}
    bool LuaEngine::RunFile(const std::string&) { return false; }
    bool LuaEngine::RunString(const std::string&) { return false; }
    bool LuaEngine::RunBuffer(const std::string&, const char*, size_t) { return false; }
    bool LuaEngine::CallFunctionVoid(const std::string&) { return false; }
    int LuaEngine::CallFunctionInt(const std::string&, int) { return 0; }
    double LuaEngine::CallFunctionDouble(const std::string&, double) { return 0.0; }
    std::string LuaEngine::CallFunctionString(const std::string&, const std::string&) { return ""; }
    void LuaEngine::RegisterFunction(const std::string&, std::function<int(lua_State*)>) {}
    void LuaEngine::RegisterSimpleFunction(const std::string&, std::function<void()>) {}
    void LuaEngine::SetGlobal(const std::string&, int) {}
    void LuaEngine::SetGlobal(const std::string&, double) {}
    void LuaEngine::SetGlobal(const std::string&, const std::string&) {}
    void LuaEngine::SetGlobal(const std::string&, bool) {}
    int LuaEngine::GetGlobalInt(const std::string&, int d) const { return d; }
    double LuaEngine::GetGlobalDouble(const std::string&, double d) const { return d; }
    std::string LuaEngine::GetGlobalString(const std::string&, const std::string& d) const { return d; }
    bool LuaEngine::GetGlobalBool(const std::string&, bool d) const { return d; }
    void LuaEngine::WatchScript(const std::string&, bool) {}
    bool LuaEngine::ReloadScript(const std::string&) { return false; }
    void LuaEngine::SetAllowedAPIs(const std::vector<std::string>&) {}
    int LuaEngine::PanicHandler(lua_State*) { return 0; }
    void LuaEngine::SetupBaseAPI() {}
    void LuaEngine::CheckStack(int) const {}
    bool LuaEngine::LoadLuaLibrary(const std::string&, const std::string&) { return false; }

}} // namespace Engine::Scripting

#endif // ENGINE_ENABLE_LUA