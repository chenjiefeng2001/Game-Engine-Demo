# 迁移指南：legacy `editor_avalonia` → Nova v0.2

## 原则

`editor_avalonia/` 仍承担 native bridge、pick、viewport 等既有验证；v0.2 **不整体替换**它。迁移按垂直切片进行，避免同时改 UI、命令层和 native 生命周期。

## 映射

| legacy | Nova v0.2 | 迁移方式 |
| --- | --- | --- |
| `MainWindow.axaml` 四区 Grid | `Nova.Editor.Views/EditorShellView.axaml` | 先用 Nova Shell 承载静态骨架；native 区域保留原 View |
| `MainViewModel` 的直接 session 调用 | `EditorWorkspace` + `CommandRouter` | 选择/编辑/增删先走命令层；查询仍可读 service |
| `ListBox` Hierarchy | `NovaTreeView` | 数据模型改为稳定的 `SceneNode` + `UiId` |
| `TextBox` + Apply 按钮 | `NovaPropertyRow` + `NovaNumberField`/`NovaTextField` | 属性提交走 schema + `EditPropertyCommand` |
| 手写颜色/尺寸 | `TokenKeys` + style classes | 删除控件内颜色字面量 |
| `imgui.ini` / 无布局恢复 | `WorkspaceLayout` + `LayoutPersistenceService` | 旧布局不自动导入；v0.3 提供迁移器 |
| 直接 `HostService` 轮询 | 空间/视口事件边界 | 保持 HostService 为唯一 pump owner，UI 不建第二定时器 |
| Console 双缓冲 | `EditorLogService` 单缓冲 + 投影 | 逐步把 spdlog/桥接日志接入同一入口 |

## 分阶段

### Phase A — 只读并行

- 新增 Nova Shell，数据来自 legacy host 的只读投影。
- 保留 legacy 写路径；Nova 不执行写命令。
- 对照选择状态、面板尺寸、日志。

### Phase B — 属性写链路

- 选择 Player → Nova Inspector → `EditPropertyCommand` → legacy session 的同值写入适配器。
- 相同输入分别走 legacy 与 Nova，比较模型/快照/撤销行为。
- 只迁移 Transform 等显式 schema，不迁移反射自动绘制。

### Phase C — 结构命令

- 增删节点、重命名、组件添加迁移到命令层。
- 此时才允许 legacy 写路径下线。

### Phase D — 视口边界

- 用 `IEngineViewportHost` 接入真实 native viewport。
- 禁止 UI Core/Controls 引用引擎图形类型。
- 处理 resize/DPI/输入转发时，复用现有 `ViewportDisplayGeometry`、pick mapper 等已验证逻辑。

## 不要做

- 不要复制 legacy 的静态单例状态。
- 不要把 legacy `MainViewModel` 的持久引用（如 `EntityVm`）作为 Nova 稳定 ID。
- 不要在 v0.2 同时删除 legacy gate/自测；先保留回归证据。
