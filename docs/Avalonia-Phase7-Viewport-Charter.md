# Avalonia Phase 7 — Viewport Charter

**日期**: 2026-09-14  
**前身**: Phase 3 FROZEN (Result A)  
**目标**: 将 ImGui Viewport 迁移到 Avalonia，实现纹理互操作 + 输入转发 + Gizmo 工具栏

---

## 1. 范围

### Phase 7-A: 纹理显示（最小可行）
- [x] Viewport FBO 纹理导出 ABI（`GetViewportTextureHandle`）
- [x] Avalonia 侧 P/Invoke 声明
- [x] ViewportPanel FBO readback 缓存（CPU Readback MVP）
- [x] Application singleton + ViewportPanel 访问接口
- [x] Bridge C ABI `GetViewportPixels`（header+body 布局）
- [x] EditorSession 像素读取实现
- [x] Avalonia `WriteableBitmap` 显示 OpenGL 纹理（~30FPS 定时器）

### Phase 7-B: 输入转发
- [x] 鼠标移动/点击/滚轮事件转发
- [x] 键盘按键/释放事件转发
- [x] 鼠标拾取（NDC 坐标 → 实体 ID）
- [x] EditorCamera 外部输入模式（`EditorCameraInput` POD 结构体 + `SetExternalInputMode`）
- [x] EditorCamera 输入解耦（`ProcessInput` 支持 ImGui / External 双路径）
- [x] EditorSession 输入累积 + ViewportTick flush

### Phase 7-C: Gizmo 工具栏
- [x] 工具栏 UI（Q/W/E/R 选择/平移/旋转/缩放）
- [x] Gizmo 模式/空间/吸附状态管理
- [ ] Gizmo 渲染（engine-side ImGuizmo 或 Avalonia-side）

### Phase 7-D: 相机控制
- [x] 相机位置设置/获取
- [x] 相机投影类型切换（Perspective/Orthographic）
- [x] 相机缩放（正交模式）
- [ ] EditorCamera 输入处理（orbit/pan/fly）

---

## 2. 架构决策

### 2.1 纹理互操作方案

**选择: NativeControlHost (临时) → Texture Presentation (目标)**

根据迁移文档 §6，第一阶段允许 NativeControlHost 作为临时 viewport adapter：
- 引擎渲染到 FBO → 导出 OpenGL 纹理句柄
- Avalonia 通过 NativeControlHost 嵌入 GL 子窗口显示纹理
- 后续迁移到 Texture Presentation（C++ Renderer → FBO → GPU interop → Avalonia Image）

### 2.2 输入处理方案

**选择: Engine-side 处理（保持现有 EditorCamera 逻辑）**

- Avalonia 捕获原始输入事件
- 通过 Bridge ABI 转发到引擎
- 引擎侧 EditorCamera 处理 orbit/pan/fly 逻辑
- 优势：保持引擎代码不变，Avalonia 只做事件转发

### 2.3 Gizmo 方案

**选择: Engine-side 渲染（Phase 7-C 临时）**

- 保持 ImGuizmo 在引擎侧渲染
- Avalonia 工具栏仅设置 GizmoMode
- 后续可迁移到 Avalonia-side 渲染（更清晰的分离）

---

## 3. 新增 ABI 函数

### 3.1 纹理共享
```cpp
uint32_t EditorSession_GetViewportTextureHandle(EditorSessionHandle h);
void EditorSession_GetViewportSize(EditorSessionHandle h, int32_t* outWidth, int32_t* outHeight);
void EditorSession_SetViewportSize(EditorSessionHandle h, int32_t width, int32_t height);
void EditorSession_ViewportTick(EditorSessionHandle h, float dt);
```

### 3.2 相机控制
```cpp
void EditorSession_SetViewportCameraPosition(EditorSessionHandle h, float x, float y, float z);
void EditorSession_GetViewportCameraPosition(EditorSessionHandle h, float out3[3]);
void EditorSession_SetViewportCameraProjection(EditorSessionHandle h, int32_t type);
void EditorSession_SetViewportCameraZoom(EditorSessionHandle h, float zoom);
```

### 3.3 输入转发
```cpp
void EditorSession_ViewportMouseMove(EditorSessionHandle h, float x, float y);
void EditorSession_ViewportMouseButton(EditorSessionHandle h, int32_t button, int32_t pressed);
void EditorSession_ViewportScroll(EditorSessionHandle h, float delta);
void EditorSession_ViewportKeyDown(EditorSessionHandle h, int32_t key);
void EditorSession_ViewportKeyUp(EditorSessionHandle h, int32_t key);
int32_t EditorSession_ViewportPick(EditorSessionHandle h, float ndcX, float ndcY);
```

