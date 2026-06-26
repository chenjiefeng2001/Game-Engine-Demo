#include "Engine/Editor/ShaderGraph/ShaderGraphPanel.h"

namespace Engine {
namespace ShaderGraph {

    static ImU32 ColorFromInt(int r, int g, int b, int a = 255) {
        return IM_COL32(r, g, b, a);
    }

    ImU32 ShaderGraphPanel::GetNodeColor(NodeCategory cat) {
        switch (cat) {
            case NodeCategory::Math:    return ColorFromInt(60, 90, 140);
            case NodeCategory::Input:   return ColorFromInt(60, 130, 80);
            case NodeCategory::Vector:  return ColorFromInt(130, 70, 140);
            case NodeCategory::Art:     return ColorFromInt(140, 100, 60);
            case NodeCategory::Texture: return ColorFromInt(60, 110, 120);
            case NodeCategory::Master:  return ColorFromInt(160, 60, 60);
            default:                    return ColorFromInt(80, 80, 80);
        }
    }

    ImU32 ShaderGraphPanel::GetPinColor(PinType type) {
        switch (type) {
            case PinType::Float:        return ColorFromInt(150, 200, 255);
            case PinType::Float2:       return ColorFromInt(100, 180, 255);
            case PinType::Float3:       return ColorFromInt(60, 150, 255);
            case PinType::Float4:
            case PinType::Color:        return ColorFromInt(255, 200, 100);
            case PinType::Texture2D:    return ColorFromInt(200, 150, 100);
            case PinType::Boolean:      return ColorFromInt(200, 100, 100);
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

    void ShaderGraphPanel::DrawNodeCanvas() {
        // Save canvas state
        m_CanvasSize = ImGui::GetContentRegionAvail();
        ImVec2 canvasPos = ImGui::GetCursorScreenPos();

        // Draw canvas background
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + m_CanvasSize.x, canvasPos.y + m_CanvasSize.y),
                                ColorFromInt(30, 30, 30));

        // ── Canvas interaction area ──
        ImGui::InvisibleButton("Canvas", m_CanvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        bool isCanvasHovered = ImGui::IsItemHovered();
        bool isCanvasActive = ImGui::IsItemActive();

        // Canvas scrolling (middle mouse or alt+drag)
        if (isCanvasHovered || isCanvasActive) {
            // Zoom
            if (ImGui::GetIO().MouseWheel != 0) {
                m_CanvasScale = std::clamp(m_CanvasScale + ImGui::GetIO().MouseWheel * 0.1f, 0.3f, 3.0f);
            }
            // Pan
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
                (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && ImGui::GetIO().KeyAlt)) {
                m_CanvasOrigin.x += ImGui::GetIO().MouseDelta.x / m_CanvasScale;
                m_CanvasOrigin.y += ImGui::GetIO().MouseDelta.y / m_CanvasScale;
            }
        }

        // ── Right-click create menu ──
        if (isCanvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup("CreateNode");
        }

        // ── Draw grid ──
        if (m_State.showGrid) {
            float gridSize = 20.0f * m_CanvasScale;
            ImVec2 origin = ImVec2(canvasPos.x + m_CanvasOrigin.x * m_CanvasScale,
                                   canvasPos.y + m_CanvasOrigin.y * m_CanvasScale);
            for (float x = fmodf(origin.x, gridSize); x < m_CanvasSize.x; x += gridSize) {
                drawList->AddLine(ImVec2(canvasPos.x + x, canvasPos.y),
                                  ImVec2(canvasPos.x + x, canvasPos.y + m_CanvasSize.y),
                                  ColorFromInt(50, 50, 50));
            }
            for (float y = fmodf(origin.y, gridSize); y < m_CanvasSize.y; y += gridSize) {
                drawList->AddLine(ImVec2(canvasPos.x, canvasPos.y + y),
                                  ImVec2(canvasPos.x + m_CanvasSize.x, canvasPos.y + y),
                                  ColorFromInt(50, 50, 50));
            }
        }

        // ── Set clipping ──
        drawList->PushClipRect(canvasPos, ImVec2(canvasPos.x + m_CanvasSize.x, canvasPos.y + m_CanvasSize.y), true);

        // ── Draw links ──
        if (m_Graph) DrawLinks();

        // ── Draw nodes ──
        if (m_Graph) {
            // Transform cursor to canvas space
            ImGui::SetCursorScreenPos(ImVec2(canvasPos.x + m_CanvasOrigin.x * m_CanvasScale,
                                              canvasPos.y + m_CanvasOrigin.y * m_CanvasScale));

            for (auto& [id, node] : m_Graph->GetNodes()) {
                DrawNode(node.get());
            }
        }

        // ── Handle link dragging ──
        HandleLinkDragging();

        drawList->PopClipRect();

        // ── Create Node Menu ──
        DrawCreateNodeMenu();
    }

