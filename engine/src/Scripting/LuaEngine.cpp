#include "Engine/Scripting/LuaEngine.h"
#include "Engine/Core/Log.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>

// ============================================================
// Lua C API 声明
// 项目需要链接 Lua 库。
// 要启用 Lua 脚本支持，请将 Lua 源码放入 third_party/lua/
// 并在 CMakeLists.txt 中添加依赖。
// ============================================================
extern "C" {
    #include <lua.h>
    #include <lauxlib.h>
    #include <lualib.h>
}

namespace Engine { namespace Scripting {

    // ============================================================
    // 内部辅助
    // ============================================================
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

    // ============================================================
    // LuaEngine 实现
    // ============================================================

    LuaEngine::LuaEngine() = default;

    LuaEngine::~LuaEngine() {
        Shutdown();
    }

    bool LuaEngine::Init() {
        if (m_State) {
            s_Log.Warn("Lua engine already initialized");
            return true;
        }

        // 创建 Lua 状态机
        m_State = luaL_newstate();
        if (!m_State) {
            s_Log.Error("Failed to create Lua state");
            return false;
        }

        // 设置 panic 处理器
        lua_atpanic(m_State, PanicHandler);

        // 打开标准库
        luaL_openlibs(m_State);

        // 注册基础 C++ API
        SetupBaseAPI();

        s_Log.Info("Lua engine initialized");
        return true;
    }

    void LuaEngine::Shutdown() {
        if (m_State) {
            lua_close(m_State);
            m_State = nullptr;
        }
        m_Functions.clear();
        m_WatchedScripts.clear();
        s_Log.Info("Lua engine shut down");
    }

    void LuaEngine::Update(float dt) {
        if (!m_State) return;

        // 热重载检查
        m_WatchTimer += dt;
        if (m_WatchTimer >= m_WatchInterval) {
            m_WatchTimer = 0.0f;

            for (auto& watch : m_WatchedScripts) {
                if (!watch.autoReload) continue;

                int64_t newTime = GetFileModifiedTime(watch.path);
                if (newTime > watch.lastModified) {
                    watch.lastModified = newTime;
                    s_Log.Info("Script changed, reloading: {}", watch.path);

                    // 备份当前全局变量
                    // TODO: save/restore globals for hot reload

                    if (RunFile(watch.path)) {
                        if (watch.onReload) watch.onReload();
                        s_Log.Info("Script reloaded: {}", watch.path);
                    } else {
                        s_Log.Error("Failed to reload script: {}", watch.path);
                    }
                }
            }
        }
    }

    // ── 脚本执行 ──

