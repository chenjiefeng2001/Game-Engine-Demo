#pragma once

/**
 * @file ScriptInstance.h
 * @brief 脚本实例 — 生命周期 + 持久状态 + 热重载（Scripting MVP · S3/S4）
 *
 * 生命周期契约（S3）：
 *   Initialize -> OnCreate -> ( OnUpdate(dt) | OnFixedUpdate(dt) )* -> OnDestroy -> Shutdown
 *   Reload 可在任意时点触发：重新执行脚本代码，保留 _PERSIST 表（S4）
 *
 * 隔离契约（S5，由内部 LuaEngine 保证）：
 *   - 每实例独立 VM（脚本间零共享）
 *   - 沙箱开启（os.execute / io / require 等被剥离）
 *   - 指令预算：任何生命周期调用超预算即中止，引擎不受影响
 *
 * 脚本约定（可选实现任意回调）：
 *   function OnCreate() end
 *   function OnUpdate(dt) end
 *   function OnFixedUpdate(fixedDt) end
 *   function OnDestroy() end
 *
 * 状态约定（S4）：
 *   _PERSIST.*     —— 跨 Reload 保留（Reload 前后自动搬运）
 *   其余全局/局部  —— Reload 后随新代码重建
 */

#include <atomic>
#include <memory>
#include <string>

namespace Engine { namespace Scripting {

    class LuaEngine;

    class ScriptInstance {
    public:
        struct Config {
            uint64_t instructionBudget = 20000000ull;  ///< 每次回调的 VM 指令上限
            bool     sandbox           = true;
        };

        ScriptInstance() = default;
        ~ScriptInstance();

        ScriptInstance(const ScriptInstance&) = delete;
        ScriptInstance& operator=(const ScriptInstance&) = delete;

        /// 创建独立 VM、注册 API 边界、加载并执行脚本文件
        // 注意：此处**不能**写成 `const Config& config = {}`。
        //
        // Config 是本类的**嵌套**类型且带默认成员初始化器（NSDMI）。按 [class.mem]，
        // NSDMI 在外层类定义体内、成员函数之外尚不可见，因此外层类成员函数声明中
        // 的 `= {}` 无法读取 instructionBudget / sandbox 的默认值 —— 这是**标准
        // 规定的行为**，不是 GCC 13 的缺陷。clang 的诊断可证：
        //   "default member initializer for 'b' needed within definition of
        //    enclosing class 'Outer' outside of member functions"
        //
        // 契约保持：保留原两参签名（const Config&），另加一个单参重载转发到
        // Config{}，使 `Initialize(path)` 与 `Initialize(path, Config{...})`
        // 两种调用形态都不变。
        bool Initialize(const std::string& scriptPath, const Config& config);

        /// 单参重载：等价于 `Initialize(scriptPath, Config{})`。
        bool Initialize(const std::string& scriptPath) { return Initialize(scriptPath, Config{}); }

        /// 触发 OnDestroy（若脚本定义）并销毁 VM
        void Shutdown();

        // ── S3 生命周期（脚本未定义对应函数时为安全空操作）──
        bool OnCreate();
        bool OnUpdate(float dt);
        bool OnFixedUpdate(float fixedDt);
        void OnDestroy();

        // ── S4 热重载（保留 _PERSIST 表）──
        bool Reload();

        // ── Console 能力：运行期执行任意语句（错误经 GetLastError 取回）──
        bool Execute(const std::string& luaCode);

         bool IsValid() const;
         bool HasOnUpdate() const { return m_HasOnUpdate; }
         uint32_t GetOnCreateCount() const { return m_OnCreateCount.load(); }
         uint32_t GetOnDestroyCount() const { return m_OnDestroyCount.load(); }

         const std::string& GetPath() const { return m_Path; }
         const std::string& GetLastError() const { return m_LastError; }
         LuaEngine* GetEngine() const { return m_Lua.get(); }   ///< 供扩展 API 注册

    private:
        std::unique_ptr<LuaEngine> m_Lua;
        std::string m_Path;
        std::string m_LastError;
        bool m_Loaded      = false;
        bool m_HasOnCreate = false;
        bool m_HasOnUpdate = false;
         bool m_HasOnFixed  = false;
         bool m_HasOnDestroy= false;
         std::atomic<uint32_t> m_OnCreateCount{0};
         std::atomic<uint32_t> m_OnDestroyCount{0};
         std::atomic<bool> m_OnDestroyCalled{false};
    };

}} // namespace Engine::Scripting
