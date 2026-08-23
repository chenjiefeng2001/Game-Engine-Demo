# UI 系统实现分析报告

> **日期**: 2026-08-23
> **基线**: `milestone-1` tag
> **方法**: 全仓库源码逐文件审读（非推测）

---

## 一、架构总览

```
Application::Run()
  │
  ├─ UIManager::Init(factory.CreateUIManager(), nativeWindow, glContext)
  │     └─ 唯一实现: OpenGLGraphicsFactory → ImGuiUIManager
  │
  ├─ 每帧:
  │   ui->Begin()          ← NewFrame + 输入抢占 (SetBlockInput)
  │   ...场景逻辑...
  │   OnImGui() 虚函数      ← 子类在此构建面板
  │   ...场景渲染...
  │   ui->End()            ← Render + 多视口 + 恢复主上下文
  │
  └─ Shutdown: UIManager::Shutdown()
```

**模式**: 静态代理（`UIManager`）持有注入实例（`IUIManager*`），工厂注入，判空转发。
**后端**: 仅 OpenGL（`ImGuiUIManager : IUIManager`，424 行）。Vulkan 无实现。

---

## 二、ImGui 集成层 — `ImGuiUIManager` (424 行)

### 初始化流程

| 步骤 | 内容 |
|------|------|
| 1 | `IMGUI_CHECKVERSION()` + `CreateContext()` |
| 2 | ConfigFlags: `NavEnableKeyboard \| DockingEnable \| ViewportsEnable` |
| 3 | `ApplyEngineStyle(1.0f)` — 约 57 色暗色主题 |
| 4 | `ImGui_ImplGlfw_InitForOpenGL(win, true)` + `ImplOpenGL3_Init("#version 460")` |
| 5 | 自定义 Viewport 渲染回调：子视口渲染前强制重置 GL 状态（修复 3D 污染） |
| 6 | `LoadFont(nullptr, 16.0f)` |

### 字体管线

| 层 | 字体 | 合并范围 | 说明 |
|----|------|---------|------|
| 基础 | ProggyClean 16px | ASCII | ImGui 默认 |
| CJK 合并 | msyh.ttc / simhei.ttf 等（4 候选） | CJK 统一表意 + 扩展A + 标点 + 全角 | `m_CjkFontAttempted` 只试一次 |
| 图标合并 | fa-solid-900.otf（3 候选路径） | U+E005–U+F8FF 私有区 | FontAwesome 7 |

**结论**: ImGui 面板天然支持中文渲染 ✓（前提：目标机器存在 Windows 系统字体）。

### 帧生命周期

```
Begin():
    应用挂起缩放（重载字体）
    ImplOpenGL3_NewFrame → ImplGlfw_NewFrame → NewFrame()
    Input::SetBlockInput(WantCaptureMouse, WantCaptureKeyboard)

End():
    Render() → 可见时 RenderDrawData
    → UpdatePlatformWindows / RenderPlatformWindowsDefault（多视口）
    → glfwMakeContextCurrent(主窗口)
```

### 缩放

`SetScale(float)` 钳制 0.5–3.0；挂起至下一帧 `Begin()` 时重建字体（`roundf(16*scale)px`）。

---

## 三、编辑器面板清单

### 3.1 SceneHierarchyPanel (89 行头 + 实现)

| 能力 | 状态 | 备注 |
|------|------|------|
| 树形显示 Scene/Entity | ✅ | DrawSceneNode 递归 |
| 单选 + 回调 | ✅ | `SetSelectionCallback(std::function<void(GameObject*)>)` |
| 多选 | ✅ | `m_MultiSelected` 集合 |
| 搜索过滤 | ✅ | 文本框过滤名称 |
| 拖拽 reparent | ✅ | HandleDragDropToRoot |
| 右键菜单 | ✅ | DrawContextMenu |
| 隐藏/锁定集合 | ✅ | m_Hidden/m_Locked 集合 |
| 延迟操作队列 | ✅ | 防迭代器失效 |
| 工具栏 | ✅ | DrawToolbar |

**评价**: 功能完备，远超 Ring13 最小需求。可直接被 ScriptSandbox 复用。

---

### 3.2 InspectorPanel (190 行头 + 537 行实现)

| 能力 | 状态 | 位置 |
|------|------|------|
| SetTarget(GameObject*) | ✅ | 单选编辑 |
| SetMultiTarget(vector*) | ✅ | 多选混合值显示 |
| Transform 编辑 | ✅ | DrawTransformComponent 固定先画（cpp:358） |
| 组件绘制器注册系统 | ✅ | `RegisterDrawer<T>(fn)` + `ComponentDrawer` 结构（displayName/category/order/builtin） |
| 反射自动绘制 | ✅ | AutoDrawComponent + PropertyAccessor 系统（cpp:154） |
| Physics/MeshRenderer 内置 | ✅ | RegisterBuiltins (cpp:517) |
| **Add Component 菜单** | ⚠️ STUB | 菜单可选名字但挂载代码被注释（cpp:486 `// obj->AddComponentByID(typeId);`） |
| Debug 模式 / 可见性 / 锁定 | ✅ | |