    void ShaderGraphPanel::DrawNode(ShaderNode* node) {
        if (!node) return;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float scale = m_CanvasScale;
        ImVec2 pos(node->GetPosX() * scale + ImGui::GetCursorScreenPos().x,
                   node->GetPosY() * scale + ImGui::GetCursorScreenPos().y);

        float nodeW = node->GetSizeX() * scale;
        float nodeH = node->GetSizeY() * scale;
        bool isSelected = m_State.selectedNodeId == node->GetId();

        // Node header
        float headerH = 24 * scale;
        ImU32 nodeColor = GetNodeColor(node->GetCategory());

        // Node body rect
        ImVec2 r0 = pos;
        ImVec2 r1 = ImVec2(pos.x + nodeW, pos.y + nodeH);
        ImVec2 headerR1 = ImVec2(pos.x + nodeW, pos.y + headerH);

        // Drop shadow
        drawList->AddRectFilled(ImVec2(r0.x + 3, r0.y + 3), ImVec2(r1.x + 3, r1.y + 3),
                                ColorFromInt(0, 0, 0, 60), 4.0f * scale);

        // Header
        drawList->AddRectFilled(r0, headerR1, nodeColor, 4.0f * scale);
        // Body
        drawList->AddRectFilled(ImVec2(r0.x, r0.y + 4 * scale), r1,
                                ColorFromInt(45, 45, 50), 0, ImDrawFlags_RoundCornersBottom);
        // Border
        ImU32 borderColor = isSelected ? ColorFromInt(255, 200, 50) : ColorFromInt(70, 70, 80);
        drawList->AddRect(r0, r1, borderColor, 4.0f * scale);

        // Title text
        drawList->AddText(ImVec2(r0.x + 8 * scale, r0.y + 4 * scale),
                          ColorFromInt(255, 255, 255), node->GetName().c_str());

        // Pins
        float pinStartY = headerH + 4 * scale;
        float pinSpacing = 18 * scale;
        int inputCount = (int)node->GetInputPins().size();
        int outputCount = (int)node->GetOutputPins().size();
        int maxPins = std::max(inputCount, outputCount);
        float bodyH = std::max((float)maxPins * pinSpacing + 8 * scale, 40.0f * scale);

        // Adjust node height for pins
        nodeH = headerH + bodyH;
        node->SetSize(node->GetSizeX(), headerH + bodyH);
        r1 = ImVec2(pos.x + nodeW, pos.y + nodeH);

        // Draw input pins
        for (int i = 0; i < inputCount; ++i) {
            auto& pin = node->GetInputPins()[i];
            float py = pos.y + pinStartY + i * pinSpacing;
            float px = pos.x;

            ImU32 pinColor = pin.isConnected ? ColorFromInt(100, 200, 100) : GetPinColor(pin.type);
            drawList->AddCircleFilled(ImVec2(px, py), 5 * scale, pinColor);
            drawList->AddText(ImVec2(px + 10 * scale, py - 6 * scale),
                              ColorFromInt(200, 200, 200), pin.name.c_str());
        }

        // Draw output pins
        for (int i = 0; i < outputCount; ++i) {
            auto& pin = node->GetOutputPins()[i];
            float py = pos.y + pinStartY + i * pinSpacing;
            float px = pos.x + nodeW;

            ImU32 pinColor = pin.isConnected ? ColorFromInt(100, 200, 100) : GetPinColor(pin.type);
            drawList->AddCircleFilled(ImVec2(px, py), 5 * scale, pinColor);

            // Draw label to the left of the pin
            ImVec2 textSize = ImGui::CalcTextSize(pin.name.c_str());
            drawList->AddText(ImVec2(px - textSize.x - 10 * scale, py - 6 * scale),
                              ColorFromInt(200, 200, 200), pin.name.c_str());
        }

        // ── Node interaction ──
        // Use an invisible button for click/drag
        ImGui::SetCursorScreenPos(pos);
        ImGui::PushID(node->GetId());
        ImGui::InvisibleButton("node", ImVec2(nodeW, nodeH));

        if (ImGui::IsItemClicked()) {
            m_State.selectedNodeId = node->GetId();
            m_State.selectedLinkId = 0;
        }

        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyAlt) {
            node->SetPosition(node->GetPosX() + ImGui::GetIO().MouseDelta.x / scale,
                              node->GetPosY() + ImGui::GetIO().MouseDelta.y / scale);
        }

