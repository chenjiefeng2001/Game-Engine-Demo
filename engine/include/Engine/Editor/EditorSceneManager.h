#pragma once

/**
 * @file EditorSceneManager.h
 * @brief 编辑器场景管理器 — PIE (Play-In-Editor) 状态隔离
 *
 * 核心功能：
 *   1. Play/Stop 状态管理 — 点击 Play 时克隆运行时场景，Stop 时销毁
 *   2. 双向隔离 — 编辑器数据与运行时数据严格分离（深拷贝克隆）
 *   3. 面板切换回调 — 通知 EngineEditor 切换 Hierarchy/Inspector/Viewport 的数据源
 *
 * 架构：
 *   - m_EditorScene : 始终指向编辑器场景（永不修改）
 *   - m_RuntimeScene: Play 时从编辑器克隆，Stop 时销毁
 *   - GetScene()    : 编辑器模式返回 m_EditorScene，运行时返回 m_RuntimeScene.get()
 *
 * 使用方式：
 * @code
 *   EditorSceneManager sceneMgr;
 *   sceneMgr.SetEditorScene(&myScene);
 *
 *   // 主循环中
 *   sceneMgr.GetScene()->Update(dt);  // 自动返回正确场景
 * @endcode
 */

#include "Engine/Types.h"
#include <string>
#include <memory>
#include <functional>

namespace Engine {

    class Scene;

    enum class EditorState {
        Edit,   ///< 编辑器模式：物理/脚本暂停
        Play,   ///< 运行模式：全速物理/脚本
        Pause   ///< 暂停模式：冻结物理/脚本，保持场景状态
    };

    class EditorSceneManager {
    public:
        EditorSceneManager();
        ~EditorSceneManager();

        EditorSceneManager(const EditorSceneManager&) = delete;
        EditorSceneManager& operator=(const EditorSceneManager&) = delete;

        // ── 场景绑定 ──
        /** 设置编辑器场景（不可为空，生命周期由外部管理） */
        void SetEditorScene(Scene* scene) { m_EditorScene = scene; }
        Scene* GetEditorScene() const { return m_EditorScene; }

        /**
         * @brief 获取当前激活的场景
         * 编辑器模式 → 返回 m_EditorScene
         * 运行时模式 → 返回 m_RuntimeScene 的裸指针
         */
        Scene* GetScene() const {
            return (m_State != EditorState::Edit && m_RuntimeScene)
                ? m_RuntimeScene.get() : m_EditorScene;
        }

        // ── PIE 运行时场景访问 ──
        std::shared_ptr<Scene> GetRuntimeScene() const { return m_RuntimeScene; }

        // ── 状态管理 ──
        EditorState GetState() const { return m_State; }
        bool IsPlaying() const { return m_State == EditorState::Play; }
        bool IsPaused() const { return m_State == EditorState::Pause; }
        bool IsEditing() const { return m_State == EditorState::Edit; }

        /** 切换到 Play 模式（克隆编辑器场景 → 启动运行时） */
        void Play();

        /** 切换到 Stop 模式（销毁运行时 → 恢复编辑器状态） */
        void Stop();

        /** 暂停/继续切换 */
        void TogglePause();

        /** 单帧步进（Pause 状态下） */
        void StepFrame();

        /** 是否请求了单帧步进 */
        bool IsStepRequested() const { return m_StepRequested; }

        /** 消费单帧步进请求 */
        void ConsumeStepRequest() { m_StepRequested = false; }

        // ── ImGui 工具栏控件 ──
        bool DrawPlayToolbar();

        // ── 场景持久化 ──
        bool SaveSceneToFile(const std::string& filePath);
        bool LoadSceneFromFile(const std::string& filePath);

        // ── 状态通知回调 ──
        using StateChangeCallback = std::function<void(EditorState newState)>;
        void SetStateChangeCallback(StateChangeCallback cb) { m_StateCallback = std::move(cb); }

        // ── 面板切换回调（Play/Stop 时通知 EngineEditor 切换面板数据源） ──
        using PanelSwitchCallback = std::function<void(Scene* activeScene)>;
        void SetPanelSwitchCallback(PanelSwitchCallback cb) { m_PanelSwitchCallback = std::move(cb); }

    private:
        EditorState m_State = EditorState::Edit;

        // 双向隔离：编辑器场景 vs 运行时场景
        Scene* m_EditorScene = nullptr;                   ///< 不可变编辑器场景（生命周期外部）
        std::shared_ptr<Scene> m_RuntimeScene;             ///< 运行时场景（Play 时从编辑器克隆）

        // 单帧步进
        bool m_StepRequested = false;

        StateChangeCallback m_StateCallback;
        PanelSwitchCallback m_PanelSwitchCallback;         ///< 面板数据源切换回调
    };

} // namespace Engine
