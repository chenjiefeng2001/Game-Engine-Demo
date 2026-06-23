#pragma once

/**
 * @file EditorSceneManager.h
 * @brief 编辑器场景管理器 — 场景快照 + Play/Stop 模式切换
 *
 * 核心功能：
 *   1. Play/Stop 状态管理 — 点击 Play 时创建场景快照，Stop 时恢复
 *   2. 系统挂起 — Edit 模式下屏蔽物理/脚本更新
 *   3. 场景序列化/反序列化到内存缓冲区
 *
 * 使用方式：
 * @code
 *   EditorSceneManager sceneMgr;
 *   sceneMgr.SetScene(&myScene);
 *
 *   // 工具栏按钮
 *   if (sceneMgr.OnPlayButton()) { ... }
 *   if (sceneMgr.OnStopButton()) { ... }
 *
 *   // 主循环中
 *   if (sceneMgr.IsPlaying()) {
 *       sceneMgr.GetScene()->Update(dt);
 *   }
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
        void SetScene(Scene* scene) { m_Scene = scene; }
        Scene* GetScene() const { return m_Scene; }

        // ── 状态管理 ──
        EditorState GetState() const { return m_State; }
        bool IsPlaying() const { return m_State == EditorState::Play; }
        bool IsPaused() const { return m_State == EditorState::Pause; }
        bool IsEditing() const { return m_State == EditorState::Edit; }

        /** 切换到 Play 模式（创建快照） */
        void Play();

        /** 切换到 Stop 模式（恢复快照） */
        void Stop();

        /** 暂停/继续切换 */
        void TogglePause();

        /** 单帧步进（Pause 状态下） */
        void StepFrame();

        /** 是否请求了单帧步进（OnUpdate 中消费后应自动清除） */
        bool IsStepRequested() const { return m_StepRequested; }

        /** 消费单帧步进请求（由 OnUpdate 调用，清除标记） */
        void ConsumeStepRequest() { m_StepRequested = false; }

        // ── ImGui 工具栏控件 ──
        /** 绘制 Play/Stop 按钮组，返回当前是否处于播放状态 */
        bool DrawPlayToolbar();

        // ── 场景持久化 ──
        /** 保存当前场景到文件 */
        bool SaveSceneToFile(const std::string& filePath);

        /** 从文件加载场景（替换当前场景） */
        bool LoadSceneFromFile(const std::string& filePath);

        // ── 状态通知回调 ──
        using StateChangeCallback = std::function<void(EditorState newState)>;
        void SetStateChangeCallback(StateChangeCallback cb) { m_StateCallback = std::move(cb); }

    private:
        /** 内部快照结构：序列化场景到 JSON 字符串 */
        struct Snapshot {
            std::string jsonData;  ///< 完整的场景 JSON 序列化
        };

        /** 拍摄当前场景快照 */
        void TakeSnapshot();

        /** 恢复场景快照 */
        void RestoreSnapshot();

        EditorState m_State = EditorState::Edit;
        Scene* m_Scene = nullptr;

        // 场景快照（Stop 时恢复用）
        std::unique_ptr<Snapshot> m_Snapshot;

        // 单帧步进
        bool m_StepRequested = false;

        StateChangeCallback m_StateCallback;
    };

} // namespace Engine