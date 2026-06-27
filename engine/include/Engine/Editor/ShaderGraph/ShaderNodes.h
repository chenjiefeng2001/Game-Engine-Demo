#pragma once

/**
 * @file ShaderNodes.h
 * @brief 内置着色器节点类型集合 — 约 30+ 个节点
 *
 * 分类：
 *   - Input:  Time, UV, Position, Normal, ViewDirection, Tangent
 *   - Math:   Add, Subtract, Multiply, Divide, Power, SquareRoot, Absolute,
 *             Negate, Clamp, Lerp, Fresnel, Remap, Saturate, OneMinus
 *   - Vector: Dot, Cross, Normalize, Distance, Length, Transform
 *   - Art:    Color, Float, Vector2/3/4, Noise, Gradient
 *   - Texture: SampleTexture2D, TexelSize
 *   - Master: PBRMaster, UnlitMaster
 */

#include "ShaderGraphCore.h"
#include <sstream>

namespace Engine {
namespace ShaderGraph {

    // ══════════════════════════════════════════════════════════════
    // 辅助宏
    // ══════════════════════════════════════════════════════════════
    #define DECLARE_SHADER_NODE(ClassName, NodeName, Category) \
        class ClassName : public ShaderNode { \
        public: \
            ClassName(uint32 id) : ShaderNode(id, NodeName, Category) { InitPins(); } \
            std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override; \
            std::unique_ptr<ShaderNode> Clone(uint32 newId) const override { return std::make_unique<ClassName>(newId); } \
        private: \
            void InitPins(); \
        };

    // ══════════════════════════════════════════════════════════════
    // Master 节点
    // ══════════════════════════════════════════════════════════════
    class PBRMasterNode : public ShaderNode {
    public:
        PBRMasterNode(uint32 id);
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override;
        std::string GeneratePropertyDecl() const override { return ""; }
        std::unique_ptr<ShaderNode> Clone(uint32 newId) const override { return std::make_unique<PBRMasterNode>(newId); }
    };

    class UnlitMasterNode : public ShaderNode {
    public:
        UnlitMasterNode(uint32 id);
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override;
        std::string GeneratePropertyDecl() const override { return ""; }
        std::unique_ptr<ShaderNode> Clone(uint32 newId) const override { return std::make_unique<UnlitMasterNode>(newId); }
    };

    // ══════════════════════════════════════════════════════════════
    // Math 节点
    // ══════════════════════════════════════════════════════════════
    DECLARE_SHADER_NODE(AddNode,          "Add",          NodeCategory::Math)
    DECLARE_SHADER_NODE(SubtractNode,     "Subtract",     NodeCategory::Math)
    DECLARE_SHADER_NODE(MultiplyNode,     "Multiply",     NodeCategory::Math)
    DECLARE_SHADER_NODE(DivideNode,       "Divide",       NodeCategory::Math)
    DECLARE_SHADER_NODE(PowerNode,        "Power",        NodeCategory::Math)
    DECLARE_SHADER_NODE(SquareRootNode,   "Square Root",  NodeCategory::Math)
    DECLARE_SHADER_NODE(AbsoluteNode,     "Absolute",     NodeCategory::Math)
    DECLARE_SHADER_NODE(NegateNode,       "Negate",       NodeCategory::Math)
    DECLARE_SHADER_NODE(ClampNode,        "Clamp",        NodeCategory::Math)
    DECLARE_SHADER_NODE(LerpNode,         "Lerp",         NodeCategory::Math)
    DECLARE_SHADER_NODE(FresnelNode,      "Fresnel",      NodeCategory::Math)
    DECLARE_SHADER_NODE(SaturateNode,     "Saturate",     NodeCategory::Math)
    DECLARE_SHADER_NODE(OneMinusNode,     "One Minus",    NodeCategory::Math)
    DECLARE_SHADER_NODE(RemapNode,        "Remap",        NodeCategory::Math)

    // ══════════════════════════════════════════════════════════════
    // Vector 节点
    // ══════════════════════════════════════════════════════════════
    DECLARE_SHADER_NODE(DotNode,          "Dot Product",  NodeCategory::Vector)
    DECLARE_SHADER_NODE(CrossNode,        "Cross Product",NodeCategory::Vector)
    DECLARE_SHADER_NODE(NormalizeNode,    "Normalize",    NodeCategory::Vector)
    DECLARE_SHADER_NODE(DistanceNode,     "Distance",     NodeCategory::Vector)
    DECLARE_SHADER_NODE(LengthNode,       "Length",       NodeCategory::Vector)

