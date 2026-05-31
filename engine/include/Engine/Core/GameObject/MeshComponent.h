#pragma once

#include "Engine/Core/GameObject/Component.h"
#include "Engine/Core/Renderer/Mesh.h"
#include "Engine/Core/RHI/MathTypes.h"

namespace Engine {

    /**
     * @brief 网格组件 — 挂载 3D 网格到 GameObject 上
     *
     * 配合 MeshRenderer 使用。每个 MeshComponent 可以携带一个 Mesh
     * 引用和简单的材质属性。
     */
    class MeshComponent : public Component {
    public:
        MeshComponent() = default;
        explicit MeshComponent(std::shared_ptr<Mesh> mesh)
            : m_Mesh(std::move(mesh)) {}

        // ── 网格 ──
        void SetMesh(std::shared_ptr<Mesh> mesh) { m_Mesh = std::move(mesh); }
        std::shared_ptr<Mesh> GetMesh() const { return m_Mesh; }
        bool HasMesh() const { return m_Mesh && m_Mesh->IsValid(); }

        // ── 材质属性（简化版，未来可拆为 Material 类） ──
        Vec4 m_Color      = {1.0f, 1.0f, 1.0f, 1.0f};
        bool m_Visible    = true;

        // 将来可以扩展：
        // std::shared_ptr<Texture> m_DiffuseTexture;
        // std::shared_ptr<Texture> m_NormalMap;
        // float m_Metallic = 0.0f;
        // float m_Roughness = 0.5f;

    private:
        std::shared_ptr<Mesh> m_Mesh;
    };

} // namespace Engine
