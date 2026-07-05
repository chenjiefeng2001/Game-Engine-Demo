#include "Engine/Editor/EditorDefs.h"
#include "Engine/Editor/EditorCamera.h"
#include "Engine/Core/Input.h"
#include "Engine/Core/Log.h"
#include "Engine/Core/IRenderContext.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>

namespace Engine {

    // ═══════════════════════════════════════════════════════════════
    // 1. 增强工具栏 — 高级变换 + 吸附 + 选择过滤 + 孤立模式
    // ═══════════════════════════════════════════════════════════════

    /** 在工具栏中绘制高级变换工具组（Gizmo 模式/空间/轴心） */
    void DrawAdvancedTransformTools(GizmoOperation& currentOp,
                                     GizmoSpace& currentSpace,
                                     PivotMode& currentPivot,
                                     bool& snapEnabled,
                                     SnapConfig& snapCfg) {
        // ── Gizmo 操作模式按钮 ──
        auto OpButton = [&](const char* label, GizmoOperation op, ImVec2 size, const char* tip) {
            bool active = (currentOp == op);
            ImGui::PushStyleColor(ImGuiCol_Button,
                active ? ImVec4(0.3f, 0.5f, 0.8f, 1.0f) : ImVec4(0.2f, 0.2f, 0.22f, 1.0f));
            if (ImGui::Button(label, size)) { currentOp = op; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
            ImGui::PopStyleColor();
        };

        OpButton("T", GizmoOperation::Translate, ImVec2(24, 22), "Translate (W)");
        ImGui::SameLine();
        OpButton("R", GizmoOperation::Rotate,    ImVec2(24, 22), "Rotate (E)");
        ImGui::SameLine();
        OpButton("S", GizmoOperation::Scale,     ImVec2(24, 22), "Scale (R)");

        ImGui::SameLine(); ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();

        // ── 坐标空间切换 ──
        const char* spaceLabels[] = { "World", "Local", "Parent" };
        ImGui::PushItemWidth(60);
        int spaceIdx = static_cast<int>(currentSpace);
        if (ImGui::Combo("##Space", &spaceIdx, spaceLabels, 3)) {
            currentSpace = static_cast<GizmoSpace>(spaceIdx);
        }
        ImGui::PopItemWidth();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Coordinate Space");

        ImGui::SameLine();

        // ── 轴心模式切换 ──
        const char* pivotLabels[] = { "Center", "Pivot" };
        int pivotIdx = static_cast<int>(currentPivot);
        if (ImGui::Combo("##Pivot", &pivotIdx, pivotLabels, 2)) {
            currentPivot = static_cast<PivotMode>(pivotIdx);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pivot: Center / Individual");

        ImGui::SameLine(); ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();

        // ── 吸附启用/模式切换 ──
        ImVec4 snapColor = snapEnabled ? ImVec4(1,1,0,1) : ImVec4(0.5f,0.5f,0.5f,1);
        ImGui::PushStyleColor(ImGuiCol_Text, snapColor);
        if (ImGui::Button("Snap", ImVec2(40, 22))) {
            snapEnabled = !snapEnabled;
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Snapping");

        if (snapEnabled) {
            ImGui::SameLine();
            const char* snapLabels[] = { "Grid", "Vertex", "Surface" };
            int snapIdx = static_cast<int>(snapCfg.mode);
            ImGui::PushItemWidth(65);
            if (ImGui::Combo("##SnapMode", &snapIdx, snapLabels, 3)) {
                snapCfg.mode = static_cast<SnapMode>(snapIdx);
            }
            ImGui::PopItemWidth();

            // 吸附步进值
            ImGui::SameLine();
            ImGui::PushItemWidth(50);
            float snapVal = snapCfg.translateSnap;
            if (ImGui::DragFloat("##SnapVal", &snapVal, 0.1f, 0.1f, 10.0f, "%.1f")) {
                snapCfg.translateSnap = snapVal;
            }
            ImGui::PopItemWidth();
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 2. 相机书签管理
    // ═══════════════════════════════════════════════════════════════

    class CameraBookmarkManager {
    public:
        static constexpr int kMaxBookmarks = 10;

        CameraBookmarkManager() {
            for (int i = 0; i < kMaxBookmarks; ++i) {
                m_Bookmarks[i].name = "Bookmark " + std::to_string(i + 1);
            }
        }

        /** 保存当前相机状态到书签索引 */
        void SaveBookmark(int index, const EditorCamera& camera) {
            if (index < 0 || index >= kMaxBookmarks) return;
            auto& bm = m_Bookmarks[index];
            bm.position   = camera.GetPosition();
            bm.focusPoint = camera.GetFocusPoint();
            bm.distance   = camera.GetDistance();
            bm.pitch      = 0.0f;   // EditorCamera 不直接暴露 pitch/yaw
            bm.yaw        = 0.0f;
            ENGINE_LOG_INFO("CameraBookmark", "Saved bookmark {}: '{}'", index + 1, bm.name);
        }

        /** 加载书签状态到相机 */
        void LoadBookmark(int index, EditorCamera& camera) {
            if (index < 0 || index >= kMaxBookmarks) return;
            const auto& bm = m_Bookmarks[index];
            camera.SetPosition(bm.position);
            camera.SetFocusPoint(bm.focusPoint);
            camera.SetDistance(bm.distance);
            ENGINE_LOG_INFO("CameraBookmark", "Loaded bookmark {}: '{}'", index + 1, bm.name);
        }

        /** 重命名书签 */
        void RenameBookmark(int index, const std::string& name) {
            if (index < 0 || index >= kMaxBookmarks) return;
            m_Bookmarks[index].name = name;
        }

        const CameraBookmark& GetBookmark(int index) const {
            return m_Bookmarks[index];
        }

        int GetCount() const { return kMaxBookmarks; }

        /** 绘制书签 UI */
        void OnImGui(EditorCamera& camera) {
            if (!ImGui::TreeNodeEx("Camera Bookmarks", ImGuiTreeNodeFlags_DefaultOpen))
                return;

            // 快捷键提示
            ImGui::TextDisabled("Ctrl+1..9 = Save | 1..9 = Load | Double-click to rename");

            for (int i = 0; i < kMaxBookmarks; ++i) {
                auto& bm = m_Bookmarks[i];
                ImGui::PushID(i);

                ImGui::Text("[%d]", i + 1);
                ImGui::SameLine();

                char buf[128];
                strncpy(buf, bm.name.c_str(), sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';

                ImGui::PushItemWidth(150);
                if (ImGui::InputText("##bm_name", buf, sizeof(buf),
                                     ImGuiInputTextFlags_EnterReturnsTrue)) {
                    bm.name = buf;
                }
                ImGui::PopItemWidth();

                ImGui::SameLine();
                if (ImGui::SmallButton("Save")) {
                    SaveBookmark(i, camera);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Load")) {
                    LoadBookmark(i, camera);
                }

                ImGui::PopID();
            }

            ImGui::TreePop();
        }

        /** 处理快捷键（外部调用） */
        bool HandleShortcuts(EditorCamera& camera) {
            IInput* input = Input::Get();
            if (!input) return false;

            bool ctrl = input->IsKeyDown(KeyCode::LeftCtrl) ||
                        input->IsKeyDown(KeyCode::RightCtrl);

            for (int i = 0; i < kMaxBookmarks; ++i) {
                KeyCode numKey = static_cast<KeyCode>(static_cast<int>(KeyCode::Num0) + (i + 1) % 10);
                if (input->IsKeyPressed(numKey)) {
                    if (ctrl) {
                        SaveBookmark(i, camera);
                    } else {
                        LoadBookmark(i, camera);
                    }
                    return true;
                }
            }
            return false;
        }

    private:
        CameraBookmark m_Bookmarks[kMaxBookmarks];
    };

    // 全局书签管理器实例
    static CameraBookmarkManager s_BookmarkManager;

    // ═══════════════════════════════════════════════════════════════
    // 3. 选择过滤面板
    // ═══════════════════════════════════════════════════════════════

    void DrawSelectionFilterPanel(SelectionMask& mask) {
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.5f, 1.0f), "Selection Filter");
        ImGui::Separator();

        ImGui::Checkbox("Geometry",  &mask.allowGeometry);
        ImGui::SameLine();
        ImGui::Checkbox("Lights",    &mask.allowLights);
        ImGui::SameLine();
        ImGui::Checkbox("Audio",     &mask.allowAudio);

        ImGui::Checkbox("Triggers",  &mask.allowTriggers);
        ImGui::SameLine();
        ImGui::Checkbox("Particles", &mask.allowParticles);
        ImGui::SameLine();
        ImGui::Checkbox("Cameras",   &mask.allowCameras);

        ImGui::Checkbox("Colliders", &mask.allowColliders);

        if (ImGui::Button("Select All")) {
            mask = SelectionMask{};
            mask.allowColliders = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Deselect All")) {
            mask = SelectionMask{};
            mask.allowGeometry = mask.allowLights = mask.allowAudio =
            mask.allowTriggers = mask.allowParticles = mask.allowCameras =
            mask.allowColliders = false;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 4. 层级/可见性管理面板
    // ═══════════════════════════════════════════════════════════════

    void DrawLayerVisibilityPanel(uint32& visibilityMask, IsolationState& isolation) {
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Layer Visibility");
        ImGui::Separator();

        static const char* layerNames[] = {
            "Static Geometry", "Dynamic Objects", "Light Icons",
            "Particles", "Trigger Volumes", "Collision Debug", "Skeleton Debug"
        };

        for (int i = 0; i < 7; ++i) {
            bool visible = (visibilityMask & (1u << i)) != 0;
            if (ImGui::Checkbox(layerNames[i], &visible)) {
                if (visible)
                    visibilityMask |= (1u << i);
                else
                    visibilityMask &= ~(1u << i);
            }
        }

        ImGui::Separator();

        // ── 孤立模式 ──
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Isolation Mode");
        if (isolation.active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("Exit Isolation", ImVec2(120, 0))) {
                isolation.active = false;
            }
            ImGui::PopStyleColor();
            ImGui::TextDisabled("Object ID: %u", isolation.isolatedObjectId);
        } else {
            if (ImGui::Button("Enter Isolation", ImVec2(120, 0))) {
                isolation.active = true;
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 5. 性能统计悬浮窗
    // ═══════════════════════════════════════════════════════════════

    void DrawStatsOverlay(const StatsOverlayConfig& cfg,
                           float fps, float frameTimeMs,
                           uint32 drawCalls, uint32 verts, uint32 tris,
                           uint64 vramBytes) {
        if (!cfg.visible) return;

        ImGuiWindowFlags overlayFlags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav;

        ImGui::SetNextWindowBgAlpha(0.45f);
        ImGui::SetNextWindowPos(ImVec2(cfg.posX, cfg.posY), ImGuiCond_FirstUseEver);

        bool visible = true;
        if (ImGui::Begin("Stats Overlay", &visible, overlayFlags)) {
            if (cfg.showFPS) {
                ImVec4 fpsColor = (fps >= 58.0f) ? ImVec4(0,1,0,1)
                                 : (fps >= 30.0f) ? ImVec4(1,1,0,1)
                                 : ImVec4(1,0,0,1);
                ImGui::TextColored(fpsColor, "FPS: %.1f (%.1f ms)", fps, frameTimeMs);
            }
            if (cfg.showDrawCalls) {
                ImGui::Text("Draw Calls: %u", drawCalls);
            }
            if (cfg.showVerts) {
                ImGui::Text("Verts: %u | Tris: %u", verts, tris);
            }
            if (cfg.showBatches) {
                // batches ≈ draw calls (simplified)
                ImGui::Text("Batches: %u", drawCalls);
            }
            if (cfg.showVRAM) {
                char vramBuf[64];
                const char* units[] = {"B", "KB", "MB", "GB"};
                int unitIdx = 0;
                double val = static_cast<double>(vramBytes);
                while (val >= 1024.0 && unitIdx < 3) { val /= 1024.0; ++unitIdx; }
                std::snprintf(vramBuf, sizeof(vramBuf), "%.2f %s", val, units[unitIdx]);
                ImGui::Text("VRAM: %s", vramBuf);
            }
        }
        ImGui::End();
    }

    // ═══════════════════════════════════════════════════════════════
    // 6. 场景视口辅助线 Gizmo 绘制（灯光范围、音频半径、触发器）
    // ═══════════════════════════════════════════════════════════════

    // 简化实现 — 在视口中绘制场景辅助图标和线框
    // 完整实现需要 WorldToScreen 投影

    // ═══════════════════════════════════════════════════════════════
    // 7. 摄像机聚焦实现
    // ═══════════════════════════════════════════════════════════════

    void FocusCameraOnObject(EditorCamera& camera, const Vec3& targetPos, float radius) {
        camera.SetFocusPoint(targetPos);
        camera.SetDistance(radius * 2.5f);
        ENGINE_LOG_INFO("Editor", "Camera focused on position (%.1f, %.1f, %.1f), dist=%.1f",
                        targetPos.x, targetPos.y, targetPos.z, radius * 2.5f);
    }

    // ═══════════════════════════════════════════════════════════════
    // 8. 书签系统顶层接口
    // ═══════════════════════════════════════════════════════════════

    void DrawCameraBookmarksUI(EditorCamera& camera) {
        s_BookmarkManager.OnImGui(camera);
    }

    bool HandleCameraBookmarkShortcuts(EditorCamera& camera) {
        return s_BookmarkManager.HandleShortcuts(camera);
    }

    // ═══════════════════════════════════════════════════════════════
    // 9. 高级设置面板完整绘制
    // ═══════════════════════════════════════════════════════════════

    void DrawAdvancedEditorSettings(GizmoOperation& currentOp,
                                     GizmoSpace& currentSpace,
                                     PivotMode& currentPivot,
                                     SnapConfig& snapCfg,
                                     SelectionMask& selMask,
                                     uint32& visibilityMask,
                                     IsolationState& isolation,
                                     EditorCamera& camera,
                                     bool showPanel) {
        if (!showPanel) return;

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
        if (!ImGui::Begin("Editor Settings", &showPanel, flags)) {
            ImGui::End();
            return;
        }

        if (ImGui::TreeNodeEx("Transform Tools", ImGuiTreeNodeFlags_DefaultOpen)) {
            DrawAdvancedTransformTools(currentOp, currentSpace,
                                       currentPivot, snapCfg.enabled, snapCfg);
            ImGui::TreePop();
        }

        ImGui::Separator();

        if (ImGui::TreeNodeEx("Selection Filter", ImGuiTreeNodeFlags_DefaultOpen)) {
            DrawSelectionFilterPanel(selMask);
            ImGui::TreePop();
        }

        ImGui::Separator();

        if (ImGui::TreeNodeEx("Layer Visibility", ImGuiTreeNodeFlags_DefaultOpen)) {
            DrawLayerVisibilityPanel(visibilityMask, isolation);
            ImGui::TreePop();
        }

        ImGui::Separator();

        DrawCameraBookmarksUI(camera);

        ImGui::End();
    }

} // namespace Engine