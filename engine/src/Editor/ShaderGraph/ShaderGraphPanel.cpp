#include "Engine/Editor/ShaderGraph/ShaderGraphPanel.h"
#include "Engine/Editor/IconsFontAwesome6.h"
#include "Engine/Platform/FileDialog.h"
#include <imgui_internal.h>
#include <algorithm>
#include <sstream>

namespace Engine {
namespace ShaderGraph {

    // ============================================================
    // 专门用于关联 Blackboard 属性的特殊节点
    // ============================================================
    class ShaderPropertyNode : public ShaderNode {
    public:
        ShaderPropertyNode(uint32 id, const std::string& propName, PinType propType)
            : ShaderNode(id, "Get " + propName, NodeCategory::Input), m_PropName(propName) {
            AddOutputPin(propName, propType);
            m_SizeX = 140.0f; m_SizeY = 45.0f;
        }

        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>&) const override {
            return "_" + m_PropName;
        }

        std::unique_ptr<ShaderNode> Clone(uint32 newId) const override {
            return std::make_unique<ShaderPropertyNode>(newId, m_PropName, m_OutputPins[0].type);
        }

        void DrawInlineContent() override {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 1.0f, 1.0f));
            ImGui::Text("Linked: %s", m_PropName.c_str());
            ImGui::PopStyleColor();
        }

        std::string m_PropName;
    };

    static std::vector<uint32> s_PendingDeleteNodeIds;

    // ============================================================
    // 颜色工具
    // ============================================================

    ImU32 ShaderGraphPanel::ColorFromInt(int r, int g, int b, int a) {
        return IM_COL32(r, g, b, a);
    }

    ImU32 ShaderGraphPanel::GetNodeColor(NodeCategory cat) const {
        switch (cat) {
            case NodeCategory::Math:    return ColorNodeMath;
            case NodeCategory::Input:   return ColorNodeInput;
            case NodeCategory::Vector:  return ColorNodeVector;
            case NodeCategory::Art:     return ColorNodeArt;
            case NodeCategory::Texture: return ColorNodeTex;
            case NodeCategory::Master:  return ColorNodeMaster;
            default:                    return ColorFromInt(80, 80, 80);
        }
    }

    ImU32 ShaderGraphPanel::GetPinColor(PinType type, bool connected) const {
        // 未连接的引脚颜色会变暗，连线颜色使用明亮的基色
        switch (type) {
            case PinType::Float:        return connected ? IM_COL32(100, 180, 255, 255) : IM_COL32(60, 100, 150, 255);
            case PinType::Float2:       return connected ? IM_COL32(180, 230, 100, 255) : IM_COL32(100, 150, 60, 255);
            case PinType::Float3:       return connected ? IM_COL32(255, 220, 80, 255)  : IM_COL32(160, 140, 50, 255);
            case PinType::Float4:       return connected ? IM_COL32(255, 150, 200, 255) : IM_COL32(150, 80, 120, 255);
            case PinType::Color:        return connected ? IM_COL32(255, 150, 200, 255) : IM_COL32(150, 80, 120, 255);
            case PinType::Texture2D:    return connected ? IM_COL32(255, 180, 80, 255)  : IM_COL32(160, 110, 50, 255);
            case PinType::Boolean:      return connected ? IM_COL32(200, 100, 100, 255) : IM_COL32(120, 60, 60, 255);
            default:                    return ColorFromInt(180, 180, 180);
        }
    }

    ShaderGraphPanel::ShaderGraphPanel() {
        m_CanvasScale = 1.0f;
    }

    // ============================================================
    // 世界与屏幕坐标系转换
    // ============================================================

    ImVec2 ShaderGraphPanel::WorldToScreen(float wx, float wy) const {
        return ImVec2(
            (wx + m_CanvasOrigin.x) * m_CanvasScale + m_CanvasScreenPos.x,
            (wy + m_CanvasOrigin.y) * m_CanvasScale + m_CanvasScreenPos.y
        );
    }

    ImVec2 ShaderGraphPanel::ScreenToWorld(float sx, float sy) const {
        return ImVec2(
            (sx - m_CanvasScreenPos.x) / m_CanvasScale - m_CanvasOrigin.x,
            (sy - m_CanvasScreenPos.y) / m_CanvasScale - m_CanvasOrigin.y
        );
    }

    // ============================================================
    // DrawToolbar — 工业级扁平工具栏
    // ============================================================
    void ShaderGraphPanel::DrawToolbar() {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

        if (ImGui::Button(ICON_FA_SAVE " Save")) {
            if (m_Graph) {
                std::string path = FileDialog::SaveFile("Shader Graph (*.sg)\0*.sg\0", "sg");
                if (!path.empty()) m_Graph->SaveToFile(path);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FOLDER_OPEN " Load")) {
            if (m_Graph) {
                std::string path = FileDialog::OpenFile("Shader Graph (*.sg)\0*.sg\0");
                if (!path.empty()) {
                    m_Graph->LoadFromFile(path);
                    m_CanvasOrigin = ImVec2(0, 0); // 重置视图
                    m_State.selectedNodeId = 0;
                    m_State.multiSelectedNodeIds.clear();
                }
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled(" | ");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_CODE " Compile")) {
            m_State.compileErrors.clear();
            if (!m_Graph || m_Graph->GetMasterNodeId() == 0) {
                m_State.compileErrors.push_back("Compile Failed: Master Node is missing.");
            } else {
                std::string code;
                if (GenerateShaderCode(code)) {
                    m_State.generatedCode = code;
                    m_State.showCodePreview = true;
                } else {
                    m_State.compileErrors.push_back("Compile Failed: Cyclic logic detected.");
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_GRID " Grid")) { m_State.showGrid = !m_State.showGrid; }

        float textWidth = ImGui::CalcTextSize("Nodes: 999  |  Links: 999").x;
        ImGui::SameLine(ImGui::GetWindowWidth() - textWidth - 20);
        ImGui::TextDisabled("Nodes: %zu  |  Links: %zu",
            m_Graph ? m_Graph->GetNodes().size() : 0,
            m_Graph ? m_Graph->GetLinks().size() : 0);

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    // ============================================================
    // OnImGui — 主入口
    // ============================================================
    void ShaderGraphPanel::OnImGui() {
        ImGui::Begin(m_Title.c_str(), nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        DrawToolbar();

        if (!m_State.compileErrors.empty()) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.3f, 0.1f, 0.1f, 1.0f));
            if (ImGui::BeginChild("ErrorBanner", ImVec2(0, 28), false, ImGuiWindowFlags_NoScrollbar)) {
                ImGui::SetCursorPos(ImVec2(10, 6));
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), ICON_FA_EXCLAMATION_TRIANGLE " %s", m_State.compileErrors[0].c_str());
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        ImGui::Separator();

        if (ImGui::BeginTabBar("ShaderTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_NoTooltip)) {
            bool isOpen = true;
            ImGuiTabItemFlags flags = (m_Graph && m_Graph->IsDirty()) ? ImGuiTabItemFlags_UnsavedDocument : 0;
            if (ImGui::BeginTabItem("CurrentShader.sg", &isOpen, flags)) {
                DrawMainEditorLayout();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        if (m_State.showCodePreview) {
            DrawCodePreview();
        }

        ImGui::End();
    }

    // ============================================================
    // DrawMainEditorLayout — 三栏布局 (左栏整合节点列表和黑板)
    // ============================================================
    void ShaderGraphPanel::DrawMainEditorLayout() {
        if (ImGui::BeginTable("SGTable", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Node List", ImGuiTableColumnFlags_WidthFixed, 220.0f);
            ImGui::TableSetupColumn("Canvas", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthFixed, 250.0f);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            if (ImGui::BeginTabBar("LeftTabs")) {
                if (ImGui::BeginTabItem("Nodes")) {
                    ImGui::TextDisabled("Search Nodes:");
                    static char searchBuf[128] = "";
                    ImGui::InputText("##nodesearch", searchBuf, sizeof(searchBuf));
                    ImGui::Separator();

                    std::string searchStr = searchBuf;
                    std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);

                    auto factoryList = GetNodeFactoryList(); 
                    std::string currentGroup;

                    for (int i = 0; i < (int)factoryList.size(); ++i) {
                        auto& entry = factoryList[i];
                        if (!searchStr.empty()) {
                            std::string lowerName = entry.name;
                            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                            if (lowerName.find(searchStr) == std::string::npos) continue;
                        }

                        std::string groupName = NodeCategoryName(entry.category);
                        if (groupName != currentGroup) {
                            currentGroup = groupName;
                            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "── %s ──", groupName.c_str());
                        }

                        ImGui::PushID(i);
                        ImGui::Selectable(entry.name, false, ImGuiSelectableFlags_AllowOverlap);
                        
                        // --- 允许直接从左栏拖拽创建 ---
                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                            ImGui::SetDragDropPayload("DND_SHADER_NODE_CREATE", &i, sizeof(int));
                            ImGui::Text(ICON_FA_CUBE " Create %s", entry.name);
                            ImGui::EndDragDropSource();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTabItem();
                }
                
                if (ImGui::BeginTabItem("Blackboard")) {
                    DrawBlackboard();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }

            ImGui::TableSetColumnIndex(1);
            DrawNodeCanvas();

            ImGui::TableSetColumnIndex(2);
            if (m_State.showProperties) DrawPropertiesPanel();

            ImGui::EndTable();
        }
    }

    // ============================================================
    // FindHoveredPin — 精准引脚碰撞检测
    // ============================================================
    bool ShaderGraphPanel::FindHoveredPin(ImVec2 mousePos, uint32& outNodeId, uint32& outPinId, PinDirection& outDir) const {
        if (!m_Graph) return false;
        float hitRadiusSqr = (12.0f * m_CanvasScale) * (12.0f * m_CanvasScale);

        for (auto& [id, node] : m_Graph->GetNodes()) {
            for (auto& pin : node->GetInputPins()) {
                ImVec2 pinPos = GetPinPosition(node.get(), pin);
                if (ImLengthSqr(ImVec2(mousePos.x - pinPos.x, mousePos.y - pinPos.y)) < hitRadiusSqr) {
                    outNodeId = id; outPinId = pin.id; outDir = pin.direction; return true;
                }
            }
            for (auto& pin : node->GetOutputPins()) {
                ImVec2 pinPos = GetPinPosition(node.get(), pin);
                if (ImLengthSqr(ImVec2(mousePos.x - pinPos.x, mousePos.y - pinPos.y)) < hitRadiusSqr) {
                    outNodeId = id; outPinId = pin.id; outDir = pin.direction; return true;
                }
            }
        }
        return false;
    }

    // ============================================================
    // DrawNodeCanvas — 核心画布
    // ============================================================
    void ShaderGraphPanel::DrawNodeCanvas() {
        m_CanvasSize = ImGui::GetContentRegionAvail();
        m_CanvasScreenPos = ImGui::GetCursorScreenPos();
        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // ── 0. 全局无阻挡滚轮缩放 ──
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
            m_State.multiSelectedNodeIds.clear();
            if (m_Graph) {
                for (auto& [id, node] : m_Graph->GetNodes()) {
                    m_State.multiSelectedNodeIds.push_back(id);
                }
            }
        }

        // ── 1. 画布底层与网格 ──
        drawList->AddRectFilled(m_CanvasScreenPos, ImVec2(m_CanvasScreenPos.x + m_CanvasSize.x, m_CanvasScreenPos.y + m_CanvasSize.y), ColorFromInt(30, 30, 30));
        
        if (m_State.showGrid) {
            float gridSize = 20.0f * m_CanvasScale;
            float majorGridSize = gridSize * 5.0f;
            ImVec2 origin = ImVec2(m_CanvasScreenPos.x + m_CanvasOrigin.x * m_CanvasScale, m_CanvasScreenPos.y + m_CanvasOrigin.y * m_CanvasScale);
            for (float x = fmodf(origin.x, gridSize); x < m_CanvasSize.x; x += gridSize) {
                drawList->AddLine(ImVec2(m_CanvasScreenPos.x + x, m_CanvasScreenPos.y), ImVec2(m_CanvasScreenPos.x + x, m_CanvasScreenPos.y + m_CanvasSize.y), ColorFromInt(45, 45, 45, 100));
            }
            for (float y = fmodf(origin.y, gridSize); y < m_CanvasSize.y; y += gridSize) {
                drawList->AddLine(ImVec2(m_CanvasScreenPos.x, m_CanvasScreenPos.y + y), ImVec2(m_CanvasScreenPos.x + m_CanvasSize.x, m_CanvasScreenPos.y + y), ColorFromInt(45, 45, 45, 100));
            }
            for (float x = fmodf(origin.x, majorGridSize); x < m_CanvasSize.x; x += majorGridSize) {
                drawList->AddLine(ImVec2(m_CanvasScreenPos.x + x, m_CanvasScreenPos.y), ImVec2(m_CanvasScreenPos.x + x, m_CanvasScreenPos.y + m_CanvasSize.y), ColorFromInt(60, 60, 60, 200));
            }
            for (float y = fmodf(origin.y, majorGridSize); y < m_CanvasSize.y; y += majorGridSize) {
                drawList->AddLine(ImVec2(m_CanvasScreenPos.x, m_CanvasScreenPos.y + y), ImVec2(m_CanvasScreenPos.x + m_CanvasSize.x, m_CanvasScreenPos.y + y), ColorFromInt(60, 60, 60, 200));
            }
        }

        // ── 2. 交互底板 ──
        ImGui::SetCursorScreenPos(m_CanvasScreenPos);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("Canvas", m_CanvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        
        bool isCanvasActive = ImGui::IsItemActive();
        bool isCanvasHovered = ImGui::IsItemHovered();
        bool isCanvasClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        ImGuiID canvasId = ImGui::GetID("Canvas");

        // ── 处理画布全局 DND 放置 ──
        if (m_Graph && ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_SHADER_NODE_CREATE")) {
                int factoryIdx = *(const int*)payload->Data;
                auto factoryList = GetNodeFactoryList();
                if (factoryIdx >= 0 && factoryIdx < (int)factoryList.size()) {
                    uint32 newId = m_Graph->AddNode(factoryList[factoryIdx].factory(0));
                    auto* newNode = m_Graph->GetNode(newId);
                    if (newNode) {
                        ImVec2 dropPos = ScreenToWorld(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
                        newNode->SetPosition(dropPos.x, dropPos.y);
                    }
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_SHADER_BLACKBOARD_PROP")) {
                int propIdx = *(const int*)payload->Data;
                auto& props = m_Graph->GetProperties();
                if (propIdx >= 0 && propIdx < (int)props.size()) {
                    auto& prop = props[propIdx];
                    uint32 newId = m_Graph->AddNode(std::make_unique<ShaderPropertyNode>(0, prop.name, prop.type));
                    auto* newNode = m_Graph->GetNode(newId);
                    if (newNode) {
                        ImVec2 dropPos = ScreenToWorld(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
                        newNode->SetPosition(dropPos.x, dropPos.y);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        // 点击画布空白处取消选择
        if (isCanvasClicked && !io.KeyCtrl && GImGui->HoveredId == canvasId) {
            m_State.selectedNodeId = 0;
            m_State.selectedLinkId = 0;
            m_State.multiSelectedNodeIds.clear();
        }

        // ── 3. 裁剪与节点绘制 ──
        drawList->PushClipRect(m_CanvasScreenPos, ImVec2(m_CanvasScreenPos.x + m_CanvasSize.x, m_CanvasScreenPos.y + m_CanvasSize.y), true);

        DrawNodeGroupBoxes();

        if (m_Graph) DrawLinks();

        s_PendingDeleteNodeIds.clear();
        if (m_Graph) {
            for (auto& [id, node] : m_Graph->GetNodes()) {
                DrawNode(node.get());
            }
        }

        // ── 4. 高级交互：框选、成组、拖拽连线 ──
        HandleMultiselect();
        
        // 快捷键 C 成组
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsKeyPressed(ImGuiKey_C) && !m_State.multiSelectedNodeIds.empty()) {
            NodeGroup newGroup;
            newGroup.id = m_NextGroupId++;
            newGroup.memberNodeIds = m_State.multiSelectedNodeIds;
            newGroup.name = "New Comment Group";

            float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
            for (uint32 id : newGroup.memberNodeIds) {
                auto* b = m_Graph->GetNode(id);
                if (b) {
                    minX = std::min(minX, b->GetPosX()); minY = std::min(minY, b->GetPosY());
                    maxX = std::max(maxX, b->GetPosX() + b->GetSizeX()); maxY = std::max(maxY, b->GetPosY() + b->GetSizeY());
                }
            }
            newGroup.posX = minX - 20; newGroup.posY = minY - 40;
            newGroup.sizeX = (maxX - minX) + 40; newGroup.sizeY = (maxY - minY) + 60;
            m_Groups.push_back(newGroup);
        }

        HandleLinkDragging();

        // 中键 / Alt 平移
        if (isCanvasActive || io.KeyAlt) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && io.KeyAlt)) {
                m_CanvasOrigin.x += io.MouseDelta.x / m_CanvasScale;
                m_CanvasOrigin.y += io.MouseDelta.y / m_CanvasScale;
            }
        }

        // 右键菜单
        if (isCanvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && GImGui->HoveredId == canvasId) {
            m_State.quickSearchPosX = ImGui::GetMousePos().x;
            m_State.quickSearchPosY = ImGui::GetMousePos().y;
            ImGui::OpenPopup("CreateNode");
        }

        for (uint32 id : s_PendingDeleteNodeIds) { m_Graph->RemoveNode(id); }
        s_PendingDeleteNodeIds.clear();

        drawList->PopClipRect();
        DrawCreateNodeMenu();
    }

    // ============================================================
    // HandleMultiselect — 精准 AABB 框选
    // ============================================================
    void ShaderGraphPanel::HandleMultiselect() {
        ImGuiIO& io = ImGui::GetIO();
        ImGuiID canvasId = ImGui::GetID("Canvas");
        ImGuiID activeId = GImGui->ActiveId;

        // 在空白画布上按住左键开始框选
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && activeId == canvasId) {
            m_IsBoxSelecting = true;
            m_BoxSelectStart = ImGui::GetMousePos();
            if (!io.KeyCtrl) m_State.multiSelectedNodeIds.clear();
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
                    for (auto& [id, node] : m_Graph->GetNodes()) {
                        ImVec2 pMin = WorldToScreen(node->GetPosX(), node->GetPosY());
                        float nodeW = node->GetSizeX() * m_CanvasScale;
                        float nodeH = (28 + std::max((int)node->GetInputPins().size(), (int)node->GetOutputPins().size()) * 22 + 12) * m_CanvasScale;
                        ImVec2 pMax = ImVec2(pMin.x + nodeW, pMin.y + nodeH);
                        
                        ImRect nodeRect(pMin, pMax);
                        if (selectRect.Overlaps(nodeRect)) {
                            if (std::find(m_State.multiSelectedNodeIds.begin(), m_State.multiSelectedNodeIds.end(), id) == m_State.multiSelectedNodeIds.end()) {
                                m_State.multiSelectedNodeIds.push_back(id);
                            }
                        }
                    }
                }
            }
        }
    }

    // ============================================================
    // DrawNodeGroupBoxes
    // ============================================================
    void ShaderGraphPanel::DrawNodeGroupBoxes() {
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

    // ============================================================
    // DrawNode — 工业级节点绘制，支持多选拖动与 LOD
    // ============================================================
    void ShaderGraphPanel::DrawNode(ShaderNode* node) {
        if (!node) return;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float scale = m_CanvasScale;

        // LOD 判定
        bool showDetail = (scale > 0.7f);
        bool showTitle = (scale > 0.4f);

        ImVec2 pos = WorldToScreen(node->GetPosX(), node->GetPosY());

        float nodeW = node->GetSizeX() * scale;
        float headerH = 28 * scale;
        float pinStartY = headerH + 8 * scale;
        float pinSpacing = 22 * scale;

        int inputCount = (int)node->GetInputPins().size();
        int outputCount = (int)node->GetOutputPins().size();
        int maxPins = std::max(inputCount, outputCount);
        float bodyH = std::max((float)maxPins * pinSpacing + 12 * scale, 40.0f * scale);
        float nodeH = headerH + bodyH;

        bool inMultiSelect = std::find(m_State.multiSelectedNodeIds.begin(), m_State.multiSelectedNodeIds.end(), node->GetId()) != m_State.multiSelectedNodeIds.end();
        bool isSelected = (m_State.selectedNodeId == node->GetId()) || inMultiSelect;

        ImVec2 r0 = pos;
        ImVec2 r1 = ImVec2(pos.x + nodeW, pos.y + nodeH);
        ImVec2 headerR1 = ImVec2(pos.x + nodeW, pos.y + headerH);

        // 背景阴影
        drawList->AddRectFilled(ImVec2(r0.x + 4 * scale, r0.y + 6 * scale), ImVec2(r1.x + 4 * scale, r1.y + 6 * scale), ColorFromInt(0, 0, 0, 120), 6.0f * scale);
        // 头部
        drawList->AddRectFilled(r0, headerR1, GetNodeColor(node->GetCategory()), 6.0f * scale, ImDrawFlags_RoundCornersTop);
        // 身体
        drawList->AddRectFilled(ImVec2(r0.x, headerR1.y), r1, ColorFromInt(35, 35, 40, 240), 6.0f * scale, ImDrawFlags_RoundCornersBottom);
        // 发光边框
        ImU32 borderColor = isSelected ? ColorFromInt(255, 165, 0, 255) : ColorFromInt(20, 20, 20, 255);
        drawList->AddRect(r0, r1, borderColor, 6.0f * scale, 0, isSelected ? 2.5f * scale : 1.5f * scale);

        // 标题 (LOD控制)
        if (showTitle) {
            ImVec2 titleSize = ImGui::CalcTextSize(node->GetName().c_str());
            ImU32 titleColor = ColorFromInt(240, 240, 240, (int)(std::clamp(scale, 0.4f, 1.0f) * 255));
            float textX = showDetail ? (r0.x + (nodeW - titleSize.x) * 0.5f) : (r0.x + 10 * scale);
            drawList->AddText(ImVec2(textX, r0.y + (headerH - titleSize.y) * 0.5f), titleColor, node->GetName().c_str());
        }

        // 引脚
        auto drawPins = [&](const std::vector<Pin>& pins, float px, bool isInput) {
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

                // 右键菜单 — 断开连接
                if (pins[i].isConnected) {
                    ImGui::PushID(pins[i].id);
                    ImGui::SetCursorScreenPos(ImVec2(px - pinRadius * 2, py - pinRadius * 2));
                    ImGui::InvisibleButton("pin_hit", ImVec2(pinRadius * 4, pinRadius * 4));
                    if (ImGui::BeginPopupContextItem("PinCtxMenu")) {
                        if (ImGui::MenuItem(ICON_FA_TIMES " Break Links")) {
                            // 在 GraphPanel 级别处理或者在 Graph 里实现 (安全起见，循环收集或调用)
                            // 由于我们没有 m_Graph->RemoveLinksConnectedToPin 的 API，
                            // 使用简单的遍历来删除：
                            for (size_t l = m_Graph->GetLinks().size(); l-- > 0; ) {
                                auto& link = m_Graph->GetLinks()[l];
                                if (link.outputPinId == pins[i].id || link.inputPinId == pins[i].id) {
                                    m_Graph->RemoveLink(link.id);
                                }
                            }
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
            }
        };
        drawPins(node->GetInputPins(), pos.x, true);
        drawPins(node->GetOutputPins(), pos.x + nodeW, false);

        // 连线拖拽判定
        bool mouseInCanvasLocal = ImGui::IsMouseHoveringRect(m_CanvasScreenPos, ImVec2(m_CanvasScreenPos.x + m_CanvasSize.x, m_CanvasScreenPos.y + m_CanvasSize.y));
        if (mouseInCanvasLocal) {
            auto checkPinClick = [&](const std::vector<Pin>& pins, float baseX, bool isInput) {
                for (size_t i = 0; i < pins.size(); ++i) {
                    float py = pos.y + pinStartY + i * pinSpacing;
                    float dx = ImGui::GetMousePos().x - baseX;
                    float dy = ImGui::GetMousePos().y - py;
                    if (dx * dx + dy * dy < (12.0f * scale) * (12.0f * scale)) {
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                            m_State.isDraggingLink = true;
                            m_State.draggingFromNodeId = node->GetId();
                            m_State.draggingFromPinId = pins[i].id;
                            m_State.draggingFromDir = pins[i].direction;
                        }
                    }
                }
            };
            checkPinClick(node->GetInputPins(), pos.x, true);
            checkPinClick(node->GetOutputPins(), pos.x + nodeW, false);
        }

        // ── 节点主体交互 (支持 Ctrl 累加多选) ──
        ImGui::SetCursorScreenPos(r0);
        ImGui::PushID(node->GetId());
        ImGui::InvisibleButton("node_body", ImVec2(nodeW, nodeH));

        if (!m_State.isDraggingLink) {
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                if (ImGui::GetIO().KeyCtrl) {
                    if (inMultiSelect) {
                        m_State.multiSelectedNodeIds.erase(
                            std::remove(m_State.multiSelectedNodeIds.begin(), m_State.multiSelectedNodeIds.end(), node->GetId()), 
                            m_State.multiSelectedNodeIds.end());
                    } else {
                        m_State.multiSelectedNodeIds.push_back(node->GetId());
                    }
                } else {
                    if (!inMultiSelect) {
                        m_State.multiSelectedNodeIds.clear();
                        m_State.multiSelectedNodeIds.push_back(node->GetId());
                    }
                }
                m_State.selectedNodeId = node->GetId();
                m_State.selectedLinkId = 0;
            }

            // 同步移动多选的节点
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 delta = ImVec2(ImGui::GetIO().MouseDelta.x / scale, ImGui::GetIO().MouseDelta.y / scale);
                if (inMultiSelect) {
                    for (uint32 selId : m_State.multiSelectedNodeIds) {
                        auto* selNode = m_Graph->GetNode(selId);
                        if (selNode) selNode->SetPosition(selNode->GetPosX() + delta.x, selNode->GetPosY() + delta.y);
                    }
                } else {
                    node->SetPosition(node->GetPosX() + delta.x, node->GetPosY() + delta.y);
                }
                if (m_Graph) m_Graph->MarkDirty();
            }
        }

        // 右键菜单
        if (ImGui::BeginPopupContextItem("BlockCtxMenu")) {
            if (ImGui::MenuItem(ICON_FA_TRASH " Delete Node")) {
                s_PendingDeleteNodeIds.push_back(node->GetId());
                m_State.selectedNodeId = 0;
            }
            ImGui::EndPopup();
        }

        if (isSelected && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            s_PendingDeleteNodeIds.push_back(node->GetId());
            m_State.selectedNodeId = 0;
        }

        ImGui::PopID();
    }

    // ============================================================
    // DrawLinks — 工业级 Spline 与高亮发光效果
    // ============================================================
    void ShaderGraphPanel::DrawLinks() {
        if (!m_Graph) return;
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        uint32 linkToDelete = 0;

        for (auto& link : m_Graph->GetLinks()) {
            auto* outNode = m_Graph->GetNode(link.outputNodeId);
            auto* inNode = m_Graph->GetNode(link.inputNodeId);
            if (!outNode || !inNode) continue;

            const Pin* outPin = nullptr;
            const Pin* inPin = nullptr;
            for (auto& p : outNode->GetOutputPins()) { if (p.id == link.outputPinId) { outPin = &p; break; } }
            for (auto& p : inNode->GetInputPins()) { if (p.id == link.inputPinId) { inPin = &p; break; } }
            if (!outPin || !inPin) continue;

            ImVec2 p1 = GetPinPosition(outNode, *outPin);
            ImVec2 p2 = GetPinPosition(inNode, *inPin);

            float dx = p2.x - p1.x;
            // 动态切线长度，保护距离过近或反向折叠
            float tangentStrength = std::clamp(std::abs(dx) * 0.5f, 30.0f * m_CanvasScale, 150.0f * m_CanvasScale);
            if (dx < 0) tangentStrength = std::max(tangentStrength, std::abs(dx) * 1.0f);

            ImVec2 cp1 = ImVec2(p1.x + tangentStrength, p1.y);
            ImVec2 cp2 = ImVec2(p2.x - tangentStrength, p2.y);

            bool isSelected = (m_State.selectedLinkId == link.id);
            // 连线颜色严格遵循输出引脚的色彩语义
            ImU32 baseColor = isSelected ? ColorFromInt(255, 200, 50) : GetPinColor(outPin->type, true);

            // 绘制阴影营造空间发光感
            drawList->AddBezierCubic(p1, cp1, cp2, p2, ColorFromInt(15, 15, 15, 200), (isSelected ? 7.0f : 5.0f) * m_CanvasScale);
            drawList->AddBezierCubic(p1, cp1, cp2, p2, baseColor, (isSelected ? 4.0f : 3.0f) * m_CanvasScale);

            // ── 交互：右键连线中点可删除 ──
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
    // HandleLinkDragging
    // ============================================================
    void ShaderGraphPanel::HandleLinkDragging() {
        if (!m_Graph || !m_State.isDraggingLink) return;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 mousePos = ImGui::GetMousePos();

        auto* srcNode = m_Graph->GetNode(m_State.draggingFromNodeId);
        if (!srcNode) { m_State.isDraggingLink = false; return; }

        const Pin* srcPin = nullptr;
        const auto& pins = (m_State.draggingFromDir == PinDirection::Input) ? srcNode->GetInputPins() : srcNode->GetOutputPins();
        for (auto& p : pins) { if (p.id == m_State.draggingFromPinId) { srcPin = &p; break; } }
        if (!srcPin) { m_State.isDraggingLink = false; return; }

        ImVec2 p1 = GetPinPosition(srcNode, *srcPin);
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
            uint32 targetNodeId, targetPinId;
            PinDirection targetDir;
            if (FindHoveredPin(mousePos, targetNodeId, targetPinId, targetDir)) {
                if (m_State.draggingFromDir != targetDir) {
                    uint32 outNodeID = (m_State.draggingFromDir == PinDirection::Output) ? m_State.draggingFromNodeId : targetNodeId;
                    uint32 inNodeID = (targetDir == PinDirection::Input) ? targetNodeId : m_State.draggingFromNodeId;
                    uint32 outPinID = (m_State.draggingFromDir == PinDirection::Output) ? srcPin->id : targetPinId;
                    uint32 inPinID = (targetDir == PinDirection::Input) ? targetPinId : srcPin->id;
                    m_Graph->ReplaceLink(outNodeID, outPinID, inNodeID, inPinID);
                }
            }
            m_State.isDraggingLink = false;
        }
    }

    // ============================================================
    // GetPinPosition
    // ============================================================
    ImVec2 ShaderGraphPanel::GetPinPosition(const ShaderNode* node, const Pin& pin) const {
        ImVec2 base = WorldToScreen(node->GetPosX(), node->GetPosY());
        float headerH = 28 * m_CanvasScale;
        float pinStartY = headerH + 8 * m_CanvasScale;
        float pinSpacing = 22 * m_CanvasScale;

        int pinIdx = 0;
        const auto& pins = (pin.direction == PinDirection::Input) ? node->GetInputPins() : node->GetOutputPins();
        for (size_t i = 0; i < pins.size(); ++i) {
            if (pins[i].id == pin.id) { pinIdx = (int)i; break; }
        }

        float px = base.x + (pin.direction == PinDirection::Input ? 0 : node->GetSizeX() * m_CanvasScale);
        float py = base.y + pinStartY + pinIdx * pinSpacing;
        return ImVec2(px, py);
    }

    // ============================================================
    // DrawCreateNodeMenu
    // ============================================================
    void ShaderGraphPanel::DrawCreateNodeMenu() {
        if (ImGui::BeginPopup("CreateNode")) {
            ImGui::Text("Create Node");
            ImGui::Separator();

            auto factoryList = GetNodeFactoryList();
            for (auto& entry : factoryList) {
                if (ImGui::MenuItem(entry.name)) {
                    if (m_Graph) {
                        uint32 newId = m_Graph->AddNode(entry.factory(0));
                        auto* newNode = m_Graph->GetNode(newId);
                        if (newNode) {
                            ImVec2 wp = ScreenToWorld(m_State.quickSearchPosX, m_State.quickSearchPosY);
                            newNode->SetPosition(wp.x, wp.y);
                        }
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
    }

    // ============================================================
    // DrawBlackboard
    // ============================================================
    void ShaderGraphPanel::DrawBlackboard() {
        if (!m_Graph) return;

        ImGui::Text("Blackboard");
        ImGui::Separator();

        if (ImGui::Button("+ Add Property", ImVec2(-1, 0))) {
            ImGui::OpenPopup("AddProperty");
        }

        if (ImGui::BeginPopup("AddProperty")) {
            static char nameBuf[128] = "NewProperty";
            static int selectedType = 0;
            const char* types[] = { "Float", "Color", "Vector2", "Vector3", "Texture2D" };

            ImGui::InputText("Name", nameBuf, sizeof(nameBuf));
            ImGui::Combo("Type", &selectedType, types, IM_ARRAYSIZE(types));
            if (ImGui::Button("Add", ImVec2(-1, 0))) {
                ShaderGraph::BlackboardProperty prop;
                prop.name = nameBuf;
                prop.type = (PinType)((int)PinType::Float + selectedType);
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
                ImGui::SetDragDropPayload("DND_SHADER_BLACKBOARD_PROP", &i, sizeof(int));
                ImGui::Text("Get %s", props[i].name.c_str());
                ImGui::EndDragDropSource();
            }

            ImGui::SetCursorPos(ImVec2(cursorPos.x + width - 22, cursorPos.y));
            if (ImGui::Button("X", ImVec2(22, 22))) {
                removeIdx = i;
            }

            ImGui::TextDisabled("  [%s]", PinTypeName(props[i].type));
            ImGui::Separator();
            ImGui::PopID();
        }

        if (removeIdx >= 0) m_Graph->RemoveProperty(removeIdx);
    }

    // ============================================================
    // DrawPropertiesPanel — 支持节点重命名及专属属性展示
    // ============================================================
    void ShaderGraphPanel::DrawPropertiesPanel() {
        if (!m_Graph) return;

        ImGui::Text("Node Properties");
        ImGui::Separator();

        if (m_State.selectedNodeId == 0) {
            ImGui::TextDisabled("Select a node to edit.");
            return;
        }

        auto* node = m_Graph->GetNode(m_State.selectedNodeId);
        if (!node) return;

        // 节点重命名
        char nameBuf[64];
        strncpy_s(nameBuf, node->GetName().c_str(), sizeof(nameBuf) - 1);
        if (ImGui::InputText(" Name", nameBuf, sizeof(nameBuf))) {
            node->SetName(nameBuf);
        }

        // 识别是否是引用的属性节点，允许修改其绑定的变量名
        auto propNode = dynamic_cast<ShaderPropertyNode*>(node);
        if (propNode) {
            char propBuf[64];
            strcpy_s(propBuf, propNode->m_PropName.c_str());
            if (ImGui::InputText("Source Property", propBuf, 64)) {
                propNode->m_PropName = propBuf;
                propNode->SetName("Get " + propNode->m_PropName);
            }
            ImGui::Separator();
        }

        ImGui::TextDisabled("Category: %s", NodeCategoryName(node->GetCategory()));
        ImGui::Spacing();

        if (ImGui::BeginTable("PropGrid", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Widget", ImGuiTableColumnFlags_WidthStretch);

            for (auto& pin : node->GetInputPins()) {
                if (pin.isConnected) continue;

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%s", pin.name.c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::PushItemWidth(-FLT_MIN);
                ImGui::PushID(pin.id);

                float* v = const_cast<float*>(pin.defaultValue.f);

                switch (pin.type) {
                    case PinType::Float:  ImGui::DragFloat("##v", &v[0], 0.01f); break;
                    case PinType::Float2: ImGui::DragFloat2("##v", v, 0.01f); break;
                    case PinType::Float3: ImGui::DragFloat3("##v", v, 0.01f); break;
                    case PinType::Float4: ImGui::DragFloat4("##v", v, 0.01f); break;
                    case PinType::Color:
                        ImGui::ColorEdit4("##c", v, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                        break;
                    case PinType::Boolean: {
                        bool bv = pin.defaultValue.b;
                        if(ImGui::Checkbox("##b", &bv)) const_cast<bool&>(pin.defaultValue.b) = bv; 
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
    }

    // ============================================================
    // DrawCodePreview
    // ============================================================
    void ShaderGraphPanel::DrawCodePreview() {
        if (!ImGui::Begin("Generated Code", &m_State.showCodePreview)) {
            ImGui::End();
            return;
        }
        ImGui::Text(ICON_FA_CODE " HLSL Shader Code");
        ImGui::Separator();
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
        static char codePreviewBuf[65536] = {};
        strncpy_s(codePreviewBuf, m_State.generatedCode.c_str(), sizeof(codePreviewBuf) - 1);
        ImGui::InputTextMultiline("##code", codePreviewBuf, sizeof(codePreviewBuf),
                                   ImVec2(-1, -1),
                                   ImGuiInputTextFlags_ReadOnly);
        ImGui::PopFont();
        ImGui::End();
    }

    // ============================================================
    // GenerateShaderCode
    // ============================================================
    bool ShaderGraphPanel::GenerateShaderCode(std::string& outCode) {
        if (!m_Graph) return false;

        std::stringstream ss;
        ss << "// Generated by Game-Engine-Demo Shader Graph\n";
        ss << "#define PI 3.1415926535\n\n";

        ss << "struct VSOutput {\n";
        ss << "    float4 position : SV_Position;\n";
        ss << "    float2 uv : TEXCOORD0;\n";
        ss << "    float3 normalWS : NORMAL;\n";
        ss << "    float3 positionWS : TEXCOORD1;\n";
        ss << "    float3 viewDir : TEXCOORD2;\n";
        ss << "    float3 tangentWS : TANGENT;\n";
        ss << "    float time : TIME;\n";
        ss << "};\n\n";

        ss << "struct ShaderOutput {\n";
        ss << "    float3 albedo;\n";
        ss << "    float3 normal;\n";
        ss << "    float opacity;\n";
        ss << "};\n\n";

        ss << "// Material Properties\n";
        for (auto& [id, node] : m_Graph->GetNodes()) {
            std::string propDecl = node->GeneratePropertyDecl();
            if (!propDecl.empty()) ss << propDecl << "\n";
        }
        for (auto& prop : m_Graph->GetProperties()) {
            switch (prop.type) {
                case PinType::Float:  ss << "float _" << prop.name << ";\n"; break;
                case PinType::Color:  ss << "float4 _" << prop.name << ";\n"; break;
                case PinType::Float2: ss << "float2 _" << prop.name << ";\n"; break;
                case PinType::Float3: ss << "float3 _" << prop.name << ";\n"; break;
                default: break;
            }
        }
        ss << "\n";

        uint32 masterId = m_Graph->GetMasterNodeId();
        if (masterId == 0) {
            for (auto& [id, node] : m_Graph->GetNodes()) {
                if (node->GetCategory() == NodeCategory::Master) {
                    masterId = id;
                    break;
                }
            }
        }
        if (masterId == 0) {
            outCode = ss.str() + "// No Master node found\n";
            return false;
        }

        ss << "float4 main(VSOutput IN) : SV_Target\n{\n";
        ss << "    ShaderOutput OUT;\n";
        ss << "    OUT.albedo = float3(0,0,0);\n";
        ss << "    OUT.normal = IN.normalWS;\n";
        ss << "    OUT.opacity = 1.0;\n\n";

        auto sortedNodes = m_Graph->TopologicalSort();
        std::unordered_map<uint32, std::string> pinOutputs;
        uint32 instrCount = 0;
        uint32 texCount = 0;

        for (uint32 nodeId : sortedNodes) {
            auto* node = m_Graph->GetNode(nodeId);
            if (!node || node->GetCategory() == NodeCategory::Master) continue;

            if (!node->GetOutputPins().empty()) {
                std::string varName = "_n" + std::to_string(nodeId);
                std::string code = node->GenerateHLSL(pinOutputs);

                PinType outType = node->GetOutputPins()[0].type;
                ss << "    " << PinTypeName(outType) << " " << varName << " = " << code << ";\n";

                instrCount++;
                if (node->GetCategory() == NodeCategory::Texture) texCount++;

                for (auto& pin : node->GetOutputPins()) {
                    pinOutputs[pin.id] = varName;
                    if (node->GetOutputPins().size() > 1) {
                        if (pin.name == "R") pinOutputs[pin.id] = varName + ".r";
                        else if (pin.name == "G") pinOutputs[pin.id] = varName + ".g";
                        else if (pin.name == "B") pinOutputs[pin.id] = varName + ".b";
                        else if (pin.name == "A") pinOutputs[pin.id] = varName + ".a";
                    }
                }
            }
        }

        auto* masterNode = m_Graph->GetNode(masterId);
        if (masterNode) {
            ss << "\n    // Master stack\n";
            ss << masterNode->GenerateHLSL(pinOutputs) << "\n";
            ss << "    return float4(OUT.albedo, OUT.opacity);\n";
        }
        ss << "}\n";

        m_State.instructionCount = instrCount;
        m_State.textureSampleCount = texCount;
        outCode = ss.str();
        return true;
    }

}} // namespace Engine::ShaderGraph
