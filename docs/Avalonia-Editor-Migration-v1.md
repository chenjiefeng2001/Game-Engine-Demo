# Avalonia Editor Migration v1

**Branch:** `avalonia` · **Base:** `6ed3a45` · **日期：** 2026-08-26
**Source UI:** ImGui Editor / GP1-DX Production UI
**目标：** Avalonia Editor v1
**原则：** Engine contracts remain frozen

---

# 1. Executive Summary

当前引擎已经具备：

```text
ContentRegistry
ResourceLifecycle
SceneSerializerV1
Scripting API v2.1
Physics
Renderer
EngineEditor
Hierarchy
Inspector
Asset Browser
Script Editor
Console
Production UI P0
```

并已经完成：

* M1–M4 FROZEN
* DF01–DF09
* VS01
* GP1-A
* GP1-B
* GP1-C
* GP1-D 自动化验证
* GP1-DX Production UI

因此 Avalonia 迁移**不应该重新解决这些问题**。

迁移目标是：

```text
                    ┌─────────────────────┐
                    │     Avalonia UI     │
                    │                     │
                    │ Hierarchy           │
                    │ Inspector           │
                    │ Asset Browser       │
                    │ Script Editor       │
                    │ Console             │
                    │ Toolbar / StatusBar │
                    └──────────┬──────────┘
                               │
                        Editor Adapter
                               │
                    ┌──────────▼──────────┐
                    │ Existing C++ Engine │
                    │                     │
                    │ Content             │
                    │ Scene               │
                    │ Scripting           │
                    │ Renderer            │
                    │ Resource            │
                    └─────────────────────┘
```

**核心要求：Avalonia 不直接成为 Engine 的新逻辑层。**

---

# 2. 三个架构分叉的裁决

## 2.1 进程模型

### 方案 A：Avalonia + C++ Engine 同进程

```text
Avalonia
   │
NativeControlHost / Native API
   │
C ABI
   │
C++ Engine
```

### 方案 B：Editor 独立进程

```text
Avalonia Editor
      │
      │ IPC / Shared Memory
      ▼
C++ Engine Process
```

### 裁决：**Phase 1 采用同进程**

原因非常明确：

1. 当前 EngineEditor 已经是同进程模型。
2. 绝大多数 Engine API 已经成熟。
3. Content / Scene / Resource / Script 的生命周期目前都是进程内状态。
4. IPC 会立即引入 serialization、request/response、handle ownership、
   crash recovery、process synchronization、asset notification——
   这些全部不是当前产品需求。

> **IPC 不作为 Avalonia v1 的前置条件。**
> 但接口必须设计成未来可以隔离（句柄模型 + 无指针泄漏的 C ABI）。

---

# 3. Native API Boundary

**不要让 C# 直接 P/Invoke 一堆 C++ 内部类。**

错误：

```text
C# → EngineEditor.cpp
C# → SceneSerializerV1.cpp
C# → ContentRegistry.cpp
C# → ScriptSandbox.cpp
```

应该建立：

```text
Avalonia
    ↓
EditorHost API
    ↓
C ABI
    ↓
EditorApplication / EditorSession
    ↓
Existing Engine
```

例如：

```cpp
extern "C" {

EditorSessionHandle Editor_CreateSession();

void Editor_DestroySession(EditorSessionHandle);

bool Editor_OpenProject(
    EditorSessionHandle,
    const char* path
);

bool Editor_SaveProject(
    EditorSessionHandle
);

EntityId Editor_CreateEntity(
    EditorSessionHandle
);

bool Editor_SetEntityName(
    EditorSessionHandle,
    EntityId,
    const char*
);

}
```

C# 只看到 `EditorSession / EditorEntity / EditorAsset / EditorScene`，
而不是 `ContentRegistry* / SceneSerializerV1* / entt::entity /
std::string / spdlog::logger*`。

---

# 4. 句柄模型

沿用已验证过的 Handle 思路。不要把 C++ pointer 暴露给 C#。

```text
EditorEntityHandle
EditorAssetHandle
EditorSceneHandle
EditorSessionHandle
```

底层为 `uint64/uint32 token`，由 C++ 管理生命周期。
未来切换到 IPC 时，API 语义不需要重新设计。

---

# 5. 视口架构裁决

## v1：**Texture Presentation 优先**

```text
C++ Renderer
      ↓
Render Target / Texture
      ↓
Interop Layer
      ↓
Avalonia Image / Native Texture
      ↓
Viewport
```

不把整个 GLFW/OpenGL 子窗口直接塞进 Avalonia。
NativeControlHost 嵌 GL 子窗口会把平台耦合（HWND/X11/Wayland/NSView、
context ownership、resize 同步、input forwarding、DPI、focus）带进
Avalonia，最终 Avalonia 只是"包着一个旧窗口"，违背迁移目标。

