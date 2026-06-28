#pragma once

/**
 * @file MeshRendererComponent.h
 * @brief 网格渲染器组件 — 挂载 3D 网格模型数据
 */

#include "Engine/Core/GameObject/Component.h"
#include "Engine/Core/RenderResources/VertexArray.h"
#include "Engine/Core/RenderResources/Shader.h"
#include <string>
#include <memory>

namespace Engine {

    // ── 1. 基础网格资产 ──
    struct Mesh {
        std::shared_ptr<VertexArray> VAO;
        uint32 IndexCount = 0;

        Mesh() = default;
        Mesh(std::shared_ptr<VertexArray> vao, uint32 count)
            : VAO(std::move(vao)), IndexCount(count) {}
    };

    // ── 2. 基础材质资产 ──
    struct Material {
        std::shared_ptr<Shader> ShaderProgram;
        float BaseColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f }; // 默认白色 RGBA

        Material() = default;
        explicit Material(std::shared_ptr<Shader> shader)
            : ShaderProgram(std::move(shader)) {}
    };

    // ── 3. 网格渲染组件 ──
    class MeshRendererComponent : public Component {
    public:
        MeshRendererComponent() = default;

        const char* GetTypeDisplayName() const override { return "Mesh Renderer"; }

        // 公有成员 — 渲染管线直接访问，无需 getter 间接跳转
        std::shared_ptr<Mesh> TargetMesh;
        std::shared_ptr<Material> TargetMaterial;

        void SetMesh(std::shared_ptr<Mesh> mesh) { TargetMesh = std::move(mesh); }
        std::shared_ptr<Mesh> GetMesh() const { return TargetMesh; }

        void SetMaterial(std::shared_ptr<Material> material) { TargetMaterial = std::move(material); }
        std::shared_ptr<Material> GetMaterial() const { return TargetMaterial; }

        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const { return m_Visible; }

        // 向后兼容 — SetMeshPath / GetMeshPath 仍保留
        void SetMeshPath(const std::string& path) { m_MeshPath = path; }
        const std::string& GetMeshPath() const { return m_MeshPath; }

        void Serialize(nlohmann::json& json) const override {
            json["type"] = "MeshRendererComponent";
            auto& data = json["data"];
            data["meshPath"] = m_MeshPath;
            data["visible"] = m_Visible;
        }

        bool Deserialize(const nlohmann::json& json) override {
            if (!json.contains("data")) return true;
            const auto& data = json["data"];
            if (data.contains("meshPath")) m_MeshPath = data["meshPath"].get<std::string>();
            if (data.contains("visible")) m_Visible = data["visible"];
            return true;
        }

    private:
        std::string m_MeshPath;
        bool m_Visible = true;
    };

} // namespace Engine