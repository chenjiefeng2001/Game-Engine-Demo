#pragma once

/**
 * @file AnimationEditorPanel.h
 * @brief 动画编辑器面板 — 数据驱动的工业级动画编辑套件
 *
 * 架构升级：
 *   1. 通过 AssetDatabase GUID 对接底层 AnimClip / AnimStateMachine 资源
 *   2. CanvasTransform 统一坐标系映射，实现稳定缩放/平移
 *   3. EventBus 打通 InspectorPanel 联动
 *   4. 与 CurveEditor 共享底层轨道数据结构
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include "Engine/Editor/Animation/CurveEditor.h"
#include "Engine/Editor/AssetTypes.h"  // GUID, AssetType
#include "Engine/Core/EventBus.h"

#include <imgui.h>
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace Engine {

    // 前向声明
    class EditorAssetDatabase;
    class Skeleton;
    class SkinnedMesh;

    namespace Animation {

    // ============================================================
    // 编辑模式
    // ============================================================
    enum class AnimEditorMode : uint8_t {
        Clip,           ///< 曲线/关键帧编辑
        StateMachine,   ///< 状态机逻辑编辑
        BlendSpace,     ///< Blend Space 混合空间
    };

    // ============================================================
    // 画布状态（统一的坐标系映射建模）
    // ============================================================
    struct CanvasTransform {
        ImVec2 origin    = ImVec2(0, 0);  ///< 画布偏移
        ImVec2 scrolling = ImVec2(0, 0);  ///< 平移量
        float  zoom      = 1.0f;          ///< 缩放因子

        // ── 坐标转换 ──
        ImVec2 WorldToScreen(const ImVec2& worldPos, const ImVec2& canvasMin) const {
            return ImVec2(
                canvasMin.x + worldPos.x * zoom + scrolling.x,
                canvasMin.y + worldPos.y * zoom + scrolling.y
            );
        }
        ImVec2 ScreenToWorld(const ImVec2& screenPos, const ImVec2& canvasMin) const {
            return ImVec2(
                (screenPos.x - canvasMin.x - scrolling.x) / zoom,
                (screenPos.y - canvasMin.y - scrolling.y) / zoom
            );
        }
    };

    // ============================================================
    // 状态机节点数据（与 AnimStateMachine 资源对应）
    // ============================================================
    struct SMNodeData {
        std::string name;
        std::string clipGUID;          // 关联的 AnimationClip GUID
        ImVec2      position;          // 画布世界坐标
        bool        isDefaultState = false;
        bool        isAnyState     = false;
    };

    struct SMTransitionData {
        uint32_t    fromNodeId;
        uint32_t    toNodeId;
        float       blendDuration = 0.1f;
        std::string conditionDesc;
    };

    // ============================================================
    // 动画剪辑数据（与 AnimationClip 资源对应）
    // ============================================================
    struct AnimClipData {
        std::string name;
        std::string guid;               // 关联的 GUID
        float       duration  = 2.0f;
        float       fps       = 30.0f;

        // 轨道列表（从 CurveEditor 同步）
        std::vector<AnimTrack> tracks;
        std::vector<AnimEvent> events;
    };

    // ============================================================
    // 动画编辑器面板（主入口）
    // ============================================================
    class AnimationEditorPanel {
    public:
        AnimationEditorPanel();
        ~AnimationEditorPanel() = default;

        // ── 主渲染 ──
        void OnImGui();

        // ── 可见性 ──
        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const { return m_Visible; }

        // ── 数据驱动接口 ──
        /** 从 AssetBrowser 双击 AnimClip 时调用 */
        void OpenClip(const GUID& clipGuid);
        /** 从 AssetBrowser 双击 AnimStateMachine 时调用 */
        void OpenStateMachine(const GUID& smGuid);

        // ── 播放控制 ──
        void SetPlayhead(float time) { m_Playhead = time; }
        float GetPlayhead() const { return m_Playhead; }
        bool IsPlaying() const { return m_IsPlaying; }

        CurveEditor& GetCurveEditor() { return m_CurveEditor; }

    private:
        // ── 内部子面板 ──
        void DrawToolbar();
        void DrawSidebar();
        void DrawClipEditor();
        void DrawStateMachineEditor();
        void DrawPreviewViewport();

        // ── 工具 ──
        void UpdatePlayback(float dt);
        void PublishSelectionToInspector(const std::string& context);

        // ── 状态 ──
        bool           m_Visible = true;
        AnimEditorMode m_CurrentMode = AnimEditorMode::StateMachine;

        // ── 当前操作资源 ──
        GUID m_ActiveAssetGUID;     // 当前编辑的核心资源 GUID
        bool m_HasActiveAsset = false;

        // ── 画布状态机 ──
        CanvasTransform m_SMCanvas;
        CanvasTransform m_CurveCanvas;

        // ── 播放控制 ──
        float m_Playhead    = 0.0f;
        bool  m_IsPlaying   = false;
        bool  m_Looping     = true;
        float m_PlaybackSpeed = 1.0f;

        // ── 子编辑器 ──
        CurveEditor m_CurveEditor;

        // ── 状态机数据 ──
        std::vector<SMNodeData>       m_SMNodes;
        std::vector<SMTransitionData> m_SMTransitions;
        int  m_SelectedSMNode     = -1;
        int  m_SelectedSMTrans    = -1;

        // ── 拖拽连线状态 ──
        bool     m_IsDraggingTransition = false;
        uint32_t m_TransitionSourceId   = 0;

        // ── 动画片段缓存 ──
        std::vector<AnimClipData> m_Clips;
        int  m_CurrentClipIndex = -1;
        int  m_SelectedTrack    = -1;

        // ── 预览（骨架指针，由外部注入） ──
        ::Engine::Skeleton* m_PreviewSkeleton = nullptr;
    };

}} // namespace Engine::Animation