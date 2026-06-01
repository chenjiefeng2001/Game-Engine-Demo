#include "Engine/Core/RHI/MeshRenderer.h"
#include "Engine/Core/RHI/PotentiallyVisibleSet.h"
#include "Engine/Core/RHI/IPrimitiveBatch.h"
#include "Engine/Core/RHI/ISceneGraph.h"
#include "Engine/Core/RHI/ShadowMapper.h"
#include "Engine/Core/RHI/GBuffer.h"
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

    MeshRenderer::~MeshRenderer()
    {
        // unique_ptr<IPrimitiveBatch> 在此析构（完整类型已可见）
    }

    MeshRenderer::MeshRenderer(IGraphicsFactory& factory, IRenderContext& context)
        : m_Factory(factory)
        , m_Context(context)
    {
        m_Batch = m_Factory.CreatePrimitiveBatch(32768);
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

        VertexAttribute attributes[4] = {
            {0, 3, sizeof(Vertex3D), offsetof(Vertex3D, position)},
            {1, 3, sizeof(Vertex3D), offsetof(Vertex3D, normal)},
            {2, 2, sizeof(Vertex3D), offsetof(Vertex3D, texCoord)},
            {3, 3, sizeof(Vertex3D), offsetof(Vertex3D, tangent)},
        };
        va->AddVertexBuffer(vb, attributes, 4);
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
    // 批处理渲染 — 所有可见网格合并到单个 DrawCall
    // ════════════════════════════════════════════════

    void MeshRenderer::RenderBatched(const std::vector<GameObject*>& objects,
                                      IPrimitiveBatch& batch)
    {
        if (!m_Shader || !m_Camera)
            return;

        m_Shader->Bind();

        // 设置相机 uniform
        const Mat4& projMatrix = m_Camera->GetProjectionMatrix();
        const Mat4& viewMatrix = m_Camera->GetViewMatrix();
        const float* vpPtr     = m_Camera->GetViewProjectionMatrixPtr();

        m_Shader->SetMat4("u_View",       viewMatrix.Data());
        m_Shader->SetMat4("u_Projection", projMatrix.Data());

        Vec3 viewPos = m_Camera->GetPosition();
        m_Shader->SetInt("u_LightCount", (int)m_Lights.size());
        for (size_t li = 0; li < m_Lights.size() && li < 4; ++li) {
            auto& L = m_Lights[li];
            std::string si = std::to_string(li);
            m_Shader->SetVec3(("u_LightPos["+si+"]").c_str(), &L.position.x);
            m_Shader->SetVec3(("u_LightColor["+si+"]").c_str(), &L.color.x);
            m_Shader->SetFloat(("u_LightIntensity["+si+"]").c_str(), L.intensity);
        }
        m_Shader->SetVec3("u_ViewPos",        &viewPos.x);
        m_Shader->SetVec3("u_AmbientColor",   &m_AmbientColor.x);

        // 收集所有网格到批处理器
        batch.Begin(PrimitiveType::Triangles);

        for (auto* obj : objects) {
            if (!obj || !obj->IsActive()) continue;

            auto* meshComp = obj->GetComponent<MeshComponent>();
            if (!meshComp || !meshComp->m_Visible || !meshComp->HasMesh())
                continue;

            auto mesh = meshComp->GetMesh();
            auto& verts = mesh->GetVertices();
            auto& indices = mesh->GetIndices();

            if (verts.empty() || indices.empty())
                continue;

            // 获取物体世界矩阵
            const Mat4& worldMatrix = obj->GetTransform().GetWorldMatrix();
            m_Shader->SetMat4("u_Model", worldMatrix.Data());

            // 计算 MVP 和法线矩阵
            glm::mat4 model = glm::make_mat4(worldMatrix.Data());
            glm::mat4 view  = glm::make_mat4(viewMatrix.Data());
            glm::mat4 proj  = glm::make_mat4(projMatrix.Data());
            glm::mat4 mvp   = proj * view * model;

            // 法线矩阵 = transpose(inverse(model))
            glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(model)));
            glm::mat4 normalMat4(normalMat);

            m_Shader->SetMat4("u_MVP",          glm::value_ptr(mvp));
            m_Shader->SetMat4("u_NormalMatrix",  glm::value_ptr(normalMat4));

            // 设置物体颜色
            Vec4 objColor = meshComp->m_Color;
            m_Shader->SetVec4("u_ObjectColor", &objColor.x);
            m_Shader->SetInt("u_HasTexture", 0);

            // 将网格顶点/索引添加到批处理器
            uint32 baseIndex = batch.GetVertexCount();
            for (auto& v : verts) {
                batch.Vertex(
                    Vec3(v.position.x, v.position.y, v.position.z),
                    Vec3(v.normal.x,   v.normal.y,   v.normal.z),
                    Vec2(v.texCoord.x, v.texCoord.y),
                    Vec4(objColor.x, objColor.y, objColor.z, 1.0f)
                );
            }
            for (uint32 idx : indices) {
                batch.Index(baseIndex + idx);
            }
        }

        // 一次性提交所有图元
        batch.Commit();
        batch.End();
    }

    // ════════════════════════════════════════════════
    // 全屏四边形（延迟渲染光照通道）
    // ════════════════════════════════════════════════

    void MeshRenderer::InitFullscreenQuad() {
        // 延迟初始化：通过 CreatePrimitiveBatch 获取批处理器
        if (m_Batch) return;
    }

    void MeshRenderer::RenderFullscreenQuad() {
        // 使用批处理器绘制全屏四边形
        if (!m_Batch) return;
        m_Batch->Begin(PrimitiveType::Triangles);
        m_Batch->Vertex(Vec3(-1, 1, 0), Vec4(1,1,1,1));
        m_Batch->Vertex(Vec3(-1,-1, 0), Vec4(1,1,1,1));
        m_Batch->Vertex(Vec3( 1,-1, 0), Vec4(1,1,1,1));
        m_Batch->Vertex(Vec3(-1, 1, 0), Vec4(1,1,1,1));
        m_Batch->Vertex(Vec3( 1,-1, 0), Vec4(1,1,1,1));
        m_Batch->Vertex(Vec3( 1, 1, 0), Vec4(1,1,1,1));
        m_Batch->Index(0);m_Batch->Index(1);m_Batch->Index(2);
        m_Batch->Index(3);m_Batch->Index(4);m_Batch->Index(5);
        m_Batch->Commit();
        m_Batch->End();
    }

    // ════════════════════════════════════════════════
    // 延迟渲染 — 几何通道 + 光照通道
    // ════════════════════════════════════════════════

    void MeshRenderer::RenderDeferred(const std::vector<GameObject*>& objects,
                                       std::shared_ptr<Shader> lightShader)
    {
        if (!m_GBuffer || !m_GBuffer->IsValid() || !lightShader) {
            Render(objects);  // 退化为前向渲染
            return;
        }

        // ── Pass 1: 几何通道 — 写入 G-Buffer ──
        m_GBuffer->BindForGeometryPass();
        m_GBuffer->Clear();

        auto geomShader = m_GeomShader ? m_GeomShader : m_Shader;
        geomShader->Bind();

        const Mat4& projMatrix = m_Camera->GetProjectionMatrix();
        const Mat4& viewMatrix = m_Camera->GetViewMatrix();
        glm::mat4 viewGlm = glm::make_mat4(viewMatrix.Data());
        glm::mat4 projGlm = glm::make_mat4(projMatrix.Data());

        for (auto* obj : objects) {
            if (!obj || !obj->IsActive()) continue;
            auto* mc = obj->GetComponent<MeshComponent>();
            if (!mc || !mc->m_Visible || !mc->HasMesh()) continue;

            auto mesh = mc->GetMesh();
            uint64 id = reinterpret_cast<uint64>(mesh.get());
            auto it = m_MeshCache.find(id);
            if (it == m_MeshCache.end()) {
                id = UploadMesh(mesh);
                if (id == 0) continue;
                it = m_MeshCache.find(id);
                if (it == m_MeshCache.end()) continue;
            }

            const auto& cached = it->second;
            glm::mat4 model = glm::make_mat4(obj->GetTransform().GetWorldMatrix().Data());
            glm::mat4 mvp = projGlm * viewGlm * model;
            glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(model)));

            geomShader->SetMat4("u_Model", obj->GetTransform().GetWorldMatrix().Data());
            geomShader->SetMat4("u_View", viewMatrix.Data());
            geomShader->SetMat4("u_Projection", projMatrix.Data());
            geomShader->SetMat4("u_MVP", glm::value_ptr(mvp));
            geomShader->SetMat4("u_NormalMatrix", glm::value_ptr(glm::mat4(nm)));
            geomShader->SetVec4("u_ObjectColor", &mc->m_Color.x);
            geomShader->SetInt("u_HasTexture", 0);
            geomShader->SetFloat("u_Shininess", mc->m_Shininess);
            geomShader->SetFloat("u_SpecularStrength", mc->m_SpecularStrength);

            // 法线贴图
            if (mc->m_NormalMap) {
                geomShader->SetInt("u_HasNormalMap", 1);
                geomShader->SetFloat("u_NormalStrength", mc->m_NormalStrength);
                mc->m_NormalMap->Bind(1);
                geomShader->SetInt("u_NormalMap", 1);
            } else {
                geomShader->SetInt("u_HasNormalMap", 0);
            }

            cached.vao->Bind();
            m_Context.DrawIndexed(cached.vao);
        }

        // ── Pass 2: 光照通道 — 全屏四边形 ──
        m_GBuffer->Unbind();
        m_Context.ClearColor(0,0,0,1);  // clears color+depth

        lightShader->Bind();
        lightShader->SetInt("u_PositionMap", 0);
        lightShader->SetInt("u_NormalMap", 1);
        lightShader->SetInt("u_AlbedoMap", 2);
        lightShader->SetInt("u_AOMap", 3);
        lightShader->SetInt("u_DepthMap", 4);

        Vec3 viewPos = m_Camera->GetPosition();
        lightShader->SetVec3("u_ViewPos", &viewPos.x);
        lightShader->SetVec3("u_AmbientColor", &m_AmbientColor.x);
        lightShader->SetMat4("u_View", viewMatrix.Data());

        // 光源
        lightShader->SetInt("u_LightCount", (int)m_Lights.size());
        for (size_t li = 0; li < m_Lights.size() && li < 4; ++li) {
            auto& L = m_Lights[li];
            std::string si = std::to_string(li);
            lightShader->SetVec3(("u_LightPos["+si+"]").c_str(), &L.position.x);
            lightShader->SetVec3(("u_LightColor["+si+"]").c_str(), &L.color.x);
            lightShader->SetFloat(("u_LightIntensity["+si+"]").c_str(), L.intensity);
        }

        // 阴影
        if (m_ShadowEnabled && m_ShadowMapper && m_ShadowMapper->IsValid()) {
            m_ShadowMapper->SetShaderUniforms(lightShader.get());
            m_ShadowMapper->BindShadowMap(5);
            lightShader->SetInt("u_ShadowMap", 5);
        } else {
            lightShader->SetInt("u_ShadowEnabled", 0);
        }

        m_GBuffer->BindTexturesForLighting();

        // 全屏四边形
        InitFullscreenQuad();  // 复用全屏 VAO
        RenderFullscreenQuad();
    }

    // ════════════════════════════════════════════════
    // 阴影渲染 — 从光源视角渲染深度图
    // ════════════════════════════════════════════════

    void MeshRenderer::RenderShadowPass(const std::vector<GameObject*>& objects) {
        if (!m_ShadowMapper || !m_ShadowMapper->IsValid() || !m_DepthShader) {
            return;
        }

        m_ShadowMapper->BindForShadowPass();
        m_DepthShader->Bind();

        for (auto* obj : objects) {
            if (!obj || !obj->IsActive()) continue;
            auto* mc = obj->GetComponent<MeshComponent>();
            if (!mc || !mc->m_Visible || !mc->HasMesh()) continue;

            auto mesh = mc->GetMesh();
            uint64 id = reinterpret_cast<uint64>(mesh.get());
            auto it = m_MeshCache.find(id);
            if (it == m_MeshCache.end()) {
                id = UploadMesh(mesh);
                if (id == 0) continue;
                it = m_MeshCache.find(id);
                if (it == m_MeshCache.end()) continue;
            }

            const auto& cached = it->second;
            glm::mat4 model = glm::make_mat4(obj->GetTransform().GetWorldMatrix().Data());
            glm::mat4 mvp   = glm::make_mat4(m_ShadowMapper->GetLightVP().Data()) * model;
            m_DepthShader->SetMat4("u_MVP", glm::value_ptr(mvp));
            cached.vao->Bind();
            m_Context.DrawIndexed(cached.vao);
        }

        m_Shader->Bind(); // 绑定主 shader 以便继续主渲染
        m_ShadowMapper->EndShadowPass();
    }

    // ════════════════════════════════════════════════
    // 深度预渲染 + 主渲染（两遍法减少 overdraw）
    // ════════════════════════════════════════════════

    void MeshRenderer::RenderWithDepthPrePass(const std::vector<GameObject*>& objects)
    {
        if (!m_DepthPrePassEnabled || !m_DepthShader || !m_Camera) {
            Render(objects);
            return;
        }

        const Mat4& projMatrix = m_Camera->GetProjectionMatrix();
        const Mat4& viewMatrix = m_Camera->GetViewMatrix();
        glm::mat4 viewGlm = glm::make_mat4(viewMatrix.Data());
        glm::mat4 projGlm = glm::make_mat4(projMatrix.Data());

        // ── 确保所有网格的 GPU 资源已创建 ──
        for (auto* obj : objects) {
            if (!obj || !obj->IsActive()) continue;
            auto* meshComp = obj->GetComponent<MeshComponent>();
            if (!meshComp || !meshComp->m_Visible || !meshComp->HasMesh()) continue;
            auto mesh = meshComp->GetMesh();
            uint64 gpuId = reinterpret_cast<uint64>(mesh.get());
            auto it = m_MeshCache.find(gpuId);
            if (it == m_MeshCache.end()) {
                UploadMesh(mesh);
            }
        }

        // ── Pass 1: 深度只写 ──
        m_Context.SetDepthMask(true);
        m_Context.SetColorMask(false, false, false, false);

        m_DepthShader->Bind();

        for (auto* obj : objects) {
            if (!obj || !obj->IsActive()) continue;
            auto* meshComp = obj->GetComponent<MeshComponent>();
            if (!meshComp || !meshComp->m_Visible || !meshComp->HasMesh()) continue;

            auto mesh = meshComp->GetMesh();
            uint64 gpuId = reinterpret_cast<uint64>(mesh.get());
            auto it = m_MeshCache.find(gpuId);
            if (it == m_MeshCache.end()) continue;

            const auto& cached = it->second;
            glm::mat4 modelGlm = glm::make_mat4(obj->GetTransform().GetWorldMatrix().Data());
            glm::mat4 mvp = projGlm * viewGlm * modelGlm;
            m_DepthShader->SetMat4("u_MVP", glm::value_ptr(mvp));
            cached.vao->Bind();
            m_Context.DrawIndexed(cached.vao);
        }

        // ── Pass 2: 主渲染（颜色）with GL_EQUAL ──
        m_Context.SetColorMask(true, true, true, true);
        m_Context.SetDepthFunc(0x0202);  // GL_EQUAL
        m_Context.SetDepthMask(false);

        m_Shader->Bind();
        m_Shader->SetMat4("u_View",       viewMatrix.Data());
        m_Shader->SetMat4("u_Projection", projMatrix.Data());

        Vec3 viewPos = m_Camera->GetPosition();
        m_Shader->SetInt("u_LightCount", (int)m_Lights.size());
        for (size_t li = 0; li < m_Lights.size() && li < 4; ++li) {
            auto& L = m_Lights[li];
            std::string si = std::to_string(li);
            m_Shader->SetVec3(("u_LightPos["+si+"]").c_str(), &L.position.x);
            m_Shader->SetVec3(("u_LightColor["+si+"]").c_str(), &L.color.x);
            m_Shader->SetFloat(("u_LightIntensity["+si+"]").c_str(), L.intensity);
        }
        m_Shader->SetVec3("u_ViewPos",        &viewPos.x);
        m_Shader->SetVec3("u_AmbientColor",   &m_AmbientColor.x);

        for (auto* obj : objects) {
            if (!obj || !obj->IsActive()) continue;
            auto* meshComp = obj->GetComponent<MeshComponent>();
            if (!meshComp || !meshComp->m_Visible || !meshComp->HasMesh()) continue;

            auto mesh = meshComp->GetMesh();
            uint64 gpuId = reinterpret_cast<uint64>(mesh.get());
            auto it = m_MeshCache.find(gpuId);
            if (it == m_MeshCache.end()) continue;

            const auto& cached = it->second;
            glm::mat4 modelGlm = glm::make_mat4(obj->GetTransform().GetWorldMatrix().Data());
            glm::mat4 mvp = projGlm * viewGlm * modelGlm;
            glm::mat3 normalMat3 = glm::transpose(glm::inverse(glm::mat3(modelGlm)));
            glm::mat4 normalMat4(normalMat3);

            m_Shader->SetMat4("u_Model", obj->GetTransform().GetWorldMatrix().Data());
            m_Shader->SetMat4("u_MVP", glm::value_ptr(mvp));
            m_Shader->SetMat4("u_NormalMatrix", glm::value_ptr(normalMat4));
            m_Shader->SetVec4("u_ObjectColor", &meshComp->m_Color.x);
            m_Shader->SetInt("u_HasTexture", 0);

            cached.vao->Bind();
            m_Context.DrawIndexed(cached.vao);
        }

        // ── 恢复默认深度状态 ──
        m_Context.SetDepthFunc(0x0201);  // GL_LESS
        m_Context.SetDepthMask(true);
    }

    // ════════════════════════════════════════════════
    // 场景图渲染 — 平截头体剔除 + 自动切换
    // ════════════════════════════════════════════════

    void MeshRenderer::RenderWithSceneGraph(const std::vector<GameObject*>& objects,
                                             const Frustum* frustum,
                                             bool forceDepthPrePass)
    {
        m_LastTotalObjects = (uint32)objects.size();

        bool useSG = m_SceneGraph && m_SceneGraph->IsValid() &&
                     objects.size() >= m_SGThreshold;

        if (!useSG) {
            if ((m_DepthPrePassEnabled || forceDepthPrePass) && m_DepthShader) {
                RenderWithDepthPrePass(objects);
            } else {
                Render(objects);
            }
            m_LastVisibleObjects = m_LastTotalObjects;
            return;
        }

        // 更新平截头体
        if (frustum) {
            const_cast<ISceneGraph*>(m_SceneGraph)->SetFrustum(*frustum);
        }

        // 获取可见物体
        thread_local std::vector<GameObject*> visibleObjects;
        visibleObjects.clear();
        m_SceneGraph->GetVisibleObjects(visibleObjects);
        m_LastVisibleObjects = (uint32)visibleObjects.size();

        bool useDepth = m_DepthPrePassEnabled || forceDepthPrePass;
        if (useDepth && m_DepthShader) {
            RenderWithDepthPrePass(visibleObjects);
        } else {
            Render(visibleObjects);
        }
    }

    // ════════════════════════════════════════════════
    // PVS 加速渲染 — 先剔除不可见物体，再调用 Render()
    // ════════════════════════════════════════════════

    void MeshRenderer::RenderWithPVS(const std::vector<GameObject*>& objects,
                                      const Vec3& cameraPos)
    {
        if (!m_PVS || !m_PVS->IsValid()) {
            // 没有 PVS，退化为普通渲染
            Render(objects);
            return;
        }

        // 使用 PVS 获取可见物体
        thread_local std::vector<GameObject*> visibleObjects;
        visibleObjects.clear();
        m_PVS->GetVisibleObjects(cameraPos, visibleObjects);

        // 渲染可见物体
        if (m_DepthPrePassEnabled && m_DepthShader) {
            RenderWithDepthPrePass(visibleObjects);
        } else {
            Render(visibleObjects);
        }
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
        m_Shader->SetInt("u_LightCount", (int)m_Lights.size());
        for (size_t li = 0; li < m_Lights.size() && li < 4; ++li) {
            auto& L = m_Lights[li];
            std::string si = std::to_string(li);
            m_Shader->SetVec3(("u_LightPos["+si+"]").c_str(), &L.position.x);
            m_Shader->SetVec3(("u_LightColor["+si+"]").c_str(), &L.color.x);
            m_Shader->SetFloat(("u_LightIntensity["+si+"]").c_str(), L.intensity);
        }
        m_Shader->SetVec3("u_ViewPos",        &viewPos.x);
        m_Shader->SetVec3("u_AmbientColor",   &m_AmbientColor.x);

        // ── 阴影 uniform ──
        if (m_ShadowEnabled && m_ShadowMapper && m_ShadowMapper->IsValid()) {
            m_ShadowMapper->BindShadowMap(3);
            m_Shader->SetMat4("u_LightSpaceMatrix", m_ShadowMapper->GetLightVP().Data());
            m_Shader->SetFloat("u_ShadowBias", 0.005f);
            m_Shader->SetInt("u_ShadowEnabled", 1);
        } else {
            m_Shader->SetInt("u_ShadowEnabled", 0);
        }

        // ── SSAO uniform ──
        if (m_SSAOEnabled && m_SSAOTex) {
            m_Shader->SetInt("u_SSAOMap", 4);
            m_Shader->SetInt("u_SSAOEnabled", 1);
            m_Shader->SetFloat("u_SSAOStrength", m_SSAOStrength);
        } else {
            m_Shader->SetInt("u_SSAOEnabled", 0);
        }

        // ── 焦散 uniform ──
        if (m_CausticsEnabled) {
            m_Shader->SetInt("u_CausticsEnabled", 1);
            m_Shader->SetFloat("u_CausticStrength", m_CausticStrength);
        } else {
            m_Shader->SetInt("u_CausticsEnabled", 0);
        }

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
            m_Shader->SetFloat("u_Shininess", meshComp->m_Shininess);
            m_Shader->SetFloat("u_SpecularStrength", meshComp->m_SpecularStrength);

            // ── 法线贴图 ──
            if (meshComp->m_NormalMap) {
                m_Shader->SetInt("u_HasNormalMap", 1);
                m_Shader->SetFloat("u_NormalStrength", meshComp->m_NormalStrength);
                meshComp->m_NormalMap->Bind(1);
                m_Shader->SetInt("u_NormalMap", 1);
            } else {
                m_Shader->SetInt("u_HasNormalMap", 0);
            }

            // ── 高度贴图（视差/位移） ──
            if (meshComp->m_HeightMap) {
                m_Shader->SetInt("u_HasHeightMap", 1);
                m_Shader->SetFloat("u_ParallaxScale", meshComp->m_ParallaxScale);
                m_Shader->SetFloat("u_DisplacementStrength", meshComp->m_DisplacementStrength);
                meshComp->m_HeightMap->Bind(2);
                m_Shader->SetInt("u_HeightMap", 2);
            } else {
                m_Shader->SetInt("u_HasHeightMap", 0);
                m_Shader->SetFloat("u_DisplacementStrength", 0.0f);
            }

            // ── 动态多光源 ──
            m_Shader->SetInt("u_LightCount", (int)m_Lights.size());
            for (size_t li = 0; li < m_Lights.size() && li < 4; ++li) {
                auto& L = m_Lights[li];
                std::string si = std::to_string(li);
                m_Shader->SetVec3(("u_LightPos["+si+"]").c_str(), &L.position.x);
                m_Shader->SetVec3(("u_LightColor["+si+"]").c_str(), &L.color.x);
                m_Shader->SetFloat(("u_LightIntensity["+si+"]").c_str(), L.intensity);
            }

            // ── 绘制 ──
            cached.vao->Bind();
            m_Context.DrawIndexed(cached.vao);
        }

        m_Shader->Unbind();
    }

} // namespace Engine
