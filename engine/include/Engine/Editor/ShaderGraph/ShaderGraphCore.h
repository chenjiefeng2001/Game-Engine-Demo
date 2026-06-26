#pragma once

/**
 * @file ShaderGraphCore.h
 * @brief 着色器图核心数据结构 — 节点、引脚、连接、图（重构版）
 *
 * 架构改进：
 *   1. 引脚连接校验 (CanConnectPins) — 类型兼容 + DAG 成环检测
 *   2. Reroute 节点为 ShaderNode 子类，统一存储
 *   3. 脏标记 (DirtyFlag) — 触发自动重编译和 * 号提示
 *   4. 子图面包屑导航 (GraphStack)
 *   5. 撤销/重做命令模式 (UndoCommand)
 *   6. 统一选中状态 (std::unordered_set)
 *   7. 剪贴板序列化 (JSON)
 *   8. 编译错误报告
 */

#include "Engine/Types.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <glm/glm.hpp>

namespace Engine {
namespace ShaderGraph {

    // ============================================================
    // 引脚方向
    // ============================================================
    enum class PinDirection : uint8 {
        Input,
        Output
    };

    // ============================================================
    // 数据类型（带颜色语义编码）
    // ============================================================
    enum class PinType : uint8 {
        Float,          // 蓝 - 单通道
        Float2,         // 黄绿 - vec2
        Float3,         // 黄 - vec3
        Float4,         // 粉 - vec4
        Color,          // 粉 - RGBA (编辑器中用拾色器)
        Texture2D,      // 橙 - 2D纹理
        SamplerState,   // 橙 - 采样器
        Boolean,        // 红 - 布尔
        DynamicVector,  // 灰 - 动态推断
        Count
    };

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

    /// 检查类型是否可自动转换（Float → Float2/3/4 可升维）
    inline bool CanAutoConvert(PinType from, PinType to) {
        if (from == to) return true;
        if (from == PinType::Float && to == PinType::Float2) return true;
        if (from == PinType::Float && to == PinType::Float3) return true;
        if (from == PinType::Float && to == PinType::Float4) return true;
        if (from == PinType::Float && to == PinType::Color) return true;
        if (from == PinType::Float2 && to == PinType::Float3) return true;
        if (from == PinType::Float2 && to == PinType::Float4) return true;
        if (from == PinType::Float3 && to == PinType::Float4) return true;
        if (from == PinType::Float3 && to == PinType::Color) return true;
        if (from == PinType::DynamicVector || to == PinType::DynamicVector) return true;
        return false;
    }

    // ============================================================
    // 引脚值
    // ============================================================
    struct PinValue {
        union { float f[4] = {0,0,0,0}; bool b; };
        std::string texturePath;
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
        uint32      nodeId;
        bool        isConnected = false;
    };

    // ============================================================
    // 连接
    // ============================================================
    struct NodeLink {
        uint32 id;
        uint32 outputNodeId;
        uint32 outputPinId;
        uint32 inputNodeId;
        uint32 inputPinId;
    };

    // ============================================================
    // 编译错误
    // ============================================================
    struct CompileError {
        enum Severity { Info, Warning, Error };
        Severity severity = Error;
        uint32 nodeId = 0;
        uint32 pinId = 0;
        std::string message;
    };

    // ============================================================
    // 节点类别
    // ============================================================
    enum class NodeCategory : uint8 {
        Input, Math, Vector, Art, Texture, Master, Utility, Reroute, CustomHLSL, SubgraphRef, Count
    };

