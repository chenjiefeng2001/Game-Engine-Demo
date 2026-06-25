#pragma once

/**
 * @file SceneContext.h
 * @brief 场景上下文 — 场景切换时的数据传输对象（DTO）
 *
 * 设计目的：
 *   告别全局变量传参。每个场景切换时携带一个 SceneContext 对象，
 *   新场景在初始化时通过 ContextAccess 获取，实现数据解耦。
 *
 * 使用示例：
 * @code
 *   // 发起切换
 *   auto ctx = SceneContext::Create("BattleScene")
 *       .WithArg("difficulty", 3)
 *       .WithArg("playerHealth", 100.0f)
 *       .WithArg("enemyList", std::vector<int>{1, 2, 3});
 *   SceneManager::SwitchScene("BattleScene", std::move(ctx));
 *
 *   // 目标场景内获取
 *   if (auto* ctx = SceneManager::GetCurrentContext()) {
 *       int difficulty = ctx->GetArg<int>("difficulty");
 *   }
 * @endcode
 */

#include "Engine/Types.h"
#include <string>
#include <any>
#include <unordered_map>
#include <memory>
#include <utility>

namespace Engine {

    // ============================================================
    // 场景上下文 — 线程安全只读上下文，跨场景传递数据
    // ============================================================
    class SceneContext {
    public:
        SceneContext() = default;

        // 禁止拷贝，允许移动
        SceneContext(const SceneContext&) = delete;
        SceneContext& operator=(const SceneContext&) = delete;
        SceneContext(SceneContext&&) noexcept = default;
        SceneContext& operator=(SceneContext&&) noexcept = default;

        /** 创建一个带场景名称的上下文（链式构造） */
        static SceneContext Create(std::string sceneName) {
            SceneContext ctx;
            ctx.m_SceneName = std::move(sceneName);
            return ctx;
        }

        // ── 参数存取 ──

        /** 添加一个具名参数（支持任意类型） */
        template<typename T>
        SceneContext& WithArg(const std::string& key, T&& value) {
            m_Args[key] = std::any(std::forward<T>(value));
            return *this;
        }

        /** 获取参数（若类型不匹配或不存在，返回默认值） */
        template<typename T>
        T GetArg(const std::string& key, const T& defaultValue = T{}) const {
            auto it = m_Args.find(key);
            if (it != m_Args.end()) {
                try {
                    return std::any_cast<T>(it->second);
                } catch (const std::bad_any_cast&) {
                    return defaultValue;
                }
            }
            return defaultValue;
        }

        /** 检查是否存在指定键 */
        bool HasArg(const std::string& key) const {
            return m_Args.find(key) != m_Args.end();
        }

        /** 获取参数数量 */
        size_t GetArgCount() const noexcept { return m_Args.size(); }

        // ── 场景标识 ──

        const std::string& GetSceneName() const noexcept { return m_SceneName; }
        void SetSceneName(const std::string& name) { m_SceneName = name; }

        // ── 场景行为控制 ──

        /** 是否启用分帧激活（默认 true） */
        bool IsDeferredActivation() const noexcept { return m_DeferredActivation; }
        void SetDeferredActivation(bool deferred) noexcept { m_DeferredActivation = deferred; }

        /** 激活延迟帧数（0 = 立即，>=1 = 等待N帧） */
        uint32 GetActivationDelayFrames() const noexcept { return m_ActivationDelayFrames; }
        void SetActivationDelayFrames(uint32 frames) noexcept { m_ActivationDelayFrames = frames; }

        /** 是否显示加载界面 */
        bool ShowLoadingScreen() const noexcept { return m_ShowLoadingScreen; }
        void SetShowLoadingScreen(bool show) noexcept { m_ShowLoadingScreen = show; }

    private:
        std::string m_SceneName;
        std::unordered_map<std::string, std::any> m_Args;

        bool    m_DeferredActivation    = true;
        uint32  m_ActivationDelayFrames = 2;
        bool    m_ShowLoadingScreen     = true;
    };

} // namespace Engine