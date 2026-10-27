# Nova Lifecycle v0.1 — 工作区 / 场景生命周期

> 状态：首个可验证切片已完成；未做 Docking、Timeline、反射属性或真实视口。

## 交付

- `ProjectId` / `SceneId` / `SessionId` 三层身份。
- `EditorProject` + `SceneDescriptor` 项目/场景描述。
- `EditorWorkspace.OpenProject`、`OpenScene`、`CloseProject` 集中转移。
- 命令上下文隔离：旧 session 命令不能进入新 session。
- 关闭确认回调：取消不改变任何会话状态。
- 加载失败前置：候选场景创建成功后才失效旧会话。
- 外部删除：Inspector 不再写入失效对象，并清理 selection。
- 草稿策略：切换对象/场景/项目丢弃未提交草稿。
- Shell 场景菜单与 `SceneLabel` 投影。
- Lifecycle Gate 覆盖切换、失败、取消关闭、重开、外部删除、重复切换事件。
- `NovaStateBinding` 处理 `UiStateStore.Clear()` 的“重读全部”信号。

## 关键 API

```csharp
LifecycleResult OpenProject(EditorProject project, UiId? sceneId = null);
LifecycleResult OpenScene(UiId sceneId);
LifecycleResult CloseProject(Func<bool>? confirm = null, bool saveLayout = true);
```

事件：

```csharp
event EventHandler<SessionChangedEventArgs>? SessionChanged;
```

## 验证

```powershell
dotnet build Nova.sln -c Release
dotnet test Nova.sln -c Release
```

当前总测试：24 Core + 6 Controls + 38 Integration + 5 Performance = **73 passed / 0 failed**。

Lifecycle 相关测试在：

`tests/Nova.Editor.IntegrationTests/LifecycleTests.cs`
`tests/Nova.Editor.IntegrationTests/ShellViewModelTests.cs`

## 明确未完成

- 真实项目/场景文件加载与保存。
- 关闭前“保存 / 丢弃 / 取消” UI 对话框。
- 多窗口共享项目。
- Docking、Timeline、真实视口。
- CI 像素截图回归。

## 下一步

在 Lifecycle v0.1 合约稳定后，再进入 Dock 树模型；不要同时修改项目加载、Dock 布局和视口生命周期。