    inline const char* NodeCategoryName(NodeCategory c) {
        switch (c) {
            case NodeCategory::Input:      return "Input";
            case NodeCategory::Math:       return "Math";
            case NodeCategory::Vector:     return "Vector";
            case NodeCategory::Art:        return "Artistic";
            case NodeCategory::Texture:    return "Texture";
            case NodeCategory::Master:     return "Master";
            case NodeCategory::Utility:    return "Utility";
            case NodeCategory::Reroute:    return "Reroute";
            case NodeCategory::CustomHLSL: return "Custom HLSL";
            case NodeCategory::SubgraphRef:return "Subgraph";
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

        void SetPosition(float x, float y) { m_PosX = x; m_PosY = y; }
        float GetPosX() const { return m_PosX; }
        float GetPosY() const { return m_PosY; }
        void SetSize(float w, float h) { m_SizeX = w; m_SizeY = h; }
        float GetSizeX() const { return m_SizeX; }
        float GetSizeY() const { return m_SizeY; }
        bool  IsCollapsed() const { return m_Collapsed; }
        void  SetCollapsed(bool c) { m_Collapsed = c; }

        virtual std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const = 0;
        virtual std::string GeneratePropertyDecl() const { return ""; }
        virtual std::unique_ptr<ShaderNode> Clone(uint32 newId) const = 0;
        virtual void DrawInlineContent() {} // 节点内联编辑控件

        // 自定义 HLSL 代码
        virtual const std::string& GetHLSLCode() const { static std::string empty; return empty; }
        virtual void SetHLSLCode(const std::string&) {}

        // 子图引用路径
        virtual const std::string& GetSubgraphPath() const { static std::string empty; return empty; }
        virtual void SetSubgraphPath(const std::string&) {}

    protected:
        uint32      m_Id;
        std::string m_Name;
        NodeCategory m_Category;
        std::vector<Pin> m_InputPins;
        std::vector<Pin> m_OutputPins;
        float m_PosX = 100.0f, m_PosY = 100.0f;
        float m_SizeX = 150.0f, m_SizeY = 100.0f;
        bool  m_Collapsed = false;

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
    // 路由节点 (RerouteNode) — 是 ShaderNode，不是独立结构
    // ============================================================
    class RerouteNode : public ShaderNode {
    public:
        RerouteNode(uint32 id);
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override;
        std::unique_ptr<ShaderNode> Clone(uint32 newId) const override;
    };

    // ============================================================
    // 自定义 HLSL 代码节点
    // ============================================================
    class CustomHLSLNode : public ShaderNode {
    public:
        CustomHLSLNode(uint32 id);
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override;
        std::string GeneratePropertyDecl() const override;
        std::unique_ptr<ShaderNode> Clone(uint32 newId) const override;
        void DrawInlineContent() override;
        const std::string& GetHLSLCode() const override { return m_HLSLCode; }
        void SetHLSLCode(const std::string& code) override { m_HLSLCode = code; ParsePins(); }

        void ParsePins(); // 从 HLSL 注释解析输入/输出引脚
    private:
        std::string m_HLSLCode = R"(// Use comments to define pins:
// @input float myInput
// @output float result
return myInput * 2.0;
)";
    };

    // ============================================================
    // 子图引用节点
    // ============================================================
    class SubgraphRefNode : public ShaderNode {
    public:
        SubgraphRefNode(uint32 id);
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override;
        std::unique_ptr<ShaderNode> Clone(uint32 newId) const override;
        const std::string& GetSubgraphPath() const override { return m_SubgraphPath; }
        void SetSubgraphPath(const std::string& path) override { m_SubgraphPath = path; }
    private:
        std::string m_SubgraphPath;
    };

    // ============================================================
    // 撤销命令
    // ============================================================
    struct UndoCommand {
        enum Type { AddNode, RemoveNode, MoveNode, AddLink, RemoveLink, EditProperty, GroupAction };
        Type type;
        std::string jsonBefore; // 操作前的 JSON 快照
        std::string jsonAfter;  // 操作后的 JSON 快照
        uint32 nodeId = 0;
        uint32 linkId = 0;
        float oldX = 0, oldY = 0, newX = 0, newY = 0;
    };

    // ============================================================
    // 着色器图（重构版）
    // ============================================================
    class ShaderGraph {
    public:
        ShaderGraph() = default;
        ~ShaderGraph() = default;

        // ── 节点操作 ──
        uint32 AddNode(std::unique_ptr<ShaderNode> node);
        bool   RemoveNode(uint32 nodeId);
        ShaderNode* GetNode(uint32 nodeId);
        const ShaderNode* GetNode(uint32 nodeId) const;
        const std::unordered_map<uint32, std::unique_ptr<ShaderNode>>& GetNodes() const { return m_Nodes; }

        // ── 连接操作（带校验） ──
        uint32 AddLink(uint32 outputNodeId, uint32 outputPinId, uint32 inputNodeId, uint32 inputPinId);
        bool   RemoveLink(uint32 linkId);
        bool   RemoveLinkByPins(uint32 outputNodeId, uint32 outputPinId, uint32 inputNodeId, uint32 inputPinId);
        const std::vector<NodeLink>& GetLinks() const { return m_Links; }

        /// 连接校验（类型兼容 + DAG 成环检测）
        bool CanConnectPins(const Pin& outPin, const Pin& inPin, std::string& outReason) const;
        /// 替换连接（Input 已连接时自动断开旧线）
        uint32 ReplaceLink(uint32 outputNodeId, uint32 outputPinId, uint32 inputNodeId, uint32 inputPinId);

        // ── 拓扑排序（代码生成用） ──
        std::vector<uint32> TopologicalSort(std::vector<CompileError>* outErrors = nullptr) const;

        // ── 图分析 ──
        bool HasCycles() const;
        std::vector<uint32> GetNodeInputs(uint32 nodeId) const;
        std::vector<uint32> GetNodeOutputs(uint32 nodeId) const;
        std::vector<uint32> FindPath(uint32 fromNodeId, uint32 toNodeId) const; // BFS 路由

        // ── 脏标记 ──
        void MarkDirty(bool dirty = true) { m_IsDirty = dirty; }
        bool IsDirty() const { return m_IsDirty; }

        // ── 黑板系统 ──
        struct BlackboardProperty {
            std::string name;
            PinType     type = PinType::Float;
            PinValue    value;
            std::string tooltip;
        };
        std::vector<BlackboardProperty>& GetProperties() { return m_Properties; }
        const std::vector<BlackboardProperty>& GetProperties() const { return m_Properties; }
        void AddProperty(const BlackboardProperty& prop) { m_Properties.push_back(prop); MarkDirty(); }
        void RemoveProperty(int index) { if (index >= 0 && index < (int)m_Properties.size()) { m_Properties.erase(m_Properties.begin() + index); MarkDirty(); } }

        // ── 主栈 ──
        void SetMasterNodeId(uint32 id) { m_MasterNodeId = id; }
        uint32 GetMasterNodeId() const { return m_MasterNodeId; }

        // ── 序列化 ──
        std::string ToJSON() const;
        bool FromJSON(const std::string& json);
        bool SaveToFile(const std::string& path) const;
        bool LoadFromFile(const std::string& path);

        // ── 编译校验 ──
        std::vector<CompileError> Validate() const;

        // ── 子图 ──
        std::vector<ShaderGraph*>& GetGraphStack() { return m_GraphStack; }
        const std::vector<ShaderGraph*>& GetGraphStack() const { return m_GraphStack; }

        // ── 清除 ──
        void Clear();

    private:
        // DAG 成环检测（DFS）
        bool HasPathTo(uint32 fromNodeId, uint32 toNodeId, std::unordered_set<uint32>& visited) const;

        std::unordered_map<uint32, std::unique_ptr<ShaderNode>> m_Nodes;
        std::vector<NodeLink> m_Links;
        std::vector<BlackboardProperty> m_Properties;
        std::vector<ShaderGraph*> m_GraphStack;
        uint32 m_MasterNodeId = 0;
        uint32 m_NextNodeId = 1;
        uint32 m_NextLinkId = 1;
        bool m_IsDirty = false;
    };

}} // namespace Engine::ShaderGraph