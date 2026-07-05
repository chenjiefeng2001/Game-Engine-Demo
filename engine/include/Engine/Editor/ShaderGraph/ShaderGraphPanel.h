#pragma once

/**
 * @file ShaderGraphPanel.h
 * @brief 工业级着色器图编辑器面板 — 节点画布、创建菜单、黑板、属性面板、实时预览
 *
 * 工业级特性（对标 Unreal Material Editor / Unity Shader Graph）：
 *   - 无限点阵网格画布（缩放/平移/对齐辅助）
 *   - 基于数据类型的端口色彩语义（Float=蓝, Vec2=绿, Vec3=黄, Vec4/Color=粉, Texture=橙）
 *   - 贝塞尔曲线连线 + 流动动画指示数据流向
 *   - 空格键极速搜索创建节点（模糊搜索+别名支持）
 *   - 智能类型转换与自动连线
 *   - 节点组框（Group Box）+ 折叠/展开
 *   - 节点对齐工具（顶部对齐/水平等距分布）
 *   - 子图系统（将节点逻辑打包复用）
 *   - 自定义 HLSL 代码节点
 *   - 性能剖析与指令统计
 *   - 每节点微预览（鼠标悬浮显示中间结果）
 */

#include "ShaderGraphCore.h"
#include "ShaderNodes.h"
#include <imgui.h>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <cmath>

namespace Engine {
namespace ShaderGraph {

    // ============================================================
    // 节点组框
    // ============================================================
    struct NodeGroup {
        uint32 id;
        std::string name = "Group";
        ImColor color = ImColor(100, 100, 200, 60);
        float posX = 0, posY = 0;
        float sizeX = 300, sizeY = 200;
        std::vector<uint32> memberNodeIds;
        bool collapsed = false;
    };

    // ============================================================
    // 编辑器增强状态
    // ============================================================
    struct SGEditorState {
        bool showGrid = true;
        bool showCodePreview = false;
        bool showBlackboard = true;
        bool showProperties = true;
        bool showMinimap = false;
        bool showFlowAnimation = true;
        bool showInstructionCount = true;
        std::string generatedCode;
        uint32 instructionCount = 0;
        uint32 textureSampleCount = 0;
        std::vector<std::string> compileErrors;

        // 连接拖拽状态
        uint32 draggingFromPinId = 0;
        uint32 draggingFromNodeId = 0;
        PinDirection draggingFromDir = PinDirection::Output;
        bool isDraggingLink = false;

        // 选择
        uint32 selectedNodeId = 0;
        uint32 selectedLinkId = 0;
        uint32 selectedGroupId = 0;
        std::vector<uint32> multiSelectedNodeIds;

        // 右键菜单
        bool showCreateMenu = false;

        // 空格键搜索
        char quickSearchBuf[128] = "";
        bool showQuickSearch = false;
        float quickSearchPosX = 0, quickSearchPosY = 0;

        // 节点微预览 — 鼠标悬浮的节点
        uint32 hoveredNodeId = 0;
        bool showNodePreview = false;

        // 子图编辑
        bool editingSubgraph = false;
        std::string subgraphPath;
    };

    // ============================================================
    // 着色器图编辑器面板（工业级）
    // ============================================================
    class ShaderGraphPanel {
    public:
        ShaderGraphPanel();
        ~ShaderGraphPanel() = default;

        void SetGraph(ShaderGraph* graph) { m_Graph = graph; }
        ShaderGraph* GetGraph() const { return m_Graph; }
        void SetTitle(const std::string& title) { m_Title = title; }
        void OnImGui();
        bool GenerateShaderCode(std::string& outCode);

    private:
        // ── 画布系统 ──
        void DrawToolbar();
        void DrawNodeCanvas();
        void DrawMainEditorLayout();
        void DrawDotGrid(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& size);
        void DrawNode(ShaderNode* node);
        void DrawNodePins(ShaderNode* node, bool isInput);
        void DrawLinks();
        void DrawNodeGroupBoxes();
        void DrawRerouteNodes();