    // ══════════════════════════════════════════════════════════════
    // Input 节点
    // ══════════════════════════════════════════════════════════════
    DECLARE_SHADER_NODE(TimeNode,         "Time",         NodeCategory::Input)
    DECLARE_SHADER_NODE(UVNode,           "UV",           NodeCategory::Input)
    DECLARE_SHADER_NODE(PositionNode,     "Position",     NodeCategory::Input)
    DECLARE_SHADER_NODE(NormalNode,       "Normal Vector",NodeCategory::Input)
    DECLARE_SHADER_NODE(ViewDirNode,      "View Direction",NodeCategory::Input)
    DECLARE_SHADER_NODE(TangentNode,      "Tangent",      NodeCategory::Input)

    // ══════════════════════════════════════════════════════════════
    // Art 节点
    // ══════════════════════════════════════════════════════════════
    class ColorNode : public ShaderNode {
    public:
        ColorNode(uint32 id);
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override;
        std::unique_ptr<ShaderNode> Clone(uint32 newId) const override { return std::make_unique<ColorNode>(newId); }
        glm::vec4 GetColor() const { return glm::vec4(m_InputPins[0].defaultValue.f[0], m_InputPins[0].defaultValue.f[1], m_InputPins[0].defaultValue.f[2], m_InputPins[0].defaultValue.f[3]); }
        void SetColor(const glm::vec4& c) { for(int i=0;i<4;i++) m_InputPins[0].defaultValue.f[i] = c[i]; }
    };

    class FloatNode : public ShaderNode {
    public:
        FloatNode(uint32 id);
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override;
        std::unique_ptr<ShaderNode> Clone(uint32 newId) const override { return std::make_unique<FloatNode>(newId); }
        float GetValue() const { return m_InputPins[0].defaultValue.f[0]; }
        void SetValue(float v) { m_InputPins[0].defaultValue.f[0] = v; }
    };

    class Vector2Node : public ShaderNode {
    public:
        Vector2Node(uint32 id);
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override;
        std::unique_ptr<ShaderNode> Clone(uint32 newId) const override { return std::make_unique<Vector2Node>(newId); }
    };

    class Vector3Node : public ShaderNode {
    public:
        Vector3Node(uint32 id);
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override;
        std::unique_ptr<ShaderNode> Clone(uint32 newId) const override { return std::make_unique<Vector3Node>(newId); }
    };

    // ══════════════════════════════════════════════════════════════
    // Texture 节点
    // ══════════════════════════════════════════════════════════════
    class SampleTexture2DNode : public ShaderNode {
    public:
        SampleTexture2DNode(uint32 id);
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override;
        std::string GeneratePropertyDecl() const override;
        std::unique_ptr<ShaderNode> Clone(uint32 newId) const override { return std::make_unique<SampleTexture2DNode>(newId); }
    };

    DECLARE_SHADER_NODE(NoiseNode,        "Noise",        NodeCategory::Art)

    // ══════════════════════════════════════════════════════════════
    // Property 节点（黑板拖拽生成）
    // ══════════════════════════════════════════════════════════════
    class PropertyNode : public ShaderNode {
    public:
        PropertyNode(uint32 id, const std::string& propName, PinType propType)
            : ShaderNode(id, "Get " + propName, NodeCategory::Input), m_PropertyName(propName) {
            AddOutputPin(propName, propType);
            m_SizeX = 120.0f; m_SizeY = 60.0f;
        }
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>&) const override {
            return "_" + m_PropertyName;
        }
        std::unique_ptr<ShaderNode> Clone(uint32 newId) const override {
            return std::make_unique<PropertyNode>(newId, m_PropertyName, m_OutputPins[0].type);
        }
        std::string m_PropertyName;
    };

    // ══════════════════════════════════════════════════════════════
    // 节点工厂
    // ══════════════════════════════════════════════════════════════
    struct NodeFactoryEntry {
        const char* name;
        NodeCategory category;
        std::function<std::unique_ptr<ShaderNode>(uint32)> factory;
    };

