#pragma once

/**
 * @file EditorDemoApp.h
 * @brief 引擎编辑器演示 — 完整的 3D 编辑测试场景
 *
 * 使用 simple.vert/simple.frag（Lambert 光照）渲染场景。
 * 顶点格式：{x,y,z, nx,ny,nz} interleaved，通过属性描述符传递。
 */

#include <Engine/Application.h>
#include <Engine/ConsolePanel.h>
#include <Engine/Editor/EngineEditor.h>
#include <Engine/Editor/ViewportPanel.h>
#include <Engine/InspectorPanel.h>
#include <Engine/SceneHierarchyPanel.h>
#include <Engine/Core/Scene/Scene.h>
#include <Engine/Core/GameObject/GameObject.h>
#include <Engine/Core/GameObject/SpriteComponent.h>
#include <Engine/Core/GameObject/MeshRendererComponent.h>
#include <Engine/Core/GameObject/LightComponent.h>
#include <Engine/Core/RenderResources/Shader.h>
#include <Engine/Core/RenderResources/VertexArray.h>
#include <Engine/Core/RenderResources/VertexBuffer.h>
#include <Engine/Core/RenderResources/IndexBuffer.h>
#include <Engine/Core/EventBus.h>
#include <Engine/OpenGL/OpenGLContext.h>
#include <iostream>
#include <memory>
#include <vector>

#include "EditorDemoTest.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Engine {

class EditorDemoApp : public Application {
public:
    EditorDemoApp(IGraphicsFactory &factory) : Application(factory), m_Test(this) {
        m_UseEngineEditorDockspace = true;
        m_DrawPerformanceWindow = false;
        m_RenderDefaultQuad = false;
    }

    ~EditorDemoApp() override {
        std::cout << "=== EditorDemo Shutdown ===" << std::endl;
    }

protected:
    void OnStartup() override {
        // ── 1. 编辑器框架 ──
        m_Editor.Init(this);
        m_Editor.RegisterSceneHierarchy(&m_HierarchyPanel);
        m_Editor.RegisterInspector(&m_InspectorPanel);
        m_Editor.RegisterConsole(&m_ConsolePanel);
        m_Editor.RegisterPerformance(&m_PerfWindow);

        // ── 2. 默认资产（simple.vert + simple.frag，Lambert 光照） ──
        InitializeRenderingResources();

        // ── 3. 场景 ──
        BuildTestScene();

        // ── 4. 桥接 ──
        m_Editor.GetSceneManager().SetEditorScene(m_Scene.get());
        m_HierarchyPanel.SetScene(m_Scene.get());

        // ── 5. DND 回调 ──
        m_Editor.GetViewport().SetDropAssetCallback([this](const std::string& assetPath, const float* dropPos3) {
            auto newObj = std::make_shared<GameObject>("New Model");
            newObj->GetTransform().SetPosition(dropPos3[0], dropPos3[1], dropPos3[2]);
            newObj->GetTransform().SetScale(0.5f);
            auto* mr = newObj->AddComponent<MeshRendererComponent>();
            mr->TargetMesh = m_CubeMesh;
            mr->TargetMaterial = m_DefaultMaterial;
            m_Scene->AddObject(newObj);
            m_Editor.OnSelectionChanged(newObj.get());
            Engine::Log::Info("[EditorDemo] DND at ({:.1f}, {:.1f}, {:.1f})",
                              dropPos3[0], dropPos3[1], dropPos3[2]);
        });

        // ── 6. 右键创建回调 ──
        m_Editor.GetViewport().SetSceneCreateCallback([this](const std::string& type) {
            auto& cam = m_Editor.GetViewport().GetCamera();
            Vec3 fwd = cam.GetFocusPoint() - cam.GetPosition();
            float len = std::sqrt(fwd.x*fwd.x + fwd.y*fwd.y + fwd.z*fwd.z);
            if (len > 0.001f) { fwd.x /= len; fwd.y /= len; fwd.z /= len; }
            Vec3 spawnPos = cam.GetPosition() + fwd * 3.0f;

            auto cube = std::make_shared<GameObject>("New Cube");
            cube->GetTransform().SetPosition(spawnPos.x, spawnPos.y, spawnPos.z);
            cube->GetTransform().SetScale(0.5f);
            auto* mr = cube->AddComponent<MeshRendererComponent>();
            mr->TargetMesh = m_CubeMesh;
            mr->TargetMaterial = m_DefaultMaterial;
            m_Scene->AddObject(cube);
            m_Editor.OnSelectionChanged(cube.get());
        });

        // ── 7. SceneViewerPanel ──
        m_Editor.GetSceneViewerPanel().SetEditorScene(m_Scene.get());
        m_Editor.GetSceneViewerPanel().SetFocusRequestCallback(
            [this](const Vec3& center, float radius) {
                (void)radius;
                m_Editor.GetViewport().GetCamera().SetFocusPoint(center);
                m_Editor.GetViewport().GetCamera().SetDistance(5.0f);
            });

        // ── 8. 渲染注入器（支持 isPicking 参数） ──
        m_Editor.SetSceneRenderInjector([this](const float* viewProj16, const float* camPos3, bool isPicking) {
            (void)isPicking;
            RenderActiveScene(viewProj16, camPos3);
        });

        m_Test.Init();

        // ── 9. 相机 ──
        m_Editor.GetViewport().GetCamera().SetPosition(Vec3(0.0f, 3.0f, 8.0f));
        m_Editor.GetViewport().GetCamera().SetFocusPoint(Vec3(0.0f, 0.0f, 0.0f));

        std::cout << "=== EditorDemo Started ===" << std::endl;
        std::cout << "  Scene: " << m_Scene->GetName()
                  << " (" << m_Scene->GetObjectCount() << " objects)" << std::endl;
    }

