/**
 * @file VFXGraphPanel.cpp
 * @brief VFX 图编辑器面板 — 完整实现
 *
 * 实现特性：
 *   - 五大生命周期上下文容器（Spawn/Initialize/Update/Output/System）
 *   - Block 拖入 & 引脚连线（数据流 + 粒子数据流）
 *   - 左栏 Block 目录浏览器 + 搜索
 *   - 黑板参数面板
 *   - 属性编辑面板
 *   - 实时预览视口
 *   - 粒子计数/指令统计
 */

#include "Engine/Editor/VFXGraph/VFXGraphPanel.h"
#include "Engine/Editor/VFXGraph/VFXNodeFactory.h"
#include "Engine/Editor/VFXGraph/VFXCompiler.h"
#include "Engine/Editor/VFXGraph/VFXUndoManager.h"
#include "Engine/Editor/IconsFontAwesome6.h"
#include "Engine/Platform/FileDialog.h"
#include <imgui_internal.h>
#include <algorithm>
#include <sstream>

namespace Engine {
namespace VFX {

    // ============================================================
    // 颜色工具
    // ============================================================

    ImU32 VFXGraphPanel::ColorFromInt(int r, int g, int b, int a) {
        return IM_COL32(r, g, b, a);
    }

    ImU32 VFXGraphPanel::GetBlockColor(BlockCategory cat) const {
        ContextType ctx = BlockCategoryToContext(cat);
        switch (ctx) {
            case ContextType::Spawn:      return ColorBlockSpawn;
            case ContextType::Initialize: return ColorBlockInit;
            case ContextType::Update:     return ColorBlockUpdate;
            case ContextType::Output:     return ColorBlockOutput;
            default:                      return ColorBlockUtility;
        }
    }

    ImU32 VFXGraphPanel::GetContextColor(ContextType type) const {
        switch (type) {
            case ContextType::Spawn:      return ColorContextSpawn;
            case ContextType::Initialize: return ColorContextInit;
            case ContextType::Update:     return ColorContextUpdate;
            case ContextType::Output:     return ColorContextOutput;
            case ContextType::System:     return ColorContextSystem;
            default:                      return ColorContextSystem;
        }
    }

    ImU32 VFXGraphPanel::GetPinColor(VFXPinType type, bool connected) const {
        // 未连接的引脚颜色会变暗，连线颜色使用明亮的基色
        switch (type) {
            case VFXPinType::Float:        return connected ? IM_COL32(100, 180, 255, 255) : IM_COL32(60, 100, 150, 255);
            case VFXPinType::Float2:       return connected ? IM_COL32(180, 230, 100, 255) : IM_COL32(100, 150, 60, 255);
            case VFXPinType::Float3:       return connected ? IM_COL32(255, 220, 80, 255)  : IM_COL32(160, 140, 50, 255);
            case VFXPinType::Float4:       return connected ? IM_COL32(255, 150, 200, 255) : IM_COL32(150, 80, 120, 255);
            case VFXPinType::Color:        return connected ? IM_COL32(255, 150, 200, 255) : IM_COL32(150, 80, 120, 255);
            case VFXPinType::Texture2D:    return connected ? IM_COL32(255, 180, 80, 255)  : IM_COL32(160, 110, 50, 255);
            case VFXPinType::Texture3D:    return connected ? IM_COL32(255, 180, 80, 255)  : IM_COL32(160, 110, 50, 255);
            case VFXPinType::Boolean:      return connected ? IM_COL32(200, 100, 100, 255) : IM_COL32(120, 60, 60, 255);
            case VFXPinType::ParticleData: return connected ? IM_COL32(150, 255, 150, 255) : IM_COL32(80, 150, 80, 255);
            default:                       return ColorFromInt(180, 180, 180, 255);
        }
    }

    // ============================================================
    // 构造
    // ============================================================

    VFXGraphPanel::VFXGraphPanel() {
        m_CanvasScale = 1.0f;
        RegisterAllVFXBlocks();
    }

    // ============================================================
    // OnImGui — 主入口
    // ============================================================

    void VFXGraphPanel::OnImGui() {
        ImGui::Begin(m_Title.c_str(), nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        DrawToolbar();

        // 编译错误栏
        if (!m_State.compileErrors.empty()) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.3f, 0.1f, 0.1f, 1.0f));
            if (ImGui::BeginChild("VFXErrorBanner", ImVec2(0, 28), false, ImGuiWindowFlags_NoScrollbar)) {
                ImGui::SetCursorPos(ImVec2(10, 6));
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                    ICON_FA_EXCLAMATION_TRIANGLE " %s",
                    m_State.compileErrors[0].message.c_str());
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        ImGui::Separator();

