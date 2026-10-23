# Nova UI v0.2 — Editor Foundation

> 状态：可运行的纵向切片（vertical slice）已完成，像素级截图回归与完整 P0 主题覆盖仍有明确缺口。
> 目标：先证明“稳定 ID → 命令 → 模型 → 撤销 → 局部 UI 更新”这条链路，而不是先堆控件数量。

## 1. 已交付

| 领域 | 交付 | 位置 |
| --- | --- | --- |
| 架构 | Abstractions / Core / Theme / Avalonia / Controls / Editor Services / Views / App 分层 | `src/` |
| 设计令牌 | Dark + Light 两套完整令牌，颜色、字体、间距、圆角、控件高度、动效时长 | `src/Nova.UI.Core/Tokens` |
| 主题适配 | Token → Avalonia Resource + 全局 Style；主题切换不改变控件行为 | `src/Nova.UI.Avalonia/NovaThemeAdapter.cs` |
| P0 原语 | Button/IconButton/TextField/NumberField/Toggle/Select/PropertyRow/InspectorSection/TreeView/ListView/Splitter 及布局原语 | `src/Nova.UI.Controls` |
| 局部更新 | `UiId` + `UiStateStore` + `NovaUiBinding`，按 (id, key) 写入单个属性 | `src/Nova.UI.Abstractions`, `src/Nova.UI.Avalonia` |
| 命令层 | `CommandRouter`、`CommandHistory`、属性编辑/增删节点命令、撤销/重做 | `src/Nova.UI.Core/Commands`, `src/Nova.Editor.Services/Commands` |
| Editor Shell | 菜单、工具栏、Hierarchy、Viewport 占位、Inspector、Console、状态栏 | `src/Nova.Editor.Views` |
| 布局持久化 | 异步、原子写入、损坏回退、隔离损坏文件、默认布局 | `src/Nova.Editor.Services/Layout` |
| 离线启动 | 无网络、无 native 渲染后端依赖；采样场景内置 | `src/Nova.Editor.App` |
| Playground | 所有 P0 控件的状态展示与主题切换 | `samples/Nova.UI.Playground` |
| 测试 | 24 个 Core、6 个 Controls/主题/绑定合约、23 个集成、5 个性能 smoke | `tests/` |
| 文档 | UI spec、ADR、迁移指南、性能基线 | `docs/ui-spec`, `docs/decisions`, `docs/migration` |

## 2. 端到端链路（已实现）

```
Hierarchy 选中 Player
  → SelectionService.SelectedId = entity.player
  → Inspector 仅重建该节点的属性行
  → NumberField 提交（Enter / blur / spinner）
  → EditorWorkspace.TryEditProperty 使用显式 schema 校验、clamp
  → EditPropertyCommand.Execute
  → SceneNode.SetProperty
  → CommandHistory 记录
  → UiStateStore.Set(entity.player, "property.position.x", value)
  → 只有对应绑定属性更新；Hierarchy/Viewport/Console 不重建
  → Ctrl+Z → EditPropertyCommand.Undo → 同一投影槽位回写旧值
```

集成测试：`tests/Nova.Editor.IntegrationTests/WorkspaceCommandChainTests.cs`。

## 3. 与计划的差异 / 未完成项

| 计划项 | 当前状态 | 说明 |
| --- | --- | --- |
| 全部 P0 控件 API + 示例 | 大部分完成 | 布局原语是薄封装；没有为每个控件单独写 API 文档页 |
| 焦点/键盘/禁用态自动化测试 | 部分完成 | 依赖 Avalonia 行为，当前无 headless UI 测试包；纯逻辑契约有测试 |
| 截图回归 | 未自动化 | 本地无 `Avalonia.Headless` 包，无法离线初始化渲染；提供手动截图脚本与布局快照回归 |
| 性能基准 | smoke 已接入 | 只设宽松门槛（P95 < 50ms），未做目标硬件正式测量 |
| 完整 Docking / Timeline / 3D | 明确不做 | 与 v0.2 范围一致 |
| 主题覆盖全部 P0 | 基础覆盖 | Button/TextBox/ComboBox/CheckBox/TreeView/ListBox/Splitter/Menu/Expander/ToolTip/Scroll 已覆盖；Fluent 模板部分细节仍由基础主题决定 |

## 4. 验收命令

```powershell
dotnet restore Nova.sln
dotnet build Nova.sln -c Release
dotnet test Nova.sln -c Release
dotnet run --project src/Nova.Editor.App/Nova.Editor.App.csproj
dotnet run --project samples/Nova.UI.Playground/Nova.UI.Playground.csproj

# Optional: start the playground on the light tokens (useful for manual baseline capture)
$env:NOVA_THEME = "nova.light"
```

性能 smoke：

```powershell
dotnet test tests/Nova.Editor.PerformanceTests/Nova.Editor.PerformanceTests.csproj `
  --logger "console;verbosity=detailed"
```

## 5. 下一版本入口（v0.3）

只有以下条件成立才进入 Docking：

1. `UiStateStore` 的局部更新在真实场景编辑中保持“无无关重建”。
2. 属性链路已接入真实场景文档，且撤销语义稳定。
3. 布局持久化在旧版本配置、损坏配置、并发保存下都可恢复。
4. 像素回归门禁补齐（建议引入 `Avalonia.Headless.XUnit/ MSTest` 与固定渲染尺寸）。

在此之前不并行开发完整 Docking、Timeline、复杂 DSL 或 3D 视口。