        // ── 交互系统 ──
        void HandleLinkDragging();
        void HandleMultiselect();
        void HandleQuickSearch();
        void DrawCreateNodeMenu();
        bool FindHoveredPin(ImVec2 mousePos, uint32& outNodeId, uint32& outPinId, PinDirection& outDir) const;
        bool IsMouseNearPin(ImVec2 pinPos, float threshold = 12.0f);

        // ── 面板系统 ──
        void DrawBlackboard();
        void DrawCodePreview();
        void DrawPropertiesPanel();
        void DrawQuickSearchOverlay(const ImVec2& canvasPos);
        void DrawInstructionStats();

        // ── 对齐工具 ──
        void AlignNodesTop();
        void AlignNodesLeft();
        void DistributeNodesHorizontally();
        void DistributeNodesVertically();

        // ── 节点微预览 ──
        void DrawNodeMiniPreview(ShaderNode* node, const ImVec2& nodePos);

        // ── 辅助 ──
        ImVec2 GetPinPosition(const ShaderNode* node, const Pin& pin) const;
        ImVec2 WorldToScreen(float wx, float wy) const;
        ImVec2 ScreenToWorld(float sx, float sy) const;
        static ImU32 ColorFromInt(int r, int g, int b, int a = 255);

        // ── 画布状态 ──
        ImVec2 m_CanvasOrigin = ImVec2(0, 0);
        float  m_CanvasScale = 1.0f;
        ImVec2 m_CanvasSize = ImVec2(0, 0);
        ImVec2 m_CanvasScreenPos = ImVec2(0, 0);

        // ── 编辑器状态 ──
        SGEditorState m_State;
        ShaderGraph* m_Graph = nullptr;
        std::string m_Title = "Shader Graph Editor";

        // ── 组框 ──
        std::vector<NodeGroup> m_Groups;
        uint32 m_NextGroupId = 1;
        bool m_DraggingGroupBorder = false;
        uint32 m_DraggingGroupId = 0;

        // ── 多选框选 ──
        bool m_IsBoxSelecting = false;
        ImVec2 m_BoxSelectStart;
        ImVec2 m_BoxSelectEnd;

        // ── 节点颜色 ──
        static constexpr ImU32 ColorNodeMath   = IM_COL32(60, 90, 140, 255);
        static constexpr ImU32 ColorNodeInput  = IM_COL32(60, 130, 80, 255);
        static constexpr ImU32 ColorNodeVector = IM_COL32(130, 70, 140, 255);
        static constexpr ImU32 ColorNodeArt    = IM_COL32(140, 100, 60, 255);
        static constexpr ImU32 ColorNodeTex    = IM_COL32(60, 110, 120, 255);
        static constexpr ImU32 ColorNodeMaster = IM_COL32(160, 60, 60, 255);

        // ── Pin 颜色（数据类型语义） ──
        static constexpr ImU32 ColorPinFloat       = IM_COL32(150, 200, 255, 255);
        static constexpr ImU32 ColorPinFloat2      = IM_COL32(180, 230, 100, 255);
        static constexpr ImU32 ColorPinFloat3      = IM_COL32(255, 220, 80, 255);
        static constexpr ImU32 ColorPinFloat4      = IM_COL32(255, 150, 200, 255);
        static constexpr ImU32 ColorPinColor       = IM_COL32(255, 150, 200, 255);
        static constexpr ImU32 ColorPinTexture     = IM_COL32(255, 180, 80, 255);
        static constexpr ImU32 ColorPinBool        = IM_COL32(200, 100, 100, 255);
        static constexpr ImU32 ColorPinConnected   = IM_COL32(100, 200, 100, 255);

        ImU32 GetPinColor(PinType type, bool connected) const;
        ImU32 GetNodeColor(NodeCategory cat) const;
    };

}} // namespace Engine::ShaderGraph