        // Tab 栏
        if (ImGui::BeginTabBar("VFXTabBar", ImGuiTabBarFlags_Reorderable)) {
            bool isOpen = true;
            ImGuiTabItemFlags flags = (m_Graph && m_Graph->IsDirty())
                ? ImGuiTabItemFlags_UnsavedDocument : 0;
            if (ImGui::BeginTabItem("VFX Effect", &isOpen, flags)) {
                DrawMainEditorLayout();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        // 浮动预览窗口
        if (m_State.showPreview) {
            DrawPreviewPanel();
        }
        if (m_State.showParticleStats) {
            DrawParticleStats();
        }

        ImGui::End();
    }

    // ============================================================
    // DrawToolbar — 工具栏
    // ============================================================

    void VFXGraphPanel::DrawToolbar() {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

        if (ImGui::Button(ICON_FA_SAVE " Save")) {
            if (m_Graph) {
                std::string path = FileDialog::SaveFile("VFX Graph (*.vfxgraph)\0*.vfxgraph\0", "vfxgraph");
                if (!path.empty()) {
                    m_Graph->SaveToFile(path);
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FOLDER_OPEN " Load")) {
            if (m_Graph) {
                std::string path = FileDialog::OpenFile("VFX Graph (*.vfxgraph)\0*.vfxgraph\0");
                if (!path.empty()) {
                    m_Graph->LoadFromFile(path);
                    m_CanvasOrigin = ImVec2(0, 0);
                    m_State.selectedBlockId = 0;
                    m_State.multiSelectedBlockIds.clear();
                }
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled(" | ");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PLAY " Play")) {
            if (m_Graph) {
                // 触发预览播放
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_STOP " Stop")) {
            if (m_Graph) {
                // 停止预览
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_CODE " Compile")) {
            m_State.compileErrors.clear();
            if (m_Graph) {
                auto result = VFXCompiler::Compile(m_Graph);
                m_State.compileErrors = result.errors;
                m_State.instructionCount = result.instructionCount;
                m_State.generatedCode = result.hlslCode;
                if (result.success) {
                    m_State.showCodePreview = true;
                }
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled(" | ");
        ImGui::SameLine();
        auto& undoMgr = VFXUndoManager::Get();
        if (undoMgr.CanUndo()) {
            if (ImGui::Button(ICON_FA_UNDO " Undo")) {
                undoMgr.Undo(m_Graph);
            }
        } else {
            ImGui::TextDisabled(ICON_FA_UNDO " Undo");
        }
        ImGui::SameLine();
        if (undoMgr.CanRedo()) {
            if (ImGui::Button(ICON_FA_REDO " Redo")) {
                undoMgr.Redo(m_Graph);
            }
        } else {
            ImGui::TextDisabled(ICON_FA_REDO " Redo");
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_GRID " Grid")) { m_State.showGrid = !m_State.showGrid; }

        float textWidth = ImGui::CalcTextSize("Blocks: 999").x;
        ImGui::SameLine(ImGui::GetWindowWidth() - textWidth - 20);
        ImGui::TextDisabled("Blocks: %zu", m_Graph ? m_Graph->GetBlocks().size() : 0);

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    // ============================================================
    // DrawMainEditorLayout — 三栏布局
    // ============================================================

    void VFXGraphPanel::DrawMainEditorLayout() {
        if (ImGui::BeginTable("VFXLTable", 3,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
        {
            ImGui::TableSetupColumn("Block List", ImGuiTableColumnFlags_WidthFixed, 220.0f);
            ImGui::TableSetupColumn("Canvas", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthFixed, 250.0f);
            ImGui::TableNextRow();

            // 左栏：Block 目录 + 黑板
            ImGui::TableSetColumnIndex(0);
            if (ImGui::BeginTabBar("LeftTabs")) {
                if (ImGui::BeginTabItem("Blocks")) {
                    DrawBlockSearchPanel();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Blackboard")) {
                    DrawBlackboard();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }

            // 中栏：节点画布
            ImGui::TableSetColumnIndex(1);
            DrawNodeCanvas();

            // 右栏：属性
            ImGui::TableSetColumnIndex(2);
            if (m_State.showProperties) DrawPropertiesPanel();

            ImGui::EndTable();
        }
    }

    // ============================================================
    // DrawBlockSearchPanel — Block 搜索/浏览面板
    // ============================================================

    void VFXGraphPanel::DrawBlockSearchPanel() {
        ImGui::TextDisabled("Search Blocks:");
        ImGui::InputText("##blocksearch", m_SearchBuf, sizeof(m_SearchBuf));
        ImGui::Separator();

        std::string searchStr = m_SearchBuf;
        std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);

        auto& factoryList = GetVFXBlockFactoryList();

        std::string currentGroup;
        for (int i = 0; i < (int)factoryList.size(); ++i) {
            auto& entry = factoryList[i];

            if (!searchStr.empty()) {
                std::string lowerName = entry.name;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                if (lowerName.find(searchStr) == std::string::npos) continue;
            }

            std::string groupName = entry.group;
            if (groupName != currentGroup) {
                currentGroup = groupName;
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "── %s ──", groupName.c_str());
            }

            ImGui::PushID(i);
            ImGui::Selectable(entry.name, false, ImGuiSelectableFlags_AllowOverlap);

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload("VFX_BLOCK_CREATE_PAYLOAD", &i, sizeof(int));
                ImGui::Text(ICON_FA_CUBE " Create %s", entry.name);
                ImGui::EndDragDropSource();
            }
            ImGui::PopID();
        }
    }

    // ============================================================
    // WorldToScreen / ScreenToWorld
    // ============================================================

    ImVec2 VFXGraphPanel::WorldToScreen(float wx, float wy) const {
        return ImVec2(
            (wx + m_CanvasOrigin.x) * m_CanvasScale + m_CanvasScreenPos.x,
            (wy + m_CanvasOrigin.y) * m_CanvasScale + m_CanvasScreenPos.y
        );
    }

    ImVec2 VFXGraphPanel::ScreenToWorld(float sx, float sy) const {
        return ImVec2(
            (sx - m_CanvasScreenPos.x) / m_CanvasScale - m_CanvasOrigin.x,
            (sy - m_CanvasScreenPos.y) / m_CanvasScale - m_CanvasOrigin.y
        );
    }

    // ============================================================
    // DrawNodeCanvas — 核心画布
    // ============================================================

    void VFXGraphPanel::DrawNodeCanvas() {
        m_CanvasSize = ImGui::GetContentRegionAvail();
        m_CanvasScreenPos = ImGui::GetCursorScreenPos();
        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // ── 0. 全局滚轮缩放 ──
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && io.MouseWheel != 0) {
            ImVec2 mousePos = ImGui::GetMousePos();
            ImVec2 worldBefore = ScreenToWorld(mousePos.x, mousePos.y);
            m_CanvasScale = std::clamp(m_CanvasScale + io.MouseWheel * 0.05f * m_CanvasScale, 0.15f, 3.0f);
            ImVec2 worldAfter = ScreenToWorld(mousePos.x, mousePos.y);
            m_CanvasOrigin.x += (worldAfter.x - worldBefore.x);
            m_CanvasOrigin.y += (worldAfter.y - worldBefore.y);
        }

        // ── 全局 Ctrl+A 全选 ──
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A)) {
            m_State.multiSelectedBlockIds.clear();
            if (m_Graph) {
                for (auto& [id, block] : m_Graph->GetBlocks()) {
                    m_State.multiSelectedBlockIds.push_back(id);
                }
            }
        }

        // ── 1. 底层：背景与网格 ──
        drawList->AddRectFilled(m_CanvasScreenPos,
            ImVec2(m_CanvasScreenPos.x + m_CanvasSize.x, m_CanvasScreenPos.y + m_CanvasSize.y),
            ColorFromInt(30, 30, 30));
        if (m_State.showGrid) DrawDotGrid(drawList, m_CanvasScreenPos, m_CanvasSize);

        // ── 2. 画布交互底板 ──
        ImGui::SetCursorScreenPos(m_CanvasScreenPos);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("VFXCanvas", m_CanvasSize,
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

        bool canvasHovered = ImGui::IsItemHovered();
        bool canvasActive = ImGui::IsItemActive();
        bool canvasClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        ImGuiID canvasId = ImGui::GetID("VFXCanvas");

        // DND 目标：支持从左侧/黑板直接拖拽到画布空白处
        if (m_Graph && ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VFX_BLOCK_CREATE_PAYLOAD")) {
                int factoryIdx = *(const int*)payload->Data;
                auto& factoryList = GetVFXBlockFactoryList();
                if (factoryIdx >= 0 && factoryIdx < (int)factoryList.size()) {
                    uint32 newId = m_Graph->AddBlock(factoryList[factoryIdx].factory(0));
                    auto* newBlock = m_Graph->GetBlock(newId);
                    if (newBlock) {
                        ImVec2 dropPos = ScreenToWorld(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
                        newBlock->SetPosition(dropPos.x, dropPos.y);
                        ContextType ctxType = BlockCategoryToContext(factoryList[factoryIdx].category);
                        m_Graph->GetOrCreateContext(ctxType).blockIds.push_back(newId);
                    }
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VFX_BLACKBOARD_PROP")) {
                int propIdx = *(const int*)payload->Data;
                auto& props = m_Graph->GetProperties();
                if (propIdx >= 0 && propIdx < (int)props.size()) {
                    auto& targetProp = props[propIdx];
                    uint32 newId = m_Graph->AddBlock(std::make_unique<VFXPropertyNode>(0, targetProp.name, targetProp.type));
                    auto* newBlock = m_Graph->GetBlock(newId);
                    if (newBlock) {
                        ImVec2 dropPos = ScreenToWorld(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
                        newBlock->SetPosition(dropPos.x, dropPos.y);
                        m_Graph->GetOrCreateContext(ContextType::System).blockIds.push_back(newId);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        // 点击画布空白处取消所有选中（不按Ctrl的情况下）
        if (canvasClicked && !io.KeyCtrl && GImGui->HoveredId == canvasId) {
            m_State.selectedBlockId = 0;
            m_State.selectedLinkId = 0;
            m_State.multiSelectedBlockIds.clear();
        }

        // 开始框选：在画布上按住左键拖动且没按 Alt
        if (canvasActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !io.KeyAlt) {
            if (!m_IsBoxSelecting) {
                m_IsBoxSelecting = true;
                m_BoxSelectStart = io.MouseClickedPos[0];
                if (!io.KeyCtrl) m_State.multiSelectedBlockIds.clear();
            }
        }

        // ── 3. 裁剪区绘制内部元素 ──
        drawList->PushClipRect(m_CanvasScreenPos,
            ImVec2(m_CanvasScreenPos.x + m_CanvasSize.x, m_CanvasScreenPos.y + m_CanvasSize.y), true);

        DrawNodeGroupBoxes();

        if (m_Graph) {
            for (auto& ctx : m_Graph->GetContexts()) {
                if (ctx.visible) DrawContextContainer(ctx);
            }
        }

        if (m_Graph) DrawLinks();

        if (m_Graph) {
            // 注意：不在此处clear，防止右键菜单 Push 的待删除被清空
            // 延迟删除由 ProcessPendingDeletions 在帧末尾统一处理并清理列表
            for (auto& [id, block] : m_Graph->GetBlocks()) {
                DrawBlockNode(block.get());
            }

            // Block 磁吸更新逻辑
            for (auto& [id, block] : m_Graph->GetBlocks()) {
                float cx = block->GetPosX() + block->GetSizeX() * 0.5f;
                float cy = block->GetPosY() + block->GetSizeY() * 0.5f;
                for (auto& ctx : m_Graph->GetContexts()) {
                    if (!ctx.visible) continue;
                    if (cx >= ctx.posX && cx <= ctx.posX + ctx.sizeX &&
                        cy >= ctx.posY && cy <= ctx.posY + ctx.sizeY) {
                        auto it = std::find(ctx.blockIds.begin(), ctx.blockIds.end(), id);
                        if (it == ctx.blockIds.end()) {
                            for (auto& otherCtx : m_Graph->GetContexts()) {
                                if (&otherCtx != &ctx) {
                                    auto oit = std::find(otherCtx.blockIds.begin(), otherCtx.blockIds.end(), id);
                                    if (oit != otherCtx.blockIds.end()) otherCtx.blockIds.erase(oit);
                                }
                            }
                            ctx.blockIds.push_back(id);
                        }
                        break;
                    }
                }
            }
        }

        // ── 4. 交互 ──
        HandleMultiselect();
        HandleCreateGroup();
        HandleLinkDragging();

        // 中键平移 / Alt+左键平移
        if (canvasActive || io.KeyAlt) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
                (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && io.KeyAlt)) {
                m_CanvasOrigin.x += io.MouseDelta.x / m_CanvasScale;
                m_CanvasOrigin.y += io.MouseDelta.y / m_CanvasScale;
            }
        }

        // 右键菜单
        if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            m_State.createMenuPosX = ImGui::GetMousePos().x;
            m_State.createMenuPosY = ImGui::GetMousePos().y;
            ImGui::OpenPopup("VFXCtxCreate");
        }

        if (!m_PendingDeleteBlockIds.empty()) ProcessPendingDeletions();
        drawList->PopClipRect();
        HandleCreateMenu();
    }

    // ============================================================
    // DrawDotGrid
    // ============================================================
    void VFXGraphPanel::DrawDotGrid(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& size) {
        float gridSize = 20.0f * m_CanvasScale;
        float majorGridSize = gridSize * 5.0f;
        ImVec2 origin = ImVec2(canvasPos.x + m_CanvasOrigin.x * m_CanvasScale,
                               canvasPos.y + m_CanvasOrigin.y * m_CanvasScale);

        for (float x = fmodf(origin.x, gridSize); x < size.x; x += gridSize) {
            drawList->AddLine(ImVec2(canvasPos.x + x, canvasPos.y), ImVec2(canvasPos.x + x, canvasPos.y + size.y), ColorFromInt(45, 45, 45, 100));
        }
        for (float y = fmodf(origin.y, gridSize); y < size.y; y += gridSize) {
            drawList->AddLine(ImVec2(canvasPos.x, canvasPos.y + y), ImVec2(canvasPos.x + size.x, canvasPos.y + y), ColorFromInt(45, 45, 45, 100));
        }
        for (float x = fmodf(origin.x, majorGridSize); x < size.x; x += majorGridSize) {
            drawList->AddLine(ImVec2(canvasPos.x + x, canvasPos.y), ImVec2(canvasPos.x + x, canvasPos.y + size.y), ColorFromInt(60, 60, 60, 200));
        }
        for (float y = fmodf(origin.y, majorGridSize); y < size.y; y += majorGridSize) {
            drawList->AddLine(ImVec2(canvasPos.x, canvasPos.y + y), ImVec2(canvasPos.x + size.x, canvasPos.y + y), ColorFromInt(60, 60, 60, 200));
        }
    }

    // ============================================================
    // DrawContextContainer — 包含 Context 删除选项
    // ============================================================
    void VFXGraphPanel::DrawContextContainer(VFXContext& ctx) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float scale = m_CanvasScale;

        ImVec2 p0 = WorldToScreen(ctx.posX, ctx.posY);
        ImVec2 p1 = ImVec2(p0.x + ctx.sizeX * scale, p0.y + ctx.sizeY * scale);
        float headerH = 26.0f * scale;

        drawList->AddRectFilled(p0, p1, GetContextColor(ctx.type), 8.0f * scale);
        drawList->AddRect(p0, p1, ColorFromInt(100, 100, 120, 200), 8.0f * scale, 0, 1.5f * scale);
        drawList->AddRectFilled(p0, ImVec2(p1.x, p0.y + headerH), ColorFromInt(40, 40, 55, 200), 8.0f * scale, ImDrawFlags_RoundCornersTop);

        ImGui::PushID(&ctx);

        // 标题栏交互
        ImGui::SetCursorScreenPos(p0);
        ImGui::InvisibleButton("header", ImVec2(ctx.sizeX * scale, headerH));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ctx.posX += ImGui::GetIO().MouseDelta.x / scale;
            ctx.posY += ImGui::GetIO().MouseDelta.y / scale;
            if (m_Graph) m_Graph->MarkDirty();
        }

        // 删除 Context 菜单
        if (ImGui::BeginPopupContextItem("CtxContextMenu")) {
            if (ImGui::MenuItem(ICON_FA_TRASH " Delete Context")) {
                ctx.blockIds.clear();
                ctx.visible = false;
                if (m_Graph) m_Graph->RemoveContext(ctx.type);
            }
            ImGui::EndPopup();
        }

        // 缩放手柄
        ImVec2 br = ImVec2(p1.x, p1.y);
        ImGui::SetCursorScreenPos(ImVec2(br.x - 15 * scale, br.y - 15 * scale));
        ImGui::InvisibleButton("resize", ImVec2(15 * scale, 15 * scale));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ctx.sizeX += ImGui::GetIO().MouseDelta.x / scale;
            ctx.sizeY += ImGui::GetIO().MouseDelta.y / scale;
            ctx.sizeX = std::max(ctx.sizeX, 120.0f);
            ctx.sizeY = std::max(ctx.sizeY, 60.0f);
            if (m_Graph) m_Graph->MarkDirty();
        }
        drawList->AddLine(ImVec2(br.x - 5 * scale, br.y - 2 * scale), ImVec2(br.x - 2 * scale, br.y - 5 * scale), IM_COL32_WHITE);
        drawList->AddLine(ImVec2(br.x - 10 * scale, br.y - 2 * scale), ImVec2(br.x - 2 * scale, br.y - 10 * scale), IM_COL32_WHITE);

        ImGui::PopID();

        ImVec2 titleSize = ImGui::CalcTextSize(ctx.name.c_str());
        drawList->AddText(ImVec2(p0.x + 10 * scale, p0.y + (headerH - titleSize.y) * 0.5f), ColorFromInt(220, 220, 240), ctx.name.c_str());

        char countStr[32];
        snprintf(countStr, sizeof(countStr), "[%zu blocks]", ctx.blockIds.size());
        drawList->AddText(ImVec2(p1.x - ImGui::CalcTextSize(countStr).x - 10 * scale, p0.y + (headerH - titleSize.y) * 0.5f), ColorFromInt(150, 150, 170), countStr);
    }

    // ============================================================
    // DrawBlockNode — 支持 Ctrl 多选与多选拖拽
    // ============================================================

    void VFXGraphPanel::DrawBlockNode(VFXBlock* block) {
        if (!block) return;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float scale = m_CanvasScale;
        bool showDetail = (scale > 0.6f);
        bool showTitle = (scale > 0.35f);

        ImVec2 pos(block->GetPosX() * scale + m_CanvasScreenPos.x + m_CanvasOrigin.x * scale,
                   block->GetPosY() * scale + m_CanvasScreenPos.y + m_CanvasOrigin.y * scale);

        float nodeW = block->GetSizeX() * scale;
        float headerH = 28 * scale;
        float pinStartY = headerH + 8 * scale;
        float pinSpacing = 22 * scale;

        int inputCount = (int)block->GetInputPins().size();
        int outputCount = (int)block->GetOutputPins().size();
        int maxPins = std::max(inputCount, outputCount);
        float bodyH = std::max((float)maxPins * pinSpacing + 12 * scale, 40.0f * scale);
        float nodeH = headerH + bodyH;

        bool isDisabled = !block->IsEnabled();
        // 判定选中：是单选焦点，或者在多选列表中
        bool inMultiSelect = std::find(m_State.multiSelectedBlockIds.begin(), m_State.multiSelectedBlockIds.end(), block->GetId()) != m_State.multiSelectedBlockIds.end();
        bool isSelected = (m_State.selectedBlockId == block->GetId()) || inMultiSelect;

        ImVec2 r0 = pos;
        ImVec2 r1 = ImVec2(pos.x + nodeW, pos.y + nodeH);
        ImVec2 headerR1 = ImVec2(pos.x + nodeW, pos.y + headerH);

        drawList->AddRectFilled(ImVec2(r0.x + 4 * scale, r0.y + 6 * scale), ImVec2(r1.x + 4 * scale, r1.y + 6 * scale), ColorFromInt(0, 0, 0, 120), 6.0f * scale);
        ImU32 blockColor = GetBlockColor(block->GetCategory());
        if (isDisabled) blockColor = IM_COL32(60, 60, 60, 255);
        drawList->AddRectFilled(r0, headerR1, blockColor, 6.0f * scale, ImDrawFlags_RoundCornersTop);
        ImU32 bodyColor = isDisabled ? ColorFromInt(25, 25, 30, 240) : ColorFromInt(40, 40, 45, 240);
        drawList->AddRectFilled(ImVec2(r0.x, headerR1.y), r1, bodyColor, 6.0f * scale, ImDrawFlags_RoundCornersBottom);
        ImU32 borderColor = isSelected ? ColorFromInt(255, 165, 0, 255) : ColorFromInt(25, 25, 25, 255);
        drawList->AddRect(r0, r1, borderColor, 6.0f * scale, 0, isSelected ? 2.5f * scale : 1.5f * scale);

        if (showTitle) {
            float titleOffsetX = (!isDisabled) ? 14 * scale : 0;
            if (!isDisabled) drawList->AddCircleFilled(ImVec2(r0.x + 8 * scale, r0.y + headerH * 0.5f), 4 * scale, ColorFromInt(80, 200, 80));
            ImVec2 titleSize = ImGui::CalcTextSize(block->GetName().c_str());
            ImU32 titleColor = ColorFromInt(240, 240, 240, (int)(std::clamp(scale, 0.4f, 1.0f) * 255));
            float textX = showDetail ? (r0.x + titleOffsetX + (nodeW - titleSize.x - titleOffsetX) * 0.5f) : (r0.x + 10 * scale);
            drawList->AddText(ImVec2(textX, r0.y + (headerH - titleSize.y) * 0.5f), titleColor, block->GetName().c_str());
        }

        auto drawPins = [&](const std::vector<VFXPin>& pins, float px, bool isInput) {
            for (size_t i = 0; i < pins.size(); ++i) {
                float py = pos.y + pinStartY + i * pinSpacing;
                ImU32 pinColor = GetPinColor(pins[i].type, pins[i].isConnected);
                float pinRadius = (showDetail ? 5.0f : 6.5f) * scale;

                if (pins[i].isConnected) {
                    drawList->AddCircleFilled(ImVec2(px, py), pinRadius, pinColor);
                } else {
                    drawList->AddCircleFilled(ImVec2(px, py), pinRadius, ColorFromInt(30, 30, 30, 255));
                    drawList->AddCircle(ImVec2(px, py), pinRadius, pinColor, 0, 1.5f * scale);
                }

                if (showDetail) {
                    ImVec2 textSize = ImGui::CalcTextSize(pins[i].name.c_str());
                    if (isInput) drawList->AddText(ImVec2(px + 12 * scale, py - textSize.y * 0.5f), ColorFromInt(200, 200, 200), pins[i].name.c_str());
                    else drawList->AddText(ImVec2(px - textSize.x - 12 * scale, py - textSize.y * 0.5f), ColorFromInt(200, 200, 200), pins[i].name.c_str());
                }
            }
        };
        drawPins(block->GetInputPins(), pos.x, true);
        drawPins(block->GetOutputPins(), pos.x + nodeW, false);

        // 连线起点判定
        bool mouseInCanvasLocal = ImGui::IsMouseHoveringRect(m_CanvasScreenPos, ImVec2(m_CanvasScreenPos.x + m_CanvasSize.x, m_CanvasScreenPos.y + m_CanvasSize.y));
        if (mouseInCanvasLocal) {
            ImVec2 mousePos = ImGui::GetMousePos();
            auto checkPinClick = [&](const std::vector<VFXPin>& pins, float baseX, bool isInput) {
                for (size_t i = 0; i < pins.size(); ++i) {
                    float py = pos.y + pinStartY + i * pinSpacing;
                    float dx = mousePos.x - baseX;
                    float dy = mousePos.y - py;
                    if (dx * dx + dy * dy < (12.0f * scale) * (12.0f * scale)) {
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                            m_State.isDraggingLink = true;
                            m_State.draggingFromBlockId = block->GetId();
                            m_State.draggingFromPinId = pins[i].id;
                            m_State.draggingFromDir = pins[i].direction;
                        }
                    }
                }
            };
            checkPinClick(block->GetInputPins(), pos.x, true);
            checkPinClick(block->GetOutputPins(), pos.x + nodeW, false);
        }

        // 节点交互 (支持 Ctrl 多选与同步移动)
        ImGui::SetCursorScreenPos(r0);
        ImGui::PushID(block->GetId());
        float interactH = block->IsCollapsed() ? headerH : nodeH;
        ImGui::InvisibleButton("block_body", ImVec2(nodeW, interactH));

        if (!m_State.isDraggingLink) {
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                if (ImGui::GetIO().KeyCtrl) {
                    if (inMultiSelect) {
                        m_State.multiSelectedBlockIds.erase(
                            std::remove(m_State.multiSelectedBlockIds.begin(), m_State.multiSelectedBlockIds.end(), block->GetId()), 
                            m_State.multiSelectedBlockIds.end());
                        if (m_State.selectedBlockId == block->GetId()) m_State.selectedBlockId = 0;
                    } else {
                        m_State.multiSelectedBlockIds.push_back(block->GetId());
                        m_State.selectedBlockId = block->GetId();
                    }
                } else {
                    if (!inMultiSelect) {
                        m_State.multiSelectedBlockIds.clear();
                        m_State.multiSelectedBlockIds.push_back(block->GetId());
                    }
                    m_State.selectedBlockId = block->GetId();
                }
                m_State.selectedLinkId = 0;
            }
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 delta = ImVec2(ImGui::GetIO().MouseDelta.x / scale, ImGui::GetIO().MouseDelta.y / scale);
                if (inMultiSelect) {
                    for (uint32 selId : m_State.multiSelectedBlockIds) {
                        auto* selBlock = m_Graph->GetBlock(selId);
                        if (selBlock) selBlock->SetPosition(selBlock->GetPosX() + delta.x, selBlock->GetPosY() + delta.y);
                    }
                } else {
                    block->SetPosition(block->GetPosX() + delta.x, block->GetPosY() + delta.y);
                }
                if (m_Graph) m_Graph->MarkDirty();
            }
        }

