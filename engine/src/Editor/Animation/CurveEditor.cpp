#include "Engine/Editor/Animation/CurveEditor.h"
#include <cmath>
#include <algorithm>
#include <sstream>

namespace Engine {
namespace Animation {

    static ImU32 Col(uint32 r, uint32 g, uint32 b, uint32 a = 255) {
        return IM_COL32(r, g, b, a);
    }

    // ══════════════════════════════════════════════════════════════
    // AnimTrack::Evaluate
    // ══════════════════════════════════════════════════════════════
    float AnimTrack::Evaluate(float time) const {
        if (keyframes.empty()) return 0.0f;
        if (time <= keyframes.front().time) return keyframes.front().value;
        if (time >= keyframes.back().time) return keyframes.back().value;

        for (size_t i = 0; i < keyframes.size() - 1; ++i) {
            if (time >= keyframes[i].time && time < keyframes[i + 1].time) {
                float t = (time - keyframes[i].time) / (keyframes[i + 1].time - keyframes[i].time);
                // Simple cubic Hermite interpolation
                float p0 = keyframes[i].value;
                float p1 = keyframes[i + 1].value;
                float m0 = keyframes[i].outTangent;
                float m1 = keyframes[i + 1].inTangent;
                float t2 = t * t;
                float t3 = t2 * t;
                return (2*t3 - 3*t2 + 1) * p0 + (t3 - 2*t2 + t) * m0 + (-2*t3 + 3*t2) * p1 + (t3 - t2) * m1;
            }
        }
        return 0.0f;
    }

    CurveEditor::CurveEditor() = default;

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
        m_Tracks.clear();
        m_Events.clear();

        auto& posX = AddTrack("Position.x");
        posX.minValue = -2.0f; posX.maxValue = 2.0f;
        posX.keyframes = {
            {0.0f, 0.0f, 0, 0, Keyframe::TangentMode::Auto, Keyframe::TangentMode::Auto},
            {1.0f, 2.0f, 1, 1, Keyframe::TangentMode::Auto, Keyframe::TangentMode::Auto},
            {2.0f, 0.0f, 0, 0, Keyframe::TangentMode::Auto, Keyframe::TangentMode::Auto},
            {3.0f, -2.0f, -1, -1, Keyframe::TangentMode::Auto, Keyframe::TangentMode::Auto},
            {4.0f, 0.0f, 0, 0, Keyframe::TangentMode::Auto, Keyframe::TangentMode::Auto},
        };

        auto& posY = AddTrack("Position.y");
        posY.minValue = -1.0f; posY.maxValue = 3.0f;
        posY.keyframes = {
            {0.0f, 0.0f, 0, 3, Keyframe::TangentMode::Auto, Keyframe::TangentMode::Auto},
            {0.5f, 1.0f, 3, 0, Keyframe::TangentMode::Auto, Keyframe::TangentMode::Auto},
            {1.0f, 0.0f, 0, 0, Keyframe::TangentMode::Auto, Keyframe::TangentMode::Auto},
        };

        auto& rot = AddTrack("Rotation");
        rot.minValue = 0.0f; rot.maxValue = 360.0f;
        rot.keyframes = {
            {0.0f, 0.0f, 0, 100, Keyframe::TangentMode::Auto, Keyframe::TangentMode::Linear},
            {2.0f, 180.0f, 100, 100, Keyframe::TangentMode::Linear, Keyframe::TangentMode::Linear},
            {4.0f, 360.0f, 100, 0, Keyframe::TangentMode::Linear, Keyframe::TangentMode::Auto},
        };

        AddEvent("Footstep", 0.5f);
        AddEvent("Jump", 1.5f);
        AddEvent("Land", 2.2f);

        m_Duration = 5.0f;
    }

    // ══════════════════════════════════════════════════════════════
    // OnImGui — 主渲染入口
    // ══════════════════════════════════════════════════════════════
    void CurveEditor::OnImGui() {
        ImGui::Begin(m_Title.c_str());

        DrawToolbar();

        ImVec2 avail = ImGui::GetContentRegionAvail();
        float topHeight = avail.y * 0.35f;
        float bottomHeight = avail.y - topHeight;

        // Split layout: top = DopeSheet, bottom = Curve Area
        if (ImGui::BeginChild("##DopeSheet", ImVec2(avail.x, topHeight), true)) {
            DrawDopeSheet();
        }
        ImGui::EndChild();

        ImGui::Separator();

        if (ImGui::BeginChild("##CurveArea", ImVec2(avail.x, bottomHeight), true)) {
            DrawCurveArea();
        }
        ImGui::EndChild();

        ImGui::End();
    }

