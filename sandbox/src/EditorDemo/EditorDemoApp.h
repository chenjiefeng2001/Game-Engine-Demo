#pragma once

/**
 * @file EditorDemoApp.h
 * @brief 引擎编辑器演示 — 多视口 3D 渲染 + 编辑器组件集成测试
 *
 * 演示内容：
 *   - EngineEditor 自动布局（SceneHierarchy / Inspector / Console 等）
 *   - 主菜单栏 + 工具栏（新建/保存场景，Play/Stop/Pause）
 *   - 多个可停靠的 3D 视口（透视/顶/前视图）
 *   - 每个视口拥有独立的 FBO、EditorCamera 和渲染状态
 *   - EditorDemoTest 集成测试（状态栏 / Profiler / 控制台 / 撤销系统等）
 */

#include <Engine/Application.h>
#include <Engine/ConsolePanel.h>
#include <Engine/Editor/EngineEditor.h>
#include <Engine/Editor/ViewportPanel.h>
#include <Engine/InspectorPanel.h>
#include <Engine/SceneHierarchyPanel.h>
#include <Engine/Core/Scene/Scene.h>
#include <Engine/Core/GameObject/GameObject.h>
#include <Engine/Core/GameObject/TransformComponent.h>
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
    // ── 显式清理 OpenGL 资源（必须在 OpenGL 上下文销毁前） ──
    for (auto& vp : m_Viewports) {
      vp->Cleanup();
    }
    m_Viewports.clear();
    std::cout << "=== EditorDemo Shutdown ===" << std::endl;
  }

