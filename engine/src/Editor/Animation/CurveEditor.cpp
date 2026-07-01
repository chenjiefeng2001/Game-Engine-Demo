#include "Engine/Editor/Animation/CurveEditor.h"
#include "Engine/Editor/IconsFontAwesome6.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <glm/glm.hpp>

namespace Engine {
namespace Animation {

    static ImU32 Col(uint32 r, uint32 g, uint32 b, uint32 a = 255) {
        return IM_COL32(r, g, b, a);
    }

    // ══════════════════════════════════════════════════════════════
    // 数值评估
    // ══════════════════════════════════════════════════════════════
    float AnimTrack::Evaluate(float time) const {
        if (keyframes.empty()) return 0.0f;
        if (time <= keyframes.front().time) return keyframes.front().value;
        if (time >= keyframes.back().time) return keyframes.back().value;

        for (size_t i = 0; i < keyframes.size() - 1; ++i) {
            if (time >= keyframes[i].time && time < keyframes[i + 1].time) {
                float t = (time - keyframes[i].time) / (keyframes[i + 1].time - keyframes[i].time);
                // Cubic Hermite
                float p0 = keyframes[i].value, p1 = keyframes[i + 1].value;
                float m0 = keyframes[i].outTangent, m1 = keyframes[i + 1].inTangent;
                float t2 = t * t, t3 = t2 * t;
                return (2*t3 - 3*t2 + 1)*p0 + (t3 - 2*t2 + t)*m0 + (-2*t3 + 3*t2)*p1 + (t3 - t2)*m1;
            }
        }
        return 0.0f;
    }

    CurveEditor::CurveEditor() {
        m_ScaleX = 100.0f; // 1秒 = 100像素
        m_ScaleY = 50.0f;  // 1单位 = 50像素
        m_ScrollX = 0.0f;
        m_ScrollY = 0.0f;
    }

    AnimTrack& CurveEditor::AddTrack(const std::string& name) {
        m_Tracks.emplace_back();
        m_Tracks.back().name = name;
        return m_Tracks.back();
    }

    void CurveEditor::RemoveTrack(int index) {
        if (index >= 0 && index < (int)m_Tracks.size())
            m_Tracks.erase(m_Tracks.begin() + index);
    }

    void CurveEditor::AddEvent(const std::string& name, float time) {
        m_Events.push_back({time, name, "", ""});
    }

    void CurveEditor::RemoveEvent(int index) {
        if (index >= 0 && index < (int)m_Events.size())
            m_Events.erase(m_Events.begin() + index);
    }

    void CurveEditor::CreateDefaultTestData() {
        m_Tracks.clear(); m_Events.clear();
        auto& posX = AddTrack("Position.x");
        posX.color = Col(255, 100, 100);
        posX.keyframes = { {0.0f, 0.0f}, {1.0f, 2.0f}, {2.0f, 0.0f} };
        auto& posY = AddTrack("Position.y");
        posY.color = Col(100, 255, 100);
        posY.keyframes = { {0.0f, 0.0f}, {0.5f, 1.0f}, {1.0f, 0.0f} };
        m_Duration = 2.0f;
    }

    // ══════════════════════════════════════════════════════════════
    // 工业级坐标映射
    // ══════════════════════════════════════════════════════════════
    float CurveEditor::TimeToScreen(float time, float startX) const { 
        return startX + (time * m_ScaleX) - m_ScrollX; 
    }
    float CurveEditor::ScreenToTime(float screenX, float startX) const { 
        return (screenX - startX + m_ScrollX) / m_ScaleX; 
    }
    float CurveEditor::ValueToScreen(float value, float centerY) const { 
        return centerY - (value * m_ScaleY) + m_ScrollY; 
    }
    float CurveEditor::ScreenToValue(float screenY, float centerY) const { 
        return (centerY - screenY + m_ScrollY) / m_ScaleY; 
    }

    // ══════════════════════════════════════════════════════════════
    // OnImGui 主渲染
    // ══════════════════════════════════════════════════════════════
    void CurveEditor::OnImGui() {
        ImGui::Begin(m_Title.c_str());

        DrawToolbar();

        ImVec2 avail = ImGui::GetContentRegionAvail();
        float topHeight = avail.y * 0.35f;
        float bottomHeight = avail.y - topHeight - 8.0f; // 留出 splitter 空间

        // ── 顶部：DopeSheet (关键帧概览) ──
        if (ImGui::BeginChild("##DopeSheet", ImVec2(avail.x, topHeight), true)) {
            DrawDopeSheet();
        }
        ImGui::EndChild();

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 4));
        ImGui::Separator();
        ImGui::PopStyleVar();

        // ── 底部：CurveArea (曲线图) ──
        if (ImGui::BeginChild("##CurveArea", ImVec2(avail.x, bottomHeight), true)) {
            DrawCurveArea();
        }
        ImGui::EndChild();