### 3.4 Gizmo
```cpp
void EditorSession_SetGizmoMode(EditorSessionHandle h, int32_t mode);
void EditorSession_SetGizmoSpace(EditorSessionHandle h, int32_t space);
void EditorSession_SetGizmoSnap(EditorSessionHandle h, int32_t enabled, float value);
int32_t EditorSession_GetGizmoMode(EditorSessionHandle h);
int32_t EditorSession_GetGizmoSpace(EditorSessionHandle h);
int32_t EditorSession_GetGizmoSnapEnabled(EditorSessionHandle h);
float EditorSession_GetGizmoSnapValue(EditorSessionHandle h);
void EditorSession_SetViewportGridVisible(EditorSessionHandle h, int32_t visible);
int32_t EditorSession_GetViewportGridVisible(EditorSessionHandle h);
```

---

## 4. 文件变更

### Engine 层
- `engine/include/Engine/Editor/EditorCameraInput.h` — 新建：外部输入 POD 结构体（无 ImGui 依赖）
- `engine/include/Engine/Editor/EditorCamera.h` — 新增外部输入模式接口 + 引用 EditorCameraInput.h
- `engine/include/Engine/Editor/ViewportPanel.h` — 新增 FBO readback 方法/成员
- `engine/include/Engine/Application.h` — 新增 singleton + ViewportPanel 访问器
- `engine/src/Editor/EditorCamera.cpp` — ProcessInput 双路径（ImGui / External）
- `engine/src/Editor/ViewportPanel.cpp` — Render3DScene 末尾 readback
- `engine/src/Editor/EngineEditor.cpp` — Init 启用 readback
- `engine/src/Core/Application.cpp` — s_Instance 赋值

### Bridge 层
- `bridge/include/editor_bridge/capi.h` — 新增 20+ Viewport ABI 声明
- `bridge/src/EditorSession.h` — 新增 Viewport 方法声明 + 状态变量
- `bridge/src/EditorSession.cpp` — 新增 ~200 行 Viewport 实现
- `bridge/src/capi.cpp` — 新增 ~150 行 C ABI 包装函数

### C# Avalonia 层
- `editor_avalonia/Native/EditorBridgeApi.cs` — 新增 20+ P/Invoke 声明
- `editor_avalonia/Services/EditorHostService.cs` — 新增 ~150 行 Viewport 包装方法
- `editor_avalonia/ViewModels/MainViewModel.cs` — 新增 Viewport 属性和方法
- `editor_avalonia/MainWindow.axaml` — Viewport 区域 + Gizmo 工具栏 UI
- `editor_avalonia/MainWindow.axaml.cs` — 新增 ~100 行输入事件处理

---

## 5. 验收标准

### Gate 7-A: 纹理显示
- [x] Avalonia 窗口显示引擎渲染的 3D 场景纹理（CPU Readback via WriteableBitmap）
- [ ] 窗口 resize 时 FBO 自动重建（待验证）

### Gate 7-B: 输入转发
- [ ] 右键拖拽旋转相机（待 Avalonia 事件 → ViewportMouseButton 验证）
- [ ] 中键拖拽平移相机
- [ ] 滚轮缩放
- [ ] 左键点击拾取实体（Hierarchy 选中）

### Gate 7-C: Gizmo
- [ ] Q/W/E/R 工具切换
- [ ] Gizmo 模式状态同步到引擎

### Gate 7-D: 相机控制
- [ ] 相机位置/缩放可编辑
- [ ] Perspective/Orthographic 切换

---

## 6. 待办事项

### P0（阻塞）
1. 实现 Avalonia Image 显示 OpenGL 纹理（需要调研 NativeControlHost 或自定义渲染方案）

### P1（重要）
2. EditorCamera 输入处理（orbit/pan/fly）需要从 ImGui 解耦
3. Gizmo 渲染需要从 ImGuizmo 解耦

### P2（改进）
4. 相机控制 UI（位置/缩放滑块）
5. 视口统计信息（FPS/三角形数）

---

## 7. 风险

1. ~~**纹理互操作复杂度**: Avalonia 显示 OpenGL 纹理需要平台特定代码~~ → 已通过 CPU Readback 解决（MVP）
2. ~~**EditorCamera 耦合**: 当前 EditorCamera 读取 ImGui::GetIO()，需要解耦~~ → 已实现双路径
3. **性能**: CPU readback 每帧 glReadPixels 有 GPU stall（~30FPS 限制）→ 后续升级到 WGL_NV_DX_interop
4. **Gizmo 耦合**: ImGuizmo 与 ImGui 紧密耦合，迁移需要额外工作

---

## 8. 下一步

Phase 7 完成后进入 Phase 8: Full Production Golden Gate（重新执行 GP1-D Human Run 全链路）。
