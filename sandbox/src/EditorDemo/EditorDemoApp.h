#pragma once

/**
 * @file EditorDemoApp.h
 * @brief 引擎编辑器演示 — 完整的 3D 编辑测试场景
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
#include <Engine/Core/EventBus.h>
#include <Engine/OpenGL/OpenGLContext.h>
#include <iostream>
#include <memory>
#include <vector>

#include "EditorDemoTest.h"

namespace Engine {

class EditorDemoApp : public Application {
public:
    EditorDemoApp(IGraphicsFactory &factory) : Application(factory), m_Test(this) {
        m_UseEngineEditorDockspace = true;
        m_DrawPerformanceWindow = false;
        m_RenderDefaultQuad = false;
    }

    ~EditorDemoApp() override {
        for (auto& vp : m_Viewports) vp->Cleanup();
        m_Viewports.clear();
        std::cout << "=== EditorDemo Shutdown ===" << std::endl;
    }

protected:
    void OnStartup() override {
        m_Editor.Init(this);
        m_Editor.RegisterSceneHierarchy(&m_HierarchyPanel);
        m_Editor.RegisterInspector(&m_InspectorPanel);
        m_Editor.RegisterConsole(&m_ConsolePanel);
        m_Editor.RegisterPerformance(&m_PerfWindow);

        BuildTestScene();

        m_Editor.GetSceneManager().SetEditorScene(m_Scene.get());
        m_HierarchyPanel.SetScene(m_Scene.get());

        // ── 视口渲染注入器：每帧将场景物体绘制到各视口的 FBO ──
        m_Editor.SetSceneRenderInjector([this](const float* viewProj16, const float* camPos3) {
            if (!m_Scene || !m_Shader) return;
            auto* oglCtx = static_cast<OpenGLContext*>(GetRenderContext());
            if (!oglCtx) return;

            m_Shader->Bind();
            m_Shader->SetMat4("u_ViewProjection", viewProj16);
            oglCtx->BindViewModeUniform(m_Shader.get());

            // 使用默认 VAO 和纹理绘制场景（简化版本）
            if (m_VAO && m_Texture) {
                m_Texture->Bind(0);

                // 遍历所有根物体
                for (const auto& obj : m_Scene->GetObjects()) {
                    if (!obj || !obj->IsActive()) continue;
                    // 设置物体的世界矩阵
                    m_Shader->SetMat4("u_Model", obj->GetTransform().GetWorldMatrix().Data());
                    m_VAO->Bind();
                    oglCtx->DrawIndexed(m_VAO);

                    // 递归子物体
                    RenderChildren(obj);
                }
            }
        });

        InitViewports();
        m_Test.Init();

        std::cout << "=== EditorDemo Started ===" << std::endl;
        std::cout << "  Scene: " << m_Scene->GetName()
                  << " (" << m_Scene->GetObjectCount() << " objects)" << std::endl;
        std::cout << "  Viewports: " << m_Viewports.size() << std::endl;
    }

    // 递归绘制子物体
    void RenderChildren(const std::shared_ptr<GameObject>& parent) {
        for (const auto& child : parent->GetChildren()) {
            if (!child || !child->IsActive()) continue;
            m_Shader->SetMat4("u_Model", child->GetTransform().GetWorldMatrix().Data());
            m_VAO->Bind();
            auto* oglCtx = static_cast<OpenGLContext*>(GetRenderContext());
            if (oglCtx) oglCtx->DrawIndexed(m_VAO);
            RenderChildren(child);
        }
    }

    void BuildTestScene() {
        m_Scene = std::make_shared<Scene>("EditorDemoScene");

        // ── 主物体群 ──
        {
            auto obj = std::make_shared<GameObject>("Red Cube");
            obj->GetTransform().SetPosition(-3.0f, 0.0f, 0.0f);
            obj->GetTransform().SetRotation(0.0f, 30.0f, 0.0f);
            obj->GetTransform().SetScale(1.0f);
            m_Scene->AddObject(obj);
        }
        {
            auto obj = std::make_shared<GameObject>("Green Cube");
            obj->GetTransform().SetPosition(0.0f, 0.0f, 0.0f);
            obj->GetTransform().SetScale(1.0f);
            m_Scene->AddObject(obj);
        }
        {
            auto obj = std::make_shared<GameObject>("Blue Cube");
            obj->GetTransform().SetPosition(3.0f, 0.0f, 0.0f);
            obj->GetTransform().SetRotation(0.0f, -30.0f, 0.0f);
            obj->GetTransform().SetScale(1.0f);
            m_Scene->AddObject(obj);
        }

        // ── 层级树 ──
        {
            auto parent = std::make_shared<GameObject>("Parent");
            parent->GetTransform().SetPosition(0.0f, 2.5f, 0.0f);
            m_Scene->AddObject(parent);

            auto child = std::make_shared<GameObject>("Child");
            child->GetTransform().SetPosition(1.5f, 0.0f, 0.0f);
            child->GetTransform().SetScale(0.6f);
            parent->AddChild(child);

            auto gchild = std::make_shared<GameObject>("Grandchild");
            gchild->GetTransform().SetPosition(0.0f, 0.0f, 1.5f);
            gchild->GetTransform().SetScale(0.4f);
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

        // ── 光源 ──
        {
            auto obj = std::make_shared<GameObject>("Sun Light");
            obj->GetTransform().SetPosition(0.0f, 10.0f, 0.0f);
            m_Scene->AddObject(obj);
            obj = std::make_shared<GameObject>("Point Light");
            obj->GetTransform().SetPosition(5.0f, 3.0f, 5.0f);
            m_Scene->AddObject(obj);
        }

        // ── 标记点 ──
        for (int i = 0; i < 6; ++i) {
            auto obj = std::make_shared<GameObject>("Marker_" + std::to_string(i));
            float angle = float(i) * 3.14159f * 2.0f / 6.0f;
            obj->GetTransform().SetPosition(cosf(angle) * 5.0f, 0.0f, sinf(angle) * 5.0f);
            obj->GetTransform().SetScale(0.3f);
            m_Scene->AddObject(obj);
        }

        std::cout << "  Built scene with " << m_Scene->GetObjectCount() << " objects" << std::endl;
    }

    void InitViewports() {
        auto* ctx = GetRenderContext();
        if (!ctx) return;
        auto* oglCtx = static_cast<OpenGLContext*>(ctx);
        GladGLContext& gl = oglCtx->GetGL();

        auto makeViewport = [&](const std::string& name) {
            auto vp = std::make_shared<ViewportPanel>(name);
            vp->SetRenderContext(ctx);
            vp->SetGLContext(&gl);
            vp->SetPickCallback([this](float, float, uint32 id) {
                if (id > 0) {
                    auto* hit = m_Scene->FindByID(id);
                    if (hit) m_Editor.OnSelectionChanged(hit);
                }
            });
            vp->InitResources(&m_Factory, "assets/shaders/3d_lit.vert", "assets/shaders/3d_lit.frag");
            return vp;
        };

        m_Viewports.push_back(makeViewport("Perspective View"));

        auto top = makeViewport("Top View");
        top->GetCamera().SetProjectionType(CameraProjectionType::Orthographic);
        top->GetCamera().SetPosition(Vec3(0.0f, 20.0f, 0.0f));
        top->GetCamera().SetFocusPoint(Vec3(0.0f, 0.0f, 0.0f));
        top->GetCamera().LockRotation(true);
        m_Viewports.push_back(top);

        auto front = makeViewport("Front View");
        front->GetCamera().SetProjectionType(CameraProjectionType::Orthographic);
        front->GetCamera().SetPosition(Vec3(0.0f, 0.0f, 20.0f));
        front->GetCamera().SetFocusPoint(Vec3(0.0f, 0.0f, 0.0f));
        front->GetCamera().LockRotation(true);
        m_Viewports.push_back(front);
    }

    void OnUpdate(float32 dt) override {
        m_Editor.OnUpdate(dt);
        m_Test.OnUpdate(dt);

        ViewportPanel* activeVp = nullptr;
        for (auto& vp : m_Viewports) {
            if (vp->IsFocused()) { activeVp = vp.get(); break; }
        }
        for (auto& vp : m_Viewports) {
            vp->OnUpdate(dt, vp.get() == activeVp);
        }
    }

    void OnRender() override {}

    void OnImGui() override {
        m_Editor.OnImGui();
        m_Test.OnImGui();
        for (auto& vp : m_Viewports) {
            vp->OnImGui();
        }
    }

private:
    EngineEditor m_Editor;
    std::shared_ptr<Scene> m_Scene;
    SceneHierarchyPanel m_HierarchyPanel;
    InspectorPanel m_InspectorPanel;
    ConsolePanel m_ConsolePanel;
    EditorDemoTest m_Test;
    std::vector<std::shared_ptr<ViewportPanel>> m_Viewports;
};

} // namespace Engine