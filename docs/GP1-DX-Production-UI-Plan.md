# GP1-DX Production UI Foundation — 阶段计划

> 日期：2026-08-25 · 前序：GP1-D 自动化契约 GREEN（`222af42`）＋
> GP-DX-003（L2 晋升，人工验证 BLOCKED）
> 一句话目标：**开发者不看 C++、不改 JSON、不调内部 API，只用 Editor，
> 就能把 GP01 从 v1 改成 v2。**

---

## 0. 定性

不是做"大编辑器"，也不是从零造 UI。ImGui 基础设施（Dockspace/Hierarchy/
Inspector/Viewport/Console/Toolbar/PIE）已技术存在；本阶段把已有 Panel
**接成可完成生产任务的工作流**，断点集中在接线层：

| 断点 | 现状 |
|------|------|
| ContentRegistry ↔ 主编辑器 | 零连接（仅 ScriptSandbox 用过） |
| 资产分配 | 三条 DND 通路全为死线 |
| 脚本编辑器 | ScriptSandbox 内死代码，主编辑器无入口 |
| 选择同步 | Hierarchy.Init() 从未被调用 |
| 工程概念 | Save 每次弹对话框，无默认工程路径 |

## 1. P0 最小闭环（本阶段交付）

```text
GP01 Production 窗口
  ├─ Open/Reload Project（manifest + Main.scene → InstantiateScene）
  ├─ ＋ Entity（创建 + 绑定表自动对齐）
  ├─ ▶ Play / ■ Stop（SceneSerializerV1 克隆 → 冻结管线运行 game.lua）
  └─ 💾 Save Project（CaptureScene + manifest，记忆路径）
Content 面板
  └─ 注册表资产列表 → [Assign Sprite] 到选中实体（同步绑定表 GUID）
Script Editor 窗口
  └─ 打开/编辑/保存 registry 内脚本 + F5 Reload 运行实例
既有能力直接复用
  ├─ Hierarchy：选择/重命名(F2)/删除/创建
  ├─ Inspector：Transform 数值编辑（Position/Rotation/Scale）
  ├─ Viewport：MRT 拾取点选 + ImGuizmo 拖拽 + EntityID 高亮
  └─ Console：日志与命令
```

## 2. 明确不做（防蔓延）

完整 Undo/Redo 接线 · 多场景/多窗口 · 缩略图网格 · CameraComponent ·
Sprite 真实四边形渲染路径（P0 以 Billboard 标记呈现位置）·
EditorAssetDatabase 与 ContentRegistry 合并 · 快捷键体系重构。

## 3. 已知限制（诚实登记）

- PIE 双轨并存：EngineEditor 自带 JSON 克隆 Play 与本阶段 GP01 Play
  （内容管线克隆+脚本）语义不同，P0 阶段以 GP01 窗口按钮为准；
- Play 期间实体渲染为 Billboard 图标（位置可见、贴图不可见）；
- Hierarchy 在 Play 时显示运行态克隆场景。

## 4. 验收 = GP1-D Human Run 解锁

构建后启动真实编辑器界面，人工执行：

```text
选实体 → Inspector 改 Position → Content 分配纹理 → Hierarchy 重命名
→ ＋Entity → 改 game.lua（Script Editor, F5 生效）→ 💾 Save
→ ▶ Play 实战验证 → ■ Stop → 再次 Save → 重启编辑器 → Load 复核
```

全程不打开 IDE/JSON/API 即 PASS → 回到 GP1-D Result A/B/C 裁决。
