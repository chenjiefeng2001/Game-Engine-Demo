#pragma once

/**
 * @file ShaderGraphCore.h
 * @brief 着色器图核心数据结构 — 节点、引脚、连接、图
 *
 * 实现类似 Unity Shader Graph / Unreal Material Editor 的节点图架构。
 * 支持 PBR/Unlit 主栈，实时预览，HLSL 代码生成。
 */

#include "Engine/Types.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <glm/glm.hpp>

namespace Engine {
namespace ShaderGraph {

    // ============================================================
    // 引脚方向
    // ============================================================
    enum class PinDirection : uint8 {
        Input,   ///< 输入引脚（在节点左侧）
        Output   ///< 输出引脚（在节点右侧）
    };

    // ============================================================
    // 数据类型
    // ============================================================
    enum class PinType : uint8 {
        Float,          ///< 单浮点
        Float2,         ///< vec2
        Float3,         ///< vec3
        Float4,         ///< vec4
        Color,          ///< 颜色（vec4，编辑器中显示颜色拾取器）
        Texture2D,      ///< 2D 纹理
        SamplerState,   ///< 采样器状态
        Boolean,        ///< 布尔
        DynamicVector,  ///< 动态向量（根据连接自动推断维度）
        Count
    };

    /// 获取数据类型的 GLSL/HLSL 类型名
    inline const char* PinTypeName(PinType t) {
        switch (t) {
            case PinType::Float:         return "float";
            case PinType::Float2:        return "float2";
            case PinType::Float3:        return "float3";
            case PinType::Float4:        return "float4";
            case PinType::Color:         return "float4";
            case PinType::Texture2D:     return "Texture2D";
            case PinType::SamplerState:  return "SamplerState";
            case PinType::Boolean:       return "bool";
            case PinType::DynamicVector: return "float4";
            default:                     return "float";
        }
    }

    /// 获取数据类型的维度（用于 DynamicVector 推断）
    inline int PinTypeDimension(PinType t) {
        switch (t) {
            case PinType::Float:    return 1;
            case PinType::Float2:   return 2;
            case PinType::Float3:   return 3;
            case PinType::Float4:
            case PinType::Color:    return 4;
            default:                return 1;
        }
    }

    // ============================================================
    // 引脚值（默认值或常量值）
    // ============================================================
    struct PinValue {
        union {
            float   f[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            bool    b;
        };
        std::string texturePath;  ///< Texture2D 的路径
    };

    // ============================================================
    // 引脚定义
    // ============================================================
    struct Pin {
        uint32      id;
        std::string name;
        PinDirection direction;
        PinType     type;
        PinValue    defaultValue;
        uint32      nodeId;    ///< 所属节点 ID

        // 运行时数据
        bool        isConnected = false;
    };

    // ============================================================
    // 连接（从输出引脚到输入引脚）
    // ============================================================
    struct NodeLink {
        uint32 id;
        uint32 outputNodeId;
        uint32 outputPinId;
        uint32 inputNodeId;
        uint32 inputPinId;
    };

    // ============================================================
    // 节点类别
    // ============================================================
    enum class NodeCategory : uint8 {
        Input,       ///< 输入节点（Time, UV, Position 等）
        Math,        ///< 数学运算（Add, Multiply, Fresnel 等）
        Vector,      ///< 向量运算（Dot, Cross, Normalize）
        Art,         ///< 艺术节点（Color, Float 常量）
        Texture,     ///< 纹理采样
        Master,      ///< 主栈节点（PBR, Unlit）
        Utility,     ///< 工具节点
        Count
    };

    inline const char* NodeCategoryName(NodeCategory c) {
        switch (c) {
            case NodeCategory::Input:   return "Input";
            case NodeCategory::Math:    return "Math";
            case NodeCategory::Vector:  return "Vector";
            case NodeCategory::Art:     return "Artistic";
            case NodeCategory::Texture: return "Texture";
            case NodeCategory::Master:  return "Master";
            case NodeCategory::Utility: return "Utility";
            default: return "Unknown";
        }
    }

    // ============================================================
    // 着色器节点基类
    // ============================================================
    class ShaderNode {
    public:
        ShaderNode(uint32 id, const std::string& name, NodeCategory category)
            : m_Id(id), m_Name(name), m_Category(category) {}
        virtual ~ShaderNode() = default;

