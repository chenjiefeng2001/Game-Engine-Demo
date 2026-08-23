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
        bool Initialize(const std::string& scriptPath, const Config& config = {});

        /// 触发 OnDestroy（若脚本定义）并销毁 VM
        void Shutdown();

        // ── S3 生命周期（脚本未定义对应函数时为安全空操作）──
        void OnCreate();
        void OnUpdate(float dt);
        void OnFixedUpdate(float fixedDt);
        void OnDestroy();

        // ── S4 热重载（保留 _PERSIST 表）──
        bool Reload();

        // ── Console 能力：运行期执行任意语句（错误经 GetLastError 取回）──
        bool Execute(const std::string& luaCode);

        bool IsValid() const;
        bool HasOnUpdate() const { return m_HasOnUpdate; }

        const std::string& GetPath() const { return m_Path; }
        LuaEngine* GetEngine() { return m_Lua.get(); }   ///< 供扩展 API 注册

    private:
        std::unique_ptr<LuaEngine> m_Lua;
        std::string m_Path;
        bool m_Loaded      = false;
        bool m_HasOnCreate = false;
        bool m_HasOnUpdate = false;
        bool m_HasOnFixed  = false;
        bool m_HasOnDestroy= false;
    };

}} // namespace Engine::Scripting
