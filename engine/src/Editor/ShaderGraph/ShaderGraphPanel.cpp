#include "Engine/Editor/ShaderGraph/ShaderGraphPanel.h"
#include "Engine/Editor/IconsFontAwesome6.h"

namespace Engine {
namespace ShaderGraph {

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
        if (connected) return ColorPinConnected;
        switch (type) {
            case PinType::Float:        return ColorPinFloat;
            case PinType::Float2:       return ColorPinFloat2;
            case PinType::Float3:       return ColorPinFloat3;
            case PinType::Float4:       return ColorPinFloat4;
            case PinType::Color:        return ColorPinColor;
            case PinType::Texture2D:    return ColorPinTexture;
            case PinType::Boolean:      return ColorPinBool;
            default:                    return ColorFromInt(180, 180, 180);
        }
    }

    ShaderGraphPanel::ShaderGraphPanel() {
        m_CanvasScale = 1.0f;
    }

    // ============================================================
    // DrawToolbar — 工业级扁平工具栏
    // ============================================================
    void ShaderGraphPanel::DrawToolbar() {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

        if (ImGui::Button(ICON_FA_SAVE " Save")) {
            if (m_Graph) m_Graph->SaveToFile("shader.sg");
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FOLDER_OPEN " Load")) {
            if (m_Graph) m_Graph->LoadFromFile("shader.sg");
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
    // OnImGui — 主入口（修复：不再设置全局 WindowPadding=0）
    // ============================================================
    void ShaderGraphPanel::OnImGui() {
        ImGui::Begin(m_Title.c_str(), nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        DrawToolbar();

        // 错误警告栏
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

        // 多文档标签页系统
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
    // DrawMainEditorLayout — 三栏布局
    // ============================================================
    void ShaderGraphPanel::DrawMainEditorLayout() {
        // 注意：在此方法中 DrawNodeCanvas 依赖 CursorScreenPos，
        // 我们需要先 Push 一个内容区域
        if (ImGui::BeginTable("SGTable", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Blackboard", ImGuiTableColumnFlags_WidthFixed, 220.0f);
            ImGui::TableSetupColumn("Canvas", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthFixed, 250.0f);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            if (m_State.showBlackboard) DrawBlackboard();

            ImGui::TableSetColumnIndex(1);
            DrawNodeCanvas();

            ImGui::TableSetColumnIndex(2);
            if (m_State.showProperties) DrawPropertiesPanel();

            ImGui::EndTable();
        }
    }

    // ============================================================
    // FindHoveredPin — 基于数学距离的精准引脚碰撞检测
    // ============================================================
    bool ShaderGraphPanel::FindHoveredPin(ImVec2 mousePos, uint32& outNodeId, uint32& outPinId, PinDirection& outDir) const {
        if (!m_Graph) return false;

        float hitRadiusSqr = (12.0f * m_CanvasScale) * (12.0f * m_CanvasScale);

        for (auto& [id, node] : m_Graph->GetNodes()) {
            for (auto& pin : node->GetInputPins()) {
                ImVec2 pinPos = GetPinPosition(node.get(), pin);
                float dx = mousePos.x - pinPos.x;
                float dy = mousePos.y - pinPos.y;
                if (dx * dx + dy * dy < hitRadiusSqr) {
                    outNodeId = id; outPinId = pin.id; outDir = pin.direction; return true;
                }
            }
            for (auto& pin : node->GetOutputPins()) {
                ImVec2 pinPos = GetPinPosition(node.get(), pin);
                float dx = mousePos.x - pinPos.x;
                float dy = mousePos.y - pinPos.y;
                if (dx * dx + dy * dy < hitRadiusSqr) {
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

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(m_CanvasScreenPos, ImVec2(m_CanvasScreenPos.x + m_CanvasSize.x, m_CanvasScreenPos.y + m_CanvasSize.y),
                                ColorFromInt(30, 30, 30));

        // ── 1. 画布底板交互 ──
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("Canvas", m_CanvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        bool isCanvasActive = ImGui::IsItemActive();
        bool mouseInCanvas = ImGui::IsMouseHoveringRect(m_CanvasScreenPos, ImVec2(m_CanvasScreenPos.x + m_CanvasSize.x, m_CanvasScreenPos.y + m_CanvasSize.y));

        // ── 2. 引脚连线起点 ──
        if (mouseInCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyAlt) {
            uint32 hitNodeId, hitPinId;
            PinDirection hitDir;
            if (FindHoveredPin(ImGui::GetMousePos(), hitNodeId, hitPinId, hitDir)) {
                m_State.isDraggingLink = true;
                m_State.draggingFromNodeId = hitNodeId;
                m_State.draggingFromPinId = hitPinId;
                m_State.draggingFromDir = hitDir;
            }
        }

        // ── 3. 记录右键坐标 ──
        if (mouseInCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            m_State.quickSearchPosX = ImGui::GetMousePos().x;
            m_State.quickSearchPosY = ImGui::GetMousePos().y;
            ImGui::OpenPopup("CreateNode");
        }

        // ── 4. 画布拖拽与缩放（以鼠标为中心） ──
        if (mouseInCanvas || isCanvasActive) {
            if (ImGui::GetIO().MouseWheel != 0) {
                ImVec2 mousePos = ImGui::GetMousePos();
                float mouseLocalX = mousePos.x - m_CanvasScreenPos.x;
                float mouseLocalY = mousePos.y - m_CanvasScreenPos.y;

                // 缩放前鼠标在世界空间中的坐标
                float worldX_before = mouseLocalX / m_CanvasScale - m_CanvasOrigin.x;
                float worldY_before = mouseLocalY / m_CanvasScale - m_CanvasOrigin.y;

                m_CanvasScale = std::clamp(m_CanvasScale + ImGui::GetIO().MouseWheel * 0.1f, 0.15f, 3.0f);

                // 缩放后世界坐标应保持不变，调整 Origin 补偿
                float worldX_after = mouseLocalX / m_CanvasScale - m_CanvasOrigin.x;
                float worldY_after = mouseLocalY / m_CanvasScale - m_CanvasOrigin.y;

                m_CanvasOrigin.x += (worldX_after - worldX_before);
                m_CanvasOrigin.y += (worldY_after - worldY_before);
            }

            if (!m_State.isDraggingLink && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && ImGui::GetIO().KeyAlt))) {
                m_CanvasOrigin.x += ImGui::GetIO().MouseDelta.x / m_CanvasScale;
                m_CanvasOrigin.y += ImGui::GetIO().MouseDelta.y / m_CanvasScale;
            }
        }

        // ── 5. 双层级网格 ──
        if (m_State.showGrid) {
            float gridSize = 20.0f * m_CanvasScale;
            float majorGridSize = gridSize * 5.0f;
            ImVec2 origin = ImVec2(m_CanvasScreenPos.x + m_CanvasOrigin.x * m_CanvasScale,
                                   m_CanvasScreenPos.y + m_CanvasOrigin.y * m_CanvasScale);
            for (float x = fmodf(origin.x, gridSize); x < m_CanvasSize.x; x += gridSize) {
                drawList->AddLine(ImVec2(m_CanvasScreenPos.x + x, m_CanvasScreenPos.y),
                                  ImVec2(m_CanvasScreenPos.x + x, m_CanvasScreenPos.y + m_CanvasSize.y),
                                  ColorFromInt(45, 45, 45, 100));
            }
            for (float y = fmodf(origin.y, gridSize); y < m_CanvasSize.y; y += gridSize) {
                drawList->AddLine(ImVec2(m_CanvasScreenPos.x, m_CanvasScreenPos.y + y),
                                  ImVec2(m_CanvasScreenPos.x + m_CanvasSize.x, m_CanvasScreenPos.y + y),
                                  ColorFromInt(45, 45, 45, 100));
            }
            for (float x = fmodf(origin.x, majorGridSize); x < m_CanvasSize.x; x += majorGridSize) {
                drawList->AddLine(ImVec2(m_CanvasScreenPos.x + x, m_CanvasScreenPos.y),
                                  ImVec2(m_CanvasScreenPos.x + x, m_CanvasScreenPos.y + m_CanvasSize.y),
                                  ColorFromInt(60, 60, 60, 200));
            }
            for (float y = fmodf(origin.y, majorGridSize); y < m_CanvasSize.y; y += majorGridSize) {
                drawList->AddLine(ImVec2(m_CanvasScreenPos.x, m_CanvasScreenPos.y + y),
                                  ImVec2(m_CanvasScreenPos.x + m_CanvasSize.x, m_CanvasScreenPos.y + y),
                                  ColorFromInt(60, 60, 60, 200));
            }
        }

        // ── 裁剪与绘制 ──
        drawList->PushClipRect(m_CanvasScreenPos, ImVec2(m_CanvasScreenPos.x + m_CanvasSize.x, m_CanvasScreenPos.y + m_CanvasSize.y), true);

        if (m_Graph) DrawLinks();

        if (m_Graph) {
            ImGui::SetCursorScreenPos(ImVec2(m_CanvasScreenPos.x + m_CanvasOrigin.x * m_CanvasScale,
                                              m_CanvasScreenPos.y + m_CanvasOrigin.y * m_CanvasScale));
            for (auto& [id, node] : m_Graph->GetNodes()) {
                DrawNode(node.get());
            }
        }

        HandleLinkDragging();
        drawList->PopClipRect();

        // ── 创建节点菜单 ──
        DrawCreateNodeMenu();

        // ── 接收 Blackboard 拖拽 ──
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_BLACKBOARD_PROP")) {
                int propIdx = *(const int*)payload->Data;
                if (m_Graph && propIdx >= 0 && propIdx < (int)m_Graph->GetProperties().size()) {
                    auto& prop = m_Graph->GetProperties()[propIdx];
                    uint32 newId = m_Graph->AddNode(std::make_unique<PropertyNode>(0, prop.name, prop.type));
                    auto* newNode = m_Graph->GetNode(newId);
                    if (newNode) {
                        float localX = (ImGui::GetMousePos().x - m_CanvasScreenPos.x) / m_CanvasScale - m_CanvasOrigin.x;
                        float localY = (ImGui::GetMousePos().y - m_CanvasScreenPos.y) / m_CanvasScale - m_CanvasOrigin.y;
                        newNode->SetPosition(localX, localY);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    // ============================================================
    // DrawNode — 工业级节点绘制
    // ============================================================
    void ShaderGraphPanel::DrawNode(ShaderNode* node) {
        if (!node) return;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float scale = m_CanvasScale;

        ImVec2 pos(node->GetPosX() * scale + m_CanvasScreenPos.x + m_CanvasOrigin.x * scale,
                   node->GetPosY() * scale + m_CanvasScreenPos.y + m_CanvasOrigin.y * scale);

        float nodeW = node->GetSizeX() * scale;
        float headerH = 28 * scale;
        float pinStartY = headerH + 8 * scale;
        float pinSpacing = 22 * scale;

        int inputCount = (int)node->GetInputPins().size();
        int outputCount = (int)node->GetOutputPins().size();
        int maxPins = std::max(inputCount, outputCount);
        float bodyH = std::max((float)maxPins * pinSpacing + 12 * scale, 40.0f * scale);
        float nodeH = headerH + bodyH;

        bool isSelected = (m_State.selectedNodeId == node->GetId());

        ImVec2 r0 = pos;
        ImVec2 r1 = ImVec2(pos.x + nodeW, pos.y + nodeH);
        ImVec2 headerR1 = ImVec2(pos.x + nodeW, pos.y + headerH);

        // 阴影
        drawList->AddRectFilled(ImVec2(r0.x + 4 * scale, r0.y + 6 * scale), ImVec2(r1.x + 4 * scale, r1.y + 6 * scale),
                                ColorFromInt(0, 0, 0, 100), 8.0f * scale);

        // 头部
        ImU32 nodeColor = GetNodeColor(node->GetCategory());
        drawList->AddRectFilled(r0, headerR1, nodeColor, 6.0f * scale, ImDrawFlags_RoundCornersTop);

        // 主体
        drawList->AddRectFilled(ImVec2(r0.x, headerR1.y), r1, ColorFromInt(35, 35, 40, 240), 6.0f * scale, ImDrawFlags_RoundCornersBottom);

        // 边框
        ImU32 borderColor = isSelected ? ColorFromInt(255, 165, 0, 255) : ColorFromInt(20, 20, 20, 255);
        drawList->AddRect(r0, r1, borderColor, 6.0f * scale, 0, isSelected ? 2.5f * scale : 1.5f * scale);

        // 标题
        ImVec2 titleSize = ImGui::CalcTextSize(node->GetName().c_str());
        drawList->AddText(ImVec2(r0.x + (nodeW - titleSize.x) * 0.5f, r0.y + (headerH - titleSize.y) * 0.5f),
                          ColorFromInt(240, 240, 240), node->GetName().c_str());

        // 引脚（纯绘制）
        auto drawPins = [&](const std::vector<Pin>& pins, float px, bool isInput) {
            for (size_t i = 0; i < pins.size(); ++i) {
                float py = pos.y + pinStartY + i * pinSpacing;
                ImU32 pinColor = GetPinColor(pins[i].type, pins[i].isConnected);
                float pinRadius = 5.0f * scale;
                if (pins[i].isConnected) {
                    drawList->AddCircleFilled(ImVec2(px, py), pinRadius, pinColor);
                } else {
                    drawList->AddCircleFilled(ImVec2(px, py), pinRadius, ColorFromInt(30, 30, 30, 255));
                    drawList->AddCircle(ImVec2(px, py), pinRadius, pinColor, 0, 1.5f * scale);
                }
                ImVec2 textSize = ImGui::CalcTextSize(pins[i].name.c_str());
                if (isInput) {
                    drawList->AddText(ImVec2(px + 12 * scale, py - textSize.y * 0.5f), ColorFromInt(200, 200, 200), pins[i].name.c_str());
                } else {
                    drawList->AddText(ImVec2(px - textSize.x - 12 * scale, py - textSize.y * 0.5f), ColorFromInt(200, 200, 200), pins[i].name.c_str());
                }
            }
        };
        drawPins(node->GetInputPins(), pos.x, true);
        drawPins(node->GetOutputPins(), pos.x + nodeW, false);

        // 节点交互
        ImGui::SetCursorScreenPos(r0);
        ImGui::PushID(node->GetId());
        ImGui::InvisibleButton("node_body", ImVec2(nodeW, nodeH));

        if (!m_State.isDraggingLink) {
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                m_State.selectedNodeId = node->GetId();
                m_State.selectedLinkId = 0;
            }
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                node->SetPosition(node->GetPosX() + ImGui::GetIO().MouseDelta.x / scale,
                                  node->GetPosY() + ImGui::GetIO().MouseDelta.y / scale);
            }
        }

        if (isSelected && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            if (m_Graph) m_Graph->RemoveNode(node->GetId());
            m_State.selectedNodeId = 0;
        }
        ImGui::PopID();
    }

    // ============================================================
    // DrawLinks — 双层贝塞尔曲线
    // ============================================================
    void ShaderGraphPanel::DrawLinks() {
        if (!m_Graph) return;
        ImDrawList* drawList = ImGui::GetWindowDrawList();

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

            float dx = std::abs(p2.x - p1.x) * 0.5f;
            dx = std::max(dx, 30.0f * m_CanvasScale);
            ImVec2 cp1(p1.x + dx, p1.y);
            ImVec2 cp2(p2.x - dx, p2.y);

            bool isSelected = (m_State.selectedLinkId == link.id);
            ImU32 baseColor = isSelected ? ColorFromInt(255, 200, 50) : GetPinColor(outPin->type, true);

            drawList->AddBezierCubic(p1, cp1, cp2, p2, ColorFromInt(15, 15, 15, 200), (isSelected ? 7.0f : 5.0f) * m_CanvasScale);
            drawList->AddBezierCubic(p1, cp1, cp2, p2, baseColor, (isSelected ? 4.0f : 3.0f) * m_CanvasScale);
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

        float dx = std::abs(p2.x - p1.x) * 0.5f;
        ImVec2 cp1(p1.x + dx, p1.y);
        ImVec2 cp2(p2.x - dx, p2.y);

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
        float x = node->GetPosX() * m_CanvasScale + m_CanvasScreenPos.x + m_CanvasOrigin.x * m_CanvasScale;
        float y = node->GetPosY() * m_CanvasScale + m_CanvasScreenPos.y + m_CanvasOrigin.y * m_CanvasScale;
        float headerH = 28 * m_CanvasScale;
        float pinStartY = headerH + 8 * m_CanvasScale;
        float pinSpacing = 22 * m_CanvasScale;

        int pinIdx = 0;
        const auto& pins = (pin.direction == PinDirection::Input) ? node->GetInputPins() : node->GetOutputPins();
        for (size_t i = 0; i < pins.size(); ++i) {
            if (pins[i].id == pin.id) { pinIdx = (int)i; break; }
        }

        float px = x + (pin.direction == PinDirection::Input ? 0 : node->GetSizeX() * m_CanvasScale);
        float py = y + pinStartY + pinIdx * pinSpacing;
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
                            float localX = (m_State.quickSearchPosX - m_CanvasScreenPos.x) / m_CanvasScale - m_CanvasOrigin.x;
                            float localY = (m_State.quickSearchPosY - m_CanvasScreenPos.y) / m_CanvasScale - m_CanvasOrigin.y;
                            newNode->SetPosition(localX, localY);
                        }
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
    }

    // ============================================================
    // DrawBlackboard — 支持拖拽 + 可删除（修复 X 按钮被 Selectable 覆盖）
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

            // 使用 AllowOverlap，允许在它上方点击按钮
            ImGui::SetNextItemAllowOverlap();
            if (ImGui::Selectable(props[i].name.c_str(), false, 0, ImVec2(width - 24, 22))) {
                // 点击属性
            }

            // 拖拽逻辑
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload("DND_BLACKBOARD_PROP", &i, sizeof(int));
                ImGui::Text("Get %s", props[i].name.c_str());
                ImGui::EndDragDropSource();
            }

            // 绝对定位右侧的"删除按钮"
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
    // DrawPropertiesPanel — 工业级双列属性网格
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

        // 显示当前选中的节点名称
        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1), "%s", node->GetName().c_str());
        ImGui::Spacing();

        // 使用工业级标准的双列属性网格
        if (ImGui::BeginTable("PropGrid", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Widget", ImGuiTableColumnFlags_WidthStretch);

            for (auto& pin : node->GetInputPins()) {
                if (pin.isConnected) continue;

                ImGui::TableNextRow();

                // 第一列：属性名称
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%s", pin.name.c_str());

                // 第二列：编辑控件
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