---

# 6. 但是不要立即重写 Renderer

当前 RenderGraph 仍然 SG6 FAIL / ISOLATED，
绝对不要借 Avalonia 迁移顺便推动 RenderGraph。

v1 应该：

```text
Existing Renderer → Existing OpenGL target → Interop adapter → Avalonia Viewport
```

如果现有 renderer 很难直接导出 texture：
**第一阶段允许 NativeControlHost 作为临时 viewport adapter**，但必须明确：

```text
Temporary: NativeControlHost + existing GL viewport
Target:    Renderer → Texture → Avalonia
```

这样不会阻塞整个 UI 迁移。

---

# 7. UI 迁移策略

* A. 一次性切换 —— 不建议
* B. 面板逐个替换 —— **推荐**
* C. 两套 UI 永久共存 —— 禁止

采用：**双 UI 短期共存 + Panel-by-Panel migration +
Feature parity gate + 最终移除 ImGui Editor。**

---

# 8. Migration Architecture

```text
Editor/
├── Core/
│   ├── EditorSession
│   ├── EditorSelection
│   ├── EditorCommands
│   ├── EditorEvents
│   └── EditorState
│
├── Bridge/
│   ├── EditorCAPI
│   └── EditorInterop
│
├── ImGui/
│   └── LegacyEditorUI
│
└── Avalonia/
    ├── AvaloniaEditor
    ├── Views
    ├── ViewModels
    └── Services
```

关键：**把现在 EngineEditor 中的业务逻辑抽出来。**
否则 ImGui 与 Avalonia 各持一份 Editor logic，行为必然漂移。

---

# 9. EditorSession

整个迁移最重要的新抽象（防腐层）。

```text
EditorSession
│
├── Project
├── Scene
├── Selection
├── Assets
├── Inspector
├── Scripts
├── Runtime
└── Commands
```

所有 UI 都只能通过 Session 操作：

```text
Hierarchy     → EditorSession.Select(entity)
Inspector     → EditorSession.SetTransform(...)
AssetBrowser  → EditorSession.AssignAsset(...)
Toolbar       → EditorSession.Play()
```

---

# 10. Event Model

禁止 Avalonia 控件之间互相调用：

```text
HierarchyView → EditorSession → SelectionChanged → InspectorView
```

事件至少包括：

```text
SelectionChanged
SceneChanged
EntityCreated
EntityDeleted
EntityRenamed
AssetChanged
ScriptChanged
RuntimeStateChanged
ProjectChanged
```

这会自然解决 GP-DX-001 / DL-02 类型的状态同步问题。

---

# 11. 阶段实施路线

## Phase 0 — Architecture Spike

**目标：证明技术路线，而不是做 UI。** 时间约 1–2 天。

完成：

* Avalonia 项目创建
* C ABI library
* C# P/Invoke
* Engine session 创建/销毁
* Project Open
* 一个简单 API 调用
* C++ → C# event callback

验收链：

```text
Avalonia → EditorSession → C ABI → C++ → 返回结果
```

成功后才进入 Phase 1。

## Phase 1 — Editor Shell

MainWindow / Menu / Toolbar / Dock Layout / Viewport / StatusBar / Panels
(placeholder)。重点验证 DPI、Resize、Docking、keyboard focus、lifecycle、
close/reopen、project open。

## Phase 2 — Hierarchy

迁移 SceneHierarchyPanel：entity list / search / select / multi-select /
create / delete / rename / hierarchy / drag-reparent。

Golden Gate：**Avalonia Hierarchy 操作产生与 ImGui 完全相同的 Scene 状态。**

## Phase 3 — Inspector

只做已冻结且实际使用的 Transform / Name / Sprite / Script。
不做完整 Component Editor。

## Phase 4 — Asset Browser

ContentRegistry → AssetBrowserViewModel → Avalonia DataGrid/List。
搜索 / 类型过滤 / GUID / Assign Sprite / Assign Script。
Browser ↔ Registry ↔ Inspector 三方状态一致。

## Phase 5 — Script Editor

UTF-8 / multiline / Save / Reload / error display / Play-time reload /
`_PERSIST`。**不换 Lua backend。**

## Phase 6 — Console

spdlog → Editor Console Event → ObservableCollection → Avalonia Console。
保留 level filter / search / regex / command history。
不让 Avalonia 自建 logging system。

## Phase 7 — Runtime / Viewport

Edit → Play → Pause → Stop；Editor Scene → Capture/Clone → Runtime Scene。
重点验证：**Editor 状态与 Runtime 状态绝不混淆**（GP-DX-005）。

## Phase 8 — Full Production Golden Gate

重新执行 GP1-D Human Run 全链路：