    bool LuaEngine::RunFile(const std::string& filePath) {
        if (!m_State) {
            m_LastError.message = "Lua engine not initialized";
            return false;
        }

        if (!std::filesystem::exists(filePath)) {
            m_LastError.message = "File not found: " + filePath;
            s_Log.Error(m_LastError.message);
            return false;
        }

        int result = luaL_loadfile(m_State, filePath.c_str());
        if (result != LUA_OK) {
            m_LastError.message = lua_tostring(m_State, -1);
            m_LastError.traceback = m_LastError.message;
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
        if (result != LUA_OK) {
            m_LastError.message = lua_tostring(m_State, -1);
            lua_pop(m_State, 1);
            return false;
        }

        result = lua_pcall(m_State, 0, LUA_MULTRET, 0);
        if (result != LUA_OK) {
            m_LastError.message = lua_tostring(m_State, -1);
            lua_pop(m_State, 1);
            return false;
        }

        return true;
    }

    // ── Lua 函数调用 ──

    bool LuaEngine::CallFunctionVoid(const std::string& name) {
        lua_getglobal(m_State, name.c_str());
        if (!lua_isfunction(m_State, -1)) {
            lua_pop(m_State, 1);
            return false;
        }

        if (lua_pcall(m_State, 0, 0, 0) != LUA_OK) {
            lua_pop(m_State, 1);
            return false;
        }
        return true;
    }

    int LuaEngine::CallFunctionInt(const std::string& name, int arg1) {
        lua_getglobal(m_State, name.c_str());
        if (!lua_isfunction(m_State, -1)) {
            lua_pop(m_State, 1);
            return 0;
        }

        lua_pushinteger(m_State, arg1);
        if (lua_pcall(m_State, 1, 1, 0) != LUA_OK) {
            lua_pop(m_State, 1);
            return 0;
        }

        int result = static_cast<int>(lua_tointeger(m_State, -1));
        lua_pop(m_State, 1);
        return result;
    }

    double LuaEngine::CallFunctionDouble(const std::string& name, double arg1) {
        lua_getglobal(m_State, name.c_str());
        if (!lua_isfunction(m_State, -1)) {
            lua_pop(m_State, 1);
            return 0.0;
        }

        lua_pushnumber(m_State, arg1);
        if (lua_pcall(m_State, 1, 1, 0) != LUA_OK) {
            lua_pop(m_State, 1);
            return 0.0;
        }

        double result = lua_tonumber(m_State, -1);
        lua_pop(m_State, 1);
        return result;
    }

    std::string LuaEngine::CallFunctionString(const std::string& name,
                                               const std::string& arg1) {
        lua_getglobal(m_State, name.c_str());
        if (!lua_isfunction(m_State, -1)) {
            lua_pop(m_State, 1);
            return "";
        }

        lua_pushstring(m_State, arg1.c_str());
        if (lua_pcall(m_State, 1, 1, 0) != LUA_OK) {
            lua_pop(m_State, 1);
            return "";
        }

        const char* result = lua_tostring(m_State, -1);
        std::string str(result ? result : "");
        lua_pop(m_State, 1);
        return str;
    }

    // ── C++ 函数注册 ──

    void LuaEngine::RegisterFunction(const std::string& name,
                                      std::function<int(lua_State*)> func) {
        if (!m_State) return;

        // 转换为 C 函数指针并注册
        auto wrapper = [](lua_State* L) -> int {
            // 通过闭包环境获取实际的函数对象
            auto* funcPtr = static_cast<std::function<int(lua_State*)>*>(
                lua_touserdata(L, lua_upvalueindex(1)));
            if (funcPtr) return (*funcPtr)(L);
            return 0;
        };

        // 存储函数对象（防止被销毁）
        auto storedFunc = std::make_shared<std::function<int(lua_State*)>>(std::move(func));
        m_Functions[name] = *storedFunc;

        // 推入 C 函数和 upvalue
        lua_pushlightuserdata(m_State, storedFunc.get());
        lua_pushcclosure(m_State, wrapper, 1);
        lua_setglobal(m_State, name.c_str());
    }

    void LuaEngine::RegisterSimpleFunction(const std::string& name,
                                            std::function<void()> func) {
        RegisterFunction(name, [func = std::move(func)](lua_State*) -> int {
            func();
            return 0;
        });
    }

    // ── 全局变量 ──

    void LuaEngine::SetGlobal(const std::string& name, int value) {
        lua_pushinteger(m_State, value);
        lua_setglobal(m_State, name.c_str());
    }

    void LuaEngine::SetGlobal(const std::string& name, double value) {
        lua_pushnumber(m_State, value);
        lua_setglobal(m_State, name.c_str());
    }

    void LuaEngine::SetGlobal(const std::string& name, const std::string& value) {
        lua_pushstring(m_State, value.c_str());
        lua_setglobal(m_State, name.c_str());
    }

    void LuaEngine::SetGlobal(const std::string& name, bool value) {
        lua_pushboolean(m_State, value);
        lua_setglobal(m_State, name.c_str());
    }

    int LuaEngine::GetGlobalInt(const std::string& name, int defaultVal) const {
        lua_getglobal(m_State, name.c_str());
        int val = static_cast<int>(lua_tointeger(m_State, -1));
        lua_pop(m_State, 1);
        return lua_isnil(m_State, -1) ? defaultVal : val;
    }

    double LuaEngine::GetGlobalDouble(const std::string& name, double defaultVal) const {
        lua_getglobal(m_State, name.c_str());
        double val = lua_tonumber(m_State, -1);
        lua_pop(m_State, 1);
        return lua_isnil(m_State, -1) ? defaultVal : val;
    }

    std::string LuaEngine::GetGlobalString(const std::string& name,
                                            const std::string& defaultVal) const {
        lua_getglobal(m_State, name.c_str());
        const char* str = lua_tostring(m_State, -1);
        lua_pop(m_State, 1);
        return str ? std::string(str) : defaultVal;
    }

    bool LuaEngine::GetGlobalBool(const std::string& name, bool defaultVal) const {
        lua_getglobal(m_State, name.c_str());
        bool val = lua_toboolean(m_State, -1);
        lua_pop(m_State, 1);
        return lua_isnil(m_State, -1) ? defaultVal : val;
    }

    // ── 热重载 ──

    void LuaEngine::WatchScript(const std::string& filePath, bool autoReload) {
        ScriptWatchEntry entry;
        entry.path = filePath;
        entry.name = std::filesystem::path(filePath).filename().string();
        entry.lastModified = GetFileModifiedTime(filePath);
        entry.autoReload = autoReload;

        // 检查是否已存在
        for (auto& w : m_WatchedScripts) {
            if (w.path == filePath) {
                w.autoReload = autoReload;
                return;
            }
        }

        m_WatchedScripts.push_back(entry);
        s_Log.Info("Watching script: {}", filePath);
    }

    bool LuaEngine::ReloadScript(const std::string& filePath) {
        return RunFile(filePath);
    }

    void LuaEngine::SetAllowedAPIs(const std::vector<std::string>& apis) {
        // TODO: 实现 API 白名单过滤
        (void)apis;
    }

    // ── 内部方法 ──

    int LuaEngine::PanicHandler(lua_State* L) {
        const char* msg = lua_tostring(L, -1);
        s_Log.Error("Lua panic: {}", msg ? msg : "(unknown error)");
        return 0;
    }

    void LuaEngine::SetupBaseAPI() {
        // 注册 print 和 error 函数
        lua_register(m_State, "print", LuaPrint);
        lua_register(m_State, "error", LuaError);

        // 设置引擎版本
        SetGlobal("ENGINE_VERSION", 1.0);

        // 注册通用工具函数
        RunString(R"(
            function clamp(val, min, max)
                if val < min then return min end
                if val > max then return max end
                return val
            end

            function lerp(a, b, t)
                return a + (b - a) * t
            end
        )");
    }

    void LuaEngine::CheckStack(int slots) const {
        if (m_State) {
            lua_checkstack(m_State, slots);
        }
    }

    bool LuaEngine::LoadLuaLibrary(const std::string& path, const std::string& name) {
        return RunFile(path + "/" + name + ".lua");
    }

}} // namespace Engine::Scripting