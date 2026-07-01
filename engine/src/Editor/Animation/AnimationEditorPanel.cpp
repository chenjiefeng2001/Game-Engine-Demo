#include "Engine/Editor/Animation/AnimationEditorPanel.h"
#include "Engine/Editor/AssetDatabase.h"
#include "Engine/Core/Log.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Animation/Skeleton.h"
#include "Engine/Editor/IconsFontAwesome6.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace Engine {
namespace Animation {

    // ══════════════════════════════════════════════════════════════
    // 颜色辅助宏
    // ══════════════════════════════════════════════════════════════
    static ImU32 Col(int r, int g, int b, int a = 255) {
        return IM_COL32(r, g, b, a);
    }

    // ══════════════════════════════════════════════════════════════
    // 构造
    // ══════════════════════════════════════════════════════════════
    AnimationEditorPanel::AnimationEditorPanel() {
        m_CurveEditor.SetTitle("##AnimEditorCurveEmbed");
        m_CurveEditor.CreateDefaultTestData();

        // 默认状态机演示节点
        m_SMNodes.push_back({"Entry", "", ImVec2(0, 300), true, false});
        m_SMNodes.push_back({"Idle",   "", ImVec2(300, 300), false, true});
        m_SMTransitions.push_back({0, 1, 0.15f, ""});
    }

    // ══════════════════════════════════════════════════════════════
    // 数据驱动接口
    // ══════════════════════════════════════════════════════════════
    void AnimationEditorPanel::OpenClip(const GUID& clipGuid) {
        m_ActiveAssetGUID  = clipGuid;
        m_HasActiveAsset   = true;
        m_CurrentMode      = AnimEditorMode::Clip;
        m_Visible          = true;

        // 通过 AssetDatabase 获取 AnimClip 的路径和元数据
        auto& db = EditorAssetDatabase::Get();
        std::string physPath = db.GetPhysicalPath(clipGuid);
        std::string virtPath = db.GetVirtualPath(clipGuid);

        ENGINE_LOG_INFO("AnimEditor", "Opened Clip: {} ({})", virtPath, physPath);

        // 创建缓存数据
        AnimClipData clip;
        clip.name = virtPath.empty() ? "Unnamed" : virtPath;
        clip.guid = clipGuid.ToString();
        clip.duration = 2.0f;
        clip.fps = 30.0f;
        m_Clips.push_back(clip);
        m_CurrentClipIndex = (int)m_Clips.size() - 1;

        // 同步到 CurveEditor
        m_CurveEditor.SetDuration(clip.duration);

        // 发布选中事件 → Inspector 面板显示该资产属性
        m_CurrentMode = AnimEditorMode::Clip;
        PublishSelectionToInspector("clip:" + clipGuid.ToString());
    }

    void AnimationEditorPanel::OpenStateMachine(const GUID& smGuid) {
        m_ActiveAssetGUID  = smGuid;
        m_HasActiveAsset   = true;
        m_CurrentMode      = AnimEditorMode::StateMachine;
        m_Visible          = true;

        auto& db = EditorAssetDatabase::Get();
        std::string virtPath = db.GetVirtualPath(smGuid);
        ENGINE_LOG_INFO("AnimEditor", "Opened StateMachine: {}", virtPath);

        PublishSelectionToInspector("statemachine:" + smGuid.ToString());
    }

    void AnimationEditorPanel::PublishSelectionToInspector(const std::string& context) {
        // 未来扩展：通过 EventBus 发布自定义选中事件
        // 期望 InspectorPanel 监听并显示动画状态/关键帧属性
        (void)context;
    }

    // ══════════════════════════════════════════════════════════════
    // OnImGui — 主入口
    // ══════════════════════════════════════════════════════════════
    void AnimationEditorPanel::OnImGui() {
        ImGui::SetNextWindowSize(ImVec2(1400, 750), ImGuiCond_FirstUseEver);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (!ImGui::Begin(ICON_FA_FILM " Animation Workspace", &m_Visible,
                          ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar)) {
            ImGui::PopStyleVar();
            ImGui::End();
            return;
        }

        // ── 菜单栏 ──
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem(ICON_FA_SAVE " Save Asset", "Ctrl+S")) {
                    if (m_HasActiveAsset) {
                        auto& db = EditorAssetDatabase::Get();
                        ENGINE_LOG_INFO("AnimEditor", "Saved asset: {}", db.GetVirtualPath(m_ActiveAssetGUID));
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
        ImGui::PopStyleVar();

        // ── 工具栏 ──
        DrawToolbar();

        // ── 无资产时的引导提示 ──
        if (!m_HasActiveAsset) {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            ImGui::SetCursorPos(ImVec2(avail.x * 0.5f - 150, avail.y * 0.5f - 20));
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1),
                ICON_FA_MOUSE_POINTER " Double-click an Animation or AnimState asset to begin editing.");
            ImGui::End();
            return;
        }

