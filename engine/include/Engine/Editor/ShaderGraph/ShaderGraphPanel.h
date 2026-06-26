#pragma once

/**
 * @file ShaderGraphPanel.h
 * @brief 着色器图编辑器面板 — 节点画布、创建菜单、黑板、属性面板
 *
 * 提供类似于 Unity Shader Graph / Unreal Material Editor 的完整编辑体验：
 *   - 无限画布（缩放/平移）
 *   - 拖拽连线
 *   - 右键创建菜单（按分类分组）
 *   - 黑板系统（全局属性）
 *   - 代码预览
 *   - 实时预览在 ViewportPanel 中
 */

#include "ShaderGraphCore.h"
#include "ShaderNodes.h"
#include <imgui.h>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace Engine {
namespace ShaderGraph {

    /// 着色器图编辑器面板状态
    struct SGEditorState {
        bool showGrid = true;
        bool showCodePreview = false;
        bool showBlackboard = true;
        bool showProperties = true;
        bool showMinimap = false;
        std::string generatedCode;

        // 连接拖拽状态
        uint32 draggingFromPinId = 0;
        uint32 draggingFromNodeId = 0;
        PinDirection draggingFromDir = PinDirection::Output;
        bool isDraggingLink = false;

        // 选择
        uint32 selectedNodeId = 0;
        uint32 selectedLinkId = 0;

        // 右键菜单
        bool showCreateMenu = false;
        NodeCategory createMenuCategory = NodeCategory::Count;
    };

    /// 着色器图编辑器面板
    class ShaderGraphPanel {
    public:
        ShaderGraphPanel();
        ~ShaderGraphPanel() = default;

        /// 设置要编辑的图
        void SetGraph(ShaderGraph* graph) { m_Graph = graph; }
        ShaderGraph* GetGraph() const { return m_Graph; }

        /// 设置标题名称
        void SetTitle(const std::string& title) { m_Title = title; }

        /// 每帧绘制 ImGui（在 Editor DockSpace 内调用）
        void OnImGui();

        /// 生成 HLSL 代码
        bool GenerateShaderCode(std::string& outCode);

    private:
        // ── 内部绘制函数 ──
        void DrawNodeCanvas();
        void DrawNode(ShaderNode* node);
        void DrawNodePins(ShaderNode* node, bool isInput);
        void DrawLinks();
        void DrawCreateNodeMenu();
        void DrawBlackboard();
        void DrawCodePreview();
        void DrawPropertiesPanel();

        // ── 连线交互 ──
        void HandleLinkDragging();
        ImVec2 GetPinPosition(const ShaderNode* node, const Pin& pin) const;

        // ── 画布状态 ──
        ImVec2 m_CanvasOrigin = ImVec2(0, 0);
        float  m_CanvasScale = 1.0f;
        ImVec2 m_CanvasSize = ImVec2(0, 0);

        // ── 编辑器状态 ──
        SGEditorState m_State;
        ShaderGraph* m_Graph = nullptr;
        std::string m_Title = "Shader Graph Editor";

        // ── 节点颜色 ──
        static ImU32 GetNodeColor(NodeCategory cat);
        static ImU32 GetPinColor(PinType type);
    };

    // ══════════════════════════════════════════════════════════════
    // 代码生成（完整 HLSL 着色器）
    // ══════════════════════════════════════════════════════════════
    class ShaderCodeGenerator {
    public:
        /// 从图生成 HLSL 片段着色器代码
        static bool Generate(const ShaderGraph& graph, std::string& outCode);

    private:
        static void GenerateHeader(std::stringstream& ss);
        static void GenerateProperties(const ShaderGraph& graph, std::stringstream& ss);
        static void GenerateBody(const ShaderGraph& graph, std::stringstream& ss,
                                 std::unordered_map<uint32, std::string>& pinOutputs);
        static void GenerateInputStruct(std::stringstream& ss);
        static void GenerateOutputStruct(std::stringstream& ss);
    };

}} // namespace Engine::ShaderGraph