protected:
  void OnStartup() override {
    // ── 初始化 Editor ──
    m_Editor.Init(this);
    m_Editor.RegisterSceneHierarchy(&m_HierarchyPanel);
    m_Editor.RegisterInspector(&m_InspectorPanel);
    m_Editor.RegisterConsole(&m_ConsolePanel);
    m_Editor.RegisterPerformance(&m_PerfWindow);

    // ── 创建默认场景 ──
    m_Scene = std::make_shared<Scene>("DefaultScene");

    auto rootObj = std::make_shared<GameObject>("Cube");
    rootObj->GetTransform().SetPosition(0.0f, 0.0f, 0.0f);
    rootObj->GetTransform().SetScale(1.0f);
    m_Scene->AddObject(rootObj);

    auto childObj = std::make_shared<GameObject>("Child");
    childObj->GetTransform().SetPosition(2.0f, 1.0f, 0.0f);
    childObj->GetTransform().SetScale(0.5f);
    rootObj->AddChild(childObj);
    m_Scene->AddObject(childObj);

    auto lightObj = std::make_shared<GameObject>("Light");
    lightObj->GetTransform().SetPosition(3.0f, 5.0f, -5.0f);
    m_Scene->AddObject(lightObj);

    m_Editor.GetSceneManager().SetEditorScene(m_Scene.get());
    m_HierarchyPanel.SetScene(m_Scene.get());

    // ── 设置场景渲染注入器：编辑器的主视口通过此回调绘制场景 ──
    // ViewportPanel 已在 Render3DScene 中绑定了 FBO 并清空，
    // 此回调只需使用传入的 viewProj 矩阵绘制场景内容。
    m_Editor.SetSceneRenderInjector([this](const float* viewProj16, const float* camPos3) {
      if (!m_Scene) return;

      // 使用引擎的默认 Shader 绘制场景
      // Shader 已由 Application::InitShader() 在启动时创建
      if (m_Shader && m_VAO && m_Texture) {
        auto* oglCtx = static_cast<OpenGLContext*>(GetRenderContext());
        if (oglCtx) {
          m_Shader->Bind();
          m_Shader->SetMat4("u_ViewProjection", viewProj16);
          oglCtx->BindViewModeUniform(m_Shader.get());
          m_Texture->Bind(0);
          m_VAO->Bind();
          oglCtx->DrawIndexed(m_VAO);
        }
      }

      // TODO: 遍历场景 GameObject 进行每个物体的绘制（需要 RenderComponent 系统）
      // 当前先绘制引擎默认的测试四边形作为占位
    });

    // ── 初始化多视口 ──
    InitViewports();

    // ── 初始化集成测试 ──
    m_Test.Init();

    std::cout << "=== EditorDemo Started ===" << std::endl;
    std::cout << "  Scene: " << m_Scene->GetName()
              << " (" << m_Scene->GetObjectCount() << " objects)" << std::endl;
    std::cout << "  Viewports: " << m_Viewports.size() << std::endl;
  }

  void InitViewports() {
    auto* ctx = GetRenderContext();
    if (!ctx) {
      std::cerr << "ERROR: No render context!" << std::endl;
      return;
    }
    auto* oglCtx = static_cast<OpenGLContext*>(ctx);
    GladGLContext& gl = oglCtx->GetGL();

    // ── 创建视口工厂 lambda（减少重复代码） ──
    auto makeViewport = [&](const std::string& name) {
      auto vp = std::make_shared<ViewportPanel>(name);
      vp->SetRenderContext(ctx);
      vp->SetGLContext(&gl);
      vp->InitResources(&m_Factory,
                        "assets/shaders/3d_lit.vert",
                        "assets/shaders/3d_lit.frag");
      return vp;
    };

    // ═════════════════════════════════════════════════
    // 1. 透视图 (Perspective) — 自由视角，透视投影
    // ═════════════════════════════════════════════════
    {
      auto vp = makeViewport("Perspective View");
      vp->GetCamera().SetProjectionType(CameraProjectionType::Perspective);
      // 默认位置 (0, 0, 10) 看向原点，留给用户自由操控
      m_Viewports.push_back(std::move(vp));
    }

    // ═════════════════════════════════════════════════
    // 2. 顶视图 (Top) — 俯视向下看 (-Y)，正交
    // ═════════════════════════════════════════════════
    {
      auto vp = makeViewport("Top View");
      vp->GetCamera().SetProjectionType(CameraProjectionType::Orthographic);
      vp->GetCamera().SetPosition(Vec3(0.0f, 20.0f, 0.0f));
      vp->GetCamera().SetFocusPoint(Vec3(0.0f, 0.0f, 0.0f));
      vp->GetCamera().LockRotation(true);
      m_Viewports.push_back(std::move(vp));
    }

    // ═════════════════════════════════════════════════
    // 3. 前视图 (Front) — 沿 -Z 看，正交
    // ═════════════════════════════════════════════════
    {
      auto vp = makeViewport("Front View");
      vp->GetCamera().SetProjectionType(CameraProjectionType::Orthographic);
      vp->GetCamera().SetPosition(Vec3(0.0f, 0.0f, 20.0f));
      vp->GetCamera().SetFocusPoint(Vec3(0.0f, 0.0f, 0.0f));
      vp->GetCamera().LockRotation(true);
      m_Viewports.push_back(std::move(vp));
    }

    // ═════════════════════════════════════════════════
    // 4. 右视图 (Right) — 沿 -X 看，正交
    // ═════════════════════════════════════════════════
    {
      auto vp = makeViewport("Right View");
      vp->GetCamera().SetProjectionType(CameraProjectionType::Orthographic);
      vp->GetCamera().SetPosition(Vec3(20.0f, 0.0f, 0.0f));
      vp->GetCamera().SetFocusPoint(Vec3(0.0f, 0.0f, 0.0f));
      vp->GetCamera().LockRotation(true);
      m_Viewports.push_back(std::move(vp));
    }
  }

  void OnUpdate(float32 dt) override {
    m_Editor.OnUpdate(dt);

    // ── 更新集成测试 ──
    m_Test.OnUpdate(dt);

    // ── 焦点控制：只有一个视口获得输入 ──
    // 找出当前聚焦/悬停的视口（优先级：focused > hovered）
    ViewportPanel* focusedVp = nullptr;
    for (auto& vp : m_Viewports) {
      if (vp->IsFocused()) {
        focusedVp = vp.get();
        break;
      }
    }
    if (!focusedVp) {
      for (auto& vp : m_Viewports) {
        if (vp->IsHovered()) {
          focusedVp = vp.get();
          break;
        }
      }
    }

    // ── 更新所有视口，只有焦点视口处理输入 ──
    for (auto& vp : m_Viewports) {
      vp->OnUpdate(dt, vp.get() == focusedVp);
    }
  }

  void OnRender() override {
    // 所有渲染已在 ViewportPanel::OnUpdate() 中完成
  }

  void OnImGui() override {
    // ── Editor 绘制 DockSpace + 菜单栏 + 工具栏 ──
    m_Editor.OnImGui();

    // ── 绘制集成测试 UI（状态栏 / 测试面板 / Profiler / Console 等） ──
    m_Test.OnImGui();

    // ── 在 DockSpace 中绘制额外的视口 ──
    for (size_t i = 0; i < m_Viewports.size(); ++i) {
      m_Viewports[i]->OnImGui();
    }
  }

private:
  EngineEditor m_Editor;
  std::shared_ptr<Scene> m_Scene;
  SceneHierarchyPanel m_HierarchyPanel;
  InspectorPanel m_InspectorPanel;
  ConsolePanel m_ConsolePanel;

  // 编辑器组件集成测试
  EditorDemoTest m_Test;

  // 多视口系统 — 每个视口拥有独立的 FBO + Camera
  std::vector<std::shared_ptr<ViewportPanel>> m_Viewports;
};

} // namespace Engine