    // ═══════════════════════════════════════════════════════════════
    // 默认资产（simple.vert + simple.frag，顶点 {pos3 + normal3}）
    // ═══════════════════════════════════════════════════════════════

    void InitializeRenderingResources() {
        m_SimpleShader = m_Factory.CreateShader("assets/shaders/simple.vert", "assets/shaders/simple.frag");
        if (!m_SimpleShader) {
            std::cout << "[ERROR] Failed to load simple.vert/simple.frag" << std::endl;
            return;
        }

        m_DefaultMaterial = std::make_shared<Material>(m_SimpleShader);
        m_DefaultMaterial->BaseColor[0] = 0.8f;
        m_DefaultMaterial->BaseColor[1] = 0.8f;
        m_DefaultMaterial->BaseColor[2] = 0.8f;
        m_DefaultMaterial->BaseColor[3] = 1.0f;

        // 标准立方体 — 8 顶点 interleaved {x,y,z, nx,ny,nz}，36 索引
        // 法线是面向外的面法线（对于立方体 8 个独立顶点用 face normal 近似）
        float vertices[] = {
            // 背面 (z = -0.5), 法线朝 -Z
            -0.5f,-0.5f,-0.5f,  0.f,0.f,-1.f,
             0.5f,-0.5f,-0.5f,  0.f,0.f,-1.f,
             0.5f, 0.5f,-0.5f,  0.f,0.f,-1.f,
            -0.5f, 0.5f,-0.5f,  0.f,0.f,-1.f,
            // 正面 (z = +0.5), 法线朝 +Z
            -0.5f,-0.5f, 0.5f,  0.f,0.f, 1.f,
             0.5f,-0.5f, 0.5f,  0.f,0.f, 1.f,
             0.5f, 0.5f, 0.5f,  0.f,0.f, 1.f,
            -0.5f, 0.5f, 0.5f,  0.f,0.f, 1.f,
            // 左面 (x = -0.5), 法线朝 -X
            -0.5f,-0.5f,-0.5f, -1.f,0.f,0.f,
            -0.5f, 0.5f,-0.5f, -1.f,0.f,0.f,
            -0.5f, 0.5f, 0.5f, -1.f,0.f,0.f,
            -0.5f,-0.5f, 0.5f, -1.f,0.f,0.f,
            // 右面 (x = +0.5), 法线朝 +X
             0.5f,-0.5f,-0.5f,  1.f,0.f,0.f,
             0.5f, 0.5f,-0.5f,  1.f,0.f,0.f,
             0.5f, 0.5f, 0.5f,  1.f,0.f,0.f,
             0.5f,-0.5f, 0.5f,  1.f,0.f,0.f,
            // 底面 (y = -0.5), 法线朝 -Y
            -0.5f,-0.5f,-0.5f,  0.f,-1.f,0.f,
             0.5f,-0.5f,-0.5f,  0.f,-1.f,0.f,
             0.5f,-0.5f, 0.5f,  0.f,-1.f,0.f,
            -0.5f,-0.5f, 0.5f,  0.f,-1.f,0.f,
            // 顶面 (y = +0.5), 法线朝 +Y
            -0.5f, 0.5f,-0.5f,  0.f,1.f,0.f,
             0.5f, 0.5f,-0.5f,  0.f,1.f,0.f,
             0.5f, 0.5f, 0.5f,  0.f,1.f,0.f,
            -0.5f, 0.5f, 0.5f,  0.f,1.f,0.f,
        };
        uint32_t indices[] = {
            0,1,2,2,3,0,     4,5,6,6,7,4,
            8,9,10,10,11,8,  12,13,14,14,15,12,
            16,17,18,18,19,16, 20,21,22,22,23,20
        };

        auto vb = m_Factory.CreateVertexBuffer(vertices, sizeof(vertices));
        auto ib = m_Factory.CreateIndexBuffer(indices, 36);

        // 使用 3D 属性描述符：pos(loc=0, size=3, stride=24, offset=0)
        //                  + normal(loc=1, size=3, stride=24, offset=12)
        VertexAttribute attrs[2] = {
            {0, 3, 6 * sizeof(float), 0},           // aPos
            {1, 3, 6 * sizeof(float), 3 * sizeof(float)} // aNormal
        };

        auto vao = m_Factory.CreateVertexArray();
        vao->AddVertexBuffer(vb, attrs, 2);
        vao->SetIndexBuffer(ib);

        m_CubeMesh = std::make_shared<Mesh>(vao, 36);
        std::cout << "  CubeMesh: 24 verts (pos+normal), 36 idx, shader=simple" << std::endl;
    }