        if (ImGui::BeginPopupContextItem("BlockCtxMenu")) {
            if (ImGui::MenuItem("Enable/Disable")) block->SetEnabled(!block->IsEnabled());
            if (ImGui::MenuItem("Delete Block")) { if (m_Graph) m_PendingDeleteBlockIds.push_back(block->GetId()); m_State.selectedBlockId = 0; }
            ImGui::EndPopup();
        }

        if (isSelected && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            if (m_Graph) m_PendingDeleteBlockIds.push_back(block->GetId());
            m_State.selectedBlockId = 0;
        }

        ImGui::PopID();
    }

    // ============================================================
    // GetPinPosition — 计算引脚屏幕坐标
    // ============================================================

    ImVec2 VFXGraphPanel::GetPinPosition(const VFXBlock* block, const VFXPin& pin) const {
        ImVec2 base = WorldToScreen(block->GetPosX(), block->GetPosY());
        float x = base.x;
        float y = base.y;
        float headerH = 28 * m_CanvasScale;
        float pinStartY = headerH + 8 * m_CanvasScale;
        float pinSpacing = 22 * m_CanvasScale;

        int pinIdx = 0;
        const auto& pins = (pin.direction == PinDirection::Input)
            ? block->GetInputPins() : block->GetOutputPins();
        for (size_t i = 0; i < pins.size(); ++i) {
            if (pins[i].id == pin.id) { pinIdx = (int)i; break; }
        }

        float px = x + (pin.direction == PinDirection::Input ? 0 : block->GetSizeX() * m_CanvasScale);
        float py = y + pinStartY + pinIdx * pinSpacing;
        return ImVec2(px, py);
    }

