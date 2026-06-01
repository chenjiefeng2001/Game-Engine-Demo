#pragma once

#include <Engine/ConsolePanel.h>
#include <Engine/Core/GameObject/GameObject.h>
#include <Engine/Core/GameObject/MeshComponent.h>
#include <Engine/Core/IGraphicsFactory.h>
#include <Engine/Core/IWindow.h>
#include <Engine/Core/Input.h>
#include <Engine/Core/InputManager.h>
#include <Engine/Core/RHI/BVHSceneGraph.h>
#include <Engine/Core/RHI/DecalSystem.h>
#include <Engine/Core/RHI/FlatSceneGraph.h>
#include <Engine/Core/RHI/GridSceneGraph.h>
#include <Engine/Core/RHI/MeshRenderer.h>
#include <Engine/Core/RHI/ParticleSystem.h>
#include <Engine/Core/RHI/PotentiallyVisibleSet.h>
#include <Engine/Core/RHI/SceneRenderer.h>
#include <Engine/Core/RenderResources/TextureManager.h>
#include <Engine/Core/Renderer/PerspectiveCamera.h>
#include <Engine/Core/Scene/Scene.h>
#include <Engine/MemoryPanel.h>
#include <Engine/PerformanceWindow.h>
#include <Engine/Rendering/GameHUD.h>
#include <Engine/Types.h>
#include <Engine/UIManager.h>


#include <imgui.h>
#include <memory>
#include <vector>


namespace Engine {

class ComplexSceneTestApp {
public:
  ComplexSceneTestApp(IGraphicsFactory &factory);
  ~ComplexSceneTestApp();

  void Run();

private:
  bool InitUI();
  void BuildScene();
  void UpdateScene(float dt);
  void HandleInput(float dt);
  void DrawDebugImGui();
  void RebuildSceneGraph();

  IGraphicsFactory &m_Factory;
  std::unique_ptr<IWindow> m_Window;
  InputManager m_InputManager;
  TextureManager m_TextureManager;
  SceneRenderer m_SceneRenderer;
  PerspectiveCamera m_Camera;

  std::shared_ptr<MeshRenderer> m_MeshRenderer;
  std::shared_ptr<Shader> m_3DShader;
  std::shared_ptr<Shader> m_DepthShader;

  Scene m_Scene;

  // 控制台 / 性能 / 内存
  ConsolePanel m_ConsolePanel;
  PerformanceWindow m_PerfWindow;
  MemoryPanel m_MemoryPanel;
  bool m_UIInitialized = false;

  // 相机控制
  float m_CameraSpeed = 10.0f;
  float m_MouseSensitivity = 0.15f;

  // ── 场景图 ──
  BVHSceneGraph m_BVHGraph;
  FlatSceneGraph m_FlatGraph;
  GridSceneGraph m_GridGraph;
  ISceneGraph *m_ActiveGraph = nullptr;
  int m_SelectedGraphType = 1; // 0=Flat, 1=BVH, 2=Grid
  static constexpr const char *kGraphTypeNames =
      "Flat (No Culling)\0BVH (Hierarchy)\0Grid (Uniform)\0";

  // ── 渲染控制 ──
  bool m_EnableDepthPrePass = true;
  bool m_EnableSceneGraph = true;
  int m_SceneGraphThreshold = 50;

  // ── 场景配置 ──
  int m_ObjectCount = 200;
  float m_WorldRadius = 40.0f;
  bool m_RotateObjects = true;
  bool m_AnimateLights = true;

  // ── 抗锯齿 ──
  AntiAliasingConfig m_AADemoConfig;
  int m_CurrentAAMode = 0;
  int m_AASampleIndex = 1;
  static constexpr int k_SampleValues[3] = {2, 4, 8};
  float m_AASCale = 1.5f;

  // ── 场景物体 ──
  std::vector<GameObject *> m_AllObjects;

  // ── 粒子系统 ──
  ParticleEmitter m_FireEmitter;
  ParticleEmitter m_SparkEmitter;
  std::shared_ptr<Shader> m_ParticleShader;
  bool m_ParticlesEnabled = true;

  // ── 贴花系统 ──
  DecalSystem m_DecalSystem;
  std::shared_ptr<Shader> m_DecalShader;
  bool m_DecalsEnabled = true;

  // ── 灯光 ──
  float m_LightAngle = 0.0f;

  // ── HUD ──
  GameHUD m_GameHUD;
  bool m_HUDVisible = true;
  // 在 m_DecalShader 声明下方加入：
  std::shared_ptr<Shader> m_UIShader;

  void OnWindowResize(int width, int height);
};

} // namespace Engine