    void AttachDefaultMeshRenderer(const std::shared_ptr<GameObject>& obj) {
        if (!m_CubeMesh || !m_DefaultMaterial) return;
        auto* renderer = obj->AddComponent<MeshRendererComponent>();
        renderer->TargetMesh = m_CubeMesh;
        renderer->TargetMaterial = m_DefaultMaterial;
    }

    // ═══════════════════════════════════════════════════════════════
    // Lambert 光照渲染管线
    // ═══════════════════════════════════════════════════════════════

    void RenderActiveScene(const float* viewProj16, const float* camPos3) {
        if (!m_Scene) return;
        auto* oglCtx = static_cast<OpenGLContext*>(GetRenderContext());
        if (!oglCtx) return;

        glm::mat4 vp = glm::make_mat4(viewProj16);
        glm::vec3 camPos = glm::make_vec3(camPos3);

        // ── 1. 收集灯光 ──
        glm::vec3 lightDir = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.5f));
        glm::vec3 lightColor = glm::vec3(0.0f);
        for (auto& obj : m_Scene->GetObjects()) {
            if (!obj || !obj->IsActive()) continue;
            CollectLights(obj.get(), lightDir, lightColor);
            CollectChildLights(obj, lightDir, lightColor);
        }

        // ── 2. 渲染网格 ──
        for (auto& obj : m_Scene->GetObjects()) {
            if (!obj || !obj->IsActive()) continue;
            RenderGameObject(obj.get(), vp, lightDir, lightColor);
            RenderChildren(obj, vp, lightDir, lightColor);
        }
    }

    void CollectLights(GameObject* obj,
                       glm::vec3& outLightDir,
                       glm::vec3& outLightColor) {
        if (!obj || !obj->HasComponent<LightComponent>()) return;
        auto* light = obj->GetComponent<LightComponent>();
        if (light->Type == LightType::Directional) {
            Vec3 fwd = obj->GetTransform().GetForward();
            outLightDir = glm::normalize(glm::vec3(fwd.x, fwd.y, fwd.z));
            outLightColor = glm::vec3(light->Color.x, light->Color.y, light->Color.z) * light->Intensity;
        }
    }

    void CollectChildLights(const std::shared_ptr<GameObject>& parent,
                            glm::vec3& outDir, glm::vec3& outCol) {
        for (const auto& child : parent->GetChildren()) {
            if (!child || !child->IsActive()) continue;
            CollectLights(child.get(), outDir, outCol);
            CollectChildLights(child, outDir, outCol);
        }
    }

    void RenderChildren(const std::shared_ptr<GameObject>& parent,
                        const glm::mat4& vp,
                        const glm::vec3& lightDir,
                        const glm::vec3& lightColor) {
        for (const auto& child : parent->GetChildren()) {
            if (!child || !child->IsActive()) continue;
            RenderGameObject(child.get(), vp, lightDir, lightColor);
            RenderChildren(child, vp, lightDir, lightColor);
        }
    }

    void RenderGameObject(GameObject* obj,
                          const glm::mat4& vp,
                          const glm::vec3& lightDir,
                          const glm::vec3& lightColor) {
        if (!obj) return;
        if (!obj->HasComponent<MeshRendererComponent>()) return;

        auto* mr = obj->GetComponent<MeshRendererComponent>();
        if (!mr->TargetMesh || !mr->TargetMaterial) return;

        auto& shader = *mr->TargetMaterial->ShaderProgram;
        shader.Bind();

        glm::mat4 model = glm::make_mat4(obj->GetTransform().GetWorldMatrix().Data());
        shader.SetMat4("u_MVP", glm::value_ptr(vp * model));
        shader.SetMat4("u_Model", glm::value_ptr(model));

        // 光照参数
        shader.SetVec3("u_LightDir", glm::value_ptr(lightDir));
        shader.SetVec3("u_LightColor", glm::value_ptr(lightColor));

        // 基础色
        shader.SetVec4("u_BaseColor", mr->TargetMaterial->BaseColor);

        // 选中高亮
        auto selectedPtr = m_Editor.GetSelectedObject();
        bool isSelected = (selectedPtr && selectedPtr.get() == obj);
        shader.SetInt("u_IsSelected", isSelected ? 1 : 0);

        mr->TargetMesh->VAO->Bind();
        auto* oglCtx = static_cast<OpenGLContext*>(GetRenderContext());
        if (oglCtx) {
            oglCtx->GetGL().DrawElements(GL_TRIANGLES,
                                         static_cast<int>(mr->TargetMesh->IndexCount),
                                         GL_UNSIGNED_INT, nullptr);
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 场景构建
    // ═══════════════════════════════════════════════════════════════

    void BuildTestScene() {
        m_Scene = std::make_shared<Scene>("EditorDemoScene");

        // ── 方向光 ──
        {
            auto obj = std::make_shared<GameObject>("Directional Light");
            obj->GetTransform().SetRotation(45.0f, -30.0f, 0.0f);
            auto* lc = obj->AddComponent<LightComponent>();
            lc->Type = LightType::Directional;
            lc->Color = Vec3(1.0f, 0.95f, 0.9f);
            lc->Intensity = 1.2f;
            m_Scene->AddObject(obj);
        }

        // ── 主物体群 ──
        for (int i = 0; i < 3; ++i) {
            auto obj = std::make_shared<GameObject>(i == 0 ? "Red Cube" :
                                                     i == 1 ? "Green Cube" : "Blue Cube");
            obj->GetTransform().SetPosition(-3.0f + i * 3.0f, 0.0f, 0.0f);
            if (i == 0) obj->GetTransform().SetRotation(0.0f, 30.0f, 0.0f);
            if (i == 2) obj->GetTransform().SetRotation(0.0f, -30.0f, 0.0f);
            obj->GetTransform().SetScale(1.0f);
            AttachDefaultMeshRenderer(obj);
            m_Scene->AddObject(obj);
        }

        // ── 层级树 ──
        {
            auto parent = std::make_shared<GameObject>("Parent");
            parent->GetTransform().SetPosition(0.0f, 2.5f, 0.0f);
            AttachDefaultMeshRenderer(parent);
            m_Scene->AddObject(parent);
            auto child = std::make_shared<GameObject>("Child");
            child->GetTransform().SetPosition(1.5f, 0.0f, 0.0f);
            child->GetTransform().SetScale(0.6f);
            AttachDefaultMeshRenderer(child);
            parent->AddChild(child);
            auto gchild = std::make_shared<GameObject>("Grandchild");
            gchild->GetTransform().SetPosition(0.0f, 0.0f, 1.5f);
            gchild->GetTransform().SetScale(0.4f);
            AttachDefaultMeshRenderer(gchild);
            child->AddChild(gchild);
        }

        // ── 地面 ──
        {
            auto ground = std::make_shared<GameObject>("Ground");
            ground->GetTransform().SetPosition(0.0f, -0.5f, 0.0f);
            ground->GetTransform().SetScale(Vec3(10.0f, 0.1f, 10.0f));
            ground->AddComponent<SpriteComponent>();
            m_Scene->AddObject(ground);
        }

        // ── 标记点 ──
        for (int i = 0; i < 6; ++i) {
            auto obj = std::make_shared<GameObject>("Marker_" + std::to_string(i));
            float angle = float(i) * 3.14159f * 2.0f / 6.0f;
            obj->GetTransform().SetPosition(cosf(angle) * 5.0f, 0.0f, sinf(angle) * 5.0f);
            obj->GetTransform().SetScale(0.3f);
            AttachDefaultMeshRenderer(obj);
            m_Scene->AddObject(obj);
        }

        std::cout << "  Built scene with " << m_Scene->GetObjectCount() << " objects" << std::endl;
    }

    void OnUpdate(float32 dt) override { m_Editor.OnUpdate(dt); m_Test.OnUpdate(dt); }
    void OnRender() override {}
    void OnImGui() override { m_Editor.OnImGui(); m_Test.OnImGui(); }

private:
    EngineEditor m_Editor;
    std::shared_ptr<Scene> m_Scene;

    std::shared_ptr<Shader> m_SimpleShader;
    std::shared_ptr<Mesh> m_CubeMesh;
    std::shared_ptr<Material> m_DefaultMaterial;

    SceneHierarchyPanel m_HierarchyPanel;
    InspectorPanel m_InspectorPanel;
    ConsolePanel m_ConsolePanel;
    EditorDemoTest m_Test;
};

} // namespace Engine