    void CurveEditor::DrawToolbar() {
        if (ImGui::Button("+ Track")) {
            AddTrack("NewTrack " + std::to_string(m_Tracks.size()));
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Event")) {
            AddEvent("NewEvent", 0.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Create Test Data")) {
            CreateDefaultTestData();
        }
        ImGui::SameLine();
        if (ImGui::Button("Snap Frame")) {
            // Snap all keyframes to nearest frame (60fps = ~0.0167s)
        }
        ImGui::SameLine();
        if (ImGui::Button("Auto Tangent")) {
            for (auto& track : m_Tracks) {
                for (auto& kf : track.keyframes) {
                    kf.inMode = Keyframe::TangentMode::Auto;
                    kf.outMode = Keyframe::TangentMode::Auto;
                }
            }
        }
        ImGui::SameLine();
        ImGui::Text("| %.2fs", m_Playhead);
    }

    void CurveEditor::DrawDopeSheet() {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetContentRegionAvail();

        float timeScale = m_ScaleX;
        float trackH = 20.0f;
        float headerH = 20.0f;
        float trackLabelW = 120.0f;
        float scrollW = ImGui::GetStyle().ScrollbarSize;

        // Draw timeline header
        ImVec2 headerPos = ImGui::GetCursorScreenPos();
        drawList->AddRectFilled(headerPos, ImVec2(headerPos.x + winSize.x, headerPos.y + headerH), Col(40, 40, 45));
        drawList->AddLine(ImVec2(headerPos.x + trackLabelW, headerPos.y),
                          ImVec2(headerPos.x + trackLabelW, headerPos.y + winSize.y), Col(60, 60, 65));

        // Time markers
        float startX = headerPos.x + trackLabelW - m_ScrollX;
        for (float t = 0; t <= m_Duration; t += 0.5f) {
            float sx = startX + t * timeScale;
            if (sx >= headerPos.x + trackLabelW && sx <= headerPos.x + winSize.x - scrollW) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%.1f", t);
                drawList->AddText(ImVec2(sx - 10, headerPos.y + 2), Col(180, 180, 180), buf);
                drawList->AddLine(ImVec2(sx, headerPos.y + headerH), ImVec2(sx, headerPos.y + winSize.y), Col(50, 50, 55));
            }
        }

        // Draw playhead in header
        float playSx = startX + m_Playhead * timeScale;
        if (playSx >= headerPos.x + trackLabelW) {
            drawList->AddLine(ImVec2(playSx, headerPos.y), ImVec2(playSx, headerPos.y + winSize.y), Col(255, 200, 50, 200), 2.0f);
        }

        // Track rows
        ImGui::SetCursorPos(ImVec2(0, headerH));
        for (int t = 0; t < (int)m_Tracks.size(); ++t) {
            auto& track = m_Tracks[t];
            float y = ImGui::GetCursorScreenPos().y;

            // Track label
            bool selected = (t == m_SelectedTrack);
            ImVec2 labelRect(winPos.x, y);
            drawList->AddRectFilled(labelRect, ImVec2(labelRect.x + trackLabelW, y + trackH),
                                    selected ? Col(60, 70, 90) : Col(35, 35, 40));
            drawList->AddText(ImVec2(labelRect.x + 4, y + 2),
                              track.color, track.name.c_str());

            // Track keyframes (diamonds)
            for (auto& kf : track.keyframes) {
                float kx = startX + kf.time * timeScale;
                ImVec2 kp(kx, y + trackH / 2);
                drawList->AddTriangleFilled(
                    ImVec2(kp.x, kp.y - 5),
                    ImVec2(kp.x - 4, kp.y + 3),
                    ImVec2(kp.x + 4, kp.y + 3),
                    kf.selected ? Col(255, 200, 50) : Col(180, 220, 255));
            }

            ImGui::SetCursorPos(ImVec2(0, ImGui::GetCursorPosY() + trackH));
            if (ImGui::InvisibleButton(("##track" + std::to_string(t)).c_str(), ImVec2(winSize.x, trackH))) {
                m_SelectedTrack = (float)t;
            }
        }

        // Handle playhead dragging
        ImGui::SetCursorPos(ImVec2(0, headerH));
        ImGui::InvisibleButton("##dopeSheetPlayhead", ImVec2(winSize.x, winSize.y - headerH));
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
            float mouseX = ImGui::GetMousePos().x - (headerPos.x + trackLabelW - m_ScrollX);
            m_Playhead = std::clamp(mouseX / timeScale, 0.0f, m_Duration);
            m_DraggingPlayhead = true;
        }
        if (m_DraggingPlayhead) {
            if (ImGui::IsMouseDown(0)) {
                float mouseX = ImGui::GetMousePos().x - (headerPos.x + trackLabelW - m_ScrollX);
                m_Playhead = std::clamp(mouseX / timeScale, 0.0f, m_Duration);
            } else {
                m_DraggingPlayhead = false;
            }
        }

