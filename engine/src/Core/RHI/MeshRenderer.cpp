#include "Engine/Core/RHI/MeshRenderer.h"
#include "Engine/Core/Renderer/Mesh.h"
#include "Engine/Core/Renderer/PerspectiveCamera.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/MeshComponent.h"
#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/RenderResources/VertexBuffer.h"
#include "Engine/Core/RenderResources/IndexBuffer.h"
#include "Engine/Core/RenderResources/VertexArray.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <numeric>
#include <cstring>

namespace Engine {

    // ════════════════════════════════════════════════
    // 构造
    // ════════════════════════════════════════════════

    MeshRenderer::MeshRenderer(IGraphicsFactory& factory, IRenderContext& context)
        : m_Factory(factory)
        , m_Context(context)
    {
    }

    // ════════════════════════════════════════════════
    // GPU 资源管理
    // ════════════════════════════════════════════════

    uint64 MeshRenderer::UploadMesh(const std::shared_ptr<Mesh>& mesh) {
        if (!mesh || !mesh->IsValid()) return 0;

        uint64 id = reinterpret_cast<uint64>(mesh.get());

        auto it = m_MeshCache.find(id);
        if (it != m_MeshCache.end())
            return id;

        const auto& vertices = mesh->GetVertices();
        const auto& indices  = mesh->GetIndices();

        // 顶点缓冲区 — 顶点在模型空间（model space / local space）中定义
        auto vb = m_Factory.CreateVertexBuffer(
            reinterpret_cast<float*>(const_cast<Vertex3D*>(vertices.data())),
            static_cast<uint32>(vertices.size() * sizeof(Vertex3D)));

        // 索引缓冲区
        auto ib = m_Factory.CreateIndexBuffer(
            const_cast<uint32*>(indices.data()),
            static_cast<uint32>(indices.size()));

        // VAO — 顶点属性布局（与 3D shader 的 layout 匹配）
        auto va = m_Factory.CreateVertexArray();

        VertexAttribute attributes[3] = {
            {0, 3, sizeof(Vertex3D), offsetof(Vertex3D, position)},
            {1, 3, sizeof(Vertex3D), offsetof(Vertex3D, normal)},
            {2, 2, sizeof(Vertex3D), offsetof(Vertex3D, texCoord)},
        };
        va->AddVertexBuffer(vb, attributes, 3);
        va->SetIndexBuffer(ib);

        CachedMeshData cached;
        cached.vao        = std::move(va);
        cached.vbo        = std::move(vb);
        cached.ibo        = std::move(ib);
        cached.indexCount = static_cast<uint32>(indices.size());
        m_MeshCache[id]   = std::move(cached);

        return id;
    }

    // ════════════════════════════════════════════════
    // 坐标轴 / 网格辅助
    // ════════════════════════════════════════════════

    MeshRenderer::AxisMesh MeshRenderer::GenerateAxes(float length) {
        AxisMesh mesh;

        // 3 条线段: 从原点沿 X/Y/Z 正方向和负方向延伸
        //   X = 红色, Y = 绿色, Z = 蓝色
        float h = length;

        // X 轴（红）
        mesh.vertices.push_back({{-h, 0, 0}, {1, 0, 0}});
        mesh.vertices.push_back({{ h, 0, 0}, {1, 0, 0}});
        // Y 轴（绿）
        mesh.vertices.push_back({{0, -h, 0}, {0, 1, 0}});
        mesh.vertices.push_back({{0,  h, 0}, {0, 1, 0}});
        // Z 轴（蓝）
        mesh.vertices.push_back({{0, 0, -h}, {0, 0, 1}});
        mesh.vertices.push_back({{0, 0,  h}, {0, 0, 1}});

        mesh.indices = {0, 1, 2, 3, 4, 5};
        return mesh;
    }

    MeshRenderer::AxisMesh MeshRenderer::GenerateGrid(float size, int32 steps) {
        AxisMesh mesh;
        float half = size / 2.0f;
        float step = size / (float)steps;

        // 平行于 X 轴的线
        for (int32 i = 0; i <= steps; ++i) {
            float z = -half + i * step;
            Vec3 color = (i == steps / 2) ? Vec3(0.3f, 0.3f, 0.3f) : Vec3(0.2f, 0.2f, 0.2f);
            mesh.vertices.push_back({{-half, 0, z}, color});
            mesh.vertices.push_back({{ half, 0, z}, color});
            mesh.indices.push_back((uint32)mesh.vertices.size() - 2);
            mesh.indices.push_back((uint32)mesh.vertices.size() - 1);
        }
        // 平行于 Z 轴的线
        for (int32 i = 0; i <= steps; ++i) {
            float x = -half + i * step;
            Vec3 color = (i == steps / 2) ? Vec3(0.3f, 0.3f, 0.3f) : Vec3(0.2f, 0.2f, 0.2f);
            mesh.vertices.push_back({{x, 0, -half}, color});
            mesh.vertices.push_back({{x, 0,  half}, color});
            mesh.indices.push_back((uint32)mesh.vertices.size() - 2);
            mesh.indices.push_back((uint32)mesh.vertices.size() - 1);
        }

        return mesh;
    }

