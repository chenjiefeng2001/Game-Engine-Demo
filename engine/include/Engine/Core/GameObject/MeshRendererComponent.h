#pragma once

/**
 * @file MeshRendererComponent.h
 * @brief 网格渲染器组件 — 挂载 3D 网格模型数据
 */

#include "Engine/Core/GameObject/Component.h"
#include <string>
#include <memory>

namespace Engine {

    /** 简单网格数据句柄（替代完整的 Mesh 类，避免依赖缺失） */
    struct MeshHandle {
        std::string path;
        uint32 vertexCount = 0;
        uint32 indexCount = 0;
        uint32 vaoID = 0;
    };

    class MeshRendererComponent : public Component {
    public:
        MeshRendererComponent() = default;

        const char* GetTypeDisplayName() const override { return "Mesh Renderer"; }

        void SetMeshPath(const std::string& path) { m_MeshPath = path; }
        const std::string& GetMeshPath() const { return m_MeshPath; }

        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const { return m_Visible; }

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