        // Scroll
        m_ScrollX = std::max(0.0f, -ImGui::GetScrollX());
    }

    void CurveEditor::DrawCurveArea() {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetContentRegionAvail();

        float timeScale = m_ScaleX * 1.5f;
        float curveH = winSize.y - 30.0f;
        float curveW = winSize.x - 20.0f;
        float trackLabelW = 100.0f;

        if (m_SelectedTrack < 0 || m_SelectedTrack >= (int)m_Tracks.size()) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "Select a track from DopeSheet");
            return;
        }

        auto& track = m_Tracks[(int)m_SelectedTrack];
        float minV = track.minValue, maxV = track.maxValue;
        float range = maxV - minV;
        if (range < 0.001f) range = 1.0f;

        ImVec2 curvePos = ImGui::GetCursorScreenPos();
        float curveBase = curvePos.y + curveH;

        // Background
        drawList->AddRectFilled(curvePos, ImVec2(curvePos.x + curveW, curvePos.y + curveH), Col(25, 25, 30));

        // Grid lines
        float startX = curvePos.x;
        for (float t = 0; t <= m_Duration; t += 0.5f) {
            float gx = startX + t * timeScale;
            drawList->AddLine(ImVec2(gx, curvePos.y), ImVec2(gx, curvePos.y + curveH), Col(40, 40, 45));
        }
        for (float v = 0; v <= range; v += range / 8.0f) {
            float gy = curveBase - v / range * curveH;
            drawList->AddLine(ImVec2(curvePos.x, gy), ImVec2(curvePos.x + curveW, gy), Col(40, 40, 45));
        }

        // Draw 0-line
        float zeroY = curveBase - (-minV / range) * curveH;
        drawList->AddLine(ImVec2(curvePos.x, zeroY), ImVec2(curvePos.x + curveW, zeroY), Col(60, 60, 70));

        // Draw curves between keyframes
        for (size_t i = 0; i + 1 < track.keyframes.size(); ++i) {
            DrawBezierCurve(drawList, track.keyframes[i], track.keyframes[i + 1],
                           minV, maxV, curveBase, curveH);
        }

        // Draw playhead
        float playX = curvePos.x + m_Playhead * timeScale;
        drawList->AddLine(ImVec2(playX, curvePos.y), ImVec2(playX, curvePos.y + curveH), Col(255, 200, 50, 150), 1.5f);

        // Draw keyframe handles
        for (size_t i = 0; i < track.keyframes.size(); ++i) {
            auto& kf = track.keyframes[i];
            float kx = curvePos.x + kf.time * timeScale;
            float ky = curveBase - (kf.value - minV) / range * curveH;
            float radius = kf.selected ? 5.0f : 3.5f;
            drawList->AddCircleFilled(ImVec2(kx, ky), radius, kf.selected ? Col(255, 200, 50) : Col(180, 220, 255));
        }

        // Handle keyframe dragging
        ImGui::SetCursorPos(ImVec2(0, winSize.y - curveH));
        ImGui::InvisibleButton("##curveArea", ImVec2(curveW, curveH));
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
            float mx = ImGui::GetMousePos().x - curvePos.x;
            float my = ImGui::GetMousePos().y;
            for (size_t i = 0; i < track.keyframes.size(); ++i) {
                float kx = track.keyframes[i].time * timeScale;
                float ky = curveBase - (track.keyframes[i].value - minV) / range * curveH;
                float dist = sqrtf((mx - kx) * (mx - kx) + (my - ky) * (my - ky));
                if (dist < 8.0f) {
                    m_SelectedKeyframe = (int)i;
                    for (auto& kf : track.keyframes) kf.selected = false;
                    track.keyframes[i].selected = true;
                    m_DraggingKeyframe = true;
                    break;
                }
            }
        }

        if (m_DraggingKeyframe && m_SelectedKeyframe >= 0 && m_SelectedKeyframe < (int)track.keyframes.size()) {
            if (ImGui::IsMouseDown(0)) {
                float mx = ImGui::GetMousePos().x - curvePos.x;
                float my = ImGui::GetMousePos().y;
                track.keyframes[m_SelectedKeyframe].time = std::clamp(mx / timeScale, 0.0f, m_Duration);
                track.keyframes[m_SelectedKeyframe].value = std::clamp((curveBase - my) / curveH * range + minV, minV, maxV);
            } else {
                m_DraggingKeyframe = false;
            }
        }

        // Double-click to add keyframe
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            float mx = ImGui::GetMousePos().x - curvePos.x;
            float my = ImGui::GetMousePos().y;
            Keyframe kf;
            kf.time = std::clamp(mx / timeScale, 0.0f, m_Duration);
            kf.value = std::clamp((curveBase - my) / curveH * range + minV, minV, maxV);
            kf.inMode = Keyframe::TangentMode::Auto;
            kf.outMode = Keyframe::TangentMode::Auto;
            track.keyframes.push_back(kf);
            std::sort(track.keyframes.begin(), track.keyframes.end(),
                      [](auto& a, auto& b) { return a.time < b.time; });
        }

        // Right-click to delete keyframe
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1) && m_SelectedKeyframe >= 0) {
            track.keyframes.erase(track.keyframes.begin() + m_SelectedKeyframe);
            m_SelectedKeyframe = -1;
        }

        // Events markers
        DrawEventMarkers();
    }

    void CurveEditor::DrawEventMarkers() {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        for (auto& evt : m_Events) {
            float ex = ImGui::GetWindowPos().x + 120.0f - m_ScrollX + evt.time * m_ScaleX;
            drawList->AddTriangleFilled(
                ImVec2(ex, ImGui::GetWindowPos().y + 15),
                ImVec2(ex - 4, ImVec2(ex + 4, ImGui::GetWindowPos().y + 22).y),
                ImVec2(ex + 4, ImGui::GetWindowPos().y + 22),
                Col(255, 100, 100));
            drawList->AddText(ImVec2(ex + 6, ImGui::GetWindowPos().y + 13),
                              Col(255, 200, 200), evt.name.c_str());
        }
    }

    void CurveEditor::DrawBezierCurve(ImDrawList* drawList, const Keyframe& kf1, const Keyframe& kf2,
                                      float minV, float maxV, float yBase, float height) {
        float range = maxV - minV;
        if (range < 0.001f) range = 1.0f;

        auto toScreen = [&](float t, float v) -> ImVec2 {
            float sx = ImGui::GetCursorScreenPos().x + t * m_ScaleX * 1.5f;
            float sy = yBase - (v - minV) / range * height;
            return ImVec2(sx, sy);
        };

        ImVec2 p0 = toScreen(kf1.time, kf1.value);
        ImVec2 p3 = toScreen(kf2.time, kf2.value);
        float dt = kf2.time - kf1.time;

        // Control points based on tangents
        ImVec2 p1(p0.x + dt * m_ScaleX * 1.5f / 3.0f, p0.y - kf1.outTangent * height / range * dt * m_ScaleX * 1.5f / 3.0f);
        ImVec2 p2(p3.x - dt * m_ScaleX * 1.5f / 3.0f, p3.y - kf2.inTangent * height / range * dt * m_ScaleX * 1.5f / 3.0f);

        drawList->AddBezierCubic(p0, p1, p2, p3, Col(100, 180, 255), 2.0f);

        // Draw tangent handles if selected
        auto& kfs = m_Tracks[(int)m_SelectedTrack].keyframes;
        if (!kfs.empty()) {
            if (kf1.selected || &kf1 == &kfs.back()) {
                drawList->AddLine(p0, p1, Col(200, 200, 50, 150), 1.0f);
                drawList->AddCircleFilled(p1, 3.0f, Col(200, 200, 50));
            }
            if (kf2.selected || &kf2 == &kfs.front()) {
                drawList->AddLine(p3, p2, Col(200, 200, 50, 150), 1.0f);
                drawList->AddCircleFilled(p2, 3.0f, Col(200, 200, 50));
            }
        }
    }

    float CurveEditor::TimeToScreen(float time) const { return time * m_ScaleX - m_ScrollX; }
    float CurveEditor::ScreenToTime(float screenX) const { return (screenX + m_ScrollX) / m_ScaleX; }
    float CurveEditor::ValueToScreen(float value, float minV, float maxV) const {
        return (value - minV) / (maxV - minV);
    }
    float CurveEditor::ScreenToValue(float screenY, float minV, float maxV) const {
        return screenY * (maxV - minV) + minV;
    }

    bool CurveEditor::SaveToFile(const std::string& path) const { return false; }
    bool CurveEditor::LoadFromFile(const std::string& path) { return false; }

}} // namespace Engine::Animation