        uint32              GetId() const { return m_Id; }
        const std::string&  GetName() const { return m_Name; }
        void                SetName(const std::string& n) { m_Name = n; }
        NodeCategory        GetCategory() const { return m_Category; }

        const std::vector<Pin>& GetInputPins() const { return m_InputPins; }
        const std::vector<Pin>& GetOutputPins() const { return m_OutputPins; }
        std::vector<Pin>&       GetInputPins() { return m_InputPins; }
        std::vector<Pin>&       GetOutputPins() { return m_OutputPins; }

        // 节点编辑器位置
        void SetPosition(float x, float y) { m_PosX = x; m_PosY = y; }
        float GetPosX() const { return m_PosX; }
        float GetPosY() const { return m_PosY; }

        // 节点大小
        void SetSize(float w, float h) { m_SizeX = w; m_SizeY = h; }
        float GetSizeX() const { return m_SizeX; }
        float GetSizeY() const { return m_SizeY; }

        // 生成 HLSL 代码（子类实现）
        virtual std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const = 0;

        // 生成属性声明（用于黑板的全局属性）
        virtual std::string GeneratePropertyDecl() const { return ""; }

        // 克隆
        virtual std::unique_ptr<ShaderNode> Clone(uint32 newId) const = 0;

    protected:
        uint32      m_Id;
        std::string m_Name;
        NodeCategory m_Category;
        std::vector<Pin> m_InputPins;
        std::vector<Pin> m_OutputPins;
        float m_PosX = 100.0f, m_PosY = 100.0f;
        float m_SizeX = 150.0f, m_SizeY = 100.0f;

        Pin& AddInputPin(const std::string& name, PinType type, PinValue defaultValue = {}) {
            m_InputPins.push_back({ nextPinId(), name, PinDirection::Input, type, defaultValue, m_Id });
            return m_InputPins.back();
        }
        Pin& AddOutputPin(const std::string& name, PinType type) {
            m_OutputPins.push_back({ nextPinId(), name, PinDirection::Output, type, {}, m_Id });
            return m_OutputPins.back();
        }

    private:
        static uint32 nextPinId() {
            static uint32 s_PinIdCounter = 1;
            return s_PinIdCounter++;
        }
    };

    // ============================================================
    // 着色器图
    // ============================================================
    class ShaderGraph {
    public:
        ShaderGraph() = default;
        ~ShaderGraph() = default;

        uint32 AddNode(std::unique_ptr<ShaderNode> node);
        bool   RemoveNode(uint32 nodeId);
        ShaderNode* GetNode(uint32 nodeId);
        const ShaderNode* GetNode(uint32 nodeId) const;
        const std::unordered_map<uint32, std::unique_ptr<ShaderNode>>& GetNodes() const { return m_Nodes; }

        uint32 AddLink(uint32 outputNodeId, uint32 outputPinId,
                       uint32 inputNodeId, uint32 inputPinId);
        bool   RemoveLink(uint32 linkId);
        const std::vector<NodeLink>& GetLinks() const { return m_Links; }

        // 拓扑排序（用于代码生成）
        std::vector<uint32> TopologicalSort() const;

        // 清除所有
        void Clear();

        // 序列化
        bool SaveToFile(const std::string& path) const;
        bool LoadFromFile(const std::string& path);

        // 黑板系统 — 全局属性
        struct BlackboardProperty {
            std::string name;
            PinType     type = PinType::Float;
            PinValue    value;
            std::string tooltip;
        };
        std::vector<BlackboardProperty>& GetProperties() { return m_Properties; }
        const std::vector<BlackboardProperty>& GetProperties() const { return m_Properties; }
        void AddProperty(const BlackboardProperty& prop) { m_Properties.push_back(prop); }
        void RemoveProperty(int index);

        // 主栈节点 ID（PBR Master / Unlit Master）
        void SetMasterNodeId(uint32 id) { m_MasterNodeId = id; }
        uint32 GetMasterNodeId() const { return m_MasterNodeId; }

    private:
        std::unordered_map<uint32, std::unique_ptr<ShaderNode>> m_Nodes;
        std::vector<NodeLink> m_Links;
        std::vector<BlackboardProperty> m_Properties;
        uint32 m_MasterNodeId = 0;
        uint32 m_NextNodeId = 1;
        uint32 m_NextLinkId = 1;
    };

}} // namespace Engine::ShaderGraph