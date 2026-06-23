#include "Engine/Editor/EditorSceneManager.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/Scene/Serializer.h"
#include "Engine/Core/Log.h"
#include <imgui.h>

namespace Engine {

    EditorSceneManager::EditorSceneManager() = default;
    EditorSceneManager::~EditorSceneManager() = default;

    void EditorSceneManager::Play() {
        if (m_State == EditorState::Play) return;
        if (!m_Scene) return;

        // 拍摄场景快照（保存编辑状态）
        TakeSnapshot();

        m_State = EditorState::Play;
        ENGINE_LOG_INFO("Editor", "Scene Play started");

        if (m_StateCallback)
            m_StateCallback(m_State);
    }

    void EditorSceneManager::Stop() {
        if (m_State == EditorState::Edit) return;
        if (!m_Scene) return;

        // 恢复场景快照（回到编辑前的状态）
        RestoreSnapshot();

        m_State = EditorState::Edit;
        ENGINE_LOG_INFO("Editor", "Scene Stopped, snapshot restored");

        if (m_StateCallback)
            m_StateCallback(m_State);
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

    // ── 快照管理 ──
    void EditorSceneManager::TakeSnapshot() {
        if (!m_Scene) return;

        // 将场景序列化到 JSON 字符串（内存快照）
        auto json = JsonSerializer::Serialize(*m_Scene);
        m_Snapshot = std::make_unique<Snapshot>();
        m_Snapshot->jsonData = json.dump(2);  // 带缩进，方便调试
    }

    void EditorSceneManager::RestoreSnapshot() {
        if (!m_Snapshot || !m_Scene) return;

        // 从快照 JSON 反序列化场景
        try {
            auto json = nlohmann::json::parse(m_Snapshot->jsonData);
            // 先清空场景，再重新加载，避免对象重复
            m_Scene->Clear();
            JsonSerializer::Deserialize(*m_Scene, json);
            ENGINE_LOG_INFO("Editor", "Snapshot restored successfully");
        } catch (const std::exception& e) {
            ENGINE_LOG_ERROR("Editor", "Failed to restore snapshot: {}", e.what());
        }

        m_Snapshot.reset();
        m_StepRequested = false;
    }

    // ── ImGui 工具栏 ──
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

    // ── 场景持久化 ──
    bool EditorSceneManager::SaveSceneToFile(const std::string& filePath) {
        if (!m_Scene) {
            ENGINE_LOG_ERROR("Editor", "SaveScene: no scene bound");
            return false;
        }
        return m_Scene->SaveToFile(filePath);
    }

    bool EditorSceneManager::LoadSceneFromFile(const std::string& filePath) {
        if (!m_Scene) {
            ENGINE_LOG_ERROR("Editor", "LoadScene: no scene bound");
            return false;
        }
        return m_Scene->LoadFromFile(filePath);
    }

} // namespace Engine