    // ════════════════════════════════════════════════
    // 主渲染入口
    // ════════════════════════════════════════════════

    void MeshRenderer::Render(const std::vector<GameObject*>& objects) {
        if (!m_Shader || !m_Camera)
            return;

        m_Shader->Bind();

        // ═══════════════════════════════════════════════════
        // 坐标空间变换链（每个坐标空间的含义）
        //
        //   模型空间 (Model Space / Local Space / Object Space)
        //     ─ 顶点在 Mesh 中定义的原始坐标
        //     ─ 由 u_Model 变换到世界空间
        //     │
        //     ▼
        //   世界空间 (World Space)
        //     ─ 所有物体共享的全局坐标空间
        //     ─ 由 u_View 变换到观察空间
        //     │
        //     ▼
        //   观察空间 (View Space / Camera Space / Eye Space)
        //     ─ 以相机为原点的坐标空间
        //     ─ 由 u_Projection 变换到裁剪空间
        //     │
        //     ▼
        //   裁剪空间 (Clip Space)
        //     ─ 齐次坐标，经过透视除法后变为 NDC (-1 ~ 1)
        // ═══════════════════════════════════════════════════

        // 获取相机的三个矩阵
        const Mat4& projMatrix = m_Camera->GetProjectionMatrix();
        const Mat4& viewMatrix = m_Camera->GetViewMatrix();
        const float* vpPtr     = m_Camera->GetViewProjectionMatrixPtr();

        // 设置观察空间 uniform
        m_Shader->SetMat4("u_View",       viewMatrix.Data());
        m_Shader->SetMat4("u_Projection", projMatrix.Data());

        // 光源 / 环境光 uniform
        Vec3 viewPos = m_Camera->GetPosition();
        m_Shader->SetVec3("u_LightPos",       &m_LightPos.x);
        m_Shader->SetVec3("u_LightColor",     &m_LightColor.x);
        m_Shader->SetFloat("u_LightIntensity", m_LightIntensity);
        m_Shader->SetVec3("u_ViewPos",        &viewPos.x);
        m_Shader->SetVec3("u_AmbientColor",   &m_AmbientColor.x);

        // ── 遍历所有 GameObject 并渲染带 MeshComponent 的 ──
        for (auto* obj : objects) {
            if (!obj || !obj->IsActive()) continue;

            auto* meshComp = obj->GetComponent<MeshComponent>();
            if (!meshComp || !meshComp->m_Visible || !meshComp->HasMesh())
                continue;

            auto mesh = meshComp->GetMesh();
            uint64 gpuId = reinterpret_cast<uint64>(mesh.get());

            // 获取或创建 GPU 资源
            auto it = m_MeshCache.find(gpuId);
            if (it == m_MeshCache.end()) {
                gpuId = UploadMesh(mesh);
                if (gpuId == 0) continue;
                it = m_MeshCache.find(gpuId);
                if (it == m_MeshCache.end()) continue;
            }

            const auto& cached = it->second;

            // ═══════════════════════════════════════════════
            // Model 矩阵:  模型空间 → 世界空间
            //   - 从 TransformComponent 的 GetWorldMatrix() 获取
            //   - 已经包含了父级变换链
            // ═══════════════════════════════════════════════
            const Mat4& worldMatrix   = obj->GetTransform().GetWorldMatrix();

            // MVP 矩阵: 模型空间 → 裁剪空间（快捷组合）
            //   MVP = Projection * View * Model
            glm::mat4 modelGlm;
            std::memcpy(&modelGlm, worldMatrix.data, sizeof(float) * 16);
            glm::mat4 viewGlm;
            std::memcpy(&viewGlm, viewMatrix.data, sizeof(float) * 16);
            glm::mat4 projGlm;
            std::memcpy(&projGlm, projMatrix.data, sizeof(float) * 16);

            glm::mat4 mvp = projGlm * viewGlm * modelGlm;
            float mvpData[16];
            std::memcpy(mvpData, &mvp, sizeof(float) * 16);

            m_Shader->SetMat4("u_Model", worldMatrix.Data());
            m_Shader->SetMat4("u_MVP",   mvpData);

            // ═══════════════════════════════════════════════
            // Normal 矩阵: 用于将法线从模型空间变换到世界空间
            //   公式: transpose(inverse(u_Model))
            //   因为法线是方向向量，不能用 Model 直接变换（会受非均匀缩放影响）
            // ═══════════════════════════════════════════════
            glm::mat3 normalMat3 = glm::transpose(glm::inverse(glm::mat3(modelGlm)));
            glm::mat4 normalMat4(normalMat3);
            m_Shader->SetMat4("u_NormalMatrix", glm::value_ptr(normalMat4));

            // ── 材质属性 ──
            m_Shader->SetVec4("u_ObjectColor", &meshComp->m_Color.x);
            m_Shader->SetInt("u_HasTexture", 0);

            // ── 绘制 ──
            cached.vao->Bind();
            m_Context.DrawIndexed(cached.vao);
        }

        m_Shader->Unbind();
    }

} // namespace Engine