```text
Open Project → Import → Create Entity → Rename → Assign Sprite
→ Edit Transform → Edit Lua → Save → Play → Reload → Stop
→ Save → Restart → Load → Continue
```

全程不改 JSON、不改 C++、不调用内部 API ⇒ **Avalonia Production
Golden Gate PASS**。

---

# 12. 每阶段 Evidence Gate

```text
Implementation → Unit/Contract Tests → Production Test
→ Integrity Gate → Manual Smoke → Ledger Review
```

示例编号：

```text
Hierarchy: AV-H1 Create / AV-H2 Rename / AV-H3 Delete / AV-H4 Reparent / AV-H5 Selection
Asset:     AV-A1 Search / AV-A2 Filter / AV-A3 Assign / AV-A4 Save / AV-A5 Reload
```

---

# 13. 双 UI 共存策略

```text
Phase 0~4:  ImGui + Avalonia
Phase 5~6:  Avalonia primary
Phase 7+:   Avalonia only
```

**两套 UI 不共享状态副本。只有 EditorSession 是真实状态：**

```text
ImGui ──┐
        ├── EditorSession ── Engine
Avalonia┘
```

---

# 14. Feature Flag

```text
EDITOR_UI=imgui | avalonia | dual     # 默认 dual 直到 Golden Gate PASS
```

PASS 后切 `avalonia`，最终删除 `imgui`。

---

# 15. 不迁移的东西

Avalonia migration **不允许顺便修改**：

* Engine：Physics / Scripting API / ResourceLifecycle / ContentRegistry /
  SceneSerializerV1 / Renderer / RenderGraph
* Gameplay：Lua API / GameState / Persistence
* Deferred：M002 / M003 / M004 / M006 / Collision Events / ECS

除非产生**可复现、最小化、独立的真实阻塞**。

---

# 16. Ledger 新条目

| 条目 | 状态 | 结论 |
|------|------|------|
| AV-001 Avalonia Architecture | PROPOSED | UI framework migration requires a stable EditorSession boundary |
| AV-002 In-Process Host | DECIDED | Avalonia v1 uses in-process C ABI bridge |
| AV-003 Viewport | DECIDED | Texture presentation is target; NativeControlHost may be temporary |
| AV-004 Dual UI | DECIDED | Short-lived coexistence, single EditorSession state, eventual ImGui removal |
| AV-005 EditorSession | **P0 ARCHITECTURAL REQUIREMENT** | 整个迁移的防腐层 |

---

# 17. 最大风险

* **Risk 1：把 C++ Editor 代码搬成 C#** —— 出现两套逻辑。禁止。
* **Risk 2：C ABI 变成垃圾桶**（SetAnything/GetAnything）—— 必须围绕领域对象
  （Project/Scene/Entity/Asset/Runtime/Script/Selection）设计。
* **Risk 3：Viewport 把迁移拖死** —— Viewport 独立 track，面板先迁完。
* **Risk 4：借迁移重构 Engine** —— 严格禁止。冻结契约 + 可重复证据是
  本项目最宝贵资产；迁移验证的是"另一个 frontend 能否消费这些契约"。

---

# 18. 完成定义

不以"删除所有 ImGui 代码"为完成条件。真正条件：

* **A 功能**：Hierarchy/Inspector/Asset Browser/Script Editor/Console/
  Project/Runtime/Viewport 全 PASS
* **B 数据**：GUID stability / Scene round-trip / Save-Load / Reload /
  `_PERSIST` 全 PASS
* **C 生产**：Human Golden Run（不碰 JSON/C++/内部 API）完成一次真实迭代
* **D 证据**：I1/I2/I3 GREEN
* **E 架构**：Engine contracts / Gameplay API / Resource lifecycle /
  Scene format unchanged

满足后 **Avalonia Editor v1 = FROZEN**。

---

# 19. 执行顺序

```text
avalonia @ 6ed3a45
      ↓
AV-001 Architecture Spike        ← Phase 0 P0（本文件 §11）
      ↓
EditorSession → C ABI Boundary → Avalonia Shell
      ↓
Hierarchy / Inspector / Assets（并行）
      ↓
Script Editor → Console → Runtime Controls → Viewport
      ↓
Avalonia Production Gate → Human GP1-D Re-run
      ↓
PASS → GP1-E   |   BLOCK → GP Ledger
```

---

## 最终裁决

现在可以迁移，但第一步不是"把 ImGui Panel 重写成 Avalonia Control"。

> **先建立 `EditorSession + C ABI`，让 Avalonia 成为第一个真正的
> Editor Frontend。**

这一步的战略价值：不只是换 UI，而是建立真正的 **Editor/Engine 边界**。
未来再换 UI、独立 Editor 进程、或走 IPC，都不需要重动 Engine 核心。
