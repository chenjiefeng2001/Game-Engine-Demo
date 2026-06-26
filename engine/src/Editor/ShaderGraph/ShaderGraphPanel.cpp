#include "Engine/Editor/ShaderGraph/ShaderGraphPanel.h"

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

    void ShaderGraphPanel::OnImGui() {
        ImGui::Begin(m_Title.c_str());

        // ── Toolbar ──
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
            if (ImGui::Button("Save")) {
                if (m_Graph) m_Graph->SaveToFile("shader.sg");
                ImGui::SameLine();
            }
            if (ImGui::Button("Load")) {
                if (m_Graph) m_Graph->LoadFromFile("shader.sg");
                ImGui::SameLine();
            }
            if (ImGui::Button("Generate Code")) {
                std::string code;
                if (GenerateShaderCode(code)) {
                    m_State.generatedCode = code;
                    m_State.showCodePreview = !m_State.showCodePreview;
                }
            }
            ImGui::SameLine();
            ImGui::Checkbox("Grid", &m_State.showGrid);
            ImGui::SameLine();
            if (m_Graph) {
                ImGui::Text("| Nodes: %zu  Links: %zu",
                    m_Graph->GetNodes().size(), m_Graph->GetLinks().size());
            }
            ImGui::PopStyleVar();
        }

        ImGui::Separator();

        // ── Main area: Canvas + Blackboard ──
        if (ImGui::BeginTable("SGTable", 3, ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Blackboard", ImGuiTableColumnFlags_WidthFixed, 200.0f);
            ImGui::TableSetupColumn("Canvas", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthFixed, 250.0f);
            ImGui::TableNextRow();

            // Column 0: Blackboard
            ImGui::TableSetColumnIndex(0);
            if (m_State.showBlackboard) DrawBlackboard();

            // Column 1: Node Canvas
            ImGui::TableSetColumnIndex(1);
            DrawNodeCanvas();

            // Column 2: Properties
            ImGui::TableSetColumnIndex(2);
            if (m_State.showProperties) DrawPropertiesPanel();

            ImGui::EndTable();
        }

        // ── Code Preview (bottom panel) ──
        if (m_State.showCodePreview) {
            DrawCodePreview();
        }

        ImGui::End();
    }

    // ============================================================
    // FindHoveredPin — 基于数学距离的精准引脚碰撞检测
    // ============================================================
    bool ShaderGraphPanel::FindHoveredPin(ImVec2 mousePos, uint32& outNodeId, uint32& outPinId, PinDirection& outDir) const {
        if (!m_Graph) return false;

        // 碰撞检测半径 (放大一点以提升手感)
        float hitRadiusSqr = (12.0f * m_CanvasScale) * (12.0f * m_CanvasScale);

        for (auto& [id, node] : m_Graph->GetNodes()) {
            // 检查输入引脚
            for (auto& pin : node->GetInputPins()) {
                ImVec2 pinPos = GetPinPosition(node.get(), pin);
                float dx = mousePos.x - pinPos.x;
                float dy = mousePos.y - pinPos.y;
                if (dx * dx + dy * dy < hitRadiusSqr) {
                    outNodeId = id; outPinId = pin.id; outDir = pin.direction; return true;
                }
            }
            // 检查输出引脚
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

        // ── 1. 画布底板交互 (关键修复：允许后续画的节点覆盖它并接收点击) ──
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("Canvas", m_CanvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        bool isCanvasActive = ImGui::IsItemActive();

        // 判断鼠标是否在画布区域内（不受层级遮挡影响）
        bool mouseInCanvas = ImGui::IsMouseHoveringRect(m_CanvasScreenPos, ImVec2(m_CanvasScreenPos.x + m_CanvasSize.x, m_CanvasScreenPos.y + m_CanvasSize.y));

        // ── 2. 处理引脚连线的起点 (纯数学判断，优先级最高) ──
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

        // ── 3. 记录右键坐标并打开菜单 ──
        if (mouseInCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            m_State.quickSearchPosX = ImGui::GetMousePos().x;
            m_State.quickSearchPosY = ImGui::GetMousePos().y;
            ImGui::OpenPopup("CreateNode");
        }

        // ── 4. 处理画布拖拽与缩放 (仅在未拖拽引脚时) ──
        if (mouseInCanvas || isCanvasActive) {
            if (ImGui::GetIO().MouseWheel != 0) {
                m_CanvasScale = std::clamp(m_CanvasScale + ImGui::GetIO().MouseWheel * 0.1f, 0.3f, 3.0f);
            }
            if (!m_State.isDraggingLink && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && ImGui::GetIO().KeyAlt))) {
                m_CanvasOrigin.x += ImGui::GetIO().MouseDelta.x / m_CanvasScale;
                m_CanvasOrigin.y += ImGui::GetIO().MouseDelta.y / m_CanvasScale;
            }
        }

        // ── 5. 工业级双层级网格 ──
        if (m_State.showGrid) {
            float gridSize = 20.0f * m_CanvasScale;
            float majorGridSize = gridSize * 5.0f;

            ImVec2 origin = ImVec2(m_CanvasScreenPos.x + m_CanvasOrigin.x * m_CanvasScale,
                                   m_CanvasScreenPos.y + m_CanvasOrigin.y * m_CanvasScale);

            // 小网格
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

            // 大网格（颜色略亮）
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

        // ── 裁剪 ──
        drawList->PushClipRect(m_CanvasScreenPos, ImVec2(m_CanvasScreenPos.x + m_CanvasSize.x, m_CanvasScreenPos.y + m_CanvasSize.y), true);

        // ── 绘制连线 ──
        if (m_Graph) DrawLinks();

        // ── 绘制节点 ──
        if (m_Graph) {
            ImGui::SetCursorScreenPos(ImVec2(m_CanvasScreenPos.x + m_CanvasOrigin.x * m_CanvasScale,
                                              m_CanvasScreenPos.y + m_CanvasOrigin.y * m_CanvasScale));

            for (auto& [id, node] : m_Graph->GetNodes()) {
                DrawNode(node.get());
            }
        }

        // ── 处理拖拽连线 ──
        HandleLinkDragging();

        drawList->PopClipRect();

        // ── 创建节点菜单 ──
        DrawCreateNodeMenu();
    }

    // ============================================================
    // DrawNode — 工业级节点绘制（无引脚 InvisibleButton，纯 Hit-Test）
    // ============================================================
    void ShaderGraphPanel::DrawNode(ShaderNode* node) {
        if (!node) return;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float scale = m_CanvasScale;

        // 使用 m_CanvasScreenPos 代替 GetCursorScreenPos，保证坐标一致
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

        // 1. 高级阴影
        drawList->AddRectFilled(ImVec2(r0.x + 4 * scale, r0.y + 6 * scale), ImVec2(r1.x + 4 * scale, r1.y + 6 * scale),
                                ColorFromInt(0, 0, 0, 100), 8.0f * scale);

        // 2. 节点头部 (圆角仅顶部)
        ImU32 nodeColor = GetNodeColor(node->GetCategory());
        drawList->AddRectFilled(r0, headerR1, nodeColor, 6.0f * scale, ImDrawFlags_RoundCornersTop);

        // 3. 节点主体 (深灰色背景，圆角仅底部)
        drawList->AddRectFilled(ImVec2(r0.x, headerR1.y), r1, ColorFromInt(35, 35, 40, 240), 6.0f * scale, ImDrawFlags_RoundCornersBottom);

        // 4. 边框
        ImU32 borderColor = isSelected ? ColorFromInt(255, 165, 0, 255) : ColorFromInt(20, 20, 20, 255);
        drawList->AddRect(r0, r1, borderColor, 6.0f * scale, 0, isSelected ? 2.5f * scale : 1.5f * scale);

        // 5. 标题文本
        ImVec2 titleSize = ImGui::CalcTextSize(node->GetName().c_str());
        drawList->AddText(ImVec2(r0.x + (nodeW - titleSize.x) * 0.5f, r0.y + (headerH - titleSize.y) * 0.5f),
                          ColorFromInt(240, 240, 240), node->GetName().c_str());

        // 6. 绘制引脚（纯绘制，没有 InvisibleButton）
        auto drawPins = [&](const std::vector<Pin>& pins, float px, bool isInput) {
            for (size_t i = 0; i < pins.size(); ++i) {
                float py = pos.y + pinStartY + i * pinSpacing;
                ImU32 pinColor = GetPinColor(pins[i].type, pins[i].isConnected);
                float pinRadius = 5.0f * scale;

                // 未连接画空心圆，已连接画实心圆
                if (pins[i].isConnected) {
                    drawList->AddCircleFilled(ImVec2(px, py), pinRadius, pinColor);
                } else {
                    drawList->AddCircleFilled(ImVec2(px, py), pinRadius, ColorFromInt(30, 30, 30, 255));
                    drawList->AddCircle(ImVec2(px, py), pinRadius, pinColor, 0, 1.5f * scale);
                }

                // 引脚文本
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

        // 7. 节点整体交互侦测（拖动节点自身）
        ImGui::SetCursorScreenPos(r0);
        ImGui::PushID(node->GetId());

        // 这里不需要 AllowOverlap，因为我们希望点击被它实体捕获
        ImGui::InvisibleButton("node_body", ImVec2(nodeW, nodeH));

        // 只有在没拖拽连线的时候，点击和拖拽节点才生效
        if (!m_State.isDraggingLink) {
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                m_State.selectedNodeId = node->GetId();
                m_State.selectedLinkId = 0;
            }

            // 关键：IsItemActive 会在鼠标按住该 InvisibleButton 时返回 true，从而实现左键拖拽移动
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                node->SetPosition(node->GetPosX() + ImGui::GetIO().MouseDelta.x / scale,
                                  node->GetPosY() + ImGui::GetIO().MouseDelta.y / scale);
            }
        }

        // 8. 节点删除逻辑
        if (isSelected && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            if (m_Graph) m_Graph->RemoveNode(node->GetId());
            m_State.selectedNodeId = 0;
        }

        ImGui::PopID();
    }

    // ============================================================
    // DrawLinks — 工业级双层贝塞尔曲线连线
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

            // 黑色底边 + 彩色内芯
            drawList->AddBezierCubic(p1, cp1, cp2, p2, ColorFromInt(15, 15, 15, 200), (isSelected ? 7.0f : 5.0f) * m_CanvasScale);
            drawList->AddBezierCubic(p1, cp1, cp2, p2, baseColor, (isSelected ? 4.0f : 3.0f) * m_CanvasScale);
        }
    }

    // ============================================================
    // HandleLinkDragging — 拖拽连线逻辑（含碰撞检测与连接建立）
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

        // 保证贝塞尔曲线从左向右弯曲
        if (m_State.draggingFromDir == PinDirection::Input) std::swap(p1, p2);

        float dx = std::abs(p2.x - p1.x) * 0.5f;
        ImVec2 cp1(p1.x + dx, p1.y);
        ImVec2 cp2(p2.x - dx, p2.y);

        drawList->AddBezierCubic(p1, cp1, cp2, p2, ColorFromInt(0, 0, 0, 150), 5.0f * m_CanvasScale);
        drawList->AddBezierCubic(p1, cp1, cp2, p2, GetPinColor(srcPin->type, true), 3.0f * m_CanvasScale);

        // ── 释放鼠标，尝试建立连接 ──
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            uint32 targetNodeId, targetPinId;
            PinDirection targetDir;
            if (FindHoveredPin(mousePos, targetNodeId, targetPinId, targetDir)) {
                // 确保方向相反 (Output 连 Input)
                if (m_State.draggingFromDir != targetDir) {
                    uint32 outNodeID = (m_State.draggingFromDir == PinDirection::Output) ? m_State.draggingFromNodeId : targetNodeId;
                    uint32 inNodeID = (targetDir == PinDirection::Input) ? targetNodeId : m_State.draggingFromNodeId;
                    uint32 outPinID = (m_State.draggingFromDir == PinDirection::Output) ? srcPin->id : targetPinId;
                    uint32 inPinID = (targetDir == PinDirection::Input) ? targetPinId : srcPin->id;

                    // ReplaceLink 自动替换旧连线 + 校验
                    m_Graph->ReplaceLink(outNodeID, outPinID, inNodeID, inPinID);
                }
            }
            m_State.isDraggingLink = false; // 结束拖拽
        }
    }

    // ============================================================
    // GetPinPosition — 获取引脚屏幕坐标（使用 m_CanvasScreenPos）
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
    // DrawCreateNodeMenu — 修复坐标系转换（使用保存的右键坐标）
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
                            // 【致命修复】使用保存的右键点击坐标，而不是当前的鼠标坐标
                            // 因为当前鼠标已经移到了菜单上！
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
            if (ImGui::Button("Add")) {
                ShaderGraph::BlackboardProperty prop;
                prop.name = nameBuf;
                prop.type = (PinType)((int)PinType::Float + selectedType);
                m_Graph->AddProperty(prop);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        auto& props = m_Graph->GetProperties();
        int removeIdx = -1;
        for (int i = 0; i < (int)props.size(); ++i) {
            ImGui::PushID(i);
            ImGui::Text("%s [%s]", props[i].name.c_str(), PinTypeName(props[i].type));
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) removeIdx = i;
            ImGui::PopID();
        }
        if (removeIdx >= 0) m_Graph->RemoveProperty(removeIdx);
    }

    // ============================================================
    // DrawPropertiesPanel
    // ============================================================
    void ShaderGraphPanel::DrawPropertiesPanel() {
        if (!m_Graph) return;

        ImGui::Text("Properties");
        ImGui::Separator();

        if (m_State.selectedNodeId == 0) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "Select a node");
            return;
        }

        auto* node = m_Graph->GetNode(m_State.selectedNodeId);
        if (!node) { ImGui::Text("Node not found"); return; }

        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1), "Selected: %s", node->GetName().c_str());
        ImGui::Separator();

        for (auto& pin : node->GetInputPins()) {
            if (pin.isConnected) continue;

            switch (pin.type) {
                case PinType::Float:
                case PinType::Float2:
                case PinType::Float3:
                case PinType::Color: {
                    int dim = PinTypeDimension(pin.type);
                    float v[4] = { pin.defaultValue.f[0], pin.defaultValue.f[1],
                                   pin.defaultValue.f[2], pin.defaultValue.f[3] };
                    std::string label = pin.name;

                    if (dim == 1) {
                        if (ImGui::DragFloat(label.c_str(), &v[0], 0.01f, -10, 10)) {
                            const_cast<Pin&>(pin).defaultValue.f[0] = v[0];
                        }
                    } else if (dim == 3) {
                        if (ImGui::DragFloat3(label.c_str(), v, 0.01f)) {
                            for (int j = 0; j < 3; j++) const_cast<Pin&>(pin).defaultValue.f[j] = v[j];
                        }
                    } else if (dim == 4) {
                        if (ImGui::ColorEdit4(label.c_str(), v)) {
                            for (int j = 0; j < 4; j++) const_cast<Pin&>(pin).defaultValue.f[j] = v[j];
                        }
                    }
                    break;
                }
                default: break;
            }
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
        ImGui::Text("HLSL Shader Code");
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