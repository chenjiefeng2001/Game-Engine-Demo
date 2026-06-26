#pragma once

/**
 * @file CurveEditor.h
 * @brief 动画曲线编辑器 — DopeSheet + Bezier Curve Editor
 *
 * 类似 Unity Animation Window / Unreal Persona Curve Editor。
 * 支持多轨道关键帧编辑、贝塞尔曲线控制柄、动画事件标记。
 */

#include "Engine/Types.h"
#include <imgui.h>
#include <vector>
#include <string>
#include <memory>

namespace Engine {
namespace Animation {

    // ============================================================
    // 关键帧
    // ============================================================
    struct Keyframe {
        float time = 0.0f;
        float value = 0.0f;

        // Bezier 控制柄（切线）
        float inTangent  = 0.0f;   // 左侧切线（输入斜率）
        float outTangent = 0.0f;   // 右侧切线（输出斜率）

        // 切线类型
        enum class TangentMode : uint8 {
            Auto,       ///< 自动平滑
            Free,       ///< 自由拖拽
            Linear,     ///< 线性（无缓入缓出）
            Stepped,    ///< 阶梯（突变）
            Constant    ///< 常量
        };
        TangentMode inMode  = TangentMode::Auto;
        TangentMode outMode = TangentMode::Auto;

        bool selected = false;
    };

    // ============================================================
    // 动画轨道
    // ============================================================
    struct AnimTrack {
        std::string name;
        std::string path;        // 目标对象路径 (GameObject hierarchy)
        std::string property;    // 属性名 (position.x, rotation, scale 等)
        std::vector<Keyframe> keyframes;
        float minValue = 0.0f;
        float maxValue = 1.0f;
        bool expanded = true;
        ImColor color = ImColor(0.3f, 0.6f, 1.0f);

        /// 获取指定时间点的插值
        float Evaluate(float time) const;
    };

    // ============================================================
    // 动画事件
    // ============================================================
    struct AnimEvent {
        float time = 0.0f;
        std::string name = "Event";
        std::string parameter;
        std::string selected;
    };

    // ============================================================
    // 曲线编辑器面板
    // ============================================================
    class CurveEditor {
    public:
        CurveEditor();
        ~CurveEditor() = default;

        void SetTitle(const std::string& title) { m_Title = title; }
        void OnImGui();

        // ── 轨道管理 ──
        AnimTrack& AddTrack(const std::string& name);
        void RemoveTrack(int index);
        AnimTrack* GetTrack(int index) { return (index >= 0 && index < (int)m_Tracks.size()) ? &m_Tracks[index] : nullptr; }
        int GetTrackCount() const { return (int)m_Tracks.size(); }

        // ── 事件管理 ──
        void AddEvent(const std::string& name, float time);
        void RemoveEvent(int index);
        int GetEventCount() const { return (int)m_Events.size(); }
        AnimEvent* GetEvent(int index) { return (index >= 0 && index < (int)m_Events.size()) ? &m_Events[index] : nullptr; }

        // ── 时间控制 ──
        void SetDuration(float duration) { m_Duration = duration; }
        float GetDuration() const { return m_Duration; }
        void SetPlayhead(float time) { m_Playhead = time; }
        float GetPlayhead() const { return m_Playhead; }

        // ── 导入/导出 ──
        bool SaveToFile(const std::string& path) const;
        bool LoadFromFile(const std::string& path);

        // ── 测试数据 ──
        void CreateDefaultTestData();

    private:
        void DrawToolbar();
        void DrawDopeSheet();
        void DrawCurveArea();
        void DrawTimeline();
        void DrawKeyframeHandles();
        void DrawEventMarkers();

        // 内部状态
        float m_Playhead = 0.0f;
        float m_Duration = 5.0f;
        float m_ScrollX = 0.0f;
        float m_ScaleX = 100.0f;     // 像素/秒
        float m_SelectedTrack = -1;
        int m_SelectedKeyframe = -1;
        bool m_DraggingPlayhead = false;
        bool m_DraggingKeyframe = false;
        std::string m_Title = "Curve Editor";

        std::vector<AnimTrack> m_Tracks;
        std::vector<AnimEvent> m_Events;

        // 内部工具
        float TimeToScreen(float time) const;
        float ScreenToTime(float screenX) const;
        float ValueToScreen(float value, float minV, float maxV) const;
        float ScreenToValue(float screenY, float minV, float maxV) const;
        void DrawBezierCurve(ImDrawList* drawList, const Keyframe& kf1, const Keyframe& kf2,
                             float minV, float maxV, float yBase, float height);
    };

}} // namespace Engine::Animation