        // Double-click to select pin for linking
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            // Start link from last output pin
        }

        // Handle input pin click (for link start)
        for (int i = 0; i < inputCount; ++i) {
            auto& pin = node->GetInputPins()[i];
            float py = pos.y + pinStartY + i * pinSpacing;
            float px = pos.x;

            ImGui::SetCursorScreenPos(ImVec2(px - 10, py - 10));
            ImGui::InvisibleButton(("pin_in_" + std::to_string(pin.id)).c_str(), ImVec2(20, 20));
            if (ImGui::IsItemClicked() && ImGui::GetIO().KeyCtrl) {
                m_State.isDraggingLink = true;
                m_State.draggingFromPinId = pin.id;
                m_State.draggingFromNodeId = node->GetId();
                m_State.draggingFromDir = PinDirection::Input;
            }
        }

        // Handle output pin click (for link start)
        for (int i = 0; i < outputCount; ++i) {
            auto& pin = node->GetOutputPins()[i];
            float py = pos.y + pinStartY + i * pinSpacing;
            float px = pos.x + nodeW;

            ImGui::SetCursorScreenPos(ImVec2(px - 10, py - 10));
            ImGui::InvisibleButton(("pin_out_" + std::to_string(pin.id)).c_str(), ImVec2(20, 20));
            if (ImGui::IsItemClicked()) {
                m_State.isDraggingLink = true;
                m_State.draggingFromPinId = pin.id;
                m_State.draggingFromNodeId = node->GetId();
                m_State.draggingFromDir = PinDirection::Output;
            }
        }

        // Delete node
        if (isSelected && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            if (m_Graph) m_Graph->RemoveNode(node->GetId());
            m_State.selectedNodeId = 0;
        }