---

### 3.3 ConsolePanel (98 行头 + 375 行实现)

| 能力 | 状态 |
|------|------|
| 窗口标题 + FontAwesome 图标 | ✅ `"ICON_FA_TERMINAL Console"` |
| 日志级别过滤（Info/Warn/Error/Fatal 复选） | ✅ |
| Channel 过滤菜单 | ✅ |
| Regex 搜索栏 | ✅ |
| 折叠重复条目（Collapse） | ✅ |
| AutoScroll | ✅ |
| Clear | ✅ |
| 命令输入行（↑↓历史 + Tab 补全回调） | ✅ |

**⚠️ 关键架构缺陷——双日志缓冲割裂**：

| 缓冲区 | 容量 | 写入者 | 消费者 |
|--------|------|--------|--------|
| `ConsoleLog` (单例) | 512 条环形 | spdlog `ConsoleLogSink`（引擎所有日志自动喂入） | 无直接 UI 渲染者 |
| `ConsolePanel::s_LogBuffer` | 10,000 条 vector | 仅 `Print()`/`SubmitCommand()`/help 文本 | `ConsolePanel::OnImGui` 渲染 |

**结果：引擎 spdlog 输出不会出现在 ConsolePanel 窗口中。** 两套缓冲之间没有任何桥接。

---

### 3.4 PerformanceWindow (173 行头 + 实现)

12 个 Draw 分组：FPS 历史（150 帧环形）、DrawCall、几何体、VRAM、GPUProfiler 瀑布图（8 个固定 DebugPassId）、ViewMode、光照阴影、剔除、后处理、纹理资源、Helper 开关。

**数据缺口**：`FeedStats(dt*1000, drawCalls, 0, 0, 0, 0)` — 物理刚体数/关节数/活跃音源/纹理计数恒为 0。

---

### 3.5 EngineEditor (162 行头 + 662 行实现)

全功能编辑器框架：

```
EditorRootWindow (全屏底座)
├── MainMenuBar (OnMenuBar 内嵌模式)
├── Toolbar (Play/Pause/Gizmo/Snap/ViewMode — 15+ 回调注入)
├── DockSpace("MainDockSpace") ← imgui.ini 记忆布局
├── ViewportPanel
├── InspectorPanel
├── ConsolePanel (外部注册包装)
├── PerformanceWindow
├── ContentBrowserPanel / AssetBrowserPanel
├── DependencyGraphPanel
├── SceneManagerPanel / SceneViewerPanel (三合一 Tab)
├── ShaderGraphPanel / VFXGraphPanel
├── AnimationEditorPanel
└── StatusBar (FPS/内存/后台任务/Git 分支/Toast)
```

外部注册接口：`RegisterConsole/RegisterSceneHierarchy/RegisterInspector/RegisterPerformance`。
使用示例：`sandbox/src/EditorDemo/EditorDemoApp.h:57` — `m_Editor.RegisterConsole(&m_ConsolePanel);`

---

### 3.6 其他 UI 工具

| 工具 | 文件 | 行数 | 说明 |
|------|------|------|------|
| MenuManager | Core/MenuManager.h/.cpp | 154+271 | 运行时游戏菜单（Page 枚举导航），DrawHUD 仅一行 "ESC: Pause" |
| Toolbar | Editor/Toolbar.h | 136 | Play/Pause/Gizmo/Snap 全回调注入零依赖 |
| MainMenuBar | Editor/MainMenuBar.h | 140 | 双模式：独立 BeginMainMenuBar 或内嵌 DockSpace |
| StatusBar | Editor/StatusBar.h | 125 | FPS 色编码 + 内存 + 后台任务 + Git 分支 + Toast |
| UiHelpers | UiHelpers.h/.cpp | 161+ | RAII: ScopedID/Indent/ItemWidth/StyleColor/StyleVar/Font/Disabled; DrawVec3Control(XYZ彩色); DrawTooltip |
| DockspaceBuilder | DockspaceBuilder.h | 82 | 全屏底座 + 默认布局(Hierarchy左/Viewport中/Inspector右/Console底) |
| MemoryPanel | MemoryPanel.h/.cpp | — | "Memory Monitor" 窗口 |
| GameHUD | Rendering/GameHUD.h | 98 | TextRenderer + PrimitiveBatch 世界文本；仅 ComplexSceneTest 使用 |
| TextRenderer | Rendering/TextRenderer.h/.cpp | 127+428 | FreeType + GL_R8 动态图集 + UTF-8 手写解码；msyh/simhei/arial/NotoSansCJK/PingFang 候选链 |

---

## 四、ScriptSandbox 的 Console 实现

自制简易控制台（与引擎 ConsolePanel 完全独立，符合 Scripting v1 冻结范围）：

