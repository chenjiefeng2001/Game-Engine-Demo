# Layout and Rendering

## Shell 分区（v0.2）

```
Menu
Toolbar
┌────────────┬──────────────┬────────────┐
│ Hierarchy  │   Viewport   │ Inspector  │
│            │  placeholder │            │
├────────────┴──────────────┴────────────┤
│ Console / Output                       │
└────────────────────────────────────────┘
Status
```

实现：`src/Nova.Editor.Views/EditorShellView.axaml`。
模型：`WorkspaceLayout`（`src/Nova.UI.Core/Layout`）。

## 布局规则

- 左/右面板最小宽度 `size.panel.min-width`（当前 160）。
- Console 最小高度 80；最大 600，且不超过窗口高度 - 220。
- 左右面板 + 中央视口至少保留 320px 中央列；贪婪配置会被按比例压缩。
- Splitter 使用 `GridSplitter`；v0.2 关闭拖动预览（`ShowsPreview=false`）。
- 面板隐藏时列/行宽度置 0，但保留上次尺寸；重新显示时恢复。
- 显示/隐藏命令执行前，workspace 通过 `LayoutMeasurer` 读取当前实际 Splitter 尺寸；因此隐藏前的手动调整不会丢失。
- 隐藏面板不销毁 workspace 状态；只改变可见性。

## 持久化

文件：`%LOCALAPPDATA%/NovaEditor/layout.json`（可注入路径用于测试）。

流程：

1. 启动后 `InitializeLayoutAsync()` 后台读取；UI 先使用 `WorkspaceLayout.Default`。
2. 反序列化失败 / schema 不匹配 / 文件不可读 → 返回默认布局并记录原因。
3. 损坏文件尝试重命名为 `layout.json.corrupt-<UTC>`，不阻塞启动。
4. 保存写入同目录临时文件，再 `File.Move(..., overwrite: true)` 原子替换。
5. 并发保存通过 `SemaphoreSlim(1,1)` 串行化；失败返回 `LayoutSaveResult`。
6. 窗口关闭时取消首次 close，`await` 保存后再次 close；UI 线程不执行阻塞 I/O。

## 渲染边界

- v0.2 的 Viewport 是占位区，不引用 Vulkan/D3D/OpenGL。
- 真实后端将通过 `IEngineViewportHost` 注入；`Nova.UI.Core` 不依赖具体图形 API。
- 自定义绘制控件（v0.4 Timeline/Grid）在进入前必须定义：
  - 命中测试区域与裁剪；
  - 资源生命周期（位图/几何缓存）；
  - 每帧分配上限；
  - 与虚拟化视口的协作方式。

## 虚拟化

- `NovaTreeView`/`NovaListView` 使用 Avalonia 默认虚拟化容器。
- 列表必须位于受约束的可视区域内（ScrollViewer/Grid 有明确高度），否则虚拟化退化为全量创建。
- 不在 ItemTemplate 中做文件扫描、同步 I/O 或大对象构造。
- Console 有界（400 行）并保留虚拟化 ListBox；这是 UI 侧上限，不是日志系统容量上限。

## 性能 smoke 预算

| 指标 | 预算 | 测试 |
| --- | --- | --- |
| 单次状态写入 P95 | < 50 ms | `InteractionBudgetTests.Local_state_update_p95_is_under_50ms` |
| 1 万节点属性命令 P95 | < 50 ms | `Edit_command_on_10k_nodes_p95_is_under_50ms` |
| 1 万次索引查找总耗时 | < 500 ms | `Scene_lookup_is_indexed_not_linear` |

这些是灾难回归门槛，不是目标硬件性能承诺。正式基准见 `docs/benchmarks/nova-ui-baseline.md`。