    /// 获取所有可创建的节点类型
    inline std::vector<NodeFactoryEntry> GetNodeFactoryList() {
        return {
            // Master
            {"PBR Master",    NodeCategory::Master,  [](uint32 id) { return std::make_unique<PBRMasterNode>(id); }},
            {"Unlit Master",  NodeCategory::Master,  [](uint32 id) { return std::make_unique<UnlitMasterNode>(id); }},
            // Math
            {"Add",           NodeCategory::Math,    [](uint32 id) { return std::make_unique<AddNode>(id); }},
            {"Subtract",      NodeCategory::Math,    [](uint32 id) { return std::make_unique<SubtractNode>(id); }},
            {"Multiply",      NodeCategory::Math,    [](uint32 id) { return std::make_unique<MultiplyNode>(id); }},
            {"Divide",        NodeCategory::Math,    [](uint32 id) { return std::make_unique<DivideNode>(id); }},
            {"Power",         NodeCategory::Math,    [](uint32 id) { return std::make_unique<PowerNode>(id); }},
            {"Square Root",   NodeCategory::Math,    [](uint32 id) { return std::make_unique<SquareRootNode>(id); }},
            {"Absolute",      NodeCategory::Math,    [](uint32 id) { return std::make_unique<AbsoluteNode>(id); }},
            {"Negate",        NodeCategory::Math,    [](uint32 id) { return std::make_unique<NegateNode>(id); }},
            {"Clamp",         NodeCategory::Math,    [](uint32 id) { return std::make_unique<ClampNode>(id); }},
            {"Lerp",          NodeCategory::Math,    [](uint32 id) { return std::make_unique<LerpNode>(id); }},
            {"Fresnel",       NodeCategory::Math,    [](uint32 id) { return std::make_unique<FresnelNode>(id); }},
            {"Saturate",      NodeCategory::Math,    [](uint32 id) { return std::make_unique<SaturateNode>(id); }},
            {"One Minus",     NodeCategory::Math,    [](uint32 id) { return std::make_unique<OneMinusNode>(id); }},
            {"Remap",         NodeCategory::Math,    [](uint32 id) { return std::make_unique<RemapNode>(id); }},
            // Vector
            {"Dot Product",   NodeCategory::Vector,  [](uint32 id) { return std::make_unique<DotNode>(id); }},
            {"Cross Product", NodeCategory::Vector,  [](uint32 id) { return std::make_unique<CrossNode>(id); }},
            {"Normalize",     NodeCategory::Vector,  [](uint32 id) { return std::make_unique<NormalizeNode>(id); }},
            {"Distance",      NodeCategory::Vector,  [](uint32 id) { return std::make_unique<DistanceNode>(id); }},
            {"Length",        NodeCategory::Vector,  [](uint32 id) { return std::make_unique<LengthNode>(id); }},
            // Input
            {"Time",          NodeCategory::Input,   [](uint32 id) { return std::make_unique<TimeNode>(id); }},
            {"UV",            NodeCategory::Input,   [](uint32 id) { return std::make_unique<UVNode>(id); }},
            {"Position",      NodeCategory::Input,   [](uint32 id) { return std::make_unique<PositionNode>(id); }},
            {"Normal Vector", NodeCategory::Input,   [](uint32 id) { return std::make_unique<NormalNode>(id); }},
            {"View Direction",NodeCategory::Input,   [](uint32 id) { return std::make_unique<ViewDirNode>(id); }},
            {"Tangent",       NodeCategory::Input,   [](uint32 id) { return std::make_unique<TangentNode>(id); }},
            // Art
            {"Color",         NodeCategory::Art,     [](uint32 id) { return std::make_unique<ColorNode>(id); }},
            {"Float",         NodeCategory::Art,     [](uint32 id) { return std::make_unique<FloatNode>(id); }},
            {"Vector 2",      NodeCategory::Art,     [](uint32 id) { return std::make_unique<Vector2Node>(id); }},
            {"Vector 3",      NodeCategory::Art,     [](uint32 id) { return std::make_unique<Vector3Node>(id); }},
            {"Noise",         NodeCategory::Art,     [](uint32 id) { return std::make_unique<NoiseNode>(id); }},
            // Texture
            {"Sample Texture2D", NodeCategory::Texture, [](uint32 id) { return std::make_unique<SampleTexture2DNode>(id); }},
        };
    }

}} // namespace Engine::ShaderGraph