#pragma once

/**
 * @file LuaEngine.h
 * @brief Lua 脚本引擎 — C++ 与 Lua 的双向绑定系统
 *
 * 架构设计（参考 Unreal Python Scripting + Unity C# Scripting）：
 *
 *   1. 注册 C++ 类型到 Lua 环境（通过模板和元表实现）
 *   2. 从 Lua 脚本调用 C++ 函数
 *   3. 从 C++ 调用 Lua 脚本函数
 *   4. 支持热重载（脚本修改后自动重新加载）
 *   5. 沙箱执行（限制 Lua 脚本的 OS 调用）
 *
 * 使用示例：
 * @code
 *   LuaEngine engine;
 *   engine.Init();
 *
 *   // 注册 C++ 函数到 Lua
 *   engine.RegisterFunction("print", [](const std::string& msg) {
 *       Log::Info("[Lua] {}", msg);
 *   });
 *
 *   // 执行 Lua 脚本
 *   engine.RunScript("scripts/test.lua");
 *
 *   // 调用 Lua 函数
 *   auto result = engine.CallFunction<int>("add", 1, 2);
 * @endcode
 */

#include "Engine/Types.h"
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <memory>
#include <mutex>

// Lua 头文件（项目需包含 Lua 源码路径）
struct lua_State;

namespace Engine { namespace Scripting {

    // ============================================================
    // Lua 值类型
    // ============================================================
    enum class LuaValueType : uint8_t {
        Nil,
        Boolean,
        Number,
        String,
        Table,
        Function,
        UserData,
        LightUserData,
        Thread
    };

    // ============================================================
    // Lua 脚本错误
    // ============================================================
    struct LuaError {
        std::string message;
        std::string traceback;
        int         line = 0;
    };

    // ============================================================
    // 脚本文件监视（支持热重载）
    // ============================================================
    struct ScriptWatchEntry {
        std::string path;
        std::string name;
        int64_t     lastModified = 0;
        bool        autoReload  = true;
        std::function<void()> onReload;
    };

    // ============================================================
    // Lua 引擎
    // ============================================================
    class LuaEngine {
    public:
        LuaEngine();
        ~LuaEngine();

        LuaEngine(const LuaEngine&) = delete;
        LuaEngine& operator=(const LuaEngine&) = delete;

        // ── 生命周期 ──
        /** 初始化 Lua 虚拟机，注册基础 API */
        bool Init();

        /** 关闭引擎，释放所有资源 */
        void Shutdown();

        /** 每帧更新（检查脚本热重载） */
        void Update(float dt);

        bool IsInitialized() const { return m_State != nullptr; }

        // ── 脚本执行 ──
        /** 从文件加载并执行 Lua 脚本 */
        bool RunFile(const std::string& filePath);

        /** 从字符串加载并执行 Lua 脚本 */
        bool RunString(const std::string& luaCode);

        /** 执行脚本缓冲区（带文件名标记用于错误报告） */
        bool RunBuffer(const std::string& name, const char* buffer, size_t size);

        // ── Lua 函数调用 ──
        /** 调用无返回值的 Lua 函数 */
        bool CallFunctionVoid(const std::string& name);

        /** 调用返回整数的 Lua 函数 */
        int CallFunctionInt(const std::string& name, int arg1 = 0);

        /** 调用返回浮点数的 Lua 函数 */
        double CallFunctionDouble(const std::string& name, double arg1 = 0.0);

        /** 调用返回字符串的 Lua 函数 */
        std::string CallFunctionString(const std::string& name,
                                        const std::string& arg1 = "");

        // ── C++ 函数注册 ──
        /** 注册一个 C++ 函数到 Lua 全局环境 */
        void RegisterFunction(const std::string& name,
                              std::function<int(lua_State*)> func);

        /** 注册一个简单 C++ 函数（自动包装） */
        void RegisterSimpleFunction(const std::string& name,
                                     std::function<void()> func);

        // ── 全局变量 ──
        void SetGlobal(const std::string& name, int value);
        void SetGlobal(const std::string& name, double value);
        void SetGlobal(const std::string& name, const std::string& value);
        void SetGlobal(const std::string& name, bool value);

        int  GetGlobalInt(const std::string& name, int defaultVal = 0) const;
        double GetGlobalDouble(const std::string& name, double defaultVal = 0.0) const;
        std::string GetGlobalString(const std::string& name,
                                     const std::string& defaultVal = "") const;
        bool GetGlobalBool(const std::string& name, bool defaultVal = false) const;

        // ── 热重载 ──
        /** 注册一个脚本文件用于热重载监视 */
        void WatchScript(const std::string& filePath, bool autoReload = true);

        /** 手动触发指定脚本重载 */
        bool ReloadScript(const std::string& filePath);

        /** 设置文件修改检查间隔（秒，默认 1.0） */
        void SetWatchInterval(float interval) { m_WatchInterval = interval; }

        // ── 沙箱 ──
        /** 启用沙箱（禁止 OS 调用、文件写入等危险操作） */
        void EnableSandbox(bool enable) { m_SandboxEnabled = enable; }
        bool IsSandboxEnabled() const { return m_SandboxEnabled; }

        /** 限制脚本可访问的 API */
        void SetAllowedAPIs(const std::vector<std::string>& apis);

        // ── 调试 ──
        /** 获取栈顶错误信息 */
        LuaError GetLastError() const { return m_LastError; }

        /** 设置错误回调 */
        using ErrorCallback = std::function<void(const LuaError& error)>;
        void SetErrorCallback(ErrorCallback cb) { m_ErrorCallback = std::move(cb); }

        /** 获取底层 lua_State 指针（仅供高级使用） */
        lua_State* GetState() { return m_State; }

    private:
        // 私有辅助
        static int PanicHandler(lua_State* L);
        void SetupBaseAPI();
        void CheckStack(int slots) const;
        bool LoadLuaLibrary(const std::string& path, const std::string& name);

        // Lua 状态
        lua_State* m_State = nullptr;

        // 注册的 C++ 函数（保持引用避免 GC）
        std::unordered_map<std::string, std::function<int(lua_State*)>> m_Functions;

        // 热重载
        std::vector<ScriptWatchEntry> m_WatchedScripts;
        float m_WatchTimer = 0.0f;
        float m_WatchInterval = 1.0f;

        // 沙箱
        bool m_SandboxEnabled = false;

        // 错误
        mutable LuaError m_LastError;
        ErrorCallback m_ErrorCallback;
        mutable std::mutex m_Mutex;
    };

}} // namespace Engine::Scripting