    // ============================================================
    // DrawLinks — 按引脚类型设置连线颜色，增强可见度
    // ============================================================

    void VFXGraphPanel::DrawLinks() {
        if (!m_Graph) return;
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        uint32 linkToDelete = 0;

        for (auto& link : m_Graph->GetLinks()) {
            auto* outBlock = m_Graph->GetBlock(link.outputNodeId);
            auto* inBlock = m_Graph->GetBlock(link.inputNodeId);
            if (!outBlock || !inBlock) continue;

            const VFXPin* outPin = nullptr;
            const VFXPin* inPin = nullptr;
            for (auto& p : outBlock->GetOutputPins()) { if (p.id == link.outputPinId) { outPin = &p; break; } }
            for (auto& p : inBlock->GetInputPins()) { if (p.id == link.inputPinId) { inPin = &p; break; } }
            if (!outPin || !inPin) continue;

            ImVec2 p1 = GetPinPosition(outBlock, *outPin);
            ImVec2 p2 = GetPinPosition(inBlock, *inPin);

            float dx = p2.x - p1.x;
            float tangentStrength = std::clamp(std::abs(dx) * 0.5f, 30.0f * m_CanvasScale, 150.0f * m_CanvasScale);
            if (dx < 0) tangentStrength = std::max(tangentStrength, std::abs(dx) * 1.0f);
            
            ImVec2 cp1 = ImVec2(p1.x + tangentStrength, p1.y);
            ImVec2 cp2 = ImVec2(p2.x - tangentStrength, p2.y);

            bool isSelected = (m_State.selectedLinkId == link.id);
            // 【连线颜色】由输出引脚的类型决定，使其非常显眼
            ImU32 baseColor = isSelected ? ColorFromInt(255, 200, 50) : GetPinColor(outPin->type, true);

            drawList->AddBezierCubic(p1, cp1, cp2, p2, ColorFromInt(15, 15, 15, 200), (isSelected ? 7.0f : 5.0f) * m_CanvasScale);
            drawList->AddBezierCubic(p1, cp1, cp2, p2, baseColor, (isSelected ? 4.0f : 3.0f) * m_CanvasScale);

            ImVec2 midPoint = ImVec2((p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f);
            if (ImLengthSqr(ImVec2(ImGui::GetMousePos().x - midPoint.x, ImGui::GetMousePos().y - midPoint.y)) < 100.0f * m_CanvasScale) {
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) linkToDelete = link.id;
            }
        }

        if (linkToDelete != 0 && m_Graph) {
            m_Graph->RemoveLink(linkToDelete);
            m_State.selectedLinkId = 0;
        }
    }

    // ============================================================
    // HandleLinkDragging — 拖拽连线
    // ============================================================

    void VFXGraphPanel::HandleLinkDragging() {
        if (!m_Graph || !m_State.isDraggingLink) return;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 mousePos = ImGui::GetMousePos();

        auto* srcBlock = m_Graph->GetBlock(m_State.draggingFromBlockId);
        if (!srcBlock) { m_State.isDraggingLink = false; return; }

        const VFXPin* srcPin = nullptr;
        const auto& pins = (m_State.draggingFromDir == PinDirection::Input) ? srcBlock->GetInputPins() : srcBlock->GetOutputPins();
        for (auto& p : pins) { if (p.id == m_State.draggingFromPinId) { srcPin = &p; break; } }
        if (!srcPin) { m_State.isDraggingLink = false; return; }

        ImVec2 p1 = GetPinPosition(srcBlock, *srcPin);
        ImVec2 p2 = mousePos;
        if (m_State.draggingFromDir == PinDirection::Input) std::swap(p1, p2);

        float dx = p2.x - p1.x;
        float tangentStrength = std::clamp(std::abs(dx) * 0.5f, 30.0f * m_CanvasScale, 150.0f * m_CanvasScale);
        if (dx < 0) tangentStrength = std::max(tangentStrength, std::abs(dx) * 1.0f);
        
        ImVec2 cp1 = ImVec2(p1.x + tangentStrength, p1.y);
        ImVec2 cp2 = ImVec2(p2.x - tangentStrength, p2.y);

        drawList->AddBezierCubic(p1, cp1, cp2, p2, ColorFromInt(0, 0, 0, 150), 5.0f * m_CanvasScale);
        drawList->AddBezierCubic(p1, cp1, cp2, p2, GetPinColor(srcPin->type, true), 3.0f * m_CanvasScale);

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            uint32 targetBlockId, targetPinId;
            PinDirection targetDir;
            FindHoveredPin(mousePos, targetBlockId, targetPinId, targetDir);
            if (targetBlockId != 0 && m_State.draggingFromDir != targetDir) {
                uint32 outBlockID = (m_State.draggingFromDir == PinDirection::Output) ? m_State.draggingFromBlockId : targetBlockId;
                uint32 inBlockID = (targetDir == PinDirection::Input) ? targetBlockId : m_State.draggingFromBlockId;
                uint32 outPinID = (m_State.draggingFromDir == PinDirection::Output) ? srcPin->id : targetPinId;
                uint32 inPinID = (targetDir == PinDirection::Input) ? targetPinId : srcPin->id;
                m_Graph->ReplaceLink(outBlockID, outPinID, inBlockID, inPinID);
            }
            m_State.isDraggingLink = false;
        }
    }

    // ============================================================
    // HandleMultiselect — 真正的 AABB 框选逻辑
    // ============================================================

    void VFXGraphPanel::HandleMultiselect() {
        ImGuiIO& io = ImGui::GetIO();
        bool canvasActive = ImGui::IsItemActive(); // 由 VFXCanvas 的 InvisibleButton 提供

        if (canvasActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !io.KeyAlt) {
            if (!m_IsBoxSelecting) {
                m_IsBoxSelecting = true;
                m_BoxSelectStart = io.MouseClickedPos[0];
                if (!io.KeyCtrl) m_State.multiSelectedBlockIds.clear();
            }
        }

        if (m_IsBoxSelecting) {
            ImVec2 currentMouse = ImGui::GetMousePos();
            ImDrawList* drawList = ImGui::GetForegroundDrawList();
            drawList->AddRectFilled(m_BoxSelectStart, currentMouse, ColorFromInt(100, 150, 255, 40));
            drawList->AddRect(m_BoxSelectStart, currentMouse, ColorFromInt(100, 150, 255, 200), 0, 0, 2.0f);

            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                m_IsBoxSelecting = false;
                ImRect selectRect(ImMin(m_BoxSelectStart, currentMouse), ImMax(m_BoxSelectStart, currentMouse));

                if (m_Graph) {
                    for (auto& [id, block] : m_Graph->GetBlocks()) {
                        ImVec2 pMin = WorldToScreen(block->GetPosX(), block->GetPosY());
                        float nodeW = block->GetSizeX() * m_CanvasScale;
                        float nodeH = (28 + std::max((int)block->GetInputPins().size(), (int)block->GetOutputPins().size()) * 22 + 12) * m_CanvasScale;
                        ImVec2 pMax = ImVec2(pMin.x + nodeW, pMin.y + nodeH);
                        
                        ImRect blockRect(pMin, pMax);
                        if (selectRect.Overlaps(blockRect)) {
                            if (std::find(m_State.multiSelectedBlockIds.begin(), m_State.multiSelectedBlockIds.end(), id) == m_State.multiSelectedBlockIds.end()) {
                                m_State.multiSelectedBlockIds.push_back(id);
                            }
                        }
                    }
                }
            }
        }
    }

    // ============================================================
    // DrawNodeGroupBoxes / HandleCreateGroup / HandleCreateMenu...
    // ============================================================

    void VFXGraphPanel::DrawNodeGroupBoxes() {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        for (auto& group : m_Groups) {
            ImVec2 p0 = WorldToScreen(group.posX, group.posY);
            ImVec2 p1 = WorldToScreen(group.posX + group.sizeX, group.posY + group.sizeY);

            drawList->AddRectFilled(p0, p1, group.color, 4.0f);
            drawList->AddRect(p0, p1, ColorFromInt(200, 200, 200, 100), 4.0f, 0, 2.0f);
            drawList->AddText(ImVec2(p0.x + 10, p0.y + 5), IM_COL32_WHITE, group.name.c_str());

            ImGui::SetCursorScreenPos(p0);
            ImGui::SetNextItemAllowOverlap();
            ImVec2 btnSize(p1.x - p0.x, p1.y - p0.y);
            ImGui::InvisibleButton(("g" + std::to_string(group.id)).c_str(), btnSize);
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                group.posX += ImGui::GetIO().MouseDelta.x / m_CanvasScale;
                group.posY += ImGui::GetIO().MouseDelta.y / m_CanvasScale;
            }
        }
    }

    void VFXGraphPanel::HandleCreateGroup() {
        if (ImGui::IsKeyPressed(ImGuiKey_C) && !m_State.multiSelectedBlockIds.empty()) {
            VFXNodeGroup newGroup;
            newGroup.id = m_NextGroupId++;
            newGroup.memberBlockIds = m_State.multiSelectedBlockIds;

            float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
            for (uint32 id : newGroup.memberBlockIds) {
                auto* b = m_Graph->GetBlock(id);
                if (b) {
                    minX = std::min(minX, b->GetPosX()); minY = std::min(minY, b->GetPosY());
                    maxX = std::max(maxX, b->GetPosX() + b->GetSizeX()); maxY = std::max(maxY, b->GetPosY() + b->GetSizeY());
                }
            }
            newGroup.posX = minX - 20; newGroup.posY = minY - 40;
            newGroup.sizeX = (maxX - minX) + 40; newGroup.sizeY = (maxY - minY) + 60;
            m_Groups.push_back(newGroup);
        }
    }

    void VFXGraphPanel::HandleCreateMenu() {
        if (ImGui::BeginPopup("VFXCtxCreate")) {
            if (ImGui::BeginMenu(ICON_FA_CUBES " Add Context")) {
                const struct { ContextType type; const char* label; } ctxConfigs[] = {
                    { ContextType::Spawn,      "Spawn" },
                    { ContextType::Initialize, "Initialize" },
                    { ContextType::Update,     "Update" },
                    { ContextType::Output,     "Output" },
                    { ContextType::System,     "System" }
                };
                for (auto& config : ctxConfigs) {
                    if (ImGui::MenuItem(config.label)) {
                        if (m_Graph) {
                            VFXContext& newCtx = m_Graph->CreateNewContext(config.type);
                            ImVec2 wp = ScreenToWorld(m_State.createMenuPosX, m_State.createMenuPosY);
                            newCtx.posX = wp.x;
                            newCtx.posY = wp.y;
                        }
                    }
                }
                ImGui::EndMenu();
            }

            ImGui::Separator();

            if (ImGui::BeginMenu(ICON_FA_CUBE " Add Block")) {
                auto& factoryList = GetVFXBlockFactoryList();
                std::string currentGroup;
                for (int i = 0; i < (int)factoryList.size(); ++i) {
                    auto& entry = factoryList[i];
                    std::string groupName = entry.group;
                    if (groupName != currentGroup) {
                        currentGroup = groupName;
                        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "── %s ──", groupName.c_str());
                    }
                    if (ImGui::MenuItem(entry.name)) {
                        if (m_Graph) {
                            uint32 newId = m_Graph->AddBlock(entry.factory(0));
                            auto* newBlock = m_Graph->GetBlock(newId);
                            if (newBlock) {
                                float localX = (m_State.createMenuPosX - m_CanvasScreenPos.x) / m_CanvasScale - m_CanvasOrigin.x;
                                float localY = (m_State.createMenuPosY - m_CanvasScreenPos.y) / m_CanvasScale - m_CanvasOrigin.y;
                                newBlock->SetPosition(localX, localY);
                                ContextType ctxType = BlockCategoryToContext(entry.category);
                                m_Graph->GetOrCreateContext(ctxType).blockIds.push_back(newId);
                            }
                        }
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }
    }

    void VFXGraphPanel::FindHoveredPin(ImVec2 mousePos, uint32& outBlockId, uint32& outPinId, PinDirection& outDir) const {
        outBlockId = 0; outPinId = 0;
        if (!m_Graph) return;
        float hitRadiusSqr = (12.0f * m_CanvasScale) * (12.0f * m_CanvasScale);
        for (auto& [id, block] : m_Graph->GetBlocks()) {
            for (auto& pin : block->GetInputPins()) {
                ImVec2 pinPos = GetPinPosition(block.get(), pin);
                if (ImLengthSqr(ImVec2(mousePos.x - pinPos.x, mousePos.y - pinPos.y)) < hitRadiusSqr) {
                    outBlockId = id; outPinId = pin.id; outDir = pin.direction; return;
                }
            }
            for (auto& pin : block->GetOutputPins()) {
                ImVec2 pinPos = GetPinPosition(block.get(), pin);
                if (ImLengthSqr(ImVec2(mousePos.x - pinPos.x, mousePos.y - pinPos.y)) < hitRadiusSqr) {
                    outBlockId = id; outPinId = pin.id; outDir = pin.direction; return;
                }
            }
        }
    }

    void VFXGraphPanel::ProcessPendingDeletions() {
        if (!m_Graph || m_PendingDeleteBlockIds.empty()) return;
        for (uint32 id : m_PendingDeleteBlockIds) {
            m_Graph->RemoveBlock(id);
            // 清除对被删除 Block 的引用
            if (m_State.selectedBlockId == id) m_State.selectedBlockId = 0;
            if (m_State.draggingFromBlockId == id) m_State.isDraggingLink = false;
            m_State.multiSelectedBlockIds.erase(
                std::remove(m_State.multiSelectedBlockIds.begin(), m_State.multiSelectedBlockIds.end(), id),
                m_State.multiSelectedBlockIds.end());
        }
        m_PendingDeleteBlockIds.clear();
    }

    // ============================================================
    // Blackboard / Properties / Stats / Preview
    // ============================================================
    // 这些功能逻辑之前已经非常完善，这里保留之前的标准实现
    
    void VFXGraphPanel::DrawBlackboard() {
        if (!m_Graph) return;

        ImGui::Text("Blackboard Properties");
        ImGui::Separator();

        if (ImGui::Button("+ Add Property", ImVec2(-1, 0))) ImGui::OpenPopup("VFXAddProperty");

        if (ImGui::BeginPopup("VFXAddProperty")) {
            static char nameBuf[128] = "VFXParam";
            static int selectedType = 0;
            const char* types[] = { "Float", "Float2", "Float3", "Color", "Texture2D", "Texture3D" };
            ImGui::InputText("Name", nameBuf, sizeof(nameBuf));
            ImGui::Combo("Type", &selectedType, types, IM_ARRAYSIZE(types));
            if (ImGui::Button("Add", ImVec2(-1, 0))) {
                VFXBlackboardProperty prop;
                prop.name = nameBuf;
                switch (selectedType) {
                    case 0: prop.type = VFXPinType::Float; break;
                    case 1: prop.type = VFXPinType::Float2; break;
                    case 2: prop.type = VFXPinType::Float3; break;
                    case 3: prop.type = VFXPinType::Color; break;
                    case 4: prop.type = VFXPinType::Texture2D; break;
                    case 5: prop.type = VFXPinType::Texture3D; break;
                }
                m_Graph->AddProperty(prop);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::Spacing();
        auto& props = m_Graph->GetProperties();
        int removeIdx = -1;

        for (int i = 0; i < (int)props.size(); ++i) {
            ImGui::PushID(i);
            float width = ImGui::GetContentRegionAvail().x;
            ImVec2 cursorPos = ImGui::GetCursorPos();
            ImGui::SetNextItemAllowOverlap();
            ImGui::Selectable(props[i].name.c_str(), false, 0, ImVec2(width - 24, 22));

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload("VFX_BLACKBOARD_PROP", &i, sizeof(int));
                ImGui::Text("Get %s", props[i].name.c_str());
                ImGui::EndDragDropSource();
            }

            ImGui::SetCursorPos(ImVec2(cursorPos.x + width - 22, cursorPos.y));
            if (ImGui::Button("X", ImVec2(22, 22))) removeIdx = i;

            ImGui::TextDisabled("  [%s]", VFXPinTypeName(props[i].type));
            ImGui::Separator();
            ImGui::PopID();
        }
        if (removeIdx >= 0) m_Graph->RemoveProperty(removeIdx);
    }

    void VFXGraphPanel::DrawPropertiesPanel() {
        if (!m_Graph) return;

        ImGui::Text("Block Properties");
        ImGui::Separator();

        if (m_State.selectedBlockId == 0) {
            ImGui::TextDisabled("Select a block to edit.");
            ImGui::Spacing(); ImGui::Separator(); ImGui::Text("System Parameters");
            uint32 cap = m_Graph->GetParticleCapacity();
            if (ImGui::DragInt("Max Particles", (int*)&cap, 1000, 1000, 1000000)) m_Graph->SetParticleCapacity(cap);
            float dur = m_Graph->GetDuration();
            if (ImGui::DragFloat("Duration", &dur, 0.1f, 0.1f, 100.0f)) m_Graph->SetDuration(dur);
            bool loop = m_Graph->IsLooping();
            if (ImGui::Checkbox("Looping", &loop)) m_Graph->SetLooping(loop);
            bool prewarm = m_Graph->IsPrewarmed();
            if (ImGui::Checkbox("Prewarm", &prewarm)) m_Graph->SetPrewarmed(prewarm);
            return;
        }

        auto* block = m_Graph->GetBlock(m_State.selectedBlockId);
        if (!block) return;

        char nameBuf[64];
        strncpy_s(nameBuf, block->GetName().c_str(), sizeof(nameBuf) - 1);
        if (ImGui::InputText(" Name", nameBuf, sizeof(nameBuf))) block->SetName(nameBuf);
        ImGui::TextDisabled("Category: %s", BlockCategoryName(block->GetCategory()));
        ImGui::Spacing();

        bool enabled = block->IsEnabled();
        if (ImGui::Checkbox("Enabled", &enabled)) block->SetEnabled(enabled);

        ImGui::Spacing(); ImGui::Separator(); ImGui::Text("Input Parameters");

        if (ImGui::BeginTable("VFXPropGrid", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            for (auto& pin : block->GetInputPins()) {
                if (pin.isConnected) continue;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::Text("%s", pin.name.c_str());
                ImGui::TableSetColumnIndex(1); ImGui::PushItemWidth(-FLT_MIN); ImGui::PushID(pin.id);
                float* v = const_cast<float*>(pin.f);
                switch (pin.type) {
                    case VFXPinType::Float: ImGui::DragFloat("##v", &v[0], 0.01f); break;
                    case VFXPinType::Float2: ImGui::DragFloat2("##v", v, 0.01f); break;
                    case VFXPinType::Float3: ImGui::DragFloat3("##v", v, 0.01f); break;
                    case VFXPinType::Float4: ImGui::DragFloat4("##v", v, 0.01f); break;
                    case VFXPinType::Color: ImGui::ColorEdit4("##c", v, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar); break;
                    case VFXPinType::Boolean: { bool bv = pin.b; if (ImGui::Checkbox("##b", &bv)) { const_cast<VFXPin&>(pin).b = bv; } break; }
                    default: ImGui::TextDisabled("N/A"); break;
                }
                ImGui::PopID(); ImGui::PopItemWidth();
            }
            ImGui::EndTable();
        }
        block->DrawInlineContent();
    }

    void VFXGraphPanel::DrawPreviewPanel() {
        ImGui::Begin("VFX Preview", &m_State.showPreview, ImGuiWindowFlags_NoScrollbar);
        ImVec2 previewSize = ImGui::GetContentRegionAvail();
        ImVec2 screenPos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(screenPos, ImVec2(screenPos.x + previewSize.x, screenPos.y + previewSize.y), ColorFromInt(10, 10, 10));
        ImGui::Dummy(previewSize);
        ImGui::SetCursorScreenPos(ImVec2(screenPos.x, screenPos.y + previewSize.y - 30));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::Button(ICON_FA_PLAY " Play")) {} ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PAUSE " Pause")) {} ImGui::SameLine();
        if (ImGui::Button(ICON_FA_STOP " Stop")) {} ImGui::SameLine();
        float textWidth = ImGui::CalcTextSize("Particles: 0/65536").x;
        ImGui::SameLine(previewSize.x - textWidth - 10);
        ImGui::TextDisabled("Particles: 0/%u", m_Graph ? m_Graph->GetParticleCapacity() : 0);
        ImGui::PopStyleColor();
        ImGui::End();
    }

    void VFXGraphPanel::DrawParticleStats() {
        ImGui::Begin("VFX Stats", &m_State.showParticleStats);
        ImGui::Text("Particle Statistics"); ImGui::Separator();
        if (m_Graph) {
            uint32 capacity = m_Graph->GetParticleCapacity();
            ImGui::Text("Active Blocks:    %u", (uint32)m_Graph->GetBlocks().size());
            ImGui::Text("Connections:      %u", (uint32)m_Graph->GetLinks().size());
            ImGui::Text("Contexts:         %u", (uint32)m_Graph->GetContexts().size());
            ImGui::Text("Particle Cap:     %u", capacity);
            ImGui::Text("Instruction Est:  %u", m_State.instructionCount);
            ImGui::Spacing(); ImGui::Separator(); ImGui::Text("Memory Estimates");
            uint64 particleMem = (uint64)capacity * 48; // Estimate
            if (particleMem < 1024) ImGui::Text("Particle Buffer:  %llu B", particleMem);
            else if (particleMem < 1024 * 1024) ImGui::Text("Particle Buffer:  %.1f KB", particleMem / 1024.0);
            else ImGui::Text("Particle Buffer:  %.1f MB", particleMem / (1024.0 * 1024.0));
        }
        ImGui::End();
    }

}} // namespace Engine::VFX
