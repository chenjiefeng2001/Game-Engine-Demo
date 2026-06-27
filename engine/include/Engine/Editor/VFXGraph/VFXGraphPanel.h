#pragma once

/**
 * @file VFXGraphPanel.h
 * @brief VFX 图编辑器面板 — 工业级节点编辑器
 *
 * 核心面板设计：
 *   - 左栏：Block 目录浏览器 + 参数黑板
 *   - 中栏：节点画布（五大生命周期 Context 容器 + Block 拖入）
 *   - 右栏：属性面板 + Block 指令统计
 *   - 浮动窗口：实时预览视口 + 粒子计数分析
 *   - 节点级错误反馈（红框 + 感叹号标记）
 *   - 框选 + 分组 (Group)
 *
 * 遵循 RHI 抽象原则：不直接创建 GPU 资源
 */

#include "VFXGraphCore.h"
#include "VFXNodes.h"
#include <imgui.h>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <cmath>

namespace Engine {
namespace VFX {

    // ============================================================
    // 编辑器状态
    // ============================================================
    struct VFXEditorState {
        bool showGrid = true;
        bool showBlockList = true;
        bool showBlackboard = true;
        bool showProperties = true;
        bool showPreview = true;
        bool showParticleStats = true;
        bool showFlowAnimation = true;

        // 连接拖拽
        uint32 draggingFromPinId = 0;
        uint32 draggingFromBlockId = 0;
        PinDirection draggingFromDir = PinDirection::Output;
        bool isDraggingLink = false;

        // 选择
        uint32 selectedBlockId = 0;
        uint32 selectedLinkId = 0;
        std::vector<uint32> multiSelectedBlockIds;

        // 右键菜单
        bool showCreateMenu = false;
        float createMenuPosX = 0, createMenuPosY = 0;

        // 搜索
        char quickSearchBuf[128] = "";
        bool showQuickSearch = false;
        float quickSearchPosX = 0, quickSearchPosY = 0;

        // 预览悬浮
        uint32 hoveredBlockId = 0;

        // 编译状态
        std::vector<VFXCompileError> compileErrors;
        uint32 instructionCount = 0;
        std::string generatedCode;
        bool showCodePreview = false;
    };

    // ============================================================
    // 分组框 (Group)
    // ============================================================
    struct VFXNodeGroup {
        uint32 id;
        std::string name = "Comment Group";
        ImColor color = ImColor(100, 100, 200, 50);
        float posX = 0, posY = 0;
        float sizeX = 300, sizeY = 200;
        std::vector<uint32> memberBlockIds;
        bool collapsed = false;
    };

    // ============================================================
    // VFX 图编辑器面板
    // ============================================================
    class VFXGraphPanel {
    public:
        VFXGraphPanel();
        ~VFXGraphPanel() = default;

        void SetGraph(VFXGraph* graph) { m_Graph = graph; }
        VFXGraph* GetGraph() const { return m_Graph; }
        void SetTitle(const std::string& title) { m_Title = title; }
        void OnImGui();

        bool GenerateVFXCode(std::string& outCode);

    private:
        // ── 画布系统 ──
        void DrawToolbar();
        void DrawBlockListPanel();
        void DrawNodeCanvas();
        void DrawMainEditorLayout();
        void DrawDotGrid(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& size);
        void DrawContextContainer(VFXContext& ctx);
        void DrawBlockNode(VFXBlock* block);
        void DrawBlockPins(VFXBlock* block, bool isInput);
        void DrawLinks();
        void DrawNodeGroupBoxes();

        // ── 交互系统 ──
        void HandleLinkDragging();
        void HandleMultiselect();
        void HandleCreateGroup();
        void FindHoveredPin(ImVec2 mousePos, uint32& outBlockId, uint32& outPinId, PinDirection& outDir) const;
        void HandleCreateMenu();
        void HandleDragDropToContext();

        // ── 面板系统 ──
        void DrawBlackboard();
        void DrawPropertiesPanel();
        void DrawPreviewPanel();
        void DrawParticleStats();

        // ── 辅助 ──
        ImVec2 GetPinPosition(const VFXBlock* block, const VFXPin& pin) const;
        ImVec2 WorldToScreen(float wx, float wy) const;
        ImVec2 ScreenToWorld(float sx, float sy) const;
        static ImU32 ColorFromInt(int r, int g, int b, int a = 255);
        ImU32 GetBlockColor(BlockCategory cat) const;
        ImU32 GetPinColor(VFXPinType type, bool connected) const;
        ImU32 GetContextColor(ContextType type) const;

        // ── 画布状态 ──
        ImVec2 m_CanvasOrigin = ImVec2(0, 0);
        float  m_CanvasScale = 1.0f;
        ImVec2 m_CanvasSize = ImVec2(0, 0);
        ImVec2 m_CanvasScreenPos = ImVec2(0, 0);

        // ── 编辑器状态 ──
        VFXEditorState m_State;
        VFXGraph* m_Graph = nullptr;
        std::string m_Title = "VFX Graph Editor";

        // ── 上下文编辑 ──
        ContextType m_EditingContext = ContextType::Spawn;
        bool m_ShowContextSettings = false;

        // ── 分组系统 ──
        std::vector<VFXNodeGroup> m_Groups;
        uint32 m_NextGroupId = 1;

        // ── 框选状态 ──
        bool m_IsBoxSelecting = false;
        ImVec2 m_BoxSelectStart;
        ImVec2 m_BoxSelectEnd;

        // ── 颜色常量 ──
        static constexpr ImU32 ColorBlockSpawn     = IM_COL32(60, 140, 80, 255);
        static constexpr ImU32 ColorBlockInit      = IM_COL32(60, 110, 150, 255);
        static constexpr ImU32 ColorBlockUpdate    = IM_COL32(130, 70, 140, 255);
        static constexpr ImU32 ColorBlockOutput    = IM_COL32(160, 80, 60, 255);
        static constexpr ImU32 ColorBlockUtility   = IM_COL32(80, 80, 100, 255);

        static constexpr ImU32 ColorContextSpawn   = IM_COL32(30, 80, 50, 40);
        static constexpr ImU32 ColorContextInit    = IM_COL32(30, 60, 90, 40);
        static constexpr ImU32 ColorContextUpdate  = IM_COL32(70, 35, 80, 40);
        static constexpr ImU32 ColorContextOutput  = IM_COL32(90, 40, 30, 40);
        static constexpr ImU32 ColorContextSystem  = IM_COL32(40, 40, 50, 40);

        static constexpr ImU32 ColorPinFloat       = IM_COL32(150, 200, 255, 255);
        static constexpr ImU32 ColorPinFloat2      = IM_COL32(180, 230, 100, 255);
        static constexpr ImU32 ColorPinFloat3      = IM_COL32(255, 220, 80, 255);
        static constexpr ImU32 ColorPinFloat4      = IM_COL32(255, 150, 200, 255);
        static constexpr ImU32 ColorPinTexture     = IM_COL32(255, 180, 80, 255);
        static constexpr ImU32 ColorPinBool        = IM_COL32(200, 100, 100, 255);
        static constexpr ImU32 ColorPinParticle    = IM_COL32(180, 100, 220, 255);
        static constexpr ImU32 ColorPinConnected   = IM_COL32(100, 200, 100, 255);

        // 搜索 Block 列表
        void DrawBlockSearchPanel();
        std::vector<int> m_FilteredBlockIndices;
        char m_SearchBuf[128] = "";

        // 延迟删除
        std::vector<uint32> m_PendingDeleteBlockIds;
        void ProcessPendingDeletions();
    };

}} // namespace Engine::VFX