        ImGui::End();
    }

    void CurveEditor::DrawToolbar() {
        if (ImGui::Button(ICON_FA_PLUS " Track")) AddTrack("NewTrack");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_CIRCLE " Event")) AddEvent("NewEvent", m_Playhead);
        ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
        if (ImGui::Button(ICON_FA_SYNC " Auto Tangents")) {
            for (auto& track : m_Tracks)
                for (auto& kf : track.keyframes) { kf.inMode = Keyframe::TangentMode::Auto; kf.outMode = Keyframe::TangentMode::Auto; }
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Time: %.2fs", m_Playhead);
    }

    // ══════════════════════════════════════════════════════════════
    // DopeSheet (关键帧概览)
    // ══════════════════════════════════════════════════════════════
    void CurveEditor::DrawDopeSheet() {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();

        float headerH = 24.0f;
        float trackH = 22.0f;
        float labelW = 150.0f;
        float startX = winPos.x + labelW;

        // 1. 处理缩放与平移
        if (ImGui::IsWindowHovered()) {
            if (ImGui::GetIO().MouseWheel != 0.0f) {
                float mouseTime = ScreenToTime(ImGui::GetMousePos().x, startX);
                m_ScaleX = std::clamp(m_ScaleX + ImGui::GetIO().MouseWheel * 10.0f, 10.0f, 1000.0f);
                m_ScrollX = (mouseTime * m_ScaleX) - (ImGui::GetMousePos().x - startX); // 保持鼠标所指时间不变
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                m_ScrollX -= ImGui::GetIO().MouseDelta.x;
            }
        }

        // 2. 绘制时间尺
        drawList->AddRectFilled(winPos, ImVec2(winPos.x + winSize.x, winPos.y + headerH), Col(40, 42, 48));
        drawList->AddLine(ImVec2(startX, winPos.y), ImVec2(startX, winPos.y + winSize.y), Col(80, 80, 80));

        float step = (m_ScaleX > 100.0f) ? 0.1f : 0.5f;
        for (float t = 0.0f; t <= m_Duration + step; t += step) {
            float px = TimeToScreen(t, startX);
            if (px > startX && px < winPos.x + winSize.x) {
                drawList->AddLine(ImVec2(px, winPos.y + 15), ImVec2(px, winPos.y + winSize.y), Col(60, 60, 60, 100));
                char buf[16]; snprintf(buf, sizeof(buf), "%.1f", t);
                drawList->AddText(ImVec2(px + 4, winPos.y + 2), Col(200, 200, 200), buf);
            }
        }

        // 3. 绘制轨道
        ImGui::SetCursorPos(ImVec2(0, headerH));
        for (int i = 0; i < (int)m_Tracks.size(); ++i) {
            auto& track = m_Tracks[i];
            float y = ImGui::GetCursorScreenPos().y;

            // 轨道背景
            bool isSelected = (m_SelectedTrack == i);
            drawList->AddRectFilled(ImVec2(winPos.x, y), ImVec2(winPos.x + labelW, y + trackH), isSelected ? Col(60, 80, 120) : Col(30, 30, 35));
            drawList->AddText(ImVec2(winPos.x + 8, y + 4), track.color, track.name.c_str());
            drawList->AddLine(ImVec2(winPos.x, y + trackH), ImVec2(winPos.x + winSize.x, y + trackH), Col(50, 50, 50));

            // 关键帧
            for (auto& kf : track.keyframes) {
                float px = TimeToScreen(kf.time, startX);
                if (px < startX) continue;
                ImVec2 kp(px, y + trackH * 0.5f);
                drawList->AddTriangleFilled(ImVec2(kp.x, kp.y - 5), ImVec2(kp.x - 4, kp.y + 3), ImVec2(kp.x + 4, kp.y + 3),
                                            kf.selected ? Col(255, 200, 50) : Col(200, 200, 200));
            }

            // 选中轨道交互
            ImGui::InvisibleButton(("##trk" + std::to_string(i)).c_str(), ImVec2(winSize.x, trackH));
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) m_SelectedTrack = i;
        }

        // 4. 播放头
        float playPx = TimeToScreen(m_Playhead, startX);
        if (playPx >= startX) {
            drawList->AddLine(ImVec2(playPx, winPos.y), ImVec2(playPx, winPos.y + winSize.y), Col(255, 50, 50), 2.0f);
            drawList->AddTriangleFilled(ImVec2(playPx - 6, winPos.y), ImVec2(playPx + 6, winPos.y), ImVec2(playPx, winPos.y + 8), Col(255, 50, 50));
        }

        // 拖动播放头
        ImGui::SetCursorPos(ImVec2(labelW, 0));
        ImGui::InvisibleButton("##HeaderInteract", ImVec2(winSize.x - labelW, headerH));
        if (ImGui::IsItemActive()) {
            m_Playhead = std::clamp(ScreenToTime(ImGui::GetMousePos().x, startX), 0.0f, m_Duration);
        }
    }

    // ══════════════════════════════════════════════════════════════
    // CurveArea (真实曲线编辑器)
    // ══════════════════════════════════════════════════════════════
    void CurveEditor::DrawCurveArea() {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();
        
        float labelW = 150.0f;
        float startX = winPos.x + labelW;
        float centerY = winPos.y + winSize.y * 0.5f;

        drawList->AddRectFilled(winPos, ImVec2(winPos.x + winSize.x, winPos.y + winSize.y), Col(20, 20, 25));
        drawList->AddLine(ImVec2(winPos.x, centerY + m_ScrollY), ImVec2(winPos.x + winSize.x, centerY + m_ScrollY), Col(100, 100, 100)); // Zero Line

        // 缩放平移交互
        if (ImGui::IsWindowHovered()) {
            if (ImGui::GetIO().MouseWheel != 0.0f && ImGui::GetIO().KeyCtrl) {
                m_ScaleY = std::clamp(m_ScaleY + ImGui::GetIO().MouseWheel * 5.0f, 5.0f, 500.0f);
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                m_ScrollY += ImGui::GetIO().MouseDelta.y;
            }
        }

        if (m_SelectedTrack < 0 || m_SelectedTrack >= (int)m_Tracks.size()) {
            drawList->AddText(ImVec2(winPos.x + winSize.x/2 - 50, centerY), Col(150, 150, 150), "Select a track to edit curves.");
            return;
        }

        auto& track = m_Tracks[m_SelectedTrack];

        // 绘制曲线与关键帧
        for (size_t i = 0; i < track.keyframes.size(); ++i) {
            auto& kf = track.keyframes[i];
            ImVec2 p0(TimeToScreen(kf.time, startX), ValueToScreen(kf.value, centerY));

            if (i + 1 < track.keyframes.size()) {
                auto& nextKf = track.keyframes[i+1];
                ImVec2 p3(TimeToScreen(nextKf.time, startX), ValueToScreen(nextKf.value, centerY));
                
                // 贝塞尔控制点
                float dt = nextKf.time - kf.time;
                float dx = dt * m_ScaleX / 3.0f;
                ImVec2 p1(p0.x + dx, p0.y - (kf.outTangent * dt / 3.0f) * m_ScaleY);
                ImVec2 p2(p3.x - dx, p3.y - (nextKf.inTangent * dt / 3.0f) * m_ScaleY);

                drawList->AddBezierCubic(p0, p1, p2, p3, track.color, 2.0f);
            }

            // 绘制关键帧控点
            drawList->AddCircleFilled(p0, kf.selected ? 5.0f : 4.0f, kf.selected ? Col(255, 255, 0) : Col(255, 255, 255));
        }

        // 关键帧框选与拖动逻辑
        ImGui::SetCursorPos(ImVec2(labelW, 0));
        ImGui::InvisibleButton("##CurveInteract", ImVec2(winSize.x - labelW, winSize.y));
        bool isHovered = ImGui::IsItemHovered();
        bool isActive = ImGui::IsItemActive();

        if (isHovered && ImGui::IsMouseClicked(0)) {
            ImVec2 mouse = ImGui::GetMousePos();
            m_SelectedKeyframe = -1;
            for (size_t i = 0; i < track.keyframes.size(); ++i) {
                ImVec2 kp(TimeToScreen(track.keyframes[i].time, startX), ValueToScreen(track.keyframes[i].value, centerY));
                float dx = mouse.x - kp.x, dy = mouse.y - kp.y;
                if (std::sqrt(dx*dx + dy*dy) < 8.0f) {
                    m_SelectedKeyframe = i;
                    for(auto& k : track.keyframes) k.selected = false;
                    track.keyframes[i].selected = true;
                    break;
                }
            }
        }

        if (isActive && ImGui::IsMouseDragging(0) && m_SelectedKeyframe >= 0) {
            float newTime = ScreenToTime(ImGui::GetMousePos().x, startX);
            float newVal = ScreenToValue(ImGui::GetMousePos().y, centerY);
            track.keyframes[m_SelectedKeyframe].time = std::clamp(newTime, 0.0f, m_Duration);
            track.keyframes[m_SelectedKeyframe].value = newVal;
        }

        // 播放头
        float playPx = TimeToScreen(m_Playhead, startX);
        if (playPx >= startX) drawList->AddLine(ImVec2(playPx, winPos.y), ImVec2(playPx, winPos.y + winSize.y), Col(255, 50, 50), 1.0f);
    }

}} // namespace Engine::Animation
