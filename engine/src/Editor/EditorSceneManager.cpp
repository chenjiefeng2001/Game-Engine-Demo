#include "Engine/Editor/EditorSceneManager.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/Scene/Serializer.h"
#include "Engine/Core/Log.h"
#include <imgui.h>

namespace Engine {

    EditorSceneManager::EditorSceneManager() = default;
    EditorSceneManager::~EditorSceneManager() = default;

    // ═══════════════════════════════════════════════════════════════
    // PIE (Play-In-Editor) — 双向隔离
    //
    // Play:
    //   1. 检查 m_EditorScene 非空
    //   2. 通过 JSON 序列化 → 反序列化 深度克隆编辑器场景
    //   3. 存储到 m_RuntimeScene（shared_ptr 管理生命周期）
    //   4. 调用 m_PanelSwitchCallback 通知 EngineEditor 切换面板
    //   5. 调用 m_RuntimeScene->OnCreate() 启动物理/脚本
    //
    // Stop:
    //   1. 调用 m_RuntimeScene->OnDestroy()
    //   2. 销毁 m_RuntimeScene（自动释放）
    //   3. 调用 m_PanelSwitchCallback 通知 EngineEditor 切回编辑器场景
    //
    // 结果：
    //   - 编辑器场景在 Play 期间完全不被污染
    //   - Stop 后编辑器场景瞬间恢复原始状态（零开销）
    // ═══════════════════════════════════════════════════════════════

    void EditorSceneManager::Play() {
        if (m_State == EditorState::Play) return;
        if (!m_EditorScene) {
            ENGINE_LOG_ERROR("Editor", "Cannot Play: no editor scene set");
            return;
        }

        // ── 1. 通过 JSON 序列化深度克隆编辑器场景 ──
        ENGINE_LOG_INFO("Editor", "=== PIE Play: cloning scene ===");
        try {
            auto json = JsonSerializer::Serialize(*m_EditorScene);

            // 创建运行时场景并反序列化
            m_RuntimeScene = std::make_shared<Scene>(m_EditorScene->GetName() + " (Runtime)");
            JsonSerializer::Deserialize(*m_RuntimeScene, json);
        } catch (const std::exception& e) {
            ENGINE_LOG_ERROR("Editor", "PIE scene clone failed: {}", e.what());
            return;
        }

        // ── 2. 切换状态 ──
        m_State = EditorState::Play;

        // ── 3. 启动物理/脚本等运行时系统 ──
        m_RuntimeScene->OnCreate();

        // ── 4. 通知 EngineEditor 切换面板数据源 ──
        if (m_PanelSwitchCallback) {
            m_PanelSwitchCallback(m_RuntimeScene.get());
        }

        // ── 5. 通知状态变更 ──
        if (m_StateCallback) {
            m_StateCallback(m_State);
        }

        ENGINE_LOG_INFO("Editor", "=== PIE Play started ===");
    }

    void EditorSceneManager::Stop() {
        if (m_State == EditorState::Edit) return;
        if (!m_RuntimeScene) return;

        ENGINE_LOG_INFO("Editor", "=== PIE Stop ===");

        // ── 1. 停止运行时系统 ──
        m_RuntimeScene->OnDestroy();

        // ── 2. 销毁运行时场景（shared_ptr 自动释放） ──
        m_RuntimeScene.reset();
        m_StepRequested = false;

        // ── 3. 恢复编辑状态 ──
        m_State = EditorState::Edit;

        // ── 4. 通知 EngineEditor 切回编辑器场景 ──
        if (m_PanelSwitchCallback) {
            m_PanelSwitchCallback(m_EditorScene);
        }

        // ── 5. 通知状态变更 ──
        if (m_StateCallback) {
            m_StateCallback(m_State);
        }

        ENGINE_LOG_INFO("Editor", "=== PIE Stopped, editor scene restored ===");
    }

    void EditorSceneManager::TogglePause() {
        if (m_State == EditorState::Play) {
            m_State = EditorState::Pause;
            ENGINE_LOG_INFO("Editor", "Scene Paused");
        } else if (m_State == EditorState::Pause) {
            m_State = EditorState::Play;
            ENGINE_LOG_INFO("Editor", "Scene Resumed");
        }
        if (m_StateCallback)
            m_StateCallback(m_State);
    }

    void EditorSceneManager::StepFrame() {
        if (m_State == EditorState::Pause) {
            m_StepRequested = true;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // ImGui 工具栏
    // ═══════════════════════════════════════════════════════════════

    bool EditorSceneManager::DrawPlayToolbar() {
        bool isPlaying = IsPlaying();

        ImGui::PushStyleColor(ImGuiCol_Button, isPlaying
            ? ImVec4(0.6f, 0.1f, 0.1f, 1.0f)  // 红色 = 播放中
            : ImVec4(0.1f, 0.5f, 0.1f, 1.0f)); // 绿色 = 编辑中

        if (ImGui::Button(isPlaying ? "■ Stop" : "▶ Play", ImVec2(80, 0))) {
            if (isPlaying) {
                Stop();
            } else {
                Play();
            }
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (ImGui::Button(IsPaused() ? "▶ Resume" : "⏸ Pause", ImVec2(80, 0))) {
            TogglePause();
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!IsPaused());
        if (ImGui::Button("⏭ Step", ImVec2(60, 0))) {
            StepFrame();
        }
        ImGui::EndDisabled();

        return isPlaying;
    }

    // ═══════════════════════════════════════════════════════════════
    // 场景持久化
    // ═══════════════════════════════════════════════════════════════

    bool EditorSceneManager::SaveSceneToFile(const std::string& filePath) {
        if (!m_EditorScene) {
            ENGINE_LOG_ERROR("Editor", "SaveScene: no scene bound");
            return false;
        }
        return m_EditorScene->SaveToFile(filePath);
    }

    bool EditorSceneManager::LoadSceneFromFile(const std::string& filePath) {
        if (!m_EditorScene) {
            ENGINE_LOG_ERROR("Editor", "LoadScene: no scene bound");
            return false;
        }
        return m_EditorScene->LoadFromFile(filePath);
    }

} // namespace Engine