        // ── 三栏 Table 布局 ──
        if (ImGui::BeginTable("AnimLayout", 3,
                ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Sidebar", ImGuiTableColumnFlags_WidthFixed, 260.0f);
            ImGui::TableSetupColumn("Canvas",  ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthFixed, 340.0f);
            ImGui::TableNextRow();

            // 左：侧边栏（轨道/参数）
            ImGui::TableSetColumnIndex(0);
            DrawSidebar();

            // 中：主画布
            ImGui::TableSetColumnIndex(1);
            if (m_CurrentMode == AnimEditorMode::StateMachine) {
                DrawStateMachineEditor();
            } else {
                DrawClipEditor();
            }

            // 右：3D 预览视口
            ImGui::TableSetColumnIndex(2);
            DrawPreviewViewport();

            ImGui::EndTable();
        }

        ImGui::End();
    }

    // ══════════════════════════════════════════════════════════════
    // 工具栏
    // ══════════════════════════════════════════════════════════════
    void AnimationEditorPanel::DrawToolbar() {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.12f, 1));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
        ImGui::BeginChild("##AnimToolbar", ImVec2(0, 36), false, ImGuiWindowFlags_NoScrollbar);

        // 保存按钮
        if (ImGui::Button(ICON_FA_FLOPPY_DISK " Save", ImVec2(0, 22))) {
            if (m_HasActiveAsset) {
                auto& db = EditorAssetDatabase::Get();
                ENGINE_LOG_INFO("AnimEditor", "Saved: {}", db.GetVirtualPath(m_ActiveAssetGUID));
            }
        }
        ImGui::SameLine(); ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();

        // 模式切换
        bool isSM = (m_CurrentMode == AnimEditorMode::StateMachine);
        if (ImGui::Selectable(ICON_FA_CUBES " AnimGraph", isSM, 0, ImVec2(110, 24)))
            m_CurrentMode = AnimEditorMode::StateMachine;
        ImGui::SameLine();
        if (ImGui::Selectable(ICON_FA_PEN_TO_SQUARE " Timeline", !isSM, 0, ImVec2(110, 24)))
            m_CurrentMode = AnimEditorMode::Clip;

        ImGui::SameLine(); ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();

        // 播放控制
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::Button(ICON_FA_BACKWARD "##First", ImVec2(28, 22)))
            { m_Playhead = 0.0f; m_CurveEditor.SetPlayhead(0.0f); }
        ImGui::SameLine();
        if (ImGui::Button(m_IsPlaying ? ICON_FA_PAUSE "##Pause" : ICON_FA_PLAY "##Play", ImVec2(28, 22)))
            m_IsPlaying = !m_IsPlaying;
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_STOP "##Stop", ImVec2(28, 22)))
            { m_IsPlaying = false; m_Playhead = 0.0f; m_CurveEditor.SetPlayhead(0.0f); }
        ImGui::PopStyleColor();
        ImGui::SameLine();

        // 播放进度
        float dur = m_CurveEditor.GetDuration();
        ImGui::SetNextItemWidth(130);
        ImGui::SliderFloat("##Playhead", &m_Playhead, 0.0f, dur, "%.2fs");
        m_CurveEditor.SetPlayhead(m_Playhead);
        ImGui::SameLine();

        // 循环
        ImGui::Checkbox("Loop", &m_Looping); ImGui::SameLine();
        ImGui::SetNextItemWidth(60);
        ImGui::DragFloat("Spd", &m_PlaybackSpeed, 0.05f, 0.1f, 5.0f, "%.1fx");

        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::Separator();
    }

    // ══════════════════════════════════════════════════════════════
    // 侧边栏
    // ══════════════════════════════════════════════════════════════
    void AnimationEditorPanel::DrawSidebar() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
        ImGui::BeginChild("##AnimSidebar", ImVec2(0, 0), false);

        if (m_CurrentMode == AnimEditorMode::StateMachine) {
            // 状态机参数
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.5f, 1), ICON_FA_LIST " Parameters");
            ImGui::Separator();
            if (ImGui::Button(ICON_FA_PLUS " Bool", ImVec2(-1, 24))) {}
            if (ImGui::Button(ICON_FA_PLUS " Float", ImVec2(-1, 24))) {}

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.5f, 1), ICON_FA_LIST " Nodes");
            ImGui::Separator();
            for (size_t i = 0; i < m_SMNodes.size(); ++i) {
                bool sel = ((int)i == m_SelectedSMNode);
                if (ImGui::Selectable(m_SMNodes[i].name.c_str(), sel, 0, ImVec2(0, 22)))
                    m_SelectedSMNode = (int)i;
            }
        } else {
            // 动画轨道树
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.5f, 1), ICON_FA_LIST " Tracks");
            ImGui::Separator();
            if (ImGui::Button(ICON_FA_PLUS " Add Track", ImVec2(-1, 24))) {
                m_CurveEditor.AddTrack("Track_" + std::to_string(m_CurveEditor.GetTrackCount()));
            }
            ImGui::Spacing();

            for (int t = 0; t < m_CurveEditor.GetTrackCount(); ++t) {
                auto* track = m_CurveEditor.GetTrack(t);
                if (!track) continue;
                bool sel = (t == m_SelectedTrack);
                if (ImGui::Selectable(track->name.c_str(), sel, 0, ImVec2(0, 22)))
                    m_SelectedTrack = t;
            }
            ImGui::TextDisabled("Keyframes: preview in DopeSheet");
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    // ══════════════════════════════════════════════════════════════
    // 曲线/Clip 编辑器 — 内联渲染（不创建独立窗口）
    // ══════════════════════════════════════════════════════════════
    void AnimationEditorPanel::DrawClipEditor() {
        ImGui::BeginChild("##ClipInlineEditor", ImVec2(0, 0), true);

        // 控件：添加关键帧 / 播放控制
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1), ICON_FA_PEN_TO_SQUARE " Keyframe Timeline");
        ImGui::SameLine(ImGui::GetWindowWidth() - 100);
        ImGui::TextDisabled("%.2fs / %.2fs", m_Playhead, m_CurveEditor.GetDuration());

        ImGui::Separator();

        // 从 CurveEditor 获取当前选中轨道的数据
        if (m_SelectedTrack >= 0 && m_SelectedTrack < m_CurveEditor.GetTrackCount()) {
            auto* track = m_CurveEditor.GetTrack(m_SelectedTrack);
            if (track) {
                ImGui::Text("Track: %s  |  Keyframes: %zu", track->name.c_str(), track->keyframes.size());

                if (ImGui::BeginChild("##KfList", ImVec2(0, 0), false)) {
                    for (size_t i = 0; i < track->keyframes.size(); ++i) {
                        auto& kf = track->keyframes[i];
                        ImGui::PushID((int)i);
                        char buf[64];
                        snprintf(buf, sizeof(buf), "kf[%zu]  t=%.3f  v=%.3f", i, kf.time, kf.value);
                        bool sel = kf.selected;
                        if (ImGui::Selectable(buf, sel)) {
                            for (auto& k : track->keyframes) k.selected = false;
                            kf.selected = true;
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndChild();
            }
        } else {
            ImGui::TextDisabled("Select a track from the sidebar to view keyframes.");
        }

        ImGui::EndChild();
    }

    // ══════════════════════════════════════════════════════════════
    // 工业级状态机画布
    // ══════════════════════════════════════════════════════════════
    void AnimationEditorPanel::DrawStateMachineEditor() {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 canvasMin = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();

        if (canvasSize.x < 10 || canvasSize.y < 10) return;

        // 1. 背景 + 网格
        drawList->AddRectFilled(canvasMin, ImVec2(canvasMin.x + canvasSize.x, canvasMin.y + canvasSize.y),
                                Col(28, 28, 32));

        float gs = 20.0f * m_SMCanvas.zoom;
        ImVec2 gridOff(fmodf(m_SMCanvas.scrolling.x, gs),
                       fmodf(m_SMCanvas.scrolling.y, gs));
        for (float x = gridOff.x; x < canvasSize.x; x += gs)
            drawList->AddLine(ImVec2(canvasMin.x + x, canvasMin.y),
                              ImVec2(canvasMin.x + x, canvasMin.y + canvasSize.y),
                              Col(45, 45, 50));
        for (float y = gridOff.y; y < canvasSize.y; y += gs)
            drawList->AddLine(ImVec2(canvasMin.x, canvasMin.y + y),
                              ImVec2(canvasMin.x + canvasSize.x, canvasMin.y + y),
                              Col(45, 45, 50));

        // 2. 交互底板（优先处理输入，确保可伸缩/可平移）
        ImGui::SetCursorScreenPos(canvasMin);
        ImGui::InvisibleButton("##SMCanvas", canvasSize,
                               ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);

        bool canvHov = ImGui::IsItemHovered();
        bool canvAct = ImGui::IsItemActive();

        // 缩放（以鼠标世界坐标为中心）
        if (canvHov && ImGui::GetIO().MouseWheel != 0.0f) {
            ImVec2 mouseWorld = m_SMCanvas.ScreenToWorld(ImGui::GetMousePos(), canvasMin);
            float prevZoom = m_SMCanvas.zoom;
            m_SMCanvas.zoom = std::clamp(m_SMCanvas.zoom + ImGui::GetIO().MouseWheel * 0.1f,
                                         0.15f, 4.0f);
            // 补偿使鼠标下的世界点不动
            ImVec2 newScreen = m_SMCanvas.WorldToScreen(mouseWorld, canvasMin);
            m_SMCanvas.scrolling.x += ImGui::GetMousePos().x - newScreen.x;
            m_SMCanvas.scrolling.y += ImGui::GetMousePos().y - newScreen.y;
        }

        // 中键平移
        if (canvAct && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            m_SMCanvas.scrolling.x += ImGui::GetIO().MouseDelta.x;
            m_SMCanvas.scrolling.y += ImGui::GetIO().MouseDelta.y;
        }

        // 点击空白取消选中
        if (canvHov && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            ImGui::GetHoveredID() == ImGui::GetID("##SMCanvas")) {
            m_SelectedSMNode = -1;
            m_SelectedSMTrans = -1;
            PublishSelectionToInspector("");
        }

        // 右键菜单
        if (canvHov && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
            ImGui::GetHoveredID() == ImGui::GetID("##SMCanvas")) {
            ImGui::OpenPopup("##SMCtxMenu");
        }

        drawList->PushClipRect(canvasMin,
            ImVec2(canvasMin.x + canvasSize.x, canvasMin.y + canvasSize.y), true);

        // ── 3. 绘制过渡线 ──
        for (auto& t : m_SMTransitions) {
            if (t.fromNodeId >= m_SMNodes.size() ||
                t.toNodeId >= m_SMNodes.size()) continue;

            auto& nA = m_SMNodes[t.fromNodeId];
            auto& nB = m_SMNodes[t.toNodeId];

            ImVec2 pA = m_SMCanvas.WorldToScreen(
                ImVec2(nA.position.x + 140, nA.position.y + 25), canvasMin);
            ImVec2 pB = m_SMCanvas.WorldToScreen(
                ImVec2(nB.position.x, nB.position.y + 25), canvasMin);

            // 贝塞尔曲线
            drawList->AddBezierCubic(pA,
                ImVec2(pA.x + 60 * m_SMCanvas.zoom, pA.y),
                ImVec2(pB.x - 60 * m_SMCanvas.zoom, pB.y),
                pB, Col(160, 200, 255), 2.5f * m_SMCanvas.zoom);

            // 箭头三角形
            float dx = pB.x - pA.x, dy = pB.y - pA.y;
            float len = std::sqrt(dx * dx + dy * dy);
            if (len > 1.0f) {
                dx /= len; dy /= len;
                ImVec2 ap(pB.x - dx * 15 * m_SMCanvas.zoom,
                          pB.y - dy * 15 * m_SMCanvas.zoom);
                float aw = 8.0f * m_SMCanvas.zoom;
                drawList->AddTriangleFilled(pB,
                    ImVec2(ap.x - dy * aw, ap.y + dx * aw),
                    ImVec2(ap.x + dy * aw, ap.y - dx * aw),
                    Col(160, 200, 255));
            }
        }

        // ── 4. 绘制拖拽中的连线 ──
        if (m_IsDraggingTransition) {
            if (m_TransitionSourceId < m_SMNodes.size()) {
                auto& src = m_SMNodes[m_TransitionSourceId];
                ImVec2 pSrc = m_SMCanvas.WorldToScreen(
                    ImVec2(src.position.x + 140, src.position.y + 25), canvasMin);
                ImVec2 pMouse = ImGui::GetMousePos();
                drawList->AddBezierCubic(pSrc,
                    ImVec2(pSrc.x + 60 * m_SMCanvas.zoom, pSrc.y),
                    ImVec2(pMouse.x - 60 * m_SMCanvas.zoom, pMouse.y),
                    pMouse, Col(255, 200, 100), 2.0f);
            }
        }

        // ── 5. 绘制状态节点 ──
        for (size_t i = 0; i < m_SMNodes.size(); ++i) {
            auto& n = m_SMNodes[i];
            ImVec2 nodePos = m_SMCanvas.WorldToScreen(
                ImVec2(n.position.x, n.position.y), canvasMin);
            ImVec2 nodeSize(140 * m_SMCanvas.zoom, 50 * m_SMCanvas.zoom);

            bool isSelected = ((int)i == m_SelectedSMNode);
            ImU32 bg = n.isAnyState    ? Col(130, 60, 60)  :
                       n.isDefaultState ? Col(60, 130, 60)  :
                       isSelected       ? Col(70, 80, 110)  :
                                         Col(50, 55, 65);

            drawList->AddRectFilled(nodePos,
                ImVec2(nodePos.x + nodeSize.x, nodePos.y + nodeSize.y),
                bg, 8.0f * m_SMCanvas.zoom);
            drawList->AddRect(nodePos,
                ImVec2(nodePos.x + nodeSize.x, nodePos.y + nodeSize.y),
                isSelected ? Col(255, 200, 50) : Col(30, 30, 35),
                8.0f * m_SMCanvas.zoom, 0, 2.0f * m_SMCanvas.zoom);

            // 节点名称
            drawList->AddText(ImVec2(nodePos.x + 12 * m_SMCanvas.zoom,
                                     nodePos.y + 16 * m_SMCanvas.zoom),
                              IM_COL32_WHITE, n.name.c_str());

            // 引脚（用于拖拽连线）
            ImVec2 pinPos(nodePos.x + nodeSize.x, nodePos.y + nodeSize.y * 0.5f);
            drawList->AddCircleFilled(pinPos, 6.0f * m_SMCanvas.zoom, Col(200, 200, 200));
            drawList->AddCircle(pinPos, 6.0f * m_SMCanvas.zoom, Col(255, 255, 255), 0, 1.5f);

            // ── 节点交互 ──
            ImGui::SetCursorScreenPos(nodePos);
            ImGui::InvisibleButton(("##SMN" + std::to_string(i)).c_str(), nodeSize);

            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                m_SelectedSMNode = (int)i;
                PublishSelectionToInspector("node:" + n.name);
            }
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                n.position.x += ImGui::GetIO().MouseDelta.x / m_SMCanvas.zoom;
                n.position.y += ImGui::GetIO().MouseDelta.y / m_SMCanvas.zoom;
            }

            // 引脚拖拽连线
            ImGui::SetCursorScreenPos(ImVec2(pinPos.x - 8, pinPos.y - 8));
            ImGui::InvisibleButton(("##SMP" + std::to_string(i)).c_str(), ImVec2(16, 16));
            if (ImGui::IsItemActivated()) {
                m_IsDraggingTransition = true;
                m_TransitionSourceId = (uint32_t)i;
            }
        }

        // 鼠标松开结束连线拖拽，检测目标节点
        if (m_IsDraggingTransition && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            m_IsDraggingTransition = false;
            ImVec2 mouseWorld = m_SMCanvas.ScreenToWorld(ImGui::GetMousePos(), canvasMin);
            for (size_t i = 0; i < m_SMNodes.size(); ++i) {
                if (i == m_TransitionSourceId) continue;
                auto& n = m_SMNodes[i];
                if (mouseWorld.x >= n.position.x &&
                    mouseWorld.x <= n.position.x + 140 &&
                    mouseWorld.y >= n.position.y &&
                    mouseWorld.y <= n.position.y + 50) {
                    m_SMTransitions.push_back({
                        m_TransitionSourceId, (uint32_t)i, 0.15f, ""
                    });
                    break;
                }
            }
        }

        // 右键菜单
        if (ImGui::BeginPopup("##SMCtxMenu")) {
            if (ImGui::MenuItem(ICON_FA_PLUS " Add State")) {
                ImVec2 worldPos = m_SMCanvas.ScreenToWorld(ImGui::GetMousePos(), canvasMin);
                m_SMNodes.push_back({"State_" + std::to_string(m_SMNodes.size()), "",
                                     worldPos, false, false});
            }
            if (ImGui::MenuItem(ICON_FA_TRASH " Delete Selected", nullptr,
                                false, m_SelectedSMNode >= 0)) {
                if (m_SelectedSMNode >= 0 && m_SelectedSMNode < (int)m_SMNodes.size()) {
                    m_SMNodes.erase(m_SMNodes.begin() + m_SelectedSMNode);
                    // 清理对该节点的引用
                    m_SMTransitions.erase(
                        std::remove_if(m_SMTransitions.begin(), m_SMTransitions.end(),
                            [&](auto& t) {
                                return t.fromNodeId == (uint32_t)m_SelectedSMNode ||
                                       t.toNodeId == (uint32_t)m_SelectedSMNode;
                            }),
                        m_SMTransitions.end());
                    m_SelectedSMNode = -1;
                }
            }
            if (ImGui::MenuItem("Set as Default", nullptr, false, m_SelectedSMNode >= 0)) {
                if (m_SelectedSMNode >= 0) {
                    for (auto& n : m_SMNodes) n.isDefaultState = false;
                    m_SMNodes[m_SelectedSMNode].isDefaultState = true;
                }
            }
            ImGui::EndPopup();
        }

        drawList->PopClipRect();
    }

    // ══════════════════════════════════════════════════════════════
    // 3D 预览视口
    // ══════════════════════════════════════════════════════════════
    void AnimationEditorPanel::DrawPreviewViewport() {
        ImGui::BeginChild("##PreviewViewport", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 size = ImGui::GetContentRegionAvail();

        // 背景
        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), Col(10, 12, 15));

        // 绘制简单骨骼预览（如果注入）
        if (m_PreviewSkeleton && m_PreviewSkeleton->GetBoneCount() > 0) {
            ImVec2 center(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
            float sc = std::min(size.x, size.y) / 3.0f;
            size_t bc = m_PreviewSkeleton->GetBoneCount();
            for (size_t idx = 0; idx < bc; ++idx) {
                int32_t parentIdx = m_PreviewSkeleton->GetBone((int32_t)idx).parentIndex;
                if (parentIdx >= 0 && parentIdx < (int32_t)bc) {
                    float a1 = (float)idx * 0.7f;
                    float a2 = (float)parentIdx * 0.7f;
                    ImVec2 p1(center.x + std::cos(a1) * sc * 0.3f,
                              center.y + std::sin(a1) * sc * 0.2f);
                    ImVec2 p2(center.x + std::cos(a2) * sc * 0.3f,
                              center.y + std::sin(a2) * sc * 0.2f);
                    drawList->AddLine(p1, p2, Col(180, 220, 255), 2.0f);
                    drawList->AddCircleFilled(p1, 4.0f, Col(255, 200, 100));
                }
            }
        } else {
            const char* msg = ICON_FA_CUBE " 3D Preview";
            ImVec2 ts = ImGui::CalcTextSize(msg);
            drawList->AddText(ImVec2(pos.x + (size.x - ts.x) * 0.5f,
                                     pos.y + (size.y - ts.y) * 0.5f),
                              Col(100, 100, 100), msg);
        }

        // 底部播放控制栏
        ImGui::SetCursorScreenPos(ImVec2(pos.x + 6, pos.y + size.y - 30));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0.6f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
        if (ImGui::BeginChild("##PreviewBar", ImVec2(size.x - 12, 24), false)) {
            ImGui::SetCursorPos(ImVec2(6, 3));
            ImGui::TextDisabled("Preview");
            ImGui::SameLine(ImGui::GetWindowWidth() - 50);
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1), "%.2fs", m_Playhead);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        ImGui::EndChild();
    }

    // ══════════════════════════════════════════════════════════════
    // 播放更新
    // ══════════════════════════════════════════════════════════════
    void AnimationEditorPanel::UpdatePlayback(float dt) {
        if (!m_IsPlaying) return;

        float dur = m_CurveEditor.GetDuration();
        if (dur < 0.001f) return;

        m_Playhead += dt * m_PlaybackSpeed;
        m_CurveEditor.SetPlayhead(m_Playhead);

        if (m_Playhead >= dur) {
            if (m_Looping) {
                m_Playhead = fmodf(m_Playhead, dur);
            } else {
                m_Playhead = dur;
                m_IsPlaying = false;
            }
        }
    }

}} // namespace Engine::Animation