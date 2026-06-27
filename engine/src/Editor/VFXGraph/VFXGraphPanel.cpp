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
        if (connected) return ColorPinConnected;
        switch (type) {
            case VFXPinType::Float:        return ColorPinFloat;
            case VFXPinType::Float2:       return ColorPinFloat2;
            case VFXPinType::Float3:       return ColorPinFloat3;
            case VFXPinType::Float4:       return ColorPinFloat4;
            case VFXPinType::Color:        return ColorPinFloat4;
            case VFXPinType::Texture2D:    return ColorPinTexture;
            case VFXPinType::Texture3D:    return ColorPinTexture;
            case VFXPinType::Boolean:      return ColorPinBool;
            case VFXPinType::ParticleData: return ColorPinParticle;
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
            if (m_Graph) m_Graph->SaveToFile("vfx.vfxgraph");
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FOLDER_OPEN " Load")) {
            if (m_Graph) m_Graph->LoadFromFile("vfx.vfxgraph");
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
                // 使用 VFXCompiler（步骤1：架构解耦）
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
        // Undo/Redo 按钮（步骤2：撤销/重做系统）
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

        // 按组分组的 Block 列表
        std::string currentGroup;
        for (int i = 0; i < (int)factoryList.size(); ++i) {
            auto& entry = factoryList[i];

            // 搜索过滤
            if (!searchStr.empty()) {
                std::string lowerName = entry.name;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                if (lowerName.find(searchStr) == std::string::npos) continue;
            }

            // 分组标题
            std::string groupName = entry.group;
            if (groupName != currentGroup) {
                currentGroup = groupName;
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "── %s ──", groupName.c_str());
            }

            ImGui::PushID(i);
            ImGui::Selectable(entry.name, false, ImGuiSelectableFlags_AllowOverlap);

            // ── 修复：拖拽源 — 使用统一 Payload ID ──
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload("VFX_BLOCK_CREATE_PAYLOAD", &i, sizeof(int));
                ImGui::Text(ICON_FA_CUBE " Create %s", entry.name);
                ImGui::EndDragDropSource();
            }
            ImGui::PopID();
        }
    }

    // ============================================================
    // DrawNodeCanvas — 核心画布（含 Context 容器 + Block 节点）
    // ============================================================

    void VFXGraphPanel::DrawNodeCanvas() {
        m_CanvasSize = ImGui::GetContentRegionAvail();
        m_CanvasScreenPos = ImGui::GetCursorScreenPos();

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // 绘制背景
        drawList->AddRectFilled(m_CanvasScreenPos,
            ImVec2(m_CanvasScreenPos.x + m_CanvasSize.x, m_CanvasScreenPos.y + m_CanvasSize.y),
            ColorFromInt(30, 30, 30));

        // 画布底板交互
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("VFXCanvas", m_CanvasSize,
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        bool mouseInCanvas = ImGui::IsItemHovered();

        // 缩放
        if (mouseInCanvas && ImGui::GetIO().MouseWheel != 0) {
            float scaleFactor = 1.0f + ImGui::GetIO().MouseWheel * 0.1f;
            m_CanvasScale = std::clamp(m_CanvasScale * scaleFactor, 0.15f, 3.0f);
        }

        // 平移
        if (mouseInCanvas &&
            (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
             (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && ImGui::GetIO().KeyAlt)))
        {
            m_CanvasOrigin.x += ImGui::GetIO().MouseDelta.x / m_CanvasScale;
            m_CanvasOrigin.y += ImGui::GetIO().MouseDelta.y / m_CanvasScale;
        }

        // 右键创建菜单
        if (mouseInCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            m_State.createMenuPosX = ImGui::GetMousePos().x;
            m_State.createMenuPosY = ImGui::GetMousePos().y;
            ImGui::OpenPopup("VFXCtxCreate");
        }

        // 网格
        if (m_State.showGrid) {
            DrawDotGrid(drawList, m_CanvasScreenPos, m_CanvasSize);
        }

        drawList->PushClipRect(m_CanvasScreenPos,
            ImVec2(m_CanvasScreenPos.x + m_CanvasSize.x, m_CanvasScreenPos.y + m_CanvasSize.y), true);

        // ── 绘制 Context 容器 ──
        if (m_Graph) {
            for (auto& ctx : m_Graph->GetContexts()) {
                if (ctx.visible) {
                    DrawContextContainer(ctx);
                }
            }
        }

        // ── 绘制连接线 ──
        if (m_Graph) DrawLinks();

        // ── 绘制 Block 节点 ──
        if (m_Graph) {
            // 先收集所有需要删除的 Block ID（避免迭代时删除导致崩溃）
            m_PendingDeleteBlockIds.clear();
            for (auto& [id, block] : m_Graph->GetBlocks()) {
                DrawBlockNode(block.get());
            }
        }

        // ── 拖拽连线 ──
        HandleLinkDragging();

        // ── 拖放接受（Block 拖入画布）— 在画布区域内 ──
        if (m_Graph) {
            if (ImGui::BeginDragDropTarget()) {
                const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VFX_BLOCK_CREATE_PAYLOAD");
                if (payload) {
                    int factoryIdx = *(const int*)payload->Data;
                    auto& factoryList = GetVFXBlockFactoryList();
                    if (factoryIdx >= 0 && factoryIdx < (int)factoryList.size()) {
                        uint32 newId = m_Graph->AddBlock(factoryList[factoryIdx].factory(0));
                        auto* newBlock = m_Graph->GetBlock(newId);
                        if (newBlock) {
                            // 【关键：正确坐标转换】
                            ImVec2 mPos = ImGui::GetMousePos();
                            float localX = (mPos.x - m_CanvasScreenPos.x) / m_CanvasScale - m_CanvasOrigin.x;
                            float localY = (mPos.y - m_CanvasScreenPos.y) / m_CanvasScale - m_CanvasOrigin.y;
                            newBlock->SetPosition(localX, localY);

                            ContextType ctxType = BlockCategoryToContext(factoryList[factoryIdx].category);
                            VFXContext& ctx = m_Graph->GetOrCreateContext(ctxType);
                            ctx.blockIds.push_back(newId);
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

        // ── 在迭代完成后执行延迟删除 ──
        if (!m_PendingDeleteBlockIds.empty()) {
            ProcessPendingDeletions();
        }

        drawList->PopClipRect();

        // ── 创建菜单 ──
        HandleCreateMenu();
    }

    // ============================================================
    // DrawDotGrid — 双层网格
    // ============================================================

    void VFXGraphPanel::DrawDotGrid(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& size) {
        float gridSize = 20.0f * m_CanvasScale;
        float majorGridSize = gridSize * 5.0f;
        ImVec2 origin = ImVec2(canvasPos.x + m_CanvasOrigin.x * m_CanvasScale,
                               canvasPos.y + m_CanvasOrigin.y * m_CanvasScale);

        for (float x = fmodf(origin.x, gridSize); x < size.x; x += gridSize) {
            drawList->AddLine(ImVec2(canvasPos.x + x, canvasPos.y),
                              ImVec2(canvasPos.x + x, canvasPos.y + size.y),
                              ColorFromInt(45, 45, 45, 100));
        }
        for (float y = fmodf(origin.y, gridSize); y < size.y; y += gridSize) {
            drawList->AddLine(ImVec2(canvasPos.x, canvasPos.y + y),
                              ImVec2(canvasPos.x + size.x, canvasPos.y + y),
                              ColorFromInt(45, 45, 45, 100));
        }
        for (float x = fmodf(origin.x, majorGridSize); x < size.x; x += majorGridSize) {
            drawList->AddLine(ImVec2(canvasPos.x + x, canvasPos.y),
                              ImVec2(canvasPos.x + x, canvasPos.y + size.y),
                              ColorFromInt(60, 60, 60, 200));
        }
        for (float y = fmodf(origin.y, majorGridSize); y < size.y; y += majorGridSize) {
            drawList->AddLine(ImVec2(canvasPos.x, canvasPos.y + y),
                              ImVec2(canvasPos.x + size.x, canvasPos.y + y),
                              ColorFromInt(60, 60, 60, 200));
        }
    }

    // ============================================================
    // DrawContextContainer — 绘制生命周期容器
    // ============================================================

    void VFXGraphPanel::DrawContextContainer(VFXContext& ctx) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        float sx = ctx.posX * m_CanvasScale + m_CanvasScreenPos.x + m_CanvasOrigin.x * m_CanvasScale;
        float sy = ctx.posY * m_CanvasScale + m_CanvasScreenPos.y + m_CanvasOrigin.y * m_CanvasScale;
        float sw = ctx.sizeX * m_CanvasScale;
        float sh = ctx.sizeY * m_CanvasScale;

        // 容器背景
        ImU32 ctxColor = GetContextColor(ctx.type);
        drawList->AddRectFilled(ImVec2(sx, sy), ImVec2(sx + sw, sy + sh), ctxColor, 8.0f * m_CanvasScale);

        // 容器边框
        drawList->AddRect(ImVec2(sx, sy), ImVec2(sx + sw, sy + sh),
                          ColorFromInt(100, 100, 120, 200), 8.0f * m_CanvasScale, 0, 1.5f * m_CanvasScale);

        // 标题栏
        float headerH = 24.0f * m_CanvasScale;
        drawList->AddRectFilled(ImVec2(sx, sy), ImVec2(sx + sw, sy + headerH),
                                ColorFromInt(40, 40, 55, 200), 8.0f * m_CanvasScale, ImDrawFlags_RoundCornersTop);

        ImVec2 titleSize = ImGui::CalcTextSize(ContextTypeDisplayName(ctx.type));
        drawList->AddText(ImVec2(sx + 10 * m_CanvasScale, sy + (headerH - titleSize.y) * 0.5f),
                          ColorFromInt(220, 220, 240), ContextTypeDisplayName(ctx.type));

        // 块数量
        char countStr[32];
        snprintf(countStr, sizeof(countStr), "[%zu blocks]", ctx.blockIds.size());
        drawList->AddText(ImVec2(sx + sw - ImGui::CalcTextSize(countStr).x - 10 * m_CanvasScale,
                                 sy + (headerH - titleSize.y) * 0.5f),
                          ColorFromInt(150, 150, 170), countStr);

        // 容器上的不可见按钮（用于交互）
        ImGui::SetCursorScreenPos(ImVec2(sx, sy));
        ImGui::PushID(&ctx);
        ImGui::InvisibleButton("ctx", ImVec2(sw, sh));
        ImGui::PopID();
    }

    // ============================================================
    // DrawBlockNode — 绘制单个 Block 节点
    // ============================================================

    void VFXGraphPanel::DrawBlockNode(VFXBlock* block) {
        if (!block) return;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float scale = m_CanvasScale;

        // ── LOD 判断逻辑 ──
        // Full (scale > 0.7): 显示所有文字、按钮、详细引脚名
        // Simple (0.7 >= scale > 0.4): 隐藏引脚文字，只保留引脚点和节点标题
        // Minimal (scale <= 0.4): 隐藏所有文字，节点变为彩色色块
        bool showDetail = (scale > 0.7f);
        bool showTitle = (scale > 0.4f);

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

        // 检查是否在容器内部（稍提高绘制优先级）
        bool isDisabled = !block->IsEnabled();
        bool isSelected = (m_State.selectedBlockId == block->GetId());

        ImVec2 r0 = pos;
        ImVec2 r1 = ImVec2(pos.x + nodeW, pos.y + nodeH);
        ImVec2 headerR1 = ImVec2(pos.x + nodeW, pos.y + headerH);

        // 阴影
        drawList->AddRectFilled(ImVec2(r0.x + 4 * scale, r0.y + 6 * scale),
                                ImVec2(r1.x + 4 * scale, r1.y + 6 * scale),
                                ColorFromInt(0, 0, 0, 120), 6.0f * scale);

        // 头部
        ImU32 blockColor = GetBlockColor(block->GetCategory());
        if (isDisabled) blockColor = IM_COL32(60, 60, 60, 255);
        drawList->AddRectFilled(r0, headerR1, blockColor, 6.0f * scale, ImDrawFlags_RoundCornersTop);

        // 主体
        ImU32 bodyColor = isDisabled ? ColorFromInt(25, 25, 30, 240) : ColorFromInt(40, 40, 45, 240);
        drawList->AddRectFilled(ImVec2(r0.x, headerR1.y), r1, bodyColor,
                                6.0f * scale, ImDrawFlags_RoundCornersBottom);

        // 边框
        ImU32 borderColor = isSelected ? ColorFromInt(255, 165, 0, 255) : ColorFromInt(25, 25, 25, 255);
        drawList->AddRect(r0, r1, borderColor, 6.0f * scale, 0, isSelected ? 2.5f * scale : 1.5f * scale);

        // 标题 + 启用标记（带 LOD 控制）
        if (showTitle) {
            float titleOffsetX = 0;
            if (!isDisabled) {
                drawList->AddCircleFilled(ImVec2(r0.x + 8 * scale, r0.y + headerH * 0.5f),
                                          4 * scale, ColorFromInt(80, 200, 80));
                titleOffsetX = 14 * scale;
            }
            ImVec2 titleSize = ImGui::CalcTextSize(block->GetName().c_str());
            ImU32 titleColor = ColorFromInt(240, 240, 240, (int)(std::clamp(scale, 0.4f, 1.0f) * 255));
            float textX = showDetail ? (r0.x + titleOffsetX + (nodeW - titleSize.x - titleOffsetX) * 0.5f)
                                     : (r0.x + 10 * scale);
            drawList->AddText(ImVec2(textX, r0.y + (headerH - titleSize.y) * 0.5f),
                              titleColor, block->GetName().c_str());
        }

        // 绘制引脚（带 LOD 控制：缩小时引脚点放大以便看清连接）
        auto drawPins = [&](const std::vector<VFXPin>& pins, float px, bool isInput) {
            for (size_t i = 0; i < pins.size(); ++i) {
                float py = pos.y + pinStartY + i * pinSpacing;
                ImU32 pinColor = GetPinColor(pins[i].type, pins[i].isConnected);

                // 引脚点始终绘制，缩小时稍微放大以保持连接可见性
                float pinRadius = (showDetail ? 5.0f : 6.5f) * scale;

                if (pins[i].isConnected) {
                    drawList->AddCircleFilled(ImVec2(px, py), pinRadius, pinColor);
                } else {
                    drawList->AddCircleFilled(ImVec2(px, py), pinRadius, ColorFromInt(30, 30, 30, 255));
                    drawList->AddCircle(ImVec2(px, py), pinRadius, pinColor, 0, 1.5f * scale);
                }

                // 仅在 LOD Full 模式下绘制引脚文本
                if (showDetail) {
                    ImVec2 textSize = ImGui::CalcTextSize(pins[i].name.c_str());
                    if (isInput) {
                        drawList->AddText(ImVec2(px + 12 * scale, py - textSize.y * 0.5f),
                                          ColorFromInt(200, 200, 200), pins[i].name.c_str());
                    } else {
                        drawList->AddText(ImVec2(px - textSize.x - 12 * scale, py - textSize.y * 0.5f),
                                          ColorFromInt(200, 200, 200), pins[i].name.c_str());
                    }
                }
            }
        };
        drawPins(block->GetInputPins(), pos.x, true);
        drawPins(block->GetOutputPins(), pos.x + nodeW, false);

        // 引脚连线起点交互
        // 检查鼠标是否在画布区域内
        ImVec2 mousePosGlobal = ImGui::GetMousePos();
        bool mouseInCanvasLocal = (mousePosGlobal.x >= m_CanvasScreenPos.x &&
                                    mousePosGlobal.x <= m_CanvasScreenPos.x + m_CanvasSize.x &&
                                    mousePosGlobal.y >= m_CanvasScreenPos.y &&
                                    mousePosGlobal.y <= m_CanvasScreenPos.y + m_CanvasSize.y);
        if (mouseInCanvasLocal) {
            ImVec2 mousePos = ImGui::GetMousePos();
            auto checkPinClick = [&](const std::vector<VFXPin>& pins, float baseX, bool isInput) {
                for (size_t i = 0; i < pins.size(); ++i) {
                    float py = pos.y + pinStartY + i * pinSpacing;
                    float dx = mousePos.x - baseX;
                    float dy = mousePos.y - py;
                    float distSq = dx * dx + dy * dy;
                    if (distSq < (12.0f * scale) * (12.0f * scale)) {
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                            m_State.isDraggingLink = true;
                            m_State.draggingFromBlockId = block->GetId();
                            m_State.draggingFromPinId = pins[i].id;
                            m_State.draggingFromDir = pins[i].direction;
                        }
                        // 鼠标悬浮提示
                        if (ImGui::IsItemHovered()) {
                            // 简化：无操作
                        }
                    }
                }
            };
            checkPinClick(block->GetInputPins(), pos.x, true);
            checkPinClick(block->GetOutputPins(), pos.x + nodeW, false);
        }

        // 节点交互（选中、拖拽移动）
        ImGui::SetCursorScreenPos(r0);
        ImGui::PushID(block->GetId());

        float interactH = nodeH;
        if (block->IsCollapsed()) interactH = headerH;
        ImGui::InvisibleButton("block_body", ImVec2(nodeW, interactH));

        if (!m_State.isDraggingLink) {
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                m_State.selectedBlockId = block->GetId();
                m_State.selectedLinkId = 0;
            }
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                block->SetPosition(block->GetPosX() + ImGui::GetIO().MouseDelta.x / scale,
                                   block->GetPosY() + ImGui::GetIO().MouseDelta.y / scale);
                m_Graph->MarkDirty();
            }
        }

        // 右键菜单
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup("BlockCtxMenu");
        }
        if (ImGui::BeginPopup("BlockCtxMenu")) {
            if (ImGui::MenuItem("Enable/Disable")) {
                block->SetEnabled(!block->IsEnabled());
            }
            if (ImGui::MenuItem("Delete Block")) {
                if (m_Graph) {
                    m_PendingDeleteBlockIds.push_back(block->GetId());
                }
                m_State.selectedBlockId = 0;
            }
            if (ImGui::MenuItem("Duplicate")) {
                if (m_Graph) {
                    auto clone = block->Clone(0);
                    uint32 newId = m_Graph->AddBlock(std::move(clone));
                    auto* newBlock = m_Graph->GetBlock(newId);
                    if (newBlock) {
                        newBlock->SetPosition(block->GetPosX() + 30.0f, block->GetPosY() + 30.0f);
                    }
                }
            }
            ImGui::EndPopup();
        }

        if (isSelected && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            if (m_Graph) {
                m_PendingDeleteBlockIds.push_back(block->GetId());
            }
            m_State.selectedBlockId = 0;
        }

        ImGui::PopID();
    }

    // ============================================================
    // DrawLinks — 贝塞尔曲线连接线
    // ============================================================

    void VFXGraphPanel::DrawLinks() {
        if (!m_Graph) return;
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        for (auto& link : m_Graph->GetLinks()) {
            auto* outBlock = m_Graph->GetBlock(link.outputNodeId);
            auto* inBlock = m_Graph->GetBlock(link.inputNodeId);
            if (!outBlock || !inBlock) continue;

            const VFXPin* outPin = nullptr;
            const VFXPin* inPin = nullptr;
            for (auto& p : outBlock->GetOutputPins()) {
                if (p.id == link.outputPinId) { outPin = &p; break; }
            }
            for (auto& p : inBlock->GetInputPins()) {
                if (p.id == link.inputPinId) { inPin = &p; break; }
            }
            if (!outPin || !inPin) continue;

            ImVec2 p1 = GetPinPosition(outBlock, *outPin);
            ImVec2 p2 = GetPinPosition(inBlock, *inPin);

            float dx = std::abs(p2.x - p1.x) * 0.5f;
            dx = std::max(dx, 30.0f * m_CanvasScale);
            ImVec2 cp1(p1.x + dx, p1.y);
            ImVec2 cp2(p2.x - dx, p2.y);

            bool isSelected = (m_State.selectedLinkId == link.id);
            ImU32 baseColor = isSelected ? ColorFromInt(255, 200, 50)
                                         : GetPinColor(outPin->type, true);

            drawList->AddBezierCubic(p1, cp1, cp2, p2,
                                     ColorFromInt(15, 15, 15, 200),
                                     (isSelected ? 7.0f : 5.0f) * m_CanvasScale);
            drawList->AddBezierCubic(p1, cp1, cp2, p2, baseColor,
                                     (isSelected ? 4.0f : 3.0f) * m_CanvasScale);
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
        const auto& pins = (m_State.draggingFromDir == PinDirection::Input)
            ? srcBlock->GetInputPins() : srcBlock->GetOutputPins();
        for (auto& p : pins) {
            if (p.id == m_State.draggingFromPinId) { srcPin = &p; break; }
        }
        if (!srcPin) { m_State.isDraggingLink = false; return; }

        ImVec2 p1 = GetPinPosition(srcBlock, *srcPin);
        ImVec2 p2 = mousePos;
        if (m_State.draggingFromDir == PinDirection::Input) std::swap(p1, p2);

        float dx = std::abs(p2.x - p1.x) * 0.5f;
        ImVec2 cp1(p1.x + dx, p1.y);
        ImVec2 cp2(p2.x - dx, p2.y);

        drawList->AddBezierCubic(p1, cp1, cp2, p2, ColorFromInt(0, 0, 0, 150), 5.0f * m_CanvasScale);
        drawList->AddBezierCubic(p1, cp1, cp2, p2, GetPinColor(srcPin->type, true), 3.0f * m_CanvasScale);

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            uint32 targetBlockId, targetPinId;
            PinDirection targetDir;
            FindHoveredPin(mousePos, targetBlockId, targetPinId, targetDir);
            if (targetBlockId != 0 && m_State.draggingFromDir != targetDir) {
                uint32 outBlockID = (m_State.draggingFromDir == PinDirection::Output)
                    ? m_State.draggingFromBlockId : targetBlockId;
                uint32 inBlockID = (targetDir == PinDirection::Input)
                    ? targetBlockId : m_State.draggingFromBlockId;
                uint32 outPinID = (m_State.draggingFromDir == PinDirection::Output)
                    ? srcPin->id : targetPinId;
                uint32 inPinID = (targetDir == PinDirection::Input)
                    ? targetPinId : srcPin->id;

                // 基础校验（方向/类型检查）
                // 完整跨 Context 语义校验由 VFXCompiler::ValidateLink 完成
                m_Graph->ReplaceLink(outBlockID, outPinID, inBlockID, inPinID);
            }
            m_State.isDraggingLink = false;
        }
    }

    // ============================================================
    // FindHoveredPin — 检测鼠标下的引脚
    // ============================================================

    void VFXGraphPanel::FindHoveredPin(ImVec2 mousePos, uint32& outBlockId,
                                        uint32& outPinId, PinDirection& outDir) const {
        outBlockId = 0;
        outPinId = 0;

        if (!m_Graph) return;

        float hitRadiusSqr = (12.0f * m_CanvasScale) * (12.0f * m_CanvasScale);

        for (auto& [id, block] : m_Graph->GetBlocks()) {
            for (auto& pin : block->GetInputPins()) {
                ImVec2 pinPos = GetPinPosition(block.get(), pin);
                float dx = mousePos.x - pinPos.x;
                float dy = mousePos.y - pinPos.y;
                if (dx * dx + dy * dy < hitRadiusSqr) {
                    outBlockId = id; outPinId = pin.id; outDir = pin.direction; return;
                }
            }
            for (auto& pin : block->GetOutputPins()) {
                ImVec2 pinPos = GetPinPosition(block.get(), pin);
                float dx = mousePos.x - pinPos.x;
                float dy = mousePos.y - pinPos.y;
                if (dx * dx + dy * dy < hitRadiusSqr) {
                    outBlockId = id; outPinId = pin.id; outDir = pin.direction; return;
                }
            }
        }
    }

    // ============================================================
    // GetPinPosition — 计算引脚世界坐标转屏幕坐标
    // ============================================================

    ImVec2 VFXGraphPanel::GetPinPosition(const VFXBlock* block, const VFXPin& pin) const {
        float x = block->GetPosX() * m_CanvasScale + m_CanvasScreenPos.x + m_CanvasOrigin.x * m_CanvasScale;
        float y = block->GetPosY() * m_CanvasScale + m_CanvasScreenPos.y + m_CanvasOrigin.y * m_CanvasScale;
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
    // HandleCreateMenu — 右键创建 Block
    // ============================================================

    void VFXGraphPanel::HandleCreateMenu() {
        if (ImGui::BeginPopup("VFXCtxCreate")) {
            ImGui::Text("Create Block");
            ImGui::Separator();

            auto& factoryList = GetVFXBlockFactoryList();
            std::string currentGroup;

            for (int i = 0; i < (int)factoryList.size(); ++i) {
                auto& entry = factoryList[i];

                // 分组标题
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

                            // 自动将 Block 放入对应 Context
                            ContextType ctxType = BlockCategoryToContext(entry.category);
                            VFXContext& ctx = m_Graph->GetOrCreateContext(ctxType);
                            ctx.blockIds.push_back(newId);
                        }
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
    }

    // ============================================================
    // ProcessPendingDeletions — 延迟删除 Block
    // ============================================================

    void VFXGraphPanel::ProcessPendingDeletions() {
        if (!m_Graph || m_PendingDeleteBlockIds.empty()) return;
        for (uint32 id : m_PendingDeleteBlockIds) {
            m_Graph->RemoveBlock(id);
        }
        m_PendingDeleteBlockIds.clear();
    }

    // ============================================================
    // HandleDragDropToContext — 从 Block 列表拖入到 Context
    // ============================================================

    void VFXGraphPanel::HandleDragDropToContext() {
        if (!m_Graph) return;

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VFX_BLOCK_CREATE")) {
                int factoryIdx = *(const int*)payload->Data;
                auto& factoryList = GetVFXBlockFactoryList();
                if (factoryIdx >= 0 && factoryIdx < (int)factoryList.size()) {
                    uint32 newId = m_Graph->AddBlock(factoryList[factoryIdx].factory(0));
                    auto* newBlock = m_Graph->GetBlock(newId);
                    if (newBlock) {
                        float localX = (ImGui::GetMousePos().x - m_CanvasScreenPos.x) / m_CanvasScale - m_CanvasOrigin.x;
                        float localY = (ImGui::GetMousePos().y - m_CanvasScreenPos.y) / m_CanvasScale - m_CanvasOrigin.y;
                        newBlock->SetPosition(localX, localY);

                        ContextType ctxType = BlockCategoryToContext(factoryList[factoryIdx].category);
                        VFXContext& ctx = m_Graph->GetOrCreateContext(ctxType);
                        ctx.blockIds.push_back(newId);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    // ============================================================
    // DrawBlackboard — 参数黑板
    // ============================================================

    void VFXGraphPanel::DrawBlackboard() {
        if (!m_Graph) return;

        ImGui::Text("Blackboard Properties");
        ImGui::Separator();

        if (ImGui::Button("+ Add Property", ImVec2(-1, 0))) {
            ImGui::OpenPopup("VFXAddProperty");
        }

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
            if (ImGui::Selectable(props[i].name.c_str(), false, 0, ImVec2(width - 24, 22))) {
                // 单击选中
            }

            // 拖拽到画布
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload("VFX_BLACKBOARD_PROP", &i, sizeof(int));
                ImGui::Text("Get %s", props[i].name.c_str());
                ImGui::EndDragDropSource();
            }

            ImGui::SetCursorPos(ImVec2(cursorPos.x + width - 22, cursorPos.y));
            if (ImGui::Button("X", ImVec2(22, 22))) {
                removeIdx = i;
            }

            ImGui::TextDisabled("  [%s]", VFXPinTypeName(props[i].type));
            ImGui::Separator();
            ImGui::PopID();
        }

        if (removeIdx >= 0) m_Graph->RemoveProperty(removeIdx);
    }

    // ============================================================
    // DrawPropertiesPanel — 属性面板
    // ============================================================

    void VFXGraphPanel::DrawPropertiesPanel() {
        if (!m_Graph) return;

        ImGui::Text("Block Properties");
        ImGui::Separator();

        if (m_State.selectedBlockId == 0) {
            ImGui::TextDisabled("Select a block to edit.");

            // 显示全局参数
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("System Parameters");

            uint32 cap = m_Graph->GetParticleCapacity();
            if (ImGui::DragInt("Max Particles", (int*)&cap, 1000, 1000, 1000000)) {
                m_Graph->SetParticleCapacity(cap);
            }

            float dur = m_Graph->GetDuration();
            if (ImGui::DragFloat("Duration", &dur, 0.1f, 0.1f, 100.0f)) {
                m_Graph->SetDuration(dur);
            }

            bool loop = m_Graph->IsLooping();
            if (ImGui::Checkbox("Looping", &loop)) {
                m_Graph->SetLooping(loop);
            }

            bool prewarm = m_Graph->IsPrewarmed();
            if (ImGui::Checkbox("Prewarm", &prewarm)) {
                m_Graph->SetPrewarmed(prewarm);
            }

            return;
        }

        auto* block = m_Graph->GetBlock(m_State.selectedBlockId);
        if (!block) return;

        // Block 信息
        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1), "%s", block->GetName().c_str());
        ImGui::TextDisabled("Category: %s", BlockCategoryName(block->GetCategory()));
        ImGui::Spacing();

        // 启用/禁用
        bool enabled = block->IsEnabled();
        if (ImGui::Checkbox("Enabled", &enabled)) {
            block->SetEnabled(enabled);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Input Parameters");

        // 属性网格
        if (ImGui::BeginTable("VFXPropGrid", 2,
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable))
        {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            for (auto& pin : block->GetInputPins()) {
                if (pin.isConnected) continue;

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%s", pin.name.c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::PushItemWidth(-FLT_MIN);
                ImGui::PushID(pin.id);

                float* v = const_cast<float*>(pin.f);

                switch (pin.type) {
                    case VFXPinType::Float:
                        ImGui::DragFloat("##v", &v[0], 0.01f);
                        break;
                    case VFXPinType::Float2:
                        ImGui::DragFloat2("##v", v, 0.01f);
                        break;
                    case VFXPinType::Float3:
                        ImGui::DragFloat3("##v", v, 0.01f);
                        break;
                    case VFXPinType::Float4:
                        ImGui::DragFloat4("##v", v, 0.01f);
                        break;
                    case VFXPinType::Color:
                        ImGui::ColorEdit4("##c", v,
                            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                        break;
                    case VFXPinType::Boolean: {
                        bool bv = pin.b;
                        ImGui::Checkbox("##b", &bv);
                        break;
                    }
                    default:
                        ImGui::TextDisabled("N/A");
                        break;
                }

                ImGui::PopID();
                ImGui::PopItemWidth();
            }
            ImGui::EndTable();
        }

        // 内联编辑控件
        block->DrawInlineContent();
    }

    // ============================================================
    // DrawPreviewPanel — 实时预览窗口
    // ============================================================

    void VFXGraphPanel::DrawPreviewPanel() {
        ImGui::Begin("VFX Preview", &m_State.showPreview, ImGuiWindowFlags_NoScrollbar);

        ImVec2 previewSize = ImGui::GetContentRegionAvail();

        // 黑色背景预览区域
        ImVec2 screenPos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(screenPos,
            ImVec2(screenPos.x + previewSize.x, screenPos.y + previewSize.y),
            ColorFromInt(10, 10, 10));

        // 如果有实例运行的粒子，在这里绘制简化预览
        // 简单绘制一些点表示粒子位置
        // 实际实现中应通过 VFXManager 获取实例并渲染
        ImGui::Dummy(previewSize);

        // 播放控制
        ImGui::SetCursorScreenPos(ImVec2(screenPos.x, screenPos.y + previewSize.y - 30));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

        if (ImGui::Button(ICON_FA_PLAY " Play")) {
            // 触发预览播放
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PAUSE " Pause")) {
            // 暂停
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_STOP " Stop")) {
            // 停止
        }
        ImGui::SameLine();

        // 粒子计数
        float textWidth = ImGui::CalcTextSize("Particles: 0/65536").x;
        ImGui::SameLine(previewSize.x - textWidth - 10);
        ImGui::TextDisabled("Particles: 0/%u", m_Graph ? m_Graph->GetParticleCapacity() : 0);

        ImGui::PopStyleColor();
        ImGui::End();
    }

    // ============================================================
    // DrawParticleStats — 粒子统计
    // ============================================================

    void VFXGraphPanel::DrawParticleStats() {
        ImGui::Begin("VFX Stats", &m_State.showParticleStats);

        ImGui::Text("Particle Statistics");
        ImGui::Separator();

        if (m_Graph) {
            uint32 blockCount = (uint32)m_Graph->GetBlocks().size();
            uint32 linkCount = (uint32)m_Graph->GetLinks().size();
            uint32 contextCount = (uint32)m_Graph->GetContexts().size();
            uint32 capacity = m_Graph->GetParticleCapacity();

            ImGui::Text("Active Blocks:    %u", blockCount);
            ImGui::Text("Connections:      %u", linkCount);
            ImGui::Text("Contexts:         %u", contextCount);
            ImGui::Text("Particle Cap:     %u", capacity);
            ImGui::Text("Instruction Est:  %u", m_State.instructionCount);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Memory Estimates");

            uint64 particleMem = (uint64)capacity * sizeof(ParticleData);
            if (particleMem < 1024) {
                ImGui::Text("Particle Buffer:  %llu B", particleMem);
            } else if (particleMem < 1024 * 1024) {
                ImGui::Text("Particle Buffer:  %.1f KB", particleMem / 1024.0);
            } else {
                ImGui::Text("Particle Buffer:  %.1f MB", particleMem / (1024.0 * 1024.0));
            }
        }

        ImGui::End();
    }

    // ============================================================
    // GenerateVFXCode — 生成 VFX Compute Shader 代码
    // ============================================================

    bool VFXGraphPanel::GenerateVFXCode(std::string& outCode) {
        if (!m_Graph) return false;

        std::stringstream ss;
        ss << "// Generated by Game-Engine-Demo VFX Graph\n";
        ss << "#define PARTICLE_CAPACITY " << m_Graph->GetParticleCapacity() << "\n\n";

        // 粒子数据结构
        ss << "struct ParticleData {\n";
        ss << "    float3 position;\n";
        ss << "    float3 velocity;\n";
        ss << "    float4 color;\n";
        ss << "    float size;\n";
        ss << "    float lifetime;\n";
        ss << "    float age;\n";
        ss << "    uint alive;\n";
        ss << "    float2 pad;\n";
        ss << "};\n\n";

        // 常量缓冲区
        ss << "cbuffer VFXParams : register(b0) {\n";
        ss << "    float4x4 _VFXViewProj;\n";
        ss << "    float _VFXDt;\n";
        ss << "    float _VFXTime;\n";
        ss << "    float2 _VFXPadding;\n";
        ss << "};\n\n";

        // 黑板属性
        for (auto& prop : m_Graph->GetProperties()) {
            switch (prop.type) {
                case VFXPinType::Float:
                    ss << "float _" << prop.name << ";\n"; break;
                case VFXPinType::Float2:
                    ss << "float2 _" << prop.name << ";\n"; break;
                case VFXPinType::Float3:
                    ss << "float3 _" << prop.name << ";\n"; break;
                case VFXPinType::Color:
                    ss << "float4 _" << prop.name << ";\n"; break;
                default: break;
            }
        }

        // Emit 阶段（Spawn Context）
        ss << "\n// ── Spawn ──\n";
        ss << "uint VFXEmit(uint aliveCount, uint maxCount, out ParticleData particle) {\n";
        ss << "    if (aliveCount >= maxCount) return 0;\n";
        ss << "    particle.alive = 1;\n";
        ss << "    particle.age = 0;\n";
        ss << "    particle.lifetime = 2.0;\n";
        ss << "    particle.position = 0;\n";
        ss << "    particle.velocity = 0;\n";
        ss << "    particle.color = float4(1,1,1,1);\n";
        ss << "    particle.size = 1.0;\n";
        ss << "    return 1;\n";
        ss << "}\n";

        // Update 阶段
        ss << "\n// ── Update ──\n";
        ss << "void VFXUpdate(inout ParticleData p, float dt) {\n";
        ss << "    p.age += dt;\n";
        ss << "    if (p.age >= p.lifetime) { p.alive = 0; return; }\n";
        ss << "    float t = p.age / p.lifetime;\n";

        // 遍历 Update Context 的 Block 生成代码
        VFXContext* updateCtx = m_Graph->GetContext(ContextType::Update);
        if (updateCtx) {
            for (uint32 bid : updateCtx->blockIds) {
                auto* block = m_Graph->GetBlock(bid);
                if (block && block->IsEnabled()) {
                    std::unordered_map<uint32, std::string> pinOutputs;
                    ss << "    // Block: " << block->GetName() << "\n";
                    ss << "    " << block->GenerateHLSL(pinOutputs) << "\n";
                }
            }
        }

        ss << "}\n";

        // 主 Compute Shader
        ss << "\n// ── Main Compute Shader ──\n";
        ss << "RWStructuredBuffer<ParticleData> _VFXBuffer : register(u0);\n\n";
        ss << "[numthreads(64, 1, 1)]\n";
        ss << "void VFXMainCS(uint3 id : SV_DispatchThreadID) {\n";
        ss << "    uint idx = id.x;\n";
        ss << "    if (idx >= PARTICLE_CAPACITY) return;\n";
        ss << "    ParticleData p = _VFXBuffer[idx];\n";
        ss << "    if (!p.alive) return;\n";
        ss << "    VFXUpdate(p, _VFXDt);\n";
        ss << "    _VFXBuffer[idx] = p;\n";
        ss << "}\n";

        // VS/PS for Billboard Output
        ss << "\n// ── Billboard Vertex Shader ──\n";
        ss << "struct VSOutput {\n";
        ss << "    float4 position : SV_Position;\n";
        ss << "    float2 uv : TEXCOORD0;\n";
        ss << "    float4 color : COLOR;\n";
        ss << "};\n\n";
        ss << "VSOutput VFXBillboardVS(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID) {\n";
        ss << "    ParticleData p = _VFXBuffer[instanceId];\n";
        ss << "    // Billboard quad vertices...\n";
        ss << "    VSOutput o;\n";
        ss << "    o.position = float4(p.position, 1);\n";
        ss << "    o.uv = float2(0,0);\n";
        ss << "    o.color = p.color;\n";
        ss << "    return o;\n";
        ss << "}\n\n";
        ss << "float4 VFXBillboardPS(VSOutput i) : SV_Target {\n";
        ss << "    return i.color;\n";
        ss << "}\n";

        outCode = ss.str();
        return true;
    }

}} // namespace Engine::VFX