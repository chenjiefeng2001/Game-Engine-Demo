#include "Engine/Editor/ShaderGraph/ShaderNodes.h"

namespace Engine {
namespace ShaderGraph {

    // ══════════════════════════════════════════════════════════════
    // ShaderGraph implementation
    // ══════════════════════════════════════════════════════════════
    uint32 ShaderGraph::AddNode(std::unique_ptr<ShaderNode> node) {
        uint32 id = m_NextNodeId++;
        node->SetName(node->GetName()); // keep name
        m_Nodes[id] = std::move(node);
        return id;
    }

    bool ShaderGraph::RemoveNode(uint32 nodeId) {
        // Remove all links connected to this node
        m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
            [nodeId](const NodeLink& link) {
                return link.outputNodeId == nodeId || link.inputNodeId == nodeId;
            }), m_Links.end());
        return m_Nodes.erase(nodeId) > 0;
    }

    ShaderNode* ShaderGraph::GetNode(uint32 nodeId) {
        auto it = m_Nodes.find(nodeId);
        return it != m_Nodes.end() ? it->second.get() : nullptr;
    }

    const ShaderNode* ShaderGraph::GetNode(uint32 nodeId) const {
        auto it = m_Nodes.find(nodeId);
        return it != m_Nodes.end() ? it->second.get() : nullptr;
    }

    uint32 ShaderGraph::AddLink(uint32 outputNodeId, uint32 outputPinId,
                                uint32 inputNodeId, uint32 inputPinId) {
        NodeLink link;
        link.id = m_NextLinkId++;
        link.outputNodeId = outputNodeId;
        link.outputPinId = outputPinId;
        link.inputNodeId = inputNodeId;
        link.inputPinId = inputPinId;
        m_Links.push_back(link);

        // Mark pins as connected
        auto* outNode = GetNode(outputNodeId);
        auto* inNode = GetNode(inputNodeId);
        if (outNode) {
            for (auto& pin : outNode->GetOutputPins()) {
                if (pin.id == outputPinId) { pin.isConnected = true; break; }
            }
        }
        if (inNode) {
            for (auto& pin : inNode->GetInputPins()) {
                if (pin.id == inputPinId) { pin.isConnected = true; break; }
            }
        }
        return link.id;
    }

    bool ShaderGraph::RemoveLink(uint32 linkId) {
        auto it = std::find_if(m_Links.begin(), m_Links.end(),
            [linkId](const NodeLink& l) { return l.id == linkId; });
        if (it == m_Links.end()) return false;

        // Unmark pins
        auto* outNode = GetNode(it->outputNodeId);
        if (outNode) {
            for (auto& pin : outNode->GetOutputPins()) {
                if (pin.id == it->outputPinId) {
                    pin.isConnected = false;
                    // Check if another link uses this pin
                    for (auto& other : m_Links) {
                        if (other.id != linkId && other.outputPinId == pin.id) {
                            pin.isConnected = true; break;
                        }
                    }
                    break;
                }
            }
        }
        auto* inNode = GetNode(it->inputNodeId);
        if (inNode) {
            for (auto& pin : inNode->GetInputPins()) {
                if (pin.id == it->inputPinId) { pin.isConnected = false; break; }
            }
        }
        m_Links.erase(it);
        return true;
    }

    std::vector<uint32> ShaderGraph::TopologicalSort() const {
        std::vector<uint32> result;
        std::unordered_map<uint32, int> inDegree;

        // Initialize in-degrees
        for (auto& [id, node] : m_Nodes) {
            inDegree[id] = 0;
        }
        for (auto& link : m_Links) {
            inDegree[link.inputNodeId]++;
        }

        // Kahn's algorithm
        std::vector<uint32> queue;
        for (auto& [id, deg] : inDegree) {
            if (deg == 0) queue.push_back(id);
        }

        while (!queue.empty()) {
            uint32 nodeId = queue.back();
            queue.pop_back();
            result.push_back(nodeId);

            for (auto& link : m_Links) {
                if (link.outputNodeId == nodeId) {
                    if (--inDegree[link.inputNodeId] == 0) {
                        queue.push_back(link.inputNodeId);
                    }
                }
            }
        }
        return result;
    }

    void ShaderGraph::Clear() {
        m_Nodes.clear();
        m_Links.clear();
        m_Properties.clear();
        m_MasterNodeId = 0;
    }

    void ShaderGraph::RemoveProperty(int index) {
        if (index >= 0 && index < (int)m_Properties.size())
            m_Properties.erase(m_Properties.begin() + index);
    }

    bool ShaderGraph::SaveToFile(const std::string& path) const {
        // TODO: JSON serialization using nlohmann/json
        return false;
    }

    bool ShaderGraph::LoadFromFile(const std::string& path) {
        // TODO: JSON deserialization
        return false;
    }

    // ══════════════════════════════════════════════════════════════
    // Node implementations
    // ══════════════════════════════════════════════════════════════

    // ── Master Nodes ──
    PBRMasterNode::PBRMasterNode(uint32 id) : ShaderNode(id, "PBR Master", NodeCategory::Master) {
        AddInputPin("Albedo",     PinType::Color,         {1.0f, 1.0f, 1.0f, 1.0f});
        AddInputPin("Normal",     PinType::Float3);
        AddInputPin("Metallic",   PinType::Float);
        AddInputPin("Roughness",  PinType::Float);
        AddInputPin("AO",         PinType::Float,         {1.0f, 0, 0, 0});
        AddInputPin("Emissive",   PinType::Color);
        AddInputPin("Opacity",    PinType::Float,         {1.0f, 0, 0, 0});
        AddInputPin("PositionOffset", PinType::Float3);
        m_SizeY = 250.0f;
    }

    std::string PBRMasterNode::GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const {
        std::stringstream ss;
        ss << "// PBR Master Stack\n";
        auto getInput = [&](int idx, const char* defaultVal) -> std::string {
            if (idx < (int)m_InputPins.size()) {
                auto it = pinOutputs.find(m_InputPins[idx].id);
                if (it != pinOutputs.end()) return it->second;
            }
            return defaultVal;
        };
        ss << "float3 albedo = " << getInput(0, "float3(1,1,1)") << ";\n";
        ss << "float3 normal = " << getInput(1, "IN.normalWS") << ";\n";
        ss << "float metallic = " << getInput(2, "0.0") << ";\n";
        ss << "float roughness = " << getInput(3, "0.5") << ";\n";
        ss << "float ao = " << getInput(4, "1.0") << ";\n";
        ss << "float3 emissive = " << getInput(5, "float3(0,0,0)") << ";\n";
        ss << "float opacity = " << getInput(6, "1.0") << ";\n";
        ss << "float3 posOffset = " << getInput(7, "float3(0,0,0)") << ";\n\n";
        ss << "// Standard PBR lighting\n";
        ss << "float3 lightDir = normalize(float3(1, 2, 1));\n";
        ss << "float NdotL = max(dot(normal, lightDir), 0);\n";
        ss << "float3 halfVec = normalize(lightDir + normalize(IN.viewDir));\n";
        ss << "float NdotH = max(dot(normal, halfVec), 0);\n";
        ss << "float NdotV = max(dot(normal, normalize(IN.viewDir)), 0);\n";
        ss << "float smoothness = 1 - roughness;\n";
        ss << "float spec = pow(NdotH, 4 / (smoothness * smoothness + 0.001));\n";
        ss << "float3 diffuse = albedo * (1 - metallic) * (1 / PI);\n";
        ss << "float3 specColor = lerp(float3(0.04,0.04,0.04), albedo, metallic);\n";
        ss << "OUT.albedo = (diffuse + specColor * spec) * NdotL * ao + emissive;\n";
        ss << "OUT.normal = normal;\n";
        ss << "OUT.opacity = opacity;\n";
        return ss.str();
    }

    UnlitMasterNode::UnlitMasterNode(uint32 id) : ShaderNode(id, "Unlit Master", NodeCategory::Master) {
        AddInputPin("Color",      PinType::Color, {1.0f, 1.0f, 1.0f, 1.0f});
        AddInputPin("Opacity",    PinType::Float, {1.0f, 0, 0, 0});
        AddInputPin("PositionOffset", PinType::Float3);
        m_SizeY = 150.0f;
    }

    std::string UnlitMasterNode::GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const {
        std::stringstream ss;
        ss << "// Unlit Master Stack\n";
        auto getInput = [&](int idx, const char* defaultVal) -> std::string {
            if (idx < (int)m_InputPins.size()) {
                auto it = pinOutputs.find(m_InputPins[idx].id);
                if (it != pinOutputs.end()) return it->second;
            }
            return defaultVal;
        };
        ss << "float4 color = " << getInput(0, "float4(1,1,1,1)") << ";\n";
        ss << "OUT.albedo = color.rgb;\n";
        ss << "OUT.opacity = " << getInput(1, "1.0") << ";\n";
        return ss.str();
    }

    // ── Math Nodes ──
    #define IMPL_BINARY_MATH_NODE(ClassName, op) \
    void ClassName::InitPins() { \
        AddInputPin("A", PinType::DynamicVector); \
        AddInputPin("B", PinType::DynamicVector); \
        AddOutputPin("Out", PinType::DynamicVector); \
    } \
    std::string ClassName::GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const { \
        auto itA = pinOutputs.find(m_InputPins[0].id); \
        auto itB = pinOutputs.find(m_InputPins[1].id); \
        std::string a = itA != pinOutputs.end() ? itA->second : "0"; \
        std::string b = itB != pinOutputs.end() ? itB->second : "0"; \
        std::stringstream ss; \
        ss << "(" << a << " " op " " << b << ")"; \
        return ss.str(); \
    }

    IMPL_BINARY_MATH_NODE(AddNode,      "+")
    IMPL_BINARY_MATH_NODE(SubtractNode, "-")
    IMPL_BINARY_MATH_NODE(MultiplyNode, "*")
    IMPL_BINARY_MATH_NODE(DivideNode,   "/")

    void PowerNode::InitPins() { AddInputPin("A", PinType::DynamicVector); AddInputPin("Exponent", PinType::Float, {1.0f,0,0,0}); AddOutputPin("Out", PinType::DynamicVector); }
    std::string PowerNode::GenerateHLSL(const std::unordered_map<uint32, std::string>& p) const {
        auto itA = p.find(m_InputPins[0].id); auto itB = p.find(m_InputPins[1].id);
        std::string a = itA != p.end() ? itA->second : "0";
        std::string b = itB != p.end() ? itB->second : "1";
        return "pow(" + a + ", " + b + ")";
    }

    void SquareRootNode::InitPins() { AddInputPin("In", PinType::DynamicVector); AddOutputPin("Out", PinType::DynamicVector); }
    std::string SquareRootNode::GenerateHLSL(const std::unordered_map<uint32, std::string>& p) const {
        auto it = p.find(m_InputPins[0].id); return "sqrt(" + (it != p.end() ? it->second : "0") + ")";
    }

    void AbsoluteNode::InitPins() { AddInputPin("In", PinType::DynamicVector); AddOutputPin("Out", PinType::DynamicVector); }
    std::string AbsoluteNode::GenerateHLSL(const std::unordered_map<uint32, std::string>& p) const {
        auto it = p.find(m_InputPins[0].id); return "abs(" + (it != p.end() ? it->second : "0") + ")";
    }

    void NegateNode::InitPins() { AddInputPin("In", PinType::DynamicVector); AddOutputPin("Out", PinType::DynamicVector); }
    std::string NegateNode::GenerateHLSL(const std::unordered_map<uint32, std::string>& p) const {
        auto it = p.find(m_InputPins[0].id); return "-(" + (it != p.end() ? it->second : "0") + ")";
    }

    void ClampNode::InitPins() { AddInputPin("In", PinType::DynamicVector); AddInputPin("Min", PinType::Float, {0.0f,0,0,0}); AddInputPin("Max", PinType::Float, {1.0f,0,0,0}); AddOutputPin("Out", PinType::DynamicVector); }
    std::string ClampNode::GenerateHLSL(const std::unordered_map<uint32, std::string>& p) const {
        auto it = p.find(m_InputPins[0].id); auto it2 = p.find(m_InputPins[1].id); auto it3 = p.find(m_InputPins[2].id);
        return "clamp(" + (it != p.end() ? it->second : "0") + ", " + (it2 != p.end() ? it2->second : "0") + ", " + (it3 != p.end() ? it3->second : "1") + ")";
    }

    void LerpNode::InitPins() { AddInputPin("A", PinType::DynamicVector); AddInputPin("B", PinType::DynamicVector); AddInputPin("T", PinType::Float, {0.5f,0,0,0}); AddOutputPin("Out", PinType::DynamicVector); }
    std::string LerpNode::GenerateHLSL(const std::unordered_map<uint32, std::string>& p) const {
        auto a = p.find(m_InputPins[0].id); auto b = p.find(m_InputPins[1].id); auto t = p.find(m_InputPins[2].id);
        return "lerp(" + (a != p.end() ? a->second : "0") + ", " + (b != p.end() ? b->second : "0") + ", " + (t != p.end() ? t->second : "0.5") + ")";
    }

    void FresnelNode::InitPins() { AddInputPin("Normal", PinType::Float3); AddInputPin("ViewDir", PinType::Float3); AddInputPin("Power", PinType::Float, {3.0f,0,0,0}); AddOutputPin("Out", PinType::Float); }
    std::string FresnelNode::GenerateHLSL(const std::unordered_map<uint32, std::string>& p) const {
        auto n = p.find(m_InputPins[0].id); auto v = p.find(m_InputPins[1].id); auto pw = p.find(m_InputPins[2].id);
        return "pow(1.0 - max(dot(normalize(" + (n != p.end() ? n->second : "IN.normalWS") + "), normalize(" + (v != p.end() ? v->second : "IN.viewDir") + ")), 0), " + (pw != p.end() ? pw->second : "3") + ")";
    }

    void SaturateNode::InitPins() { AddInputPin("In", PinType::DynamicVector); AddOutputPin("Out", PinType::DynamicVector); }
    std::string SaturateNode::GenerateHLSL(const std::unordered_map<uint32, std::string>& p) const {
        auto it = p.find(m_InputPins[0].id); return "saturate(" + (it != p.end() ? it->second : "0") + ")";
    }

    void OneMinusNode::InitPins() { AddInputPin("In", PinType::DynamicVector); AddOutputPin("Out", PinType::DynamicVector); }
    std::string OneMinusNode::GenerateHLSL(const std::unordered_map<uint32, std::string>& p) const {
        auto it = p.find(m_InputPins[0].id); return "(1.0 - " + (it != p.end() ? it->second : "0") + ")";
    }

    void RemapNode::InitPins() { AddInputPin("In", PinType::Float); AddInputPin("InMin", PinType::Float, {-1.0f,0,0,0}); AddInputPin("InMax", PinType::Float, {1.0f,0,0,0}); AddInputPin("OutMin", PinType::Float, {0.0f,0,0,0}); AddInputPin("OutMax", PinType::Float, {1.0f,0,0,0}); AddOutputPin("Out", PinType::Float); }
    std::string RemapNode::GenerateHLSL(const std::unordered_map<uint32, std::string>& p) const {
        auto in = p.find(m_InputPins[0].id); auto imin = p.find(m_InputPins[1].id); auto imax = p.find(m_InputPins[2].id);
        auto omin = p.find(m_InputPins[3].id); auto omax = p.find(m_InputPins[4].id);
        std::string a = in != p.end() ? in->second : "0", b = imin != p.end() ? imin->second : "-1", c = imax != p.end() ? imax->second : "1";
        std::string d = omin != p.end() ? omin->second : "0", e = omax != p.end() ? omax->second : "1";
        return "(" + d + " + (" + a + " - " + b + ") / (" + c + " - " + b + ") * (" + e + " - " + d + "))";
    }

    // ── Vector Nodes ──
    void DotNode::InitPins() { AddInputPin("A", PinType::Float3); AddInputPin("B", PinType::Float3); AddOutputPin("Out", PinType::Float); }
    std::string DotNode::GenerateHLSL(const std::unordered_map<uint32, std::string>& p) const {
        auto a = p.find(m_InputPins[0].id); auto b = p.find(m_InputPins[1].id);
        return "dot(" + (a != p.end() ? a->second : "0") + ", " + (b != p.end() ? b->second : "0") + ")";
    }

    void CrossNode::InitPins() { AddInputPin("A", PinType::Float3); AddInputPin("B", PinType::Float3); AddOutputPin("Out", PinType::Float3); }
    std::string CrossNode::GenerateHLSL(const std::unordered_map<uint32, std::string>& p) const {
        auto a = p.find(m_InputPins[0].id); auto b = p.find(m_InputPins[1].id);
        return "cross(" + (a != p.end() ? a->second : "float3(0,0,0)") + ", " + (b != p.end() ? b->second : "float3(0,0,0)") + ")";
    }

    void NormalizeNode::InitPins() { AddInputPin("In", PinType::Float3); AddOutputPin("Out", PinType::Float3); }
    std::string NormalizeNode::GenerateHLSL(const std::unordered_map<uint32, std::string>& p) const {
        auto in = p.find(m_InputPins[0].id); return "normalize(" + (in != p.end() ? in->second : "float3(0,0,0)") + ")";
    }

    void DistanceNode::InitPins() { AddInputPin("A", PinType::Float3); AddInputPin("B", PinType::Float3); AddOutputPin("Out", PinType::Float); }
    std::string DistanceNode::GenerateHLSL(const std::unordered_map<uint32, std::string>& p) const {
        auto a = p.find(m_InputPins[0].id); auto b = p.find(m_InputPins[1].id);
        return "distance(" + (a != p.end() ? a->second : "float3(0,0,0)") + ", " + (b != p.end() ? b->second : "float3(0,0,0)") + ")";
    }

    void LengthNode::InitPins() { AddInputPin("In", PinType::Float3); AddOutputPin("Out", PinType::Float); }
    std::string LengthNode::GenerateHLSL(const std::unordered_map<uint32, std::string>& p) const {
        auto in = p.find(m_InputPins[0].id); return "length(" + (in != p.end() ? in->second : "float3(0,0,0)") + ")";
    }

    // ── Input Nodes ──
    void TimeNode::InitPins() { AddOutputPin("Time", PinType::Float); }
    std::string TimeNode::GenerateHLSL(const std::unordered_map<uint32, std::string>&) const { return "IN.time"; }

    void UVNode::InitPins() { AddOutputPin("UV", PinType::Float2); }
    std::string UVNode::GenerateHLSL(const std::unordered_map<uint32, std::string>&) const { return "IN.uv"; }

    void PositionNode::InitPins() { AddOutputPin("Position", PinType::Float3); }
    std::string PositionNode::GenerateHLSL(const std::unordered_map<uint32, std::string>&) const { return "IN.positionWS"; }

    void NormalNode::InitPins() { AddOutputPin("Normal", PinType::Float3); }
    std::string NormalNode::GenerateHLSL(const std::unordered_map<uint32, std::string>&) const { return "IN.normalWS"; }

    void ViewDirNode::InitPins() { AddOutputPin("ViewDir", PinType::Float3); }
    std::string ViewDirNode::GenerateHLSL(const std::unordered_map<uint32, std::string>&) const { return "IN.viewDir"; }

    void TangentNode::InitPins() { AddOutputPin("Tangent", PinType::Float3); }
    std::string TangentNode::GenerateHLSL(const std::unordered_map<uint32, std::string>&) const { return "IN.tangentWS"; }

    // ── Art Nodes ──
    ColorNode::ColorNode(uint32 id) : ShaderNode(id, "Color", NodeCategory::Art) {
        PinValue def; def.f[0] = 1; def.f[1] = 1; def.f[2] = 1; def.f[3] = 1;
        AddInputPin("RGBA", PinType::Color, def);
        AddOutputPin("Out", PinType::Color);
    }
    std::string ColorNode::GenerateHLSL(const std::unordered_map<uint32, std::string>&) const {
        auto& c = m_InputPins[0].defaultValue.f;
        std::stringstream ss; ss << "float4(" << c[0] << ", " << c[1] << ", " << c[2] << ", " << c[3] << ")";
        return ss.str();
    }

    FloatNode::FloatNode(uint32 id) : ShaderNode(id, "Float", NodeCategory::Art) {
        AddInputPin("Value", PinType::Float);
        AddOutputPin("Out", PinType::Float);
    }
    std::string FloatNode::GenerateHLSL(const std::unordered_map<uint32, std::string>&) const {
        std::stringstream ss; ss << m_InputPins[0].defaultValue.f[0];
        return ss.str();
    }

    Vector2Node::Vector2Node(uint32 id) : ShaderNode(id, "Vector 2", NodeCategory::Art) {
        AddInputPin("XY", PinType::Float2);
        AddOutputPin("Out", PinType::Float2);
    }
    std::string Vector2Node::GenerateHLSL(const std::unordered_map<uint32, std::string>&) const {
        auto& v = m_InputPins[0].defaultValue.f;
        std::stringstream ss; ss << "float2(" << v[0] << ", " << v[1] << ")";
        return ss.str();
    }

    Vector3Node::Vector3Node(uint32 id) : ShaderNode(id, "Vector 3", NodeCategory::Art) {
        AddInputPin("XYZ", PinType::Float3);
        AddOutputPin("Out", PinType::Float3);
    }
    std::string Vector3Node::GenerateHLSL(const std::unordered_map<uint32, std::string>&) const {
        auto& v = m_InputPins[0].defaultValue.f;
        std::stringstream ss; ss << "float3(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
        return ss.str();
    }

    // ── Texture Node ──
    SampleTexture2DNode::SampleTexture2DNode(uint32 id) : ShaderNode(id, "Sample Texture 2D", NodeCategory::Texture) {
        AddInputPin("UV", PinType::Float2);
        AddInputPin("Texture", PinType::Texture2D);
        AddOutputPin("RGBA", PinType::Color);
        AddOutputPin("R", PinType::Float);
        AddOutputPin("G", PinType::Float);
        AddOutputPin("B", PinType::Float);
        AddOutputPin("A", PinType::Float);
        m_SizeY = 180.0f;
    }

    std::string SampleTexture2DNode::GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const {
        auto uvIt = pinOutputs.find(m_InputPins[0].id);
        std::string uv = uvIt != pinOutputs.end() ? uvIt->second : "IN.uv";
        std::stringstream ss;
        ss << "SAMPLE_TEXTURE2D(_MainTex, sampler_MainTex, " << uv << ")";
        return ss.str();
    }

    std::string SampleTexture2DNode::GeneratePropertyDecl() const {
        return "Texture2D _MainTex;\nSamplerState sampler_MainTex;\n";
    }

    // ── Noise Node ──
    void NoiseNode::InitPins() { AddInputPin("UV", PinType::Float2); AddInputPin("Scale", PinType::Float, {1.0f,0,0,0}); AddOutputPin("Out", PinType::Float); }
    std::string NoiseNode::GenerateHLSL(const std::unordered_map<uint32, std::string>& p) const {
        auto uv = p.find(m_InputPins[0].id); auto sc = p.find(m_InputPins[1].id);
        std::string u = uv != p.end() ? uv->second : "IN.uv";
        std::string s = sc != p.end() ? sc->second : "1.0";
        std::stringstream ss;
        ss << "((sin(" << u << ".x * " << s << " * 12.9898 + " << u << ".y * " << s << " * 78.233) * 43758.5453 - floor(sin(" << u << ".x * " << s << " * 12.9898 + " << u << ".y * " << s << " * 78.233) * 43758.5453))";
        return ss.str();
    }

}} // namespace Engine::ShaderGraph