        ImGui::PopID();
    }

    void ShaderGraphPanel::DrawLinks() {
        if (!m_Graph) return;
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        for (auto& link : m_Graph->GetLinks()) {
            auto* outNode = m_Graph->GetNode(link.outputNodeId);
            auto* inNode = m_Graph->GetNode(link.inputNodeId);
            if (!outNode || !inNode) continue;

            // Find the pins
            const Pin* outPin = nullptr;
            const Pin* inPin = nullptr;
            for (auto& p : outNode->GetOutputPins()) {
                if (p.id == link.outputPinId) { outPin = &p; break; }
            }
            for (auto& p : inNode->GetInputPins()) {
                if (p.id == link.inputPinId) { inPin = &p; break; }
            }
            if (!outPin || !inPin) continue;

            ImVec2 p1 = GetPinPosition(outNode, *outPin);
            ImVec2 p2 = GetPinPosition(inNode, *inPin);

            // Draw bezier curve
            float dx = std::abs(p2.x - p1.x) * 0.5f;
            ImVec2 cp1(p1.x + dx, p1.y);
            ImVec2 cp2(p2.x - dx, p2.y);

            bool isSelected = m_State.selectedLinkId == link.id;
            drawList->AddBezierCubic(p1, cp1, cp2, p2,
                                     isSelected ? ColorFromInt(255, 200, 50) : ColorFromInt(120, 180, 255),
                                     2.0f * m_CanvasScale);

            // Click on link for selection
            // (Simplified: not pixel-perfect hit detection)
        }
    }

    void ShaderGraphPanel::HandleLinkDragging() {
        if (!m_Graph || !m_State.isDraggingLink) return;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 mousePos = ImGui::GetMousePos();

        // Get source pin position
        auto* srcNode = m_Graph->GetNode(m_State.draggingFromNodeId);
        if (!srcNode) { m_State.isDraggingLink = false; return; }

        const Pin* srcPin = nullptr;
        for (auto& p : srcNode->GetInputPins()) {
            if (p.id == m_State.draggingFromPinId) { srcPin = &p; break; }
        }
        for (auto& p : srcNode->GetOutputPins()) {
            if (p.id == m_State.draggingFromPinId) { srcPin = &p; break; }
        }
        if (!srcPin) { m_State.isDraggingLink = false; return; }

        ImVec2 p1 = GetPinPosition(srcNode, *srcPin);
        ImVec2 p2 = mousePos;

        // Draw temporary bezier
        float dx = std::abs(p2.x - p1.x) * 0.5f;
        drawList->AddBezierCubic(p1, ImVec2(p1.x + dx, p1.y),
                                 ImVec2(p2.x - dx, p2.y), p2,
                                 ColorFromInt(200, 200, 50, 180), 1.5f * m_CanvasScale);

        // Release to create link
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            m_State.isDraggingLink = false;
        }
    }

    ImVec2 ShaderGraphPanel::GetPinPosition(const ShaderNode* node, const Pin& pin) const {
        ImVec2 pos(node->GetPosX() * m_CanvasScale + ImGui::GetCursorScreenPos().x,
                   node->GetPosY() * m_CanvasScale + ImGui::GetCursorScreenPos().y);
        float headerH = 24 * m_CanvasScale;
        float pinStartY = headerH + 4 * m_CanvasScale;
        float pinSpacing = 18 * m_CanvasScale;

        int pinIdx = 0;
        const auto& pins = (pin.direction == PinDirection::Input) ? node->GetInputPins() : node->GetOutputPins();
        for (size_t i = 0; i < pins.size(); ++i) {
            if (pins[i].id == pin.id) { pinIdx = (int)i; break; }
        }

        float px = pos.x + (pin.direction == PinDirection::Input ? 0 : node->GetSizeX() * m_CanvasScale);
        float py = pos.y + pinStartY + pinIdx * pinSpacing;
        return ImVec2(px, py);
    }

    void ShaderGraphPanel::DrawCreateNodeMenu() {
        if (ImGui::BeginPopup("CreateNode")) {
            ImGui::Text("Create Node");
            ImGui::Separator();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));

            auto factoryList = GetNodeFactoryList();
            std::string currentCategory = "";

            for (auto& entry : factoryList) {
                std::string catName = NodeCategoryName(entry.category);
                if (catName != currentCategory) {
                    if (!currentCategory.empty()) ImGui::Separator();
                    currentCategory = catName;
                    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "--- %s ---", catName.c_str());
                }

                if (ImGui::MenuItem(entry.name)) {
                    if (m_Graph) {
                        auto node = entry.factory(0);
                        uint32 newId = m_Graph->AddNode(std::move(node));
                        auto* newNode = m_Graph->GetNode(newId);
                        if (newNode) {
                            // Place at mouse position in canvas space
                            ImVec2 mousePos = ImGui::GetMousePos();
                            ImVec2 canvasPos = ImGui::GetCursorScreenPos();
                            newNode->SetPosition((mousePos.x - canvasPos.x) / m_CanvasScale,
                                                  (mousePos.y - canvasPos.y) / m_CanvasScale);
                        }
                    }
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::PopStyleVar();
            ImGui::EndPopup();
        }
    }

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

        // List properties
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

        // Input pin values (for Art nodes with editable defaults)
        for (auto& pin : node->GetInputPins()) {
            if (pin.isConnected) continue; // Don't edit connected pins

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
                            // Update (not mutating const - this is OK for our editable defaults)
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

    bool ShaderGraphPanel::GenerateShaderCode(std::string& outCode) {
        if (!m_Graph) return false;
        return ShaderCodeGenerator::Generate(*m_Graph, outCode);
    }

    // ══════════════════════════════════════════════════════════════
    // ShaderCodeGenerator
    // ══════════════════════════════════════════════════════════════

    bool ShaderCodeGenerator::Generate(const ShaderGraph& graph, std::string& outCode) {
        std::stringstream ss;

        GenerateHeader(ss);
        GenerateInputStruct(ss);
        GenerateOutputStruct(ss);
        GenerateProperties(graph, ss);

        // Find master node
        uint32 masterId = graph.GetMasterNodeId();
        if (masterId == 0) {
            // Find first master node
            for (auto& [id, node] : graph.GetNodes()) {
                if (node->GetCategory() == NodeCategory::Master) {
                    masterId = id;
                    break;
                }
            }
        }
        if (masterId == 0) {
            outCode = ss.str() + "\n// No Master node found\n";
            return false;
        }

        ss << "float4 main(VSOutput IN) : SV_Target\n{\n";
        ss << "    ShaderOutput OUT;\n";
        ss << "    OUT.albedo = float3(0,0,0);\n";
        ss << "    OUT.normal = IN.normalWS;\n";
        ss << "    OUT.opacity = 1.0;\n\n";

        // Topological sort and generate
        auto sortedNodes = graph.TopologicalSort();
        std::unordered_map<uint32, std::string> pinOutputs;

        for (uint32 nodeId : sortedNodes) {
            auto* node = graph.GetNode(nodeId);
            if (!node) continue;

            // Skip master node (handle at end)
            if (node->GetCategory() == NodeCategory::Master) continue;

            // For nodes with outputs, assign a variable name
            if (!node->GetOutputPins().empty()) {
                // Generate output variable name
                std::string varName = "_n" + std::to_string(nodeId);
                std::string code = node->GenerateHLSL(pinOutputs);

                // Assign to variable
                PinType outType = node->GetOutputPins()[0].type;
                ss << "    " << PinTypeName(outType) << " " << varName << " = " << code << ";\n";

                // Register outputs
                for (auto& pin : node->GetOutputPins()) {
                    pinOutputs[pin.id] = varName;
                    // Handle swizzle for multi-output nodes (like SampleTexture2D)
                    if (node->GetOutputPins().size() > 1) {
                        if (pin.name == "R") pinOutputs[pin.id] = varName + ".r";
                        else if (pin.name == "G") pinOutputs[pin.id] = varName + ".g";
                        else if (pin.name == "B") pinOutputs[pin.id] = varName + ".b";
                        else if (pin.name == "A") pinOutputs[pin.id] = varName + ".a";
                    }
                }
            } else {
                // No output nodes (e.g. blackboard property)
                node->GenerateHLSL(pinOutputs);
            }
        }

        // Generate master node code
        auto* masterNode = graph.GetNode(masterId);
        if (masterNode) {
            ss << "\n    // Master stack\n";
            ss << masterNode->GenerateHLSL(pinOutputs) << "\n";
            ss << "    return float4(OUT.albedo, OUT.opacity);\n";
        }

        ss << "}\n";
        outCode = ss.str();
        return true;
    }

    void ShaderCodeGenerator::GenerateHeader(std::stringstream& ss) {
        ss << "// Generated by Game-Engine-Demo Shader Graph\n";
        ss << "#include \"ShaderCommon.hlsl\"\n\n";
        ss << "#define PI 3.1415926535\n\n";
    }

    void ShaderCodeGenerator::GenerateInputStruct(std::stringstream& ss) {
        ss << "struct VSOutput {\n";
        ss << "    float4 position : SV_Position;\n";
        ss << "    float2 uv : TEXCOORD0;\n";
        ss << "    float3 normalWS : NORMAL;\n";
        ss << "    float3 positionWS : TEXCOORD1;\n";
        ss << "    float3 viewDir : TEXCOORD2;\n";
        ss << "    float3 tangentWS : TANGENT;\n";
        ss << "    float time : TIME;\n";
        ss << "};\n\n";
    }

    void ShaderCodeGenerator::GenerateOutputStruct(std::stringstream& ss) {
        ss << "struct ShaderOutput {\n";
        ss << "    float3 albedo;\n";
        ss << "    float3 normal;\n";
        ss << "    float opacity;\n";
        ss << "};\n\n";
    }

    void ShaderCodeGenerator::GenerateProperties(const ShaderGraph& graph, std::stringstream& ss) {
        ss << "// Material Properties\n";
        for (auto& [id, node] : graph.GetNodes()) {
            std::string propDecl = node->GeneratePropertyDecl();
            if (!propDecl.empty()) ss << propDecl << "\n";
        }
        for (auto& prop : graph.GetProperties()) {
            switch (prop.type) {
                case PinType::Float:  ss << "float _" << prop.name << ";\n"; break;
                case PinType::Color:  ss << "float4 _" << prop.name << ";\n"; break;
                case PinType::Float2: ss << "float2 _" << prop.name << ";\n"; break;
                case PinType::Float3: ss << "float3 _" << prop.name << ";\n"; break;
                default: break;
            }
        }
        ss << "\n";
    }

}} // namespace Engine::ShaderGraph