```cpp
// ScriptSandboxApp.cpp:242-270
void DrawConsolePanel() {
    // 头部: api_version / entities / selected
    // BeginChild("scroll") → 遍历 m_Scrollback (256 条上限)
    // 自动滚底: GetScrollY >= GetScrollMaxY → SetScrollHereY
    // InputText + EnterReturnsTrue → AppendLog("> cmd") → Execute(cmd)
    // 成功 → "[ok]" ; 失败 → "[error] GetLastError().message"
}
```

同一 OnImGui 还画自制 Hierarchy（Selectable 列表 + Create/Delete）和 Inspector（DragFloat3 Position + GUID Assign 按钮）。

---

## 五、FreeType / TextRenderer（世界文本）

独立于 ImGui 的世界空间文本渲染器：

| 特性 | 实现 |
|------|------|
| 库 | FreeType (FT_Library/FT_Face) |
| 图集 | 1024×1024 GL_R8 动态扩展 |
| 字体候选 | msyh.ttc → simhei.ttf → arial.ttf → NotoSansCJK → PingFang |
| 编码 | UTF-8 手写解码器 |
| API | DrawText / DrawTextMultiline / MeasureText / GetFontTextureID |
| TextConfig | 字号 / 颜色 / 对齐 / 三种坐标空间 |
| 消费方 | 仅 GameHUD（仅 ComplexSceneTest sandbox 使用） |

---

## 六、差距与问题清单（代码验证事实）

### 🔴 关键

| # | 问题 | 影响 | 位置 |
|---|------|------|------|
| G1 | **M005 Engine.ui.text 未接到屏幕** | Lua 脚本的 HUD 文本只写 s_HudMsg+s_Log，GetHudText()/ClearHudText() 全仓库无调用者。dogfood_game.lua 的 Victory 消息玩家看不到 | ScriptAPI.cpp:87-94; ScriptSandboxApp.cpp 无消费路径 |
| G2 | **ConsolePanel 与引擎日志割裂** | ConsolePanel 显示自有 s_LogBuffer（仅命令输出），引擎 spdlog 日志进 ConsoleLog(512) 但无人渲染到该面板 | ConsolePanel.cpp 匿名 ns vs Log.cpp ConsoleLogSink |

### 🟡 中等

| # | 问题 | 影响 | 位置 |
|---|------|------|------|
| G3 | InspectorPanel Add Component 为 stub | 菜单可选名字但实际挂载代码被注释 | InspectorPanel.cpp:486 |
| G4 | PerformanceWindow 数据不全 | 物理/音频/纹理计数恒为 0 | Application.cpp:689 |
| G5 | UI 仅 OpenGL 后端 | Vulkan 无 IUIManager 实现 | IGraphicsFactory.h:173 |
| G6 | ConsolePanel "Ping Hierarchy" 和 "Open in IDE" 为 TODO | 点击无效 | ConsolePanel.cpp:253,263 |

### 🟢 低

| # | 问题 | 说明 |
|---|------|------|
| G7 | ResetClockOrigin() 空函数体 | ScriptAPI.cpp:98-100 |
| G8 | MenuManager::DrawHUD 仅一行静态文本 | cpp:242-251 |
| G9 | imgui.ini 仅记录 2 窗口旧布局 | 仓库根目录残留 |

---

## 七、中文支持现状总结

| 场景 | 中文能力 | 机制 |
|------|---------|------|
| ImGui 编辑器面板 | ✅ 完整 | MergeMode 合并系统 CJK 字体（msyh/simhei/msjh） |
| FreeType 世界文本 | ✅ 完整 | 同字体候选链 + 动态图集 |
| ScriptSandbox Console | ✅（继承 ImGui） | 同上 |
| 游戏 HUD（GameHUD） | ✅（但未在 ScriptSandbox 中使用） | TextRenderer |
| M005 Engine.ui.text | ❌ 未到达屏幕 | 仅写日志，无消费方 |

---

## 八、建议优先行动

| 优先级 | 行动 | 对应 Ledger | 工作量估计 |
|--------|------|------------|-----------|
| P0 | ScriptSandbox OnRender 后读取 `ScriptAPI::GetHudText()` → ImGui overlay 渲染 → `ClearHudText()` | 关闭 M005 | ~30 行 |
| P0 | 桥接 ConsoleLog→ConsolePanel（或让 ConsolePanel 直接读 ConsoleLog 单例） | 修复 G2 | ~50 行 |
| P1 | ScriptSandbox 替换自制 Hierarchy/Inspector 为引擎 SceneHierarchyPanel/InspectorPanel | 复用成熟面板 | ~100 行重构 |
| P1 | 解除 InspectorPanel Add Component 注释并接通 | 修复 G3 | ~10 行 |
| P2 | PerformanceWindow FeedStats 补齐物理/音频计数 | 修